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

	// Draws one frame of text. `text` may contain '\n'; each line is laid out
	// horizontally centered at the top of the back buffer in hunting green.
	// The bottom two lines show the mode title and the key hints while
	// hunting. `notice`/`noticeColor` draw one transient line below the
	// hunting text (or at the top when hunting is off).
	bool Render(
		IDXGISwapChain *swapChain, ID3D12CommandQueue *queue,
		const wchar_t *text, bool hunting,
		const wchar_t *notice, COLORREF noticeColor)
	{
		if (!swapChain || !queue || !mDevice)
			return false;
		std::lock_guard<std::mutex> renderLock(mRenderLock);

		Bunny::DX12CustomShaderSurfaceCache *surfaceCache =
			Bunny::DX12CustomShaderSurfaceCache::ForDevice(mDevice);
		Bunny::DX12PresentOverlayChannel *channel =
			Bunny::DX12PresentOverlayChannel::ForDevice(mDevice);
		if (!surfaceCache || !channel)
			return false;
		const LONG present = DX12RuntimeState::GetPresentCount();

		D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT width = 0;
		UINT height = 0;
		if (!surfaceCache->EnsureBackBufferView(swapChain, &rtv, &format, &width, &height))
			return false;
		if (!EnsurePipeline(format))
			return false;
		if (!EnsureFontTexture(swapChain, queue))
			return false;
		mBackW = static_cast<float>(width);
		mBackH = static_cast<float>(height);

		mVertices.clear();
		float y = kTextMarginY;
		if (hunting) {
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
				L".+1/2/3 CS | Num+ done | F8 frame F9 shaders F10 reload",
				mBackH - (mCellH + kLineGap) - kTextMarginY, kHuntDim);
		} else if (notice && notice[0]) {
			float noticeColor4[4];
			ColorFromRef(noticeColor, noticeColor4);
			EmitLinesLeft(notice, y, noticeColor4);
		}
		if (mVertices.empty())
			return true; // Nothing to show this frame; not an error.

		const UINT indexCount = static_cast<UINT>(mVertices.size() / 4 * 6);
		void *vertexData = AcquireVertexSlot();
		if (!vertexData) {
			// Its previous submission has not retired yet: drop this frame's
			// text rather than rewriting memory the GPU is reading.
			const LONG skippedFor = present - mSlotBusyLogPresent;
			if (skippedFor >= kSkipLogFrames) {
				mSlotBusyLogPresent = present;
				DX12_LOG_FLUSH("DX12InGameTextOverlay",
					"event=skip_frame reason=vertex_slot_busy frames=%ld",
					static_cast<long>(skippedFor));
			}
			return false;
		}
		memcpy(vertexData, mVertices.data(), mVertices.size() * sizeof(TextVertex));

		// BeginRecord..Submit is one critical section: the channel hands out a
		// list and closes whichever slot its cursor points at, so another
		// recording thread in between would close the wrong slot.
		Bunny::DX12PresentOverlayChannel::RecordScope recordScope(channel);
		ID3D12GraphicsCommandList *commandList = channel->BeginRecord();
		if (!commandList) {
			const LONG failedFor = present - mBeginRecordFailLogPresent;
			if (failedFor >= kSkipLogFrames) {
				mBeginRecordFailLogPresent = present;
				DX12_LOG_FLUSH("DX12InGameTextOverlay",
					"event=skip_frame reason=begin_record_failed detail=%s frames=%ld",
					channel->LastFailure(), static_cast<long>(failedFor));
			}
			return false;
		}

		ID3D12Resource *backBuffer = CurrentBackBuffer(swapChain);
		if (backBuffer) {
			ResourceTransition(commandList, backBuffer,
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		}

		ID3D12DescriptorHeap *heaps[] = { mSrvHeap };
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(mRootSignature);
		commandList->SetGraphicsRootDescriptorTable(
			0, mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		commandList->SetPipelineState(mPipeline);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = mVertexBuffer->GetGPUVirtualAddress() +
			static_cast<UINT64>(mVertexCursor) * kVertexSlotBytes;
		vbv.StrideInBytes = sizeof(TextVertex);
		vbv.SizeInBytes = static_cast<UINT>(mVertices.size() * sizeof(TextVertex));
		commandList->IASetVertexBuffers(0, 1, &vbv);

		D3D12_INDEX_BUFFER_VIEW ibv = {};
		ibv.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
		ibv.Format = DXGI_FORMAT_R16_UINT;
		ibv.SizeInBytes = kMaxChars * 6 * sizeof(uint16_t);
		commandList->IASetIndexBuffer(&ibv);

		commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		D3D12_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &viewport);
		D3D12_RECT scissor = {};
		scissor.right = static_cast<LONG>(width);
		scissor.bottom = static_cast<LONG>(height);
		commandList->RSSetScissorRects(1, &scissor);
		commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

		if (backBuffer) {
			ResourceTransition(commandList, backBuffer,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			backBuffer->Release();
		}

		if (!channel->Submit(queue)) {
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=submit_failed detail=%s", channel->LastFailure());
			return false;
		}
		// Tag the vertex slot with the fence value of this submission; the
		// ring is only rewritten after the GPU passed it.
		mSlotFenceValues[mVertexCursor] = ++mFenceCounter;
		queue->Signal(mFence, mFenceCounter);
		if (!mFirstFrameLogged) {
			mFirstFrameLogged = true;
			DX12_LOG_FLUSH("DX12InGameTextOverlay",
				"event=first_frame_drawn backBuffer=%ux%u quads=%zu indexCount=%u slot=%u",
				width, height, mVertices.size() / 4, indexCount, mVertexCursor);
		}
		return true;
	}

private:
	struct TextVertex
	{
		float x, y;
		float u, v;
		float r, g, b, a;
	};

	static constexpr size_t kMaxChars = 1024;
	// Slot count is GPU-latency headroom only: every slot is rewritten strictly
	// after its previous submission retired (AcquireVertexSlot drops the frame
	// otherwise), so extra slots trade 128 KB of upload heap each for fewer
	// dropped text frames while the GPU is behind.
	static constexpr UINT kVertexSlotCount = 6;
	static constexpr UINT kVertexSlotBytes = kMaxChars * 4 * sizeof(TextVertex);
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

	static ID3D12Resource *CurrentBackBuffer(IDXGISwapChain *swapChain)
	{
		UINT index = 0;
		IDXGISwapChain3 *swapChain3 = nullptr;
		if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) && swapChain3) {
			index = swapChain3->GetCurrentBackBufferIndex();
			swapChain3->Release();
		}
		ID3D12Resource *backBuffer = nullptr;
		if (FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer))))
			return nullptr;
		return backBuffer;
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
		D3D12_ROOT_PARAMETER parameter = {};
		parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameter.DescriptorTable.NumDescriptorRanges = 1;
		parameter.DescriptorTable.pDescriptorRanges = &range;
		parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.ShaderRegister = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = 1;
		desc.pParameters = &parameter;
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
	output.pos = float4(input.pos, 0.0f, 1.0f);
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
	// Text layout: monospace cells, NDC computed on the CPU so the pipeline
	// needs no constant buffers.
	// ------------------------------------------------------------------
	void EmitChar(wchar_t ch, float px, float py, float backW, float backH, const float color[4])
	{
		if (mVertices.size() + 4 > kMaxChars * 4)
			return;
		if (ch < kFirstGlyph || ch >= kFirstGlyph + kGlyphCount)
			ch = L'?';
		const UINT glyph = static_cast<UINT>(ch - kFirstGlyph);
		const UINT col = glyph % kAtlasCols;
		const UINT row = glyph / kAtlasCols;
		const float atlasW = static_cast<float>(mAtlasW);
		const float atlasH = static_cast<float>(mAtlasH);
		const float u0 = (col * mCellW) / atlasW;
		const float v0 = (row * mCellH) / atlasH;
		const float u1 = ((col + 1) * mCellW) / atlasW;
		const float v1 = ((row + 1) * mCellH) / atlasH;
		const float x0 = px / backW * 2.0f - 1.0f;
		const float x1 = (px + mCellW) / backW * 2.0f - 1.0f;
		const float y0 = 1.0f - py / backH * 2.0f;
		const float y1 = 1.0f - (py + mCellH) / backH * 2.0f;
		mVertices.push_back({ x0, y0, u0, v0, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x1, y0, u1, v0, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x1, y1, u1, v1, color[0], color[1], color[2], color[3] });
		mVertices.push_back({ x0, y1, u0, v1, color[0], color[1], color[2], color[3] });
	}

	void EmitLine(const wchar_t *line, size_t length, float px, float py,
		float backW, float backH, const float color[4])
	{
		for (size_t i = 0; i < length; ++i)
			EmitChar(line[i], px + i * mCellW, py, backW, backH, color);
	}

	float EmitLineCentered(const wchar_t *line, float py, const float color[4])
	{
		const size_t length = line ? wcslen(line) : 0;
		float px = (mBackW - length * mCellW) * 0.5f;
		if (px < kTextMarginX)
			px = kTextMarginX;
		EmitLine(line, length, px, py, mBackW, mBackH, color);
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
			EmitLine(line, length, px, py, mBackW, mBackH, color);
			py += mCellH + kLineGap;
			line = end ? end + 1 : nullptr;
		}
		return py;
	}

	float EmitLinesLeft(const wchar_t *text, float py, const float color[4])
	{
		for (const wchar_t *line = text; line && *line;) {
			const wchar_t *end = wcschr(line, L'\n');
			const size_t length = end ? static_cast<size_t>(end - line) : wcslen(line);
			EmitLine(line, length, kTextMarginX, py, mBackW, mBackH, color);
			py += mCellH + kLineGap;
			line = end ? end + 1 : nullptr;
		}
		return py;
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
	LONG mBeginRecordFailLogPresent = 0;
	float mCellW = 0.0f;
	float mCellH = 0.0f;
	UINT mAtlasW = 0;
	UINT mAtlasH = 0;
	float mBackW = 0.0f;
	float mBackH = 0.0f;
	// A title may present from more than one thread; the frame is serialised
	// on this so the ring, the layout vector and the cursor stay consistent.
	std::mutex mRenderLock;
};

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;

	// Fast path: with hunting off and no live notice there is nothing to
	// draw, so this costs two atomic reads per frame and no GPU work at all.
	const bool hunting = DX12HuntIsEnabled();
	wchar_t notice[512] = {};
	COLORREF noticeColor = RGB(0, 255, 0);
	const bool hasNotice =
		DX12OverlayStatusState::GetTransientStatus(notice, ARRAYSIZE(notice), &noticeColor);
	if (!hunting && !hasNotice)
		return false;

	// Frame-boundary breadcrumb: the freeze watchdog reports the last stage
	// this process entered, so an overlay-side stall names itself in the log.
	DX12MarkStage("hunt_overlay_begin");

	ID3D12Device *device = nullptr;
	if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return false;
	DX12InGameTextOverlay *overlay = DX12InGameTextOverlay::ForDevice(device);
	device->Release(); // the instance borrows the device, cache-lifetime owned
	ID3D12CommandQueue *queue = DX12ModAcquireGameDirectQueue();
	if (!overlay || !queue) {
		if (queue)
			queue->Release();
		return false;
	}

	wchar_t text[1024] = {};
	if (hunting)
		DX12HuntComposeOverlayText(text, ARRAYSIZE(text));

	const bool drawn = overlay->Render(
		swapChain, queue, text, hunting, hasNotice ? notice : nullptr, noticeColor);
	queue->Release();
	DX12MarkStage("hunt_overlay_done");
	return drawn;
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
