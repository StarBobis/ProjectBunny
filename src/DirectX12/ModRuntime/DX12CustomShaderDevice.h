#pragma once

// DX12CustomShaderDevice.h - device-side objects for custom element draws
// (phase 2.3, device half).
//
//  * DX12IniParamsTexture - the "IniParams" 1D float4 texture custom shaders
//    read at register t120 (see DX12CustomShaderRegisters). Owns the default
//    heap texture, its CPU-only SRV descriptor and the upload ring: 4 frame
//    slots (frames-in-flight) each subdivided into per-element regions
//    (kMaxCustomElementsPerFrame), because upload memcpys land before the
//    list is submitted and elements of one frame must not overwrite each
//    other. The executor uploads the packed register values on the game
//    command list right before the custom draw (CopyTextureRegion +
//    barriers), so no per-frame GPU sync is needed.
//  * DX12CustomShaderPipelineCache - lazily created ID3D12RootSignature
//    (shared by every element) and per-element ID3D12PipelineState (keyed by
//    section + RTV format). Compiles the element's stage files on first use.
//
// Both are process-lifetime singletons keyed by device, owned by the cache
// (callers borrow and must not release) and cleared on mod reload.

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_3.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Config/DX12Config.h"
#include "DX12CustomShaderCompile.h"
#include "DX12CustomShaderElement.h"
#include "DX12CustomShaderPipeline.h"
#include "DX12CustomShaderRegisters.h"

namespace Bunny {

class DX12IniParamsTexture
{
public:
	// Per-device singleton; never released while the process lives. Cleared
	// together with the other caches on mod reload so a new generation
	// re-creates everything (devices stay valid across reloads).
	static DX12IniParamsTexture *ForDevice(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		DX12IniParamsTexture *&slot = Instances()[device];
		if (!slot)
			slot = new DX12IniParamsTexture(device);
		return slot;
	}

	static void ClearAll()
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : Instances()) {
			entry.second->ReleaseResources();
			delete entry.second;
		}
		Instances().clear();
	}

	// Creates the texture, the SRV descriptor and the upload ring once.
	bool EnsureResources()
	{
		if (mTexture)
			return true;
		return CreateTextureAndViews();
	}

	// Uploads `float4` (kIniParamsFloat4Count * 4 floats) to the texture on
	// `commandList` and leaves the texture in the shader-read state so the
	// custom draw that follows can sample it without another barrier.
	//
	// `elementIndex` selects the per-element region inside this frame's
	// upload buffer slot. It must be unique per element within the frame
	// (the table heap hands out the same ordinal): the memcpy is CPU-side
	// and lands before submission, so two elements sharing a region would
	// both read the later element's values when the GPU executes the
	// ordered CopyTextureRegion.
	bool UpdateAndUpload(
		ID3D12GraphicsCommandList *commandList,
		UINT64 presentCount,
		UINT elementIndex,
		const float *float4)
	{
		if (!commandList || !float4 || !EnsureResources())
			return false;
		if (elementIndex >= DX12CustomShaderRegisters::kMaxCustomElementsPerFrame)
			return false;
		const UINT slot = static_cast<UINT>(presentCount % kUploadSlots);
		if (!mUploads[slot] || !mUploadMap[slot])
			return false;
		const UINT64 offset =
			static_cast<UINT64>(elementIndex) * kUploadBytes;
		memcpy(static_cast<unsigned char*>(mUploadMap[slot]) + offset,
			float4, kUploadBytes);
		ID3D12Resource *upload = mUploads[slot];

		if (mState == TextureState::ShaderRead) {
			Transition(commandList, kIniParamsReadState,
				D3D12_RESOURCE_STATE_COPY_DEST);
			mState = TextureState::CopyDest;
		}

		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = upload;
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = mUploadFootprint;
		source.PlacedFootprint.Offset += offset;
		D3D12_TEXTURE_COPY_LOCATION destination = {};
		destination.pResource = mTexture;
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = 0;
		commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

		if (mState == TextureState::CopyDest) {
			Transition(commandList, D3D12_RESOURCE_STATE_COPY_DEST,
				kIniParamsReadState);
			mState = TextureState::ShaderRead;
		}
		return true;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle() const
	{
		return mSrvCpu;
	}

	ID3D12Resource *Texture() const
	{
		return mTexture;
	}

private:
	// Read state must cover both graphics stages: custom UI shaders read the
	// ini params texture from the vertex and the pixel stage.
	static constexpr D3D12_RESOURCE_STATES kIniParamsReadState =
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	static constexpr UINT kUploadSlots = 4;
	static constexpr UINT kUploadBytes =
		DX12CustomShaderRegisters::kIniParamsFloat4Count * 16u;

	enum class TextureState
	{
		CopyDest,
		ShaderRead
	};

	explicit DX12IniParamsTexture(ID3D12Device *device)
		: mDevice(device)
	{
	}

	~DX12IniParamsTexture()
	{
		ReleaseResources();
	}

	static D3D12_RESOURCE_DESC MakeTextureDesc()
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		desc.Width = DX12CustomShaderRegisters::kIniParamsFloat4Count;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		return desc;
	}

	static D3D12_HEAP_PROPERTIES DefaultHeap()
	{
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		return heap;
	}

	static D3D12_HEAP_PROPERTIES UploadHeap()
	{
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		return heap;
	}

	static D3D12_RESOURCE_DESC UploadBufferDesc()
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		// One region per replayed element (see UpdateAndUpload).
		desc.Width = static_cast<UINT64>(kUploadBytes) *
			DX12CustomShaderRegisters::kMaxCustomElementsPerFrame;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		return desc;
	}

	bool CreateTextureAndViews()
	{
		ID3D12Device *device = mDevice;
		if (!device || mTexture)
			return false;

		const D3D12_HEAP_PROPERTIES defaultHeap = DefaultHeap();
		const D3D12_RESOURCE_DESC textureDesc = MakeTextureDesc();
		HRESULT hr = device->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&mTexture));
		if (FAILED(hr) || !mTexture)
			return false;

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mHeap));
		if (FAILED(hr) || !mHeap) {
			ReleaseResources();
			return false;
		}
		mSrvCpu = mHeap->GetCPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture1D.MipLevels = 1;
		device->CreateShaderResourceView(mTexture, &srvDesc, mSrvCpu);

		UINT64 totalBytes = 0;
		device->GetCopyableFootprints(
			&textureDesc, 0, 1, 0, &mUploadFootprint, nullptr, nullptr, &totalBytes);

		const D3D12_HEAP_PROPERTIES uploadHeap = UploadHeap();
		const D3D12_RESOURCE_DESC bufferDesc = UploadBufferDesc();
		for (UINT i = 0; i < kUploadSlots; ++i) {
			hr = device->CreateCommittedResource(
				&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
				IID_PPV_ARGS(&mUploads[i]));
			if (FAILED(hr) || !mUploads[i]) {
				ReleaseResources();
				return false;
			}
			hr = mUploads[i]->Map(0, nullptr, &mUploadMap[i]);
			if (FAILED(hr) || !mUploadMap[i]) {
				ReleaseResources();
				return false;
			}
		}
		mState = TextureState::CopyDest;
		return true;
	}

	void ReleaseResources()
	{
		for (UINT i = 0; i < kUploadSlots; ++i) {
			if (mUploadMap[i] && mUploads[i]) {
				mUploads[i]->Unmap(0, nullptr);
				mUploadMap[i] = nullptr;
			}
			if (mUploads[i]) {
				mUploads[i]->Release();
				mUploads[i] = nullptr;
			}
		}
		if (mHeap) {
			mHeap->Release();
			mHeap = nullptr;
		}
		if (mTexture) {
			mTexture->Release();
			mTexture = nullptr;
		}
		mState = TextureState::CopyDest;
	}

	void Transition(
		ID3D12GraphicsCommandList *commandList,
		D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = mTexture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		commandList->ResourceBarrier(1, &barrier);
	}

	static std::mutex &Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::unordered_map<ID3D12Device *, DX12IniParamsTexture *> &Instances()
	{
		static std::unordered_map<ID3D12Device *, DX12IniParamsTexture *> instances;
		return instances;
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12Resource *mTexture = nullptr;
	ID3D12DescriptorHeap *mHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE mSrvCpu = {};
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT mUploadFootprint = {};
	ID3D12Resource *mUploads[kUploadSlots] = {};
	void *mUploadMap[kUploadSlots] = {};
	TextureState mState = TextureState::CopyDest;
};

// Root signature + pipeline state cache for custom elements.
class DX12CustomShaderPipelineCache
{
public:
	struct PipelineKey
	{
		std::wstring section;
		DXGI_FORMAT rtvFormat = DXGI_FORMAT_UNKNOWN;

		bool operator==(const PipelineKey &other) const
		{
			return section == other.section && rtvFormat == other.rtvFormat;
		}
	};

	struct PipelineKeyHash
	{
		size_t operator()(const PipelineKey &key) const
		{
			return std::hash<std::wstring>()(key.section) ^
				(static_cast<size_t>(key.rtvFormat) << 1);
		}
	};

	// Cache-owned root signature shared by all elements.
	static ID3D12RootSignature *EnsureRootSignature(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		return EnsureRootSignatureLocked(device);
	}

	// Cache-owned pipeline for an element draw; compiles the stage files on
	// first use. Returns nullptr when the element is unusable (missing
	// stages, compile failure, unsupported state) - the caller logs.
	static ID3D12PipelineState *EnsurePipeline(
		ID3D12Device *device,
		const DX12CustomShaderElementSpec &spec,
		DXGI_FORMAT rtvFormat)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		DeviceSet *set = DeviceSetLocked(device);
		if (!set)
			return nullptr;

		PipelineKey key{ spec.section, rtvFormat };
		auto existing = set->pipelines.find(key);
		if (existing != set->pipelines.end())
			return existing->second;

		ID3D12RootSignature *rootSignature = set->rootSignature;
		if (!rootSignature) {
			rootSignature = EnsureRootSignatureLocked(device);
			if (!rootSignature)
				return nullptr;
		}

		// Stage files must both compile (partial-stage elements cannot form
		// a PSO; DX11 swapped stages on the fly, D3D12 cannot).
		std::wstring errorText;
		DX12CustomShaderBytecode vs;
		if (spec.hasStage[0]) {
			const std::wstring vsPath = ResolveStageFile(spec, 0);
			if (!vsPath.empty())
				vs = DX12CustomShaderCompile::CompileStage(vsPath, 0, &errorText);
		}
		DX12CustomShaderBytecode ps;
		if (spec.hasStage[4]) {
			const std::wstring psPath = ResolveStageFile(spec, 4);
			if (!psPath.empty())
				ps = DX12CustomShaderCompile::CompileStage(psPath, 4, &errorText);
		}
		if (!vs || !ps)
			return nullptr;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		DX12CustomShaderPipelineAssembler::FillGraphicsPipelineDesc(
			spec, rtvFormat, nullptr, nullptr, &desc);
		desc.pRootSignature = rootSignature;
		desc.VS = { vs.data, vs.size };
		desc.PS = { ps.data, ps.size };

		ID3D12PipelineState *pipeline = nullptr;
		const HRESULT hr = device->CreateGraphicsPipelineState(
			&desc, IID_PPV_ARGS(&pipeline));
		if (FAILED(hr) || !pipeline)
			return nullptr;
		set->pipelines.emplace(key, pipeline);
		return pipeline;
	}

	// Drops all device caches on mod reload.
	static void ClearAll()
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : DeviceSets()) {
			delete entry.second;
		}
		DeviceSets().clear();
	}

private:
	class DeviceSet
	{
	public:
		ID3D12RootSignature *rootSignature = nullptr;
		std::unordered_map<PipelineKey, ID3D12PipelineState *, PipelineKeyHash> pipelines;

		~DeviceSet()
		{
			if (rootSignature)
				rootSignature->Release();
			for (auto &pipeline : pipelines)
				pipeline.second->Release();
		}
	};

	static DeviceSet *DeviceSetLocked(ID3D12Device *device)
	{
		DeviceSet *&slot = DeviceSets()[device];
		if (!slot)
			slot = new DeviceSet();
		return slot;
	}

	static ID3D12RootSignature *EnsureRootSignatureLocked(ID3D12Device *device)
	{
		DeviceSet *set = DeviceSetLocked(device);
		if (set->rootSignature)
			return set->rootSignature;
		ID3DBlob *signatureBlob = nullptr;
		ID3DBlob *errorBlob = nullptr;
		if (FAILED(DX12CustomShaderPipelineAssembler::SerializeRootSignature(
				&signatureBlob, &errorBlob)) ||
		    !signatureBlob) {
			if (errorBlob)
				errorBlob->Release();
			return nullptr;
		}
		const HRESULT hr = device->CreateRootSignature(
			0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&set->rootSignature));
		signatureBlob->Release();
		if (errorBlob)
			errorBlob->Release();
		if (FAILED(hr))
			return nullptr;
		return set->rootSignature;
	}

	static std::wstring ResolveStageFile(
		const DX12CustomShaderElementSpec &spec, size_t stage)
	{
		if (stage >= DX12CustomShaderElementSpec::StageCount ||
		    !spec.hasStage[stage])
			return L"";
		return DX12CustomShaderCompile::ResolveStageFilePath(
			spec.stageFiles[stage], spec.sourceDir, L"");
	}

	static std::mutex &Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::unordered_map<ID3D12Device *, DeviceSet *> &DeviceSets()
	{
		static std::unordered_map<ID3D12Device *, DeviceSet *> sets;
		return sets;
	}
};

// Shader-visible descriptor tables for custom element draws: slot i maps to
// shader register ti of the shared root signature's table parameter
// (DX12CustomShaderPipelineAssembler::kTableRegisterCount slots). Slot 120
// always carries the ini params texture SRV; the executor overrides the
// element's texture slots before the draw.
//
// The table is assembled with CPU-side descriptor copies. Two reuse hazards
// shape the layout:
//  * cross-frame: the GPU may still be executing a previous present that
//    reads the same heap, so heaps rotate per present (kHeapSlots, mirroring
//    the ini params upload ring) - the heap written this present was last
//    referenced kHeapSlots presents ago and is therefore idle;
//  * intra-frame: every element of one present needs its own 128-slot
//    region (kMaxCustomElementsPerFrame regions per heap), because CPU
//    descriptor writes take effect before the GPU executes any draw of the
//    list and a shared region would make every element read the last one's
//    descriptors.
// The 1x1 black default texture and its SRV live in a separate CPU-only
// heap so one source serves every region of every rotation slot.
class DX12CustomShaderTableHeap
{
public:
	// Rotation depth; matches the DX12IniParamsTexture upload ring so both
	// share the same frames-in-flight assumption.
	static constexpr UINT kHeapSlots = 4;

	static DX12CustomShaderTableHeap *ForDevice(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		DX12CustomShaderTableHeap *&slot = Instances()[device];
		if (!slot)
			slot = new DX12CustomShaderTableHeap(device);
		return slot;
	}

	static void ClearAll()
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : Instances()) {
			entry.second->ReleaseResources();
			delete entry.second;
		}
		Instances().clear();
	}

	// Selects the rotation slot for this present and resets the per-element
	// region cursor. All subsequent calls operate on the region acquired by
	// AcquireElementRegion.
	bool BeginPresent(UINT64 presentCount)
	{
		mActiveSlot = static_cast<UINT>(presentCount % kHeapSlots);
		mElementCursor = 0;
		mCurrentRegion = 0;
		return EnsureHeap();
	}

	// Advances to the next per-element table region inside the active
	// frame-slot heap and reports its ordinal (the same ordinal indexes the
	// ini-params upload regions). Returns false when the frame capacity is
	// exhausted; the caller must skip that element - drawing with a reused
	// region would corrupt earlier elements of this frame (descriptor writes
	// are CPU-side and land before the GPU executes any draw of the list).
	bool AcquireElementRegion(UINT *elementIndexOut)
	{
		if (mElementCursor >=
		    DX12CustomShaderRegisters::kMaxCustomElementsPerFrame)
			return false;
		if (elementIndexOut)
			*elementIndexOut = mElementCursor;
		mCurrentRegion = mElementCursor++;
		return true;
	}

	bool EnsureHeap()
	{
		if (mHeaps[0])
			return true;
		// CPU-only heap holding the default-texture SRV: the copy source for
		// every rotation slot (a source in a shader-visible heap would be
		// legal, but keeping it CPU-only avoids any doubt about which heap a
		// copy reads while the GPU uses the sibling slots).
		D3D12_DESCRIPTOR_HEAP_DESC defaultHeapDesc = {};
		defaultHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		defaultHeapDesc.NumDescriptors = 1;
		HRESULT hr = mDevice->CreateDescriptorHeap(
			&defaultHeapDesc, IID_PPV_ARGS(&mDefaultHeap));
		if (FAILED(hr) || !mDefaultHeap)
			return false;
		mDefaultSrv = mDefaultHeap->GetCPUDescriptorHandleForHeapStart();

		// A 1x1 black 2D texture backs every untouched table slot. Custom UI
		// shaders sample their textures as Texture2D at arbitrary registers;
		// filling unused slots with the (1D) ini params SRV would hand the
		// GPU a mismatched descriptor type the moment a shader reads a slot
		// nobody bound - a device-removal hazard.
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width = 1;
		textureDesc.Height = 1;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		hr = mDevice->CreateCommittedResource(
			&heapProperties, D3D12_HEAP_FLAG_NONE, &textureDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			nullptr, IID_PPV_ARGS(&mDefaultTexture));
		if (FAILED(hr) || !mDefaultTexture) {
			ReleaseResources();
			return false;
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		mDevice->CreateShaderResourceView(mDefaultTexture, &srvDesc, mDefaultSrv);

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = DX12CustomShaderPipelineAssembler::kTableRegisterCount *
			DX12CustomShaderRegisters::kMaxCustomElementsPerFrame;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		for (UINT slot = 0; slot < kHeapSlots; ++slot) {
			hr = mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeaps[slot]));
			if (FAILED(hr) || !mHeaps[slot]) {
				ReleaseResources();
				return false;
			}
			mGpuStart[slot] = mHeaps[slot]->GetGPUDescriptorHandleForHeapStart();
			mCpuStart[slot] = mHeaps[slot]->GetCPUDescriptorHandleForHeapStart();
		}
		mIncrement = mDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		return true;
	}

	// Pre-fills every table slot of the current element region with the
	// black 1x1 texture, then places the ini params SRV at its shader
	// register (t120) so untouched slots are always valid Texture2D
	// descriptors.
	void FillDefault(D3D12_CPU_DESCRIPTOR_HANDLE iniParamsSrv)
	{
		if (!EnsureHeap() || !mDefaultSrv.ptr || !iniParamsSrv.ptr)
			return;
		const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = RegionCpuStart();
		// One batched copy, not one device call per slot. This 128-slot region
		// fill used to issue 128 CopyDescriptorsSimple calls per element per
		// frame, and each of those also entered the device wrapper's descriptor
		// record. ID3D12Device::CopyDescriptors takes ranges, so both sides are
		// expressed as kTableRegisterCount single-descriptor ranges over the same
		// black 1x1 texture - the API requires equal source and destination
		// descriptor counts, so both sides carry kSlots ranges of size 1.
		constexpr UINT kSlots = DX12CustomShaderPipelineAssembler::kTableRegisterCount;
		D3D12_CPU_DESCRIPTOR_HANDLE destStarts[kSlots];
		D3D12_CPU_DESCRIPTOR_HANDLE srcStarts[kSlots];
		UINT rangeSizes[kSlots];
		for (UINT slot = 0; slot < kSlots; ++slot) {
			destStarts[slot] = cpuStart;
			destStarts[slot].ptr += static_cast<UINT64>(slot) * mIncrement;
			srcStarts[slot] = mDefaultSrv;
			rangeSizes[slot] = 1;
		}
		mDevice->CopyDescriptors(
			kSlots, destStarts, rangeSizes,
			kSlots, srcStarts, rangeSizes,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE paramsSlot = cpuStart;
		paramsSlot.ptr += static_cast<UINT64>(
			Bunny::DX12CustomShaderRegisters::kIniParamsShaderRegister) *
			mIncrement;
		mDevice->CopyDescriptorsSimple(1, paramsSlot, iniParamsSrv,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	void CopySrv(UINT slot, D3D12_CPU_DESCRIPTOR_HANDLE source)
	{
		if (!EnsureHeap() || !source.ptr ||
		    slot >= DX12CustomShaderPipelineAssembler::kTableRegisterCount)
			return;
		D3D12_CPU_DESCRIPTOR_HANDLE destination = RegionCpuStart();
		destination.ptr += static_cast<UINT64>(slot) * mIncrement;
		mDevice->CopyDescriptorsSimple(1, destination, source,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	ID3D12DescriptorHeap *Heap() const
	{
		return mHeaps[mActiveSlot];
	}

	// GPU handle of the current element region's table base.
	D3D12_GPU_DESCRIPTOR_HANDLE GpuStart() const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = mGpuStart[mActiveSlot];
		handle.ptr += static_cast<UINT64>(mCurrentRegion) *
			DX12CustomShaderPipelineAssembler::kTableRegisterCount *
			mIncrement;
		return handle;
	}

private:
	explicit DX12CustomShaderTableHeap(ID3D12Device *device)
		: mDevice(device)
	{
	}

	~DX12CustomShaderTableHeap()
	{
		ReleaseResources();
	}

	void ReleaseResources()
	{
		for (UINT slot = 0; slot < kHeapSlots; ++slot) {
			if (mHeaps[slot]) {
				mHeaps[slot]->Release();
				mHeaps[slot] = nullptr;
			}
			mGpuStart[slot] = {};
			mCpuStart[slot] = {};
		}
		if (mDefaultHeap) {
			mDefaultHeap->Release();
			mDefaultHeap = nullptr;
		}
		if (mDefaultTexture) {
			mDefaultTexture->Release();
			mDefaultTexture = nullptr;
		}
		mDefaultSrv = {};
	}

	static std::mutex &Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::unordered_map<ID3D12Device *, DX12CustomShaderTableHeap *> &Instances()
	{
		static std::unordered_map<ID3D12Device *, DX12CustomShaderTableHeap *> instances;
		return instances;
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12DescriptorHeap *mHeaps[kHeapSlots] = {};
	D3D12_GPU_DESCRIPTOR_HANDLE mGpuStart[kHeapSlots] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE mCpuStart[kHeapSlots] = {};
	ID3D12DescriptorHeap *mDefaultHeap = nullptr;
	ID3D12Resource *mDefaultTexture = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE mDefaultSrv = {};
	UINT mActiveSlot = 0;
	UINT mElementCursor = 0;
	UINT mCurrentRegion = 0;
	UINT mIncrement = 0;

	// CPU handle of the current element region's table base.
	D3D12_CPU_DESCRIPTOR_HANDLE RegionCpuStart() const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = mCpuStart[mActiveSlot];
		handle.ptr += static_cast<UINT64>(mCurrentRegion) *
			DX12CustomShaderPipelineAssembler::kTableRegisterCount *
			mIncrement;
		return handle;
	}
};

// Lazily-created render target view of the current swap chain back buffer.
// Swap chain buffers cycle every frame, so the view is re-created when the
// (chain, buffer index) pair changes; the descriptor slots are indexed by
// buffer index modulo the slot count, which makes the steady-state flip cost
// a single format/dimension comparison.
class DX12CustomShaderSurfaceCache
{
public:
	// RTV and PSO formats must be the typed view of a possibly typeless
	// back buffer format; returns DXGI_FORMAT_UNKNOWN when unsupported.
	static DXGI_FORMAT MakeTypedRtvFormat(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
		case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
		default:
			return format;
		}
	}
	static DX12CustomShaderSurfaceCache *ForDevice(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		DX12CustomShaderSurfaceCache *&slot = Instances()[device];
		if (!slot)
			slot = new DX12CustomShaderSurfaceCache(device);
		return slot;
	}

	static void ClearAll()
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : Instances()) {
			entry.second->ReleaseResources();
			delete entry.second;
		}
		Instances().clear();
	}

	// Called before ResizeBuffers and before the wrapper releases its native
	// swap chain. Drop every owned reference, including the cached QI result:
	// retaining IDXGISwapChain3 keeps the old flip chain attached to its HWND
	// and prevents frame generation from creating the replacement chain.
	static void InvalidateChain(IDXGISwapChain *swapChain)
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : Instances()) {
			std::lock_guard<std::mutex> chainLock(entry.second->mLock);
			if (entry.second->mChain == swapChain ||
			    entry.second->mChain3Owner == swapChain) {
				entry.second->ReleaseChainResources();
			}
		}
	}

	// Returns true with the RTV handle/format/size of the current back
	// buffer when it could be resolved (or refreshed).
	//
	// A flip-model chain rotates the back-buffer index every present, so the
	// view for a given index stays valid for the life of the chain: each of the
	// kRtvSlots descriptors is built once and then only compared against. That
	// matters because the old shape rebuilt the view whenever the *index*
	// changed, i.e. on every frame - two GetBuffer calls, a
	// CreateRenderTargetView and a QueryInterface per frame, for a view whose
	// contents never actually changed.
	bool EnsureBackBufferView(
		IDXGISwapChain *swapChain,
		D3D12_CPU_DESCRIPTOR_HANDLE *rtvOut,
		DXGI_FORMAT *formatOut,
		UINT *widthOut,
		UINT *heightOut,
		ID3D12Resource **backBufferOut = nullptr)
	{
		if (!rtvOut || !formatOut || !widthOut || !heightOut)
			return false;
		if (!swapChain || !mRtvCpuStart.ptr)
			return false;
		// The mod [Present] overlay and the overlay text both resolve the view
		// in the same frame, and a resize may invalidate it from a different
		// thread; the cache key and the descriptor slot it guards must never be
		// updated concurrently.
		std::lock_guard<std::mutex> cacheLock(mLock);

		if (!mSwapChain3 || mChain3Owner != swapChain) {
			// A device can present more than one chain. Retire the old buffers
			// before replacing either the chain key or its cached interface.
			ReleaseChainResources();
			if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&mSwapChain3))) ||
			    !mSwapChain3)
				return false;
			mChain3Owner = swapChain;
		}

		const UINT bufferIndex = mSwapChain3->GetCurrentBackBufferIndex();
		if (bufferIndex >= kRtvSlots)
			return false;
		if (mChain != swapChain || mValidMask == 0) {
			mValidMask = 0;
			mChain = swapChain;
		} else if (mValidMask & (1u << bufferIndex)) {
			// Steady state: the slot was built for this chain and index, so
			// only the rotation bookkeeping changes.
			mBufferIndex = bufferIndex;
			*rtvOut = mRtvHandles[bufferIndex];
			*formatOut = mFormat;
			*widthOut = mWidth;
			*heightOut = mHeight;
			if (backBufferOut)
				*backBufferOut = AcquireCachedBackBuffer(bufferIndex);
			return true;
		}

		ID3D12Resource *backBuffer = nullptr;
		HRESULT hr = swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer));
		if (FAILED(hr) || !backBuffer)
			return false;
		D3D12_RESOURCE_DESC desc = backBuffer->GetDesc();
		if (desc.Width == 0 || desc.Height == 0) {
			backBuffer->Release();
			return false;
		}
		// Typeless back buffers need a typed view format for the RTV and the
		// matching PSO RTV format.
		const DXGI_FORMAT typedFormat = MakeTypedRtvFormat(desc.Format);
		if (typedFormat == DXGI_FORMAT_UNKNOWN) {
			backBuffer->Release();
			return false;
		}

		// A format change means every cached view is stale, not just this one.
		if (mValidMask && mFormat != typedFormat) {
			mValidMask = 0;
			ReleaseCachedBackBuffers();
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = typedFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		D3D12_CPU_DESCRIPTOR_HANDLE slot = mRtvCpuStart;
		slot.ptr += static_cast<UINT64>(bufferIndex) * mRtvIncrement;
		mDevice->CreateRenderTargetView(backBuffer, &rtvDesc, slot);

		// The resource stays referenced for as long as its view does. Without
		// this the steady-state path would have to call GetBuffer on every
		// frame just to hand the caller something to barrier.
		mBackBuffers[bufferIndex] = backBuffer;
		mRtvHandles[bufferIndex] = slot;
		mValidMask |= 1u << bufferIndex;
		mFormat = typedFormat;
		mWidth = static_cast<UINT>(desc.Width);
		mHeight = static_cast<UINT>(desc.Height);
		mBufferIndex = bufferIndex;

		*rtvOut = slot;
		*formatOut = mFormat;
		*widthOut = mWidth;
		*heightOut = mHeight;
		if (backBufferOut)
			*backBufferOut = AcquireCachedBackBuffer(bufferIndex);
		return mRtvHandle().ptr != 0;
	}

private:
	static constexpr UINT kRtvSlots = 8;

	// Hands the caller its own reference to a cached back buffer. Callers
	// Release() it, which keeps their existing ownership discipline intact.
	ID3D12Resource *AcquireCachedBackBuffer(UINT bufferIndex)
	{
		if (!mBackBuffers[bufferIndex])
			return nullptr;
		mBackBuffers[bufferIndex]->AddRef();
		return mBackBuffers[bufferIndex];
	}

	void ReleaseCachedBackBuffers()
	{
		for (ID3D12Resource *&resource : mBackBuffers) {
			if (resource) {
				resource->Release();
				resource = nullptr;
			}
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE mRtvHandle() const
	{
		return (mValidMask && mBufferIndex < kRtvSlots)
			? mRtvHandles[mBufferIndex]
			: D3D12_CPU_DESCRIPTOR_HANDLE{};
	}

	explicit DX12CustomShaderSurfaceCache(ID3D12Device *device)
		: mDevice(device)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = kRtvSlots;
		if (SUCCEEDED(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeap))))
			mRtvCpuStart = mHeap->GetCPUDescriptorHandleForHeapStart();
		mRtvIncrement = mDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	~DX12CustomShaderSurfaceCache()
	{
		ReleaseResources();
	}

	// Caller holds mLock (or has exclusive ownership during destruction).
	void ReleaseChainResources()
	{
		ReleaseCachedBackBuffers();
		mValidMask = 0;
		mChain = nullptr;
		mChain3Owner = nullptr;
		if (mSwapChain3) {
			mSwapChain3->Release();
			mSwapChain3 = nullptr;
		}
		mBufferIndex = 0;
		mFormat = DXGI_FORMAT_UNKNOWN;
		mWidth = mHeight = 0;
		for (D3D12_CPU_DESCRIPTOR_HANDLE &handle : mRtvHandles)
			handle = {};
	}

	void ReleaseResources()
	{
		ReleaseChainResources();
		if (mHeap) {
			mHeap->Release();
			mHeap = nullptr;
		}
	}

	static std::mutex &Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::unordered_map<ID3D12Device *, DX12CustomShaderSurfaceCache *> &Instances()
	{
		static std::unordered_map<ID3D12Device *, DX12CustomShaderSurfaceCache *> instances;
		return instances;
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12DescriptorHeap *mHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE mRtvCpuStart = {};
	UINT mRtvIncrement = 0;
	IDXGISwapChain *mChain = nullptr;
	UINT mBufferIndex = 0;
	DXGI_FORMAT mFormat = DXGI_FORMAT_UNKNOWN;
	UINT mWidth = 0;
	UINT mHeight = 0;
	// One entry per back-buffer index, built once and then only compared
	// against. mValidMask says which entries of the current chain are built;
	// mBackBuffers holds the reference each built view needs to stay alive, so
	// the steady-state path never has to call GetBuffer again.
	D3D12_CPU_DESCRIPTOR_HANDLE mRtvHandles[kRtvSlots] = {};
	ID3D12Resource *mBackBuffers[kRtvSlots] = {};
	UINT mValidMask = 0;
	// Cached so the per-frame path never pays a QueryInterface. mChain3Owner
	// pins it to the chain it was queried from.
	IDXGISwapChain3 *mSwapChain3 = nullptr;
	IDXGISwapChain *mChain3Owner = nullptr;
	// Guards the cache key, the metrics and the descriptor slot it describes.
	std::mutex mLock;
};

// Internal direct-queue submission channel for the [Present] custom element
// overlay. The DX11 runtime replays [Present] element draws at Present time,
// after the game's rendering; on DX12 the game's lists are already closed by
// then, so the replay records onto this mod-owned allocator/list pair and is
// submitted on the game's direct queue right before the original Present
// call. Two slots rotate so an allocator is only Reset after its previous
// submission completed (tracked by the fence).
class DX12PresentOverlayChannel
{
public:
	static DX12PresentOverlayChannel *ForDevice(ID3D12Device *device)
	{
		if (!device)
			return nullptr;
		std::lock_guard<std::mutex> lock(Mutex());
		DX12PresentOverlayChannel *&slot = Instances()[device];
		if (!slot)
			slot = new DX12PresentOverlayChannel(device);
		return slot && slot->Valid() ? slot : nullptr;
	}

	static void ClearAll()
	{
		std::lock_guard<std::mutex> lock(Mutex());
		for (auto &entry : Instances()) {
			delete entry.second;
		}
		Instances().clear();
	}

	bool Valid() const
	{
		return mFence && mSlots[0].allocator && mSlots[0].list &&
			mSlots[1].allocator && mSlots[1].list;
	}

	// BeginRecord() hands out a list but Submit() closes whichever slot the
	// cursor points at, so the pair is only correct while no other thread can
	// record in between. Hold one of these across the whole recording; the
	// mod [Present] overlay and the hunting text overlay share this channel.
	// A null channel is accepted so callers can declare the scope before they
	// know whether the channel resolved.
	class RecordScope
	{
	public:
		explicit RecordScope(DX12PresentOverlayChannel *channel)
			: mChannel(channel)
		{
			if (mChannel)
				mChannel->mRecordLock.lock();
		}

		~RecordScope()
		{
			if (mChannel)
				mChannel->mRecordLock.unlock();
		}

		RecordScope(const RecordScope &) = delete;
		RecordScope &operator=(const RecordScope &) = delete;

	private:
		DX12PresentOverlayChannel *mChannel;
	};

	// Why the last BeginRecord() returned nullptr; for the callers' existing
	// diagnostic lines.
	const char *LastFailure() const
	{
		return mLastFailure;
	}

	// Hands out the next recording list after waiting up to kSlotWaitMs for the
	// slot's previous submission to retire before resetting it. Returns nullptr
	// when that submission is still in flight: resetting an allocator whose list
	// is still executing is invalid D3D12 usage and can remove the device, so
	// the caller drops the frame instead.
	ID3D12GraphicsCommandList *BeginRecord()
	{
		mCursor = (mCursor + 1) % kSlots;
		Slot &slot = mSlots[mCursor];
		if (slot.submittedFenceValue &&
			mFence->GetCompletedValue() < slot.submittedFenceValue) {
			bool retired = false;
			HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (event) {
				if (SUCCEEDED(mFence->SetEventOnCompletion(
					    slot.submittedFenceValue, event)))
					retired = WaitForSingleObject(event, kSlotWaitMs) ==
						WAIT_OBJECT_0;
				CloseHandle(event);
			}
			if (!retired) {
				mLastFailure = event ? "slot_in_flight" : "event_create_failed";
				return nullptr;
			}
		}
		if (FAILED(slot.allocator->Reset())) {
			mLastFailure = "allocator_reset_failed";
			return nullptr;
		}
		if (FAILED(slot.list->Reset(slot.allocator, nullptr))) {
			mLastFailure = "list_reset_failed";
			return nullptr;
		}
		mLastFailure = "none";
		return slot.list;
	}

	// Closes the recording list, submits it on the game queue and signals the
	// fence so the slot can be reused once this execution completes.
	bool Submit(ID3D12CommandQueue *queue)
	{
		if (!queue) {
			mLastFailure = "null_queue";
			return false;
		}
		Slot &slot = mSlots[mCursor];
		if (FAILED(slot.list->Close())) {
			mLastFailure = "list_close_failed";
			return false;
		}
		ID3D12CommandList *lists[] = { slot.list };
		queue->ExecuteCommandLists(1, lists);
		const UINT64 fenceValue = mNextFenceValue++;
		if (FAILED(queue->Signal(mFence, fenceValue))) {
			mLastFailure = "queue_signal_failed";
			return false;
		}
		slot.submittedFenceValue = fenceValue;
		mLastFailure = "none";
		return true;
	}

private:
	static constexpr UINT kSlots = 2;
	static constexpr DWORD kSlotWaitMs = 2000;

	struct Slot
	{
		ID3D12CommandAllocator *allocator = nullptr;
		ID3D12GraphicsCommandList *list = nullptr;
		UINT64 submittedFenceValue = 0;
	};

	explicit DX12PresentOverlayChannel(ID3D12Device *device)
	{
		if (!device)
			return;
		for (Slot &slot : mSlots) {
			if (FAILED(device->CreateCommandAllocator(
				    D3D12_COMMAND_LIST_TYPE_DIRECT,
				    IID_PPV_ARGS(&slot.allocator))))
				return;
			if (FAILED(device->CreateCommandList(
				    0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator,
				    nullptr, IID_PPV_ARGS(&slot.list))))
				return;
			slot.list->Close();
		}
		if (FAILED(device->CreateFence(
			    0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence))))
			return;
	}

	~DX12PresentOverlayChannel()
	{
		for (Slot &slot : mSlots) {
			if (slot.list)
				slot.list->Release();
			if (slot.allocator)
				slot.allocator->Release();
		}
		if (mFence)
			mFence->Release();
	}

	static std::mutex &Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::unordered_map<ID3D12Device *, DX12PresentOverlayChannel *> &Instances()
	{
		static std::unordered_map<ID3D12Device *, DX12PresentOverlayChannel *> instances;
		return instances;
	}

	Slot mSlots[kSlots];
	ID3D12Fence *mFence = nullptr;
	UINT64 mNextFenceValue = 1;
	UINT mCursor = 0;
	// Held across BeginRecord..Submit so a second recording thread cannot
	// close another thread's slot.
	std::mutex mRecordLock;
	const char *mLastFailure = "none";
};

} // namespace Bunny
