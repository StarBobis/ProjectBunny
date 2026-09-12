// DX12NativeOverlay: DX11-style in-game green text, drawn into the back
// buffer right before Present. Split from DX12ShaderTools.cpp; implementation
// unit for its module.
//
// Design boundary: the text is visible exactly while hunting is enabled
// (Numpad 0 toggles both as one indivisible state) plus a short-lived
// transient notice line (reload/shader-dump feedback). When hunting is off
// and no notice is alive this module does zero GPU work, so a hidden overlay
// can never keep burning tracking or rendering resources.
//
// It is also never called from the Present skipAll fast path: hunting always
// clears skipAll, so the text still draws on the normal path, while the fast
// path stays free of GPU object creation and queue submissions.
//
// Rendering reuses the mod runtime's present-overlay channel (mod-owned
// command list submitted on the game's direct queue before the original
// Present) and its back-buffer RTV cache, so submission ordering matches the
// [Present] custom-element replay; the text is recorded after it and stays
// on top. Glyphs come from a GDI-rasterized monospace font atlas (no font
// files, no embedded blobs), drawn as alpha-blended quads.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include "ModRuntime/DX12CustomShaderDevice.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mutex>
#include <unordered_map>
#include <vector>

class DX12InGameTextOverlay
{
public:
	static DX12InGameTextOverlay *ForDevice(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		DX12InGameTextOverlay *&slot = Instances()[device];
		if (!slot)
			slot = new DX12InGameTextOverlay(device);
		return slot && slot->mDevice ? slot : nullptr;
	}

	static void ClearAll()
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : Instances())
			delete entry.second;
		Instances().clear();
	}

	// Looks up an existing instance without creating one. The skipAll fast path
	// uses this: it may not allocate, so a device that has never drawn overlay
	// text simply reports "not ready" and the HUD appears on the next frame
	// that takes the normal path.
	static DX12InGameTextOverlay *FindDevice(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		auto it = Instances().find(device);
		return it == Instances().end() ? nullptr : it->second;
	}

	// One-time preparation, and it MUST run before the caller acquires the
	// shared overlay channel: the font atlas upload needs a command list of its
	// own, and taking a channel slot while the composer holds the recording
	// lock would deadlock on it.
	bool Prepare(
		IDXGISwapChain *swapChain, ID3D12CommandQueue *queue, DXGI_FORMAT format)
	{
		if (!swapChain || !queue || !mDevice)
			return false;
		if (!EnsurePipeline(format))
			return false;
		return EnsureFontTexture(swapChain, queue);
	}

	// Records one frame of overlay geometry into an already-open command list.
	// The caller owns the channel, the back-buffer barrier and the submission,
	// so every overlay source in a frame lands in ONE command list and ONE
	// ExecuteCommandLists. Returns the index count to draw, 0 when this frame
	// has nothing visible.
	//
	// Cost shape: the geometry is only rebuilt and re-uploaded when the content
	// actually changes. Every input that can move a vertex folds into one
	// content key, and while that key matches what is already in the upload
	// heap the whole layout stage - and the fence check that guards the ring
	// slot - is skipped. The profiling report changes once per interval, so in
	// practice this is a handful of frames per second.
	UINT Record(
		ID3D12GraphicsCommandList *commandList,
		IDXGISwapChain *swapChain,
		ID3D12CommandQueue *queue,
		D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		DXGI_FORMAT format, UINT width, UINT height,
		const wchar_t *text, bool hunting,
		const wchar_t *notice, COLORREF noticeColor)
	{
		(void)swapChain;
		(void)queue;
		(void)format;
		if (!commandList || !mDevice)
			return 0;
		if (!rtv.ptr || width == 0 || height == 0)
			return 0;
		// Nothing to draw from until Prepare() has run; in particular this must
		// not fall back to EnsureFontTexture, which would re-enter the channel.
		if (!Ready())
			return 0;
		// One recording thread at a time: the vertex ring, the layout vector
		// and the content cache are shared state.
		std::lock_guard<std::mutex> renderLock(mRenderLock);
		mBackW = static_cast<float>(width);
		mBackH = static_cast<float>(height);

		// The HUD snapshot is read unconditionally: its version is part of the
		// content key, and reading it is a single interlocked load.
		char report[DX12ProfilingHud::kTextBytes] = {};
		size_t reportLength = 0;
		const UINT64 reportVersion = DX12ProfilingHud::CurrentVersion();
		if (DX12ProfilingHud::IsEnabled() && reportVersion != mHudTextVersion)
			DX12ProfilingHud::AcquireLatest(
				&mHudTextVersion, report, sizeof(report), &reportLength);

		const UINT64 contentKey = ContentKey(
			text, hunting, notice, noticeColor, reportVersion, width, height);
		if (contentKey != mRenderedContentKey) {
			mVertices.clear();
			const UINT indexCount = BuildVertices(
				text, hunting, notice, noticeColor, report, reportLength);
			if (indexCount) {
				void *vertexData = AcquireVertexSlot();
				if (!vertexData) {
					// Its previous submission has not retired yet: drop this
					// frame's text rather than rewriting memory the GPU is
					// reading.
					const LONG present = DX12RuntimeState::GetPresentCount();
					const LONG skippedFor = present - mSlotBusyLogPresent;
					if (skippedFor >= kSkipLogFrames) {
						mSlotBusyLogPresent = present;
						DX12_LOG_FLUSH("DX12InGameTextOverlay",
							"event=skip_frame reason=vertex_slot_busy frames=%ld",
							static_cast<long>(skippedFor));
					}
					return 0;
				}
				mRenderedVertexBytes = static_cast<UINT>(
					mVertices.size() * sizeof(TextVertex));
				memcpy(vertexData, mVertices.data(), mRenderedVertexBytes);
				mRenderedSlot = mVertexCursor;
				mRenderedIndexCount = indexCount;
				mRenderedContentKey = contentKey;
			} else {
				mRenderedIndexCount = 0;
				mRenderedContentKey = contentKey;
			}
		}
		if (!mRenderedIndexCount)
			return 0;

		ID3D12DescriptorHeap *heaps[] = { mSrvHeap };
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(mRootSignature);
		commandList->SetGraphicsRootDescriptorTable(
			0, mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		// The pixel-to-NDC scale, two root constants: cheaper than a constant
		// buffer and it keeps the cached geometry resolution independent.
		const float invHalf[2] = {
			2.0f / mBackW,
			2.0f / mBackH,
		};
		commandList->SetGraphicsRoot32BitConstants(1, 2, invHalf, 0);
		commandList->SetPipelineState(mPipeline);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = mVertexBuffer->GetGPUVirtualAddress() +
			static_cast<UINT64>(mRenderedSlot) * kVertexSlotBytes;
		vbv.StrideInBytes = sizeof(TextVertex);
		vbv.SizeInBytes = static_cast<UINT>(mRenderedVertexBytes);
		commandList->IASetVertexBuffers(0, 1, &vbv);

		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
		ibv.Format = DXGI_FORMAT_R16_UINT;
		ibv.SizeInBytes = static_cast<UINT>(kMaxChars * 6 * sizeof(uint16_t));
		commandList->IASetIndexBuffer(&ibv);

		commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		D3D12_VIEWPORT viewport = {};
		viewport.Width = mBackW;
		viewport.Height = mBackH;
		viewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &viewport);
		D3D12_RECT scissor = {};
		scissor.right = static_cast<LONG>(width);
		scissor.bottom = static_cast<LONG>(height);
		commandList->RSSetScissorRects(1, &scissor);
		commandList->DrawIndexedInstanced(mRenderedIndexCount, 1, 0, 0, 0);

		if (!mFirstFrameLogged) {
			mFirstFrameLogged = true;
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=first_frame_drawn backBuffer=%ux%u quads=%u indexCount=%u slot=%u",
				width, height,
				static_cast<UINT>(mRenderedIndexCount / 6), mRenderedIndexCount,
				mRenderedSlot);
		}
		return mRenderedIndexCount;
	}

	// Called by the composer right after it submits the list this object
	// recorded into. The slot is re-tagged on EVERY submission, not just the
	// ones that rewrote it: the tag has to describe the newest GPU work
	// reading that memory, or AcquireVertexSlot could hand out a slot a later
	// submission is still drawing from.
	void OnSubmitted(ID3D12CommandQueue *queue)
	{
		if (!queue || !mRenderedIndexCount || !mFence)
			return;
		mSlotFenceValues[mRenderedSlot] = ++mFenceCounter;
		queue->Signal(mFence, mFenceCounter);
	}

	// True when the frame has anything to draw at all. Read by the composer
	// before it acquires a command list, so a frame with no overlay content
	// costs two atomic reads and no recording work.
	static bool HasContent(bool hunting, bool hasNotice)
	{
		return hunting || hasNotice || DX12ProfilingHud::IsEnabled();
	}

	// True once the GPU objects exist. The skipAll fast path uses this to avoid
	// creating a device, a PSO or a heap from inside the fast path - which is
	// what used to stall startup.
	bool Ready() const
	{
		return mDevice && mRootSignature && mPipeline && mFontTextureReady &&
			mVertexBuffer && mIndexBuffer && mSrvHeap;
	}

private:
	struct TextVertex
	{
		float x, y;
		float u, v;
		float r, g, b, a;
	};

	static constexpr size_t kMaxChars = 3072;
	// Slot count is GPU-latency headroom only: every slot is rewritten strictly
	// after its previous submission retired (AcquireVertexSlot drops the frame
	// otherwise), so extra slots trade upload-heap bytes for fewer dropped text
	// frames while the GPU is behind. With content-generation caching a slot is
	// only rewritten when the text actually changed, so the ring is rarely
	// exercised at all.
	static constexpr UINT kVertexSlotCount = 6;
	static constexpr UINT kVertexSlotBytes = kMaxChars * 4 * sizeof(TextVertex);
	// The index buffer is 16-bit, and each quad consumes four vertices: the
	// highest index emitted is kMaxChars*4-1, so the buffer overflows the
	// moment this cap reaches 16384.
	static_assert(kMaxChars < 16384, "16-bit indices cannot address this many quads");
	static constexpr DWORD kFenceWaitMs = 2000;
	// A failed one-time font build is retried only after this cool-down, so a
	// persistent failure cannot rebuild the GDI atlas every single frame.
	static constexpr LONG kFontRetryFrames = 600;
	// Cool-down value that never elapses: used once the upload may already be
	// referenced by submitted GPU work.
	static constexpr LONG kFontNoRetry = 0x7fffffff;
	// Recurring skip diagnostics repeat at most once per this many frames.
	static constexpr LONG kSkipLogFrames = 600;
	static constexpr float kTextMarginX = 8.0f;
	static constexpr float kTextMarginY = 8.0f;
	static constexpr float kLineGap = 2.0f;
	static constexpr float kHuntGreen[4] = { 0.45f, 1.0f, 0.45f, 1.0f };
	static constexpr float kHuntDim[4] = { 0.35f, 0.8f, 0.35f, 1.0f };

	static constexpr wchar_t kFirstGlyph = 32;
	static constexpr UINT kGlyphCount = 95; // printable ASCII 32..126
	static constexpr UINT kAtlasCols = 16;
	// The atlas grid is kAtlasCols wide, so 95 glyphs leave exactly one cell
	// over in the last row. It is filled opaque white and reserved for solid
	// quads (the HUD backdrop): sampling a texel of value 1.0 lets a rectangle
	// reuse the text pipeline verbatim instead of needing a second PSO.
	static constexpr UINT kAtlasRows = (kGlyphCount + kAtlasCols - 1) / kAtlasCols;
	static constexpr UINT kSolidCell = kGlyphCount; // first free cell
	static_assert(kSolidCell < kAtlasRows * kAtlasCols,
		"the solid-quad cell must exist inside the atlas grid");
	// HUD panel geometry. The panel is anchored top-left and padded to a fixed
	// number of rows, so a report that grows or shrinks between intervals
	// cannot make the centred hunting text jump.
	static constexpr int kHudMaxRows = 44;
	static constexpr float kHudBackdropAlpha = 0.75f;
	static constexpr float kHudPadding = 4.0f;
	static constexpr float kHudColor[4] = { 0.93f, 0.79f, 0.30f, 1.0f }; // goldenrod
	static constexpr float kHudBackdrop[4] = { 0.0f, 0.0f, 0.0f, kHudBackdropAlpha };

	explicit DX12InGameTextOverlay(ID3D12Device *device)
		: mDevice(device)
	{
		mVertices.reserve(kMaxChars * 4);
	}

	~DX12InGameTextOverlay()
	{
		if (mVertexBuffer) {
			mVertexBuffer->Unmap(0, nullptr);
			mVertexBuffer->Release();
		}
		if (mIndexBuffer) {
			mIndexBuffer->Unmap(0, nullptr);
			mIndexBuffer->Release();
		}
		if (mFontUploadBuffer)
			mFontUploadBuffer->Release();
		if (mFontTexture)
			mFontTexture->Release();
		if (mSrvHeap)
			mSrvHeap->Release();
		if (mPipeline)
			mPipeline->Release();
		if (mRootSignature)
			mRootSignature->Release();
		if (mFence)
			mFence->Release();
	}

	static std::mutex &Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::unordered_map<ID3D12Device *, DX12InGameTextOverlay *> &Instances()
	{
		static std::unordered_map<ID3D12Device *, DX12InGameTextOverlay *> instances;
		return instances;
	}

	static void ResourceTransition(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		commandList->ResourceBarrier(1, &barrier);
	}

	static void ColorFromRef(COLORREF color, float out[4])
	{
		out[0] = GetRValue(color) / 255.0f;
		out[1] = GetGValue(color) / 255.0f;
		out[2] = GetBValue(color) / 255.0f;
		out[3] = 1.0f;
	}

	// ------------------------------------------------------------------
	// One-time GPU object setup (root signature, shaders, index buffer,
	// vertex ring, fence). The font atlas upload needs a command list and is
	// handled separately by EnsureFontTexture.
	// ------------------------------------------------------------------
	bool EnsureBaseObjects()
	{
		if (mRootSignature)
			return !mInitFailed;
		if (mInitFailed)
			return false;

		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		range.BaseShaderRegister = 0;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		// Two floats of root constants carry the pixel-to-NDC scale. Baking the
		// back-buffer size into the vertices instead (as this used to) makes
		// every cached vertex blob invalid on a resize; with the transform in
		// the shader the geometry is resolution independent, which is what lets
		// an unchanged report be re-submitted without re-uploading anything.
		D3D12_ROOT_PARAMETER parameters[2] = {};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[0].DescriptorTable.NumDescriptorRanges = 1;
		parameters[0].DescriptorTable.pDescriptorRanges = &range;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[1].Constants.ShaderRegister = 0;
		parameters[1].Constants.RegisterSpace = 0;
		parameters[1].Constants.Num32BitValues = 2;
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.ShaderRegister = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = ARRAYSIZE(parameters);
		desc.pParameters = parameters;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &sampler;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ID3DBlob *serialized = nullptr;
		ID3DBlob *error = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(
			&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
		if (error)
			error->Release();
		if (FAILED(hr)) {
			if (serialized)
				serialized->Release();
			return FailInit("root_signature_serialize");
		}
		hr = mDevice->CreateRootSignature(
			0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
			IID_PPV_ARGS(&mRootSignature));
		serialized->Release();
		if (FAILED(hr))
			return FailInit("root_signature_create");

		static const char kShaderSource[] = R"(
cbuffer HudParams : register(b0)
{
	// (2/backBufferWidth, 2/backBufferHeight)
	float2 invHalfSize;
};
struct VSIn {
	float2 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 col : COLOR0;
};
struct PSIn {
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 col : COLOR0;
};
PSIn VSMain(VSIn input)
{
	PSIn output;
	// Vertices arrive in pixels; the flip to NDC happens here so the geometry
	// stays valid across a back-buffer resize.
	output.pos = float4(input.pos.x * invHalfSize.x - 1.0f,
	                    1.0f - input.pos.y * invHalfSize.y, 0.0f, 1.0f);
	output.uv = input.uv;
	output.col = input.col;
	return output;
}
Texture2D<float> fontTexture : register(t0);
SamplerState fontSampler : register(s0);
float4 PSMain(PSIn input) : SV_TARGET
{
	float alpha = fontTexture.Sample(fontSampler, input.uv);
	return float4(input.col.rgb, input.col.a * alpha);
}
)";
		ID3DBlob *vs = nullptr;
		ID3DBlob *ps = nullptr;
		hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, nullptr,
			nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &error);
		if (error) {
			DX12_LOG_FLUSH("DX12InGameTextOverlay", "event=shader_compile_error stage=vs error=%s",
				static_cast<const char *>(error->GetBufferPointer()));
			error->Release();
		}
		if (FAILED(hr))
			return FailInit("vs_compile");
		hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, nullptr,
			nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &error);
		if (error) {
			DX12_LOG_FLUSH("DX12InGameTextOverlay", "event=shader_compile_error stage=ps error=%s",
				static_cast<const char *>(error->GetBufferPointer()));
			error->Release();
		}
		if (FAILED(hr)) {
			vs->Release();
			return FailInit("ps_compile");
		}
		mShaderVS.assign(
			static_cast<const uint8_t *>(vs->GetBufferPointer()),
			static_cast<const uint8_t *>(vs->GetBufferPointer()) + vs->GetBufferSize());
		mShaderPS.assign(
			static_cast<const uint8_t *>(ps->GetBufferPointer()),
			static_cast<const uint8_t *>(ps->GetBufferPointer()) + ps->GetBufferSize());
		vs->Release();
		ps->Release();

		// Static quad index buffer shared by every slot.
		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		bufferDesc.Width = kMaxChars * 6 * sizeof(uint16_t);
		hr = mDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIndexBuffer));
		if (FAILED(hr))
			return FailInit("index_buffer_create");
		uint16_t *indices = nullptr;
		if (FAILED(mIndexBuffer->Map(0, nullptr, reinterpret_cast<void **>(&indices))))
			return FailInit("index_buffer_map");
		for (size_t i = 0; i < kMaxChars; ++i) {
			const uint16_t base = static_cast<uint16_t>(i * 4);
			indices[i * 6 + 0] = base + 0;
			indices[i * 6 + 1] = base + 1;
			indices[i * 6 + 2] = base + 2;
			indices[i * 6 + 3] = base + 0;
			indices[i * 6 + 4] = base + 2;
			indices[i * 6 + 5] = base + 3;
		}

		bufferDesc.Width = static_cast<UINT64>(kVertexSlotBytes) * kVertexSlotCount;
		hr = mDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVertexBuffer));
		if (FAILED(hr))
			return FailInit("vertex_buffer_create");
		if (FAILED(mVertexBuffer->Map(0, nullptr, &mVertexData)))
			return FailInit("vertex_buffer_map");

		hr = mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
		if (FAILED(hr))
			return FailInit("fence_create");

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		hr = mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvHeap));
		if (FAILED(hr))
			return FailInit("srv_heap_create");
		return true;
	}

	bool FailInit(const char *stage)
	{
		DX12_LOG_FLUSH("DX12InGameTextOverlay", "event=init_failed stage=%s", stage);
		mInitFailed = true;
		return false;
	}

	// Recreates the PSO when the back buffer format changed (or on first
	// use). Everything else is format independent.
	bool EnsurePipeline(DXGI_FORMAT format)
	{
		if (!EnsureBaseObjects())
			return false;
		if (mPipeline && mPipelineFormat == format)
			return true;
		if (mPipeline) {
			mPipeline->Release();
			mPipeline = nullptr;
		}

		D3D12_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = mRootSignature;
		desc.VS.pShaderBytecode = mShaderVS.data();
		desc.VS.BytecodeLength = mShaderVS.size();
		desc.PS.pShaderBytecode = mShaderPS.data();
		desc.PS.BytecodeLength = mShaderPS.size();
		desc.InputLayout.pInputElementDescs = layout;
		desc.InputLayout.NumElements = ARRAYSIZE(layout);
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
		desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.DepthStencilState.DepthEnable = FALSE;
		desc.DepthStencilState.StencilEnable = FALSE;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = format;
		desc.SampleDesc.Count = 1;
		HRESULT hr = mDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&mPipeline));
		if (FAILED(hr)) {
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=pipeline_create_failed format=%d hr=0x%lx",
				static_cast<int>(format), hr);
			return false;
		}
		mPipelineFormat = format;
		if (!mPipelineLogged) {
			mPipelineLogged = true;
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=pipeline_ready format=%d pso=%p", static_cast<int>(format),
				static_cast<void *>(mPipeline));
		}
		return true;
	}

	// Rasterizes the printable ASCII range with GDI into an R8 alpha atlas.
	// No font files or embedded blobs: Consolas is guaranteed on any Windows
	// that can run a DX12 game, and GDI gives clean antialiased glyphs.
	bool BuildFontAtlas(std::vector<uint8_t> *alpha)
	{
		HDC screen = GetDC(nullptr);
		HDC hdc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		if (!hdc)
			return false;
		HFONT font = CreateFontW(
			-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
			FIXED_PITCH | FF_MODERN, L"Consolas");
		if (!font) {
			DeleteDC(hdc);
			return false;
		}
		HGDIOBJ oldFont = SelectObject(hdc, font);
		TEXTMETRICW metrics = {};
		GetTextMetricsW(hdc, &metrics);
		mCellW = static_cast<float>(metrics.tmAveCharWidth);
		mCellH = static_cast<float>(metrics.tmHeight + metrics.tmExternalLeading);
		const UINT cellW = static_cast<UINT>(mCellW);
		const UINT cellH = static_cast<UINT>(mCellH);
		const UINT rows = (kGlyphCount + kAtlasCols - 1) / kAtlasCols;
		const UINT atlasW = cellW * kAtlasCols;
		const UINT atlasH = cellH * rows;

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = static_cast<LONG>(atlasW);
		bmi.bmiHeader.biHeight = -static_cast<LONG>(atlasH); // top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		void *bits = nullptr;
		HBITMAP bitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bitmap || !bits) {
			SelectObject(hdc, oldFont);
			DeleteObject(font);
			DeleteDC(hdc);
			return false;
		}
		HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);
		HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		RECT fill = { 0, 0, static_cast<LONG>(atlasW), static_cast<LONG>(atlasH) };
		FillRect(hdc, &fill, black);
		SetTextColor(hdc, RGB(255, 255, 255));
		SetBkMode(hdc, TRANSPARENT);

		for (UINT glyph = 0; glyph < kGlyphCount; ++glyph) {
			const wchar_t ch = static_cast<wchar_t>(kFirstGlyph + glyph);
			const UINT col = glyph % kAtlasCols;
			const UINT row = glyph / kAtlasCols;
			RECT cell = {
				static_cast<LONG>(col * cellW), static_cast<LONG>(row * cellH),
				static_cast<LONG>((col + 1) * cellW), static_cast<LONG>((row + 1) * cellH)
			};
			ExtTextOutW(hdc, cell.left, cell.top, ETO_CLIPPED, &cell, &ch, 1, nullptr);
		}

		alpha->resize(static_cast<size_t>(atlasW) * atlasH);
		const uint8_t *pixels = static_cast<const uint8_t *>(bits);
		for (size_t i = 0; i < alpha->size(); ++i)
			(*alpha)[i] = pixels[i * 4 + 2]; // white text: any channel works

		// The one cell the glyph range does not use is filled opaque white and
		// reserved for solid quads (the HUD backdrop). Filling it here rather
		// than adding a second pipeline means a rectangle costs one vertex
		// quad and no state of its own.
		const UINT solidCol = kSolidCell % kAtlasCols;
		const UINT solidRow = kSolidCell / kAtlasCols;
		for (UINT row = solidRow * cellH; row < (solidRow + 1) * cellH; ++row) {
			uint8_t *dst = alpha->data() + static_cast<size_t>(row) * atlasW +
				solidCol * cellW;
			memset(dst, 255, cellW);
		}

		mAtlasW = atlasW;
		mAtlasH = atlasH;
		SelectObject(hdc, oldBitmap);
		DeleteObject(bitmap);
		SelectObject(hdc, oldFont);
		DeleteObject(font);
		DeleteDC(hdc);
		return true;
	}

	// One-time font atlas upload on the render thread with the game queue
	// available. The SRV is published only once the copy retired, a failed
	// attempt is cooled down instead of being rebuilt every frame, and the
	// upload buffer is owned by the object because the GPU reads it.
	bool EnsureFontTexture(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue)
	{
		(void)swapChain;
		if (mFontTextureReady)
			return true;
		if (!queue)
			return false;
		const LONG present = DX12RuntimeState::GetPresentCount();
		if (mFontInitFailed && present < mFontRetryPresent)
			return false;

		std::vector<uint8_t> alpha;
		if (!BuildFontAtlas(&alpha))
			return FailFontTexture("build_atlas", true);

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width = mAtlasW;
		textureDesc.Height = mAtlasH;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		HRESULT hr = mDevice->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mFontTexture));
		if (FAILED(hr)) {
			mFontTexture = nullptr;
			return FailFontTexture("texture_create", true);
		}

		const UINT rowPitch = (mAtlasW + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
			~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
		const UINT64 uploadBytes = static_cast<UINT64>(rowPitch) * mAtlasH;
		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width = uploadBytes;
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource *upload = nullptr;
		hr = mDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
		if (FAILED(hr))
			return FailFontTexture("upload_create", true);
		uint8_t *mapped = nullptr;
		if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped)))) {
			upload->Release();
			return FailFontTexture("upload_map", true);
		}
		for (UINT row = 0; row < mAtlasH; ++row)
			memcpy(mapped + static_cast<size_t>(row) * rowPitch,
				alpha.data() + static_cast<size_t>(row) * mAtlasW, mAtlasW);
		upload->Unmap(0, nullptr);
		// The recorded copy reads this buffer on the GPU: hand it to the object
		// instead of releasing it right after the submission.
		if (mFontUploadBuffer)
			mFontUploadBuffer->Release();
		mFontUploadBuffer = upload;

		Bunny::DX12PresentOverlayChannel *channel =
			Bunny::DX12PresentOverlayChannel::ForDevice(mDevice);
		if (!channel)
			return FailFontTexture("channel_unavailable", true);
		Bunny::DX12PresentOverlayChannel::RecordScope recordScope(channel);
		ID3D12GraphicsCommandList *commandList = channel->BeginRecord();
		if (!commandList)
			return FailFontTexture(channel->LastFailure(), true);
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = mFontTexture;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = upload;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Offset = 0;
		src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
		src.PlacedFootprint.Footprint.Width = mAtlasW;
		src.PlacedFootprint.Footprint.Height = mAtlasH;
		src.PlacedFootprint.Footprint.Depth = 1;
		src.PlacedFootprint.Footprint.RowPitch = rowPitch;
		commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		ResourceTransition(commandList, mFontTexture,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		// Past this point the texture may already be referenced by submitted
		// GPU work, so it is never released early: a failure keeps it and
		// stops retrying rather than risking work against freed memory.
		if (!channel->Submit(queue))
			return FailFontTexture(channel->LastFailure(), false);

		// The SRV must not be published before the copy retired; wait out the
		// one-time upload inline (init path, not a frame hot path).
		const UINT64 uploadFenceValue = ++mFenceCounter;
		queue->Signal(mFence, uploadFenceValue);
		if (!WaitForFenceValue(uploadFenceValue))
			return FailFontTexture("upload_fence_timeout", false);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		mDevice->CreateShaderResourceView(
			mFontTexture, &srvDesc, mSrvHeap->GetCPUDescriptorHandleForHeapStart());
		mFontTextureReady = true;
		mFontInitFailed = false;
		DX12_LOG_FLUSH("DX12InGameTextOverlay",
			"event=font_atlas_ready atlas=%ux%u cell=%ux%u",
			mAtlasW, mAtlasH,
			static_cast<UINT>(mCellW), static_cast<UINT>(mCellH));
		return true;
	}

	// `dropTexture` is false only once the copy may have reached the GPU: the
	// half-built texture is then kept (a retry could otherwise race in-flight
	// work) and no further attempt is made, because reaching this state means
	// the queue or the device is already unusable.
	bool FailFontTexture(const char *stage, bool dropTexture)
	{
		if (dropTexture && mFontTexture) {
			mFontTexture->Release();
			mFontTexture = nullptr;
		}
		mFontTextureReady = false;
		mFontInitFailed = true;
		mFontRetryPresent = dropTexture ?
			DX12RuntimeState::GetPresentCount() + kFontRetryFrames : kFontNoRetry;
		DX12_LOG_FLUSH("DX12InGameTextOverlay",
			"event=font_atlas_failed stage=%s retry=%s",
			stage, dropTexture ? "cooldown" : "never");
		return false;
	}

	// Returns true when the fence value has been reached. A false result means
	// the GPU still owns whatever was tagged with that value, so every caller
	// must bail out instead of rewriting or releasing it.
	bool WaitForFenceValue(UINT64 value)
	{
		if (mFence->GetCompletedValue() >= value)
			return true;
		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!event) {
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=fence_wait_event_failed value=%llu",
				static_cast<unsigned long long>(value));
			return false;
		}
		bool retired = false;
		if (SUCCEEDED(mFence->SetEventOnCompletion(value, event)))
			retired = WaitForSingleObject(event, kFenceWaitMs) == WAIT_OBJECT_0;
		CloseHandle(event);
		if (!retired) {
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=fence_wait_timeout value=%llu completed=%llu",
				static_cast<unsigned long long>(value),
				static_cast<unsigned long long>(mFence->GetCompletedValue()));
		}
		return retired;
	}

	// Returns the next writable vertex slot after its previous submission
	// retired, or nullptr when that slot is still in flight: overwriting
	// memory the GPU is reading is worse than dropping one text frame.
	void *AcquireVertexSlot()
	{
		const UINT next = (mVertexCursor + 1) % kVertexSlotCount;
		const UINT64 pending = mSlotFenceValues[next];
		if (pending && !WaitForFenceValue(pending))
			return nullptr;
		mVertexCursor = next;
		return static_cast<uint8_t *>(mVertexData) +
			static_cast<size_t>(mVertexCursor) * kVertexSlotBytes;
	}

	// ------------------------------------------------------------------
	// Text layout: monospace cells emitted in *pixel* space. The vertex shader
	// applies the pixel-to-NDC scale from a root constant, so the geometry is
	// resolution independent and an unchanged report can be re-submitted
	// without touching the upload heap.
	// ------------------------------------------------------------------
	void GlyphUv(UINT cell, float *u0, float *v0, float *u1, float *v1) const
	{
		const UINT col = cell % kAtlasCols;
		const UINT row = cell / kAtlasCols;
		const float atlasW = static_cast<float>(mAtlasW);
		const float atlasH = static_cast<float>(mAtlasH);
		*u0 = (col * mCellW) / atlasW;
		*v0 = (row * mCellH) / atlasH;
		*u1 = ((col + 1) * mCellW) / atlasW;
		*v1 = ((row + 1) * mCellH) / atlasH;
	}

	void EmitQuad(float px, float py, float w, float h, UINT cell, const float color[4])
	{
		if (mVertices.size() + 4 > kMaxChars * 4)
			return;
		float u0 = 0.0f;
		float v0 = 0.0f;
		float u1 = 0.0f;
		float v1 = 0.0f;
		GlyphUv(cell, &u0, &v0, &u1, &v1);
		const float x1 = px + w;
		const float y1 = py + h;
		mVertices.push_back({ px, py, u0, v0, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x1, py, u1, v0, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x1, y1, u1, v1, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ px, y1, u0, v1, color[0], color[1], color[2], color[3] });
	}

	void EmitChar(wchar_t ch, float px, float py, const float color[4])
	{
		if (ch < kFirstGlyph || ch >= kFirstGlyph + kGlyphCount)
			ch = L'?';
		EmitQuad(px, py, mCellW, mCellH,
			static_cast<UINT>(ch - kFirstGlyph), color);
	}

	// A filled rectangle. All four corners sample the *centre* of the reserved
	// white cell, so linear filtering cannot pull a neighbouring glyph in and
	// the rectangle needs no second pipeline, root signature or blend state.
	void EmitSolid(float px, float py, float w, float h, const float color[4])
	{
		if (mVertices.size() + 4 > kMaxChars * 4)
			return;
		const UINT col = kSolidCell % kAtlasCols;
		const UINT row = kSolidCell / kAtlasCols;
		const float u = (col + 0.5f) * mCellW / static_cast<float>(mAtlasW);
		const float v = (row + 0.5f) * mCellH / static_cast<float>(mAtlasH);
		const float x1 = px + w;
		const float y1 = py + h;
		mVertices.push_back({ px, py, u, v, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x1, py, u, v, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x1, y1, u, v, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ px, y1, u, v, color[0], color[1], color[2], color[3] });
	}

	void EmitLine(const wchar_t *line, size_t length, float px, float py, const float color[4])
	{
		for (size_t i = 0; i < length; ++i)
			EmitChar(line[i], px + i * mCellW, py, color);
	}

	// The report is ASCII, so a byte maps straight onto a glyph.
	void EmitAsciiLine(const char *text, size_t length, float px, float py, const float color[4])
	{
		for (size_t i = 0; i < length; ++i)
			EmitChar(static_cast<wchar_t>(static_cast<unsigned char>(text[i])),
				px + i * mCellW, py, color);
	}

	float EmitLineCentered(const wchar_t *line, float py, const float color[4])
	{
		const size_t length = line ? wcslen(line) : 0;
		float px = (mBackW - length * mCellW) * 0.5f;
		if (px < kTextMarginX)
			px = kTextMarginX;
		EmitLine(line, length, px, py, color);
		return py + mCellH + kLineGap;
	}

	float EmitLinesCentered(const wchar_t *text, float py, const float color[4])
	{
		for (const wchar_t *line = text; line && *line;) {
			const wchar_t *end = wcschr(line, L'\n');
			const size_t length = end ? static_cast<size_t>(end - line) : wcslen(line);
			float px = (mBackW - length * mCellW) * 0.5f;
			if (px < kTextMarginX)
				px = kTextMarginX;
			EmitLine(line, length, px, py, color);
			py += mCellH + kLineGap;
			line = end ? end + 1 : nullptr;
		}
		return py;
	}

	float EmitLinesLeft(const wchar_t *text, float py, const float color[4], float px = kTextMarginX)
	{
		for (const wchar_t *line = text; line && *line;) {
			const wchar_t *end = wcschr(line, L'\n');
			const size_t length = end ? static_cast<size_t>(end - line) : wcslen(line);
			EmitLine(line, length, px, py, color);
			py += mCellH + kLineGap;
			line = end ? end + 1 : nullptr;
		}
		return py;
	}

	// ------------------------------------------------------------------
	// Profiling panel: a translucent backdrop sized to the widest line with
	// the report drawn on top, anchored top-left. The backdrop height uses a
	// grow-only high-water mark for the current view, so a report that gains
	// or loses a row between intervals cannot make the centred hunting text
	// below it drift; switching views resets the mark.
	// ------------------------------------------------------------------
	float EmitProfilingPanel(const char *report, size_t reportLength, float topY)
	{
		if (!report || !reportLength)
			return topY;

		// The high-water mark belongs to one view: a new view starts from its
		// own height instead of inheriting the previous one's.
		const LONG mode = static_cast<LONG>(DX12ProfilingHud::GetMode());
		if (mode != mHudRowPadMode) {
			mHudRowPadMode = mode;
			mHudRowPad = 0;
		}

		const UINT rows = CountLines(report, reportLength);
		const size_t widest = WidestLine(report, reportLength);
		if (!rows || !widest)
			return topY;
		if (static_cast<int>(rows) > mHudRowPad)
			mHudRowPad = static_cast<int>(rows);

		const float lineH = mCellH + kLineGap;
		const float panelW = widest * mCellW + 2.0f * kHudPadding;
		const float panelH = mHudRowPad * lineH + 2.0f * kHudPadding;
		EmitSolid(kTextMarginX, topY, panelW, panelH, kHudBackdrop);

		float y = topY + kHudPadding;
		const float textX = kTextMarginX + kHudPadding;
		for (const char *line = report; line && *line;) {
			const char *end = static_cast<const char *>(memchr(line, '\n',
				reportLength - static_cast<size_t>(line - report)));
			const size_t length = end
				? static_cast<size_t>(end - line)
				: reportLength - static_cast<size_t>(line - report);
			EmitAsciiLine(line, length, textX, y, kHudColor);
			y += lineH;
			if (!end)
				break;
			line = end + 1;
		}
		return topY + panelH;
	}

	static UINT CountLines(const char *text, size_t length)
	{
		if (!text || !length)
			return 0;
		UINT rows = 1;
		for (size_t i = 0; i < length; ++i) {
			if (text[i] != '\n')
				continue;
			// A trailing newline terminates the last row rather than starting
			// an empty one.
			if (i + 1 < length)
				++rows;
		}
		return rows;
	}

	static size_t WidestLine(const char *text, size_t length)
	{
		size_t widest = 0;
		size_t current = 0;
		for (size_t i = 0; i < length; ++i) {
			if (text[i] == '\n') {
				if (current > widest)
					widest = current;
				current = 0;
			} else {
				++current;
			}
		}
		if (current > widest)
			widest = current;
		return widest;
	}

	// ------------------------------------------------------------------
	// Content identity. Everything that can move a vertex folds into one key:
	// when the key matches what is already in the upload heap nothing is
	// rebuilt, nothing is uploaded and the ring slot stays untouched.
	// ------------------------------------------------------------------
	static UINT64 HashText(const wchar_t *text)
	{
		UINT64 hash = 1469598103934665603ull; // FNV-1a
		for (; text && *text; ++text) {
			hash ^= static_cast<UINT64>(static_cast<UINT16>(*text));
			hash *= 1099511628211ull;
		}
		return hash;
	}

	static UINT64 Mix(UINT64 hash, UINT64 value)
	{
		hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		return hash;
	}

	UINT64 ContentKey(
		const wchar_t *text, bool hunting,
		const wchar_t *notice, COLORREF noticeColor,
		UINT64 reportVersion, UINT width, UINT height) const
	{
		UINT64 key = HashText(text);
		key = Mix(key, hunting ? 1u : 0u);
		key = Mix(key, HashText(notice));
		key = Mix(key, static_cast<UINT64>(noticeColor));
		// The HUD contributes its version, not its bytes: the version only
		// moves when the writer published a different report.
		key = Mix(key, DX12ProfilingHud::IsEnabled() ? reportVersion + 1 : 0);
		// Width and height matter even though the geometry is in pixels: the
		// centred hunting text is laid out from the back-buffer width.
		key = Mix(key, (static_cast<UINT64>(width) << 32) | height);
		key = Mix(key, static_cast<UINT64>(mHudRowPad));
		return key;
	}

	// Emits the whole frame's geometry and returns the index count to draw.
	UINT BuildVertices(
		const wchar_t *text, bool hunting,
		const wchar_t *notice, COLORREF noticeColor,
		const char *report, size_t reportLength)
	{
		float y = kTextMarginY;

		// The panel is drawn first so the text it carries lands on top, and so
		// the hunting text below it can start after the panel's full height.
		if (DX12ProfilingHud::IsEnabled() && reportLength)
			y = EmitProfilingPanel(report, reportLength, kTextMarginY);

		if (hunting) {
			if (y > kTextMarginY)
				y += kLineGap;
			y = EmitLinesCentered(text, y, kHuntGreen);
			if (notice && notice[0]) {
				float noticeColor4[4];
				ColorFromRef(noticeColor, noticeColor4);
				y = EmitLinesLeft(notice, y + kLineGap, noticeColor4);
			}
			EmitLineCentered(L"Shader Hunting Mode (DX12)",
				mBackH - 2.0f * (mCellH + kLineGap) - kTextMarginY, kHuntGreen);
			EmitLineCentered(
				L"Num0 off | 1/2 4/5 7/8 / * prev/next PS VS IB VB | 3 6 9 - copy hash | "
				L".+1/2/3 CS | Num+ done | F8 frame F9 shaders F10 reload | F11 profiling",
				mBackH - (mCellH + kLineGap) - kTextMarginY, kHuntDim);
		} else if (notice && notice[0]) {
			if (y > kTextMarginY)
				y += kLineGap;
			float noticeColor4[4];
			ColorFromRef(noticeColor, noticeColor4);
			EmitLinesLeft(notice, y, noticeColor4);
		}

		return mVertices.empty()
			? 0u
			: static_cast<UINT>(mVertices.size() / 4 * 6);
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12RootSignature *mRootSignature = nullptr;
	ID3D12PipelineState *mPipeline = nullptr;
	ID3D12Resource *mVertexBuffer = nullptr;
	ID3D12Resource *mIndexBuffer = nullptr;
	// Kept alive for the object's lifetime on purpose: the atlas copy reads it
	// on the GPU, so it must outlive the submission that retired the fence.
	ID3D12Resource *mFontUploadBuffer = nullptr;
	ID3D12Resource *mFontTexture = nullptr;
	ID3D12DescriptorHeap *mSrvHeap = nullptr;
	ID3D12Fence *mFence = nullptr;
	void *mVertexData = nullptr;
	std::vector<uint8_t> mShaderVS;
	std::vector<uint8_t> mShaderPS;
	std::vector<TextVertex> mVertices;
	DXGI_FORMAT mPipelineFormat = DXGI_FORMAT_UNKNOWN;
	UINT mVertexCursor = 0;
	UINT64 mFenceCounter = 0;
	UINT64 mSlotFenceValues[kVertexSlotCount] = {};
	bool mInitFailed = false;
	// True only once the SRV exists; mFontTexture alone means "upload started".
	bool mFontTextureReady = false;
	bool mFontInitFailed = false;
	LONG mFontRetryPresent = 0;
	bool mPipelineLogged = false;
	bool mFirstFrameLogged = false;
	LONG mSlotBusyLogPresent = 0;
	float mCellW = 0.0f;
	float mCellH = 0.0f;
	UINT mAtlasW = 0;
	UINT mAtlasH = 0;
	float mBackW = 0.0f;
	float mBackH = 0.0f;

	// --- frame-to-frame reuse -------------------------------------------
	// The key of the geometry currently sitting in the upload heap, the slot
	// that holds it and how much of it to draw. While a frame's key matches,
	// the layout pass and the upload are skipped and only the draw is
	// re-recorded - which is what makes an idle HUD nearly free.
	UINT64 mRenderedContentKey = 0;
	UINT mRenderedSlot = 0;
	UINT mRenderedIndexCount = 0;
	UINT mRenderedVertexBytes = 0;
	// Version of the report currently held in mReport, so a frame that changes
	// for some other reason (hunting toggled, notice posted) does not re-copy
	// the profiling text.
	UINT64 mHudTextVersion = 0;
	// Grow-only backdrop height for the current view.
	int mHudRowPad = 0;
	LONG mHudRowPadMode = -1;
	// A title may present from more than one thread; the frame is serialised
	// on this so the ring, the layout vector and the cursor stay consistent.
	std::mutex mRenderLock;
};

// ---------------------------------------------------------------------------
// Module entry points.
//
// The overlay no longer submits anything itself: every overlay source in a
// frame is recorded into the one command list DX12PresentOverlayCompose owns,
// so a frame costs a single ExecuteCommandLists instead of one per source.
// ---------------------------------------------------------------------------

bool DX12NativeOverlayHasContent()
{
	// Two atomic reads: the hunting flag and the notice slot. Callers use this
	// to skip resolving a device, a queue and a command list on a frame that
	// would draw nothing.
	const bool hunting = DX12HuntIsEnabled();
	wchar_t notice[512] = {};
	const bool hasNotice =
		DX12OverlayStatusState::GetTransientStatus(notice, ARRAYSIZE(notice), nullptr);
	return DX12InGameTextOverlay::HasContent(hunting, hasNotice);
}

// Prepares the overlay's GPU objects and font atlas. MUST be called before the
// composer acquires the shared channel: the atlas upload takes a channel slot
// of its own, and doing that while the recording lock is held would deadlock.
bool DX12NativeOverlayPrepare(
	IDXGISwapChain *swapChain, ID3D12CommandQueue *queue, DXGI_FORMAT format)
{
	if (!swapChain || !queue)
		return false;
	ID3D12Device *device = nullptr;
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return false;
	DX12InGameTextOverlay *overlay = DX12InGameTextOverlay::ForDevice(device);
	device->Release(); // the instance borrows the device, cache-lifetime owned
	return overlay && overlay->Prepare(swapChain, queue, format);
}

// Records the overlay text into `commandList`, which the caller has already
// opened and will submit. Returns the index count drawn (0 = nothing visible).
UINT DX12NativeOverlayRecord(
	ID3D12GraphicsCommandList *commandList,
	IDXGISwapChain *swapChain,
	ID3D12CommandQueue *queue,
	D3D12_CPU_DESCRIPTOR_HANDLE rtv,
	DXGI_FORMAT format,
	UINT width,
	UINT height)
{
	if (!commandList || !swapChain || !queue)
		return 0;

	const bool hunting = DX12HuntIsEnabled();
	wchar_t notice[512] = {};
	COLORREF noticeColor = RGB(0, 255, 0);
	const bool hasNotice =
		DX12OverlayStatusState::GetTransientStatus(notice, ARRAYSIZE(notice), &noticeColor);
	if (!DX12InGameTextOverlay::HasContent(hunting, hasNotice))
		return 0;

	// Frame-boundary breadcrumb: the freeze watchdog reports the last stage
	// this process entered, so an overlay-side stall names itself in the log.
	DX12MarkStage("hunt_overlay_begin");

	ID3D12Device *device = nullptr;
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return 0;
	DX12InGameTextOverlay *overlay = DX12InGameTextOverlay::FindDevice(device);
	device->Release(); // the instance borrows the device, cache-lifetime owned
	if (!overlay)
		return 0;

	wchar_t text[1024] = {};
	if (hunting)
		DX12HuntComposeOverlayText(text, ARRAYSIZE(text));

	const UINT drawn = overlay->Record(
		commandList, swapChain, queue, rtv, format, width, height,
		text, hunting, hasNotice ? notice : nullptr, noticeColor);
	DX12MarkStage("hunt_overlay_done");
	return drawn;
}

void DX12NativeOverlayOnSubmitted(IDXGISwapChain *swapChain, ID3D12CommandQueue *queue)
{
	if (!swapChain || !queue)
		return;
	ID3D12Device *device = nullptr;
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return;
	DX12InGameTextOverlay *overlay = DX12InGameTextOverlay::ForDevice(device);
	device->Release();
	if (overlay)
		overlay->OnSubmitted(queue);
}

// One-shot permission for the skipAll fast path to create the overlay's GPU
// objects, plus the frame count past which an ini-enabled HUD may arm itself.
//
// The fast path is barred from creating GPU objects (root signature, runtime
// shader compile, PSO, heaps, and a font-atlas upload that waits on a fence)
// because doing that during the first frames of a session is what used to stall
// startup. Two things therefore have to be true before it may:
//   - a hotkey press, which by definition happens well past startup, or
//   - the session is demonstrably running (kFastPathInitMinPresents presents
//     served, the same order as the default config-initialisation delay), which
//     covers an ini-configured HUD in a session whose mod does no per-draw work
//     and so never leaves the fast path.
namespace
{
	constexpr LONG kFastPathInitMinPresents = 120;
	std::atomic<LONG> gFastPathInitRequested{ 0 };
}

void DX12NativeOverlayRequestInit()
{
	gFastPathInitRequested.store(1, std::memory_order_relaxed);
}

// True once the overlay's GPU objects exist and its font atlas is uploaded.
// The skipAll fast path uses this to draw the HUD without creating a device, a
// PSO, a heap or a queue submission from inside the fast path - except in the
// two cases above, where nothing else would ever create them.
bool DX12NativeOverlayReady(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;
	if (!DX12NativeOverlayHasContent())
		return false;
	ID3D12Device *device = nullptr;
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return false;

	bool mayInitialize =
		gFastPathInitRequested.load(std::memory_order_relaxed) != 0;
	if (!mayInitialize && DX12ProfilingHud::IsEnabled())
		mayInitialize = DX12RuntimeState::GetPresentCount() >=
			kFastPathInitMinPresents;

	DX12InGameTextOverlay *overlay = mayInitialize
		? DX12InGameTextOverlay::ForDevice(device)
		: DX12InGameTextOverlay::FindDevice(device);
	device->Release();
	if (!overlay)
		return false;
	if (overlay->Ready()) {
		// Spent: from here on the fast path only ever draws.
		gFastPathInitRequested.store(0, std::memory_order_relaxed);
		return true;
	}
	// Keep the request armed so the next frame finishes the job. If that frame
	// takes the normal path instead, it initialises there and clears the flag.
	return false;
}

void DX12NativeOverlayInvalidate(IDXGISwapChain *swapChain)
{
	// ResizeBuffers swapped the back buffers under a stable chain pointer:
	// drop the cached RTV so the next frame recreates it (also covers the
	// mod [Present] overlay, which shares the same cache).
	Bunny::DX12CustomShaderSurfaceCache::InvalidateChain(swapChain);
}

void DX12NativeOverlayShutdown()
{
	DX12InGameTextOverlay::ClearAll();
}
