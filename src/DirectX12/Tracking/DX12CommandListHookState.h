// Command-list hook signatures, vtable caches and registries.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12CaptureLedger.h"

class DX12CommandListHookSignatures
{
public:
	using CreateCommandQueue = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
	using CreateCommandAllocator = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, D3D12_COMMAND_LIST_TYPE, REFIID, void**);
	using CreateCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
	using CreateCommandList1 = HRESULT(STDMETHODCALLTYPE *)(ID3D12Device4*, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**);

	using QueueUpdateTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*, const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS);
	using QueueCopyTileMappings = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, D3D12_TILE_MAPPING_FLAGS);
	using QueueExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, ID3D12CommandList *const*);
	using QueueSetMarker = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT, const void*, UINT);
	using QueueEndEvent = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue*);
	using QueueSignal = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueWait = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
	using QueueGetTimestampFrequency = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*);
	using QueueGetClockCalibration = HRESULT(STDMETHODCALLTYPE *)(ID3D12CommandQueue*, UINT64*, UINT64*);

	using CloseCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ResetCommandList = HRESULT(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
	using ClearState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using DrawInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	using DrawIndexedInstanced = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	using Dispatch = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using CopyBufferRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64);
	using CopyTextureRegion = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*);
	using CopyResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
	using CopyTiles = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Resource*, UINT64, D3D12_TILE_COPY_FLAGS);
	using ResolveSubresource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT, DXGI_FORMAT);
	using IaSetPrimitiveTopology = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	using RsSetViewports = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	using RsSetScissorRects = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	using OmSetBlendFactor = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const FLOAT[4]);
	using OmSetStencilRef = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT);
	using SetPipelineState = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	using ResourceBarrier = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
	using ExecuteBundle = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
	using SetDescriptorHeaps = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap *const*);
	using SetRootSignature = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	using SetRootDescriptorTable = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	using SetRoot32BitConstant = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	using SetRoot32BitConstants = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	using SetRootGpuVa = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	using IaSetIndexBuffer = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	using IaSetVertexBuffers = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	using SoSetTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_STREAM_OUTPUT_BUFFER_VIEW*);
	using OmSetRenderTargets = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	using ClearDepthStencilView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*);
	using ClearRenderTargetView = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT[4], UINT, const D3D12_RECT*);
	using ClearUavUint = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const UINT[4], UINT, const D3D12_RECT*);
	using ClearUavFloat = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const FLOAT[4], UINT, const D3D12_RECT*);
	using DiscardResource = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_DISCARD_REGION*);
	using BeginQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using EndQuery = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
	using ResolveQueryData = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT, UINT, ID3D12Resource*, UINT64);
	using SetPredication = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, D3D12_PREDICATION_OP);
	using CommandListSetMarker = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListBeginEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, UINT, const void*, UINT);
	using CommandListEndEvent = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*);
	using ExecuteIndirect = void(STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
};

class DX12ThreadLocalCommandListOriginal
{
public:
	void *target = nullptr;
	void *original = nullptr;
	bool resolved = false;
};


class DX12ThreadLocalIaCallbacksCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12IaReplacementExecutorCallbacks callbacks;
};


class DX12VtableBaseCache
{
public:
	void *vtable = nullptr;
	std::array<void*, 256> originals{};
	std::array<bool, 256> slotResolved{};
};


class DX12CommandListHookState
{
public:
	static DX12CommandListHookSignatures::CreateCommandQueue OrigCreateCommandQueue;
	static DX12CommandListHookSignatures::CreateCommandAllocator OrigCreateCommandAllocator;
	static DX12CommandListHookSignatures::CreateCommandList OrigCreateCommandList;
	static DX12CommandListHookSignatures::CreateCommandList1 OrigCreateCommandList1;

	static DX12CommandListHookSignatures::QueueUpdateTileMappings OrigQueueUpdateTileMappings;
	static DX12CommandListHookSignatures::QueueCopyTileMappings OrigQueueCopyTileMappings;
	static DX12CommandListHookSignatures::QueueExecuteCommandLists OrigQueueExecuteCommandLists;
	static DX12CommandListHookSignatures::QueueSetMarker OrigQueueSetMarker;
	static DX12CommandListHookSignatures::QueueBeginEvent OrigQueueBeginEvent;
	static DX12CommandListHookSignatures::QueueEndEvent OrigQueueEndEvent;
	static DX12CommandListHookSignatures::QueueSignal OrigQueueSignal;
	static DX12CommandListHookSignatures::QueueWait OrigQueueWait;
	static DX12CommandListHookSignatures::QueueGetTimestampFrequency OrigQueueGetTimestampFrequency;
	static DX12CommandListHookSignatures::QueueGetClockCalibration OrigQueueGetClockCalibration;

	static DX12CommandListHookSignatures::CloseCommandList OrigCloseCommandList;
	static DX12CommandListHookSignatures::ResetCommandList OrigResetCommandList;
	static DX12CommandListHookSignatures::ClearState OrigClearState;
	static DX12CommandListHookSignatures::DrawInstanced OrigDrawInstanced;
	static DX12CommandListHookSignatures::DrawIndexedInstanced OrigDrawIndexedInstanced;
	static DX12CommandListHookSignatures::Dispatch OrigDispatch;
	static DX12CommandListHookSignatures::CopyBufferRegion OrigCopyBufferRegion;
	static DX12CommandListHookSignatures::CopyTextureRegion OrigCopyTextureRegion;
	static DX12CommandListHookSignatures::CopyResource OrigCopyResource;
	static DX12CommandListHookSignatures::CopyTiles OrigCopyTiles;
	static DX12CommandListHookSignatures::ResolveSubresource OrigResolveSubresource;
	static DX12CommandListHookSignatures::IaSetPrimitiveTopology OrigIASetPrimitiveTopology;
	static DX12CommandListHookSignatures::RsSetViewports OrigRSSetViewports;
	static DX12CommandListHookSignatures::RsSetScissorRects OrigRSSetScissorRects;
	static DX12CommandListHookSignatures::OmSetBlendFactor OrigOMSetBlendFactor;
	static DX12CommandListHookSignatures::OmSetStencilRef OrigOMSetStencilRef;
	static DX12CommandListHookSignatures::SetPipelineState OrigSetPipelineState;
	static DX12CommandListHookSignatures::ResourceBarrier OrigResourceBarrier;
	static DX12CommandListHookSignatures::ExecuteBundle OrigExecuteBundle;
	static DX12CommandListHookSignatures::SetDescriptorHeaps OrigSetDescriptorHeaps;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetComputeRootSignature;
	static DX12CommandListHookSignatures::SetRootSignature OrigSetGraphicsRootSignature;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetComputeRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRootDescriptorTable OrigSetGraphicsRootDescriptorTable;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetComputeRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstant OrigSetGraphicsRoot32BitConstant;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetComputeRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRoot32BitConstants OrigSetGraphicsRoot32BitConstants;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootConstantBufferView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootShaderResourceView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetComputeRootUnorderedAccessView;
	static DX12CommandListHookSignatures::SetRootGpuVa OrigSetGraphicsRootUnorderedAccessView;
	static DX12CommandListHookSignatures::IaSetIndexBuffer OrigIASetIndexBuffer;
	static DX12CommandListHookSignatures::IaSetVertexBuffers OrigIASetVertexBuffers;
	static DX12CommandListHookSignatures::SoSetTargets OrigSOSetTargets;
	static DX12CommandListHookSignatures::OmSetRenderTargets OrigOMSetRenderTargets;
	static DX12CommandListHookSignatures::ClearDepthStencilView OrigClearDepthStencilView;
	static DX12CommandListHookSignatures::ClearRenderTargetView OrigClearRenderTargetView;
	static DX12CommandListHookSignatures::ClearUavUint OrigClearUnorderedAccessViewUint;
	static DX12CommandListHookSignatures::ClearUavFloat OrigClearUnorderedAccessViewFloat;
	static DX12CommandListHookSignatures::DiscardResource OrigDiscardResource;
	static DX12CommandListHookSignatures::BeginQuery OrigBeginQuery;
	static DX12CommandListHookSignatures::EndQuery OrigEndQuery;
	static DX12CommandListHookSignatures::ResolveQueryData OrigResolveQueryData;
	static DX12CommandListHookSignatures::SetPredication OrigSetPredication;
	static DX12CommandListHookSignatures::CommandListSetMarker OrigCommandListSetMarker;
	static DX12CommandListHookSignatures::CommandListBeginEvent OrigCommandListBeginEvent;
	static DX12CommandListHookSignatures::CommandListEndEvent OrigCommandListEndEvent;
	static DX12CommandListHookSignatures::ExecuteIndirect OrigExecuteIndirect;

	static thread_local DX12ThreadLocalCommandListOriginal CommandListOriginals[64];
	static thread_local DX12ThreadLocalCommandListOriginal CommandQueueOriginals[32];
	static thread_local DX12VtableBaseCache VtableBaseCache;
	static thread_local DX12ThreadLocalIaCallbacksCache IaCallbacksCache;
};


class DX12CommandListHookDiagnostics
{
public:
	static void RecordIaReplacementDrawExecute(bool indexed)
	{
		DX12Profiling::RecordIaReplacementDraw(indexed);
	}

	static void RecordIaReplacementDispatchExecute()
	{
		DX12Profiling::RecordIaReplacementDispatch();
	}
};


class DX12OriginalFunctionRegistry
{
public:
static void *GetVTableSlotTarget(void *object, UINT slot)
	{
		if (!object)
			return nullptr;
		void **vtable = *reinterpret_cast<void***>(object);
		return vtable ? vtable[slot] : nullptr;
	}

	// Resolve all hot vtable slots in one batch and populate the L2 cache.
	// Later ResolveCached calls bypass the global registry lookup.
	template <typename SlotCache, typename VtableCache>
	static void BulkResolveVtableSlots(
		void *object,
		const UINT *slots,
		size_t slotCount,
		SlotCache *slotCaches,
		size_t slotCacheCount,
		VtableCache *vtableCache)
	{
		if (!object || !slots || !slotCount || !vtableCache)
			return;

		void **vtable = *reinterpret_cast<void***>(object);
		if (!vtable)
			return;

		// This vtable was already resolved as a batch.
		if (vtableCache->vtable == vtable)
			return;

		// Reset the cache for the new vtable.
		vtableCache->vtable = vtable;
		vtableCache->originals = {};
		vtableCache->slotResolved = {};

		for (size_t i = 0; i < slotCount; ++i) {
			UINT slot = slots[i];
			if (slot >= vtableCache->originals.size())
				continue;

			void *target = vtable[slot];
			if (!target) {
				vtableCache->slotResolved[slot] = true;
				continue;
			}

			void *original = DX12HookRegistry::GetOriginalFunction(target);
			// Wrapper mode never installs vtable method hooks; cache the raw
			// vtable entry as the positive original so per-call resolution
			// takes the lock-free fast path from the start.
			if (!original && !DX12HookRegistry::AnyVtableHooksInstalled())
				original = target;
			vtableCache->originals[slot] = original;
			vtableCache->slotResolved[slot] = true;

			// Update the L1 slot cache as well.
			if (slotCaches && slot < slotCacheCount) {
				slotCaches[slot].target = target;
				slotCaches[slot].original = original;
				slotCaches[slot].resolved = true;
			}
		}
	}

	template <typename T>
	static T Resolve(void *object, UINT slot, T fallback, const char *name)
	{
		void *target = GetVTableSlotTarget(object, slot);
		if (target) {
			void *original = DX12HookRegistry::GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
			// Wrapper mode installs no code patches: an unhooked vtable entry
			// itself is the original implementation.
			return reinterpret_cast<T>(target);
		}

		if (fallback) {
						return fallback;
		}

		DX12LogManager::Fields(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}

	template <typename T, typename SlotCache, typename VtableCache>
	static T ResolveCached(
		void *object, UINT slot, T fallback, const char *name,
		SlotCache *slotCaches, size_t slotCacheCount, VtableCache *vtableCache)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *target = vtable[slot];

				// L1: per-slot TLS cache with the highest expected hit rate.
				if (slotCaches && slot < slotCacheCount &&
					slotCaches[slot].resolved &&
					slotCaches[slot].target == target) {
					if (slotCaches[slot].original)
						return reinterpret_cast<T>(slotCaches[slot].original);
					// Cached negative result. When no vtable method hooks were ever
					// installed (wrapper mode), the raw vtable entry is permanently
					// the original implementation: promote it to a positive hit so
					// subsequent calls take the lock-free fast path.
					if (!DX12HookRegistry::AnyVtableHooksInstalled()) {
						slotCaches[slot].original = target;
						return reinterpret_cast<T>(target);
					}
					void *lateOriginal = DX12HookRegistry::GetOriginalFunction(target);
					if (lateOriginal)
						return reinterpret_cast<T>(lateOriginal);
					return reinterpret_cast<T>(target);
				}

				// L2: batched per-vtable cache.
				if (vtableCache && vtableCache->vtable == vtable &&
					slot < vtableCache->slotResolved.size() &&
					vtableCache->slotResolved[slot]) {
					void *original = vtableCache->originals[slot];
					// Promote the result into L1.
					if (slotCaches && slot < slotCacheCount) {
						slotCaches[slot].target = target;
						slotCaches[slot].original = original;
						slotCaches[slot].resolved = true;
					}
					if (original)
						return reinterpret_cast<T>(original);
					// Cached negative result: promote to positive when no vtable
					// method hooks exist (see L1 above).
					if (!DX12HookRegistry::AnyVtableHooksInstalled()) {
						vtableCache->originals[slot] = target;
						if (slotCaches && slot < slotCacheCount)
							slotCaches[slot].original = target;
						return reinterpret_cast<T>(target);
					}
					void *lateOriginal = DX12HookRegistry::GetOriginalFunction(target);
					if (lateOriginal)
						return reinterpret_cast<T>(lateOriginal);
					return reinterpret_cast<T>(target);
				}

				// L3: global hook registry, used only for the first lookup.
				void *original = DX12HookRegistry::GetOriginalFunction(target);

				// Wrapper mode never installs vtable method hooks; treat the raw
				// vtable entry as the positive original so it is cached below
				// instead of being re-looked-up on every call.
				if (!original && !DX12HookRegistry::AnyVtableHooksInstalled())
					original = target;

				// Store positive and negative results in L1.
				if (slotCaches && slot < slotCacheCount) {
					slotCaches[slot].target = target;
					slotCaches[slot].original = original;
					slotCaches[slot].resolved = true;
				}

				// Store positive and negative results in L2.
				if (vtableCache && slot < vtableCache->slotResolved.size()) {
					if (vtableCache->vtable != vtable) {
						vtableCache->vtable = vtable;
						vtableCache->originals = {};
						vtableCache->slotResolved = {};
					}
					vtableCache->originals[slot] = original;
					vtableCache->slotResolved[slot] = true;
				}

				if (original)
					return reinterpret_cast<T>(original);

				// The slot is not hooked; the vtable entry itself is the original
				// implementation (wrapper mode patches no code).
				return reinterpret_cast<T>(target);
			}
		}

		if (fallback) {
						return fallback;
		}

		DX12LogManager::Fields(name ? name : "DX12::UnknownOriginal",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}
};


class DX12CommandListHookRegistry
{
public:


	template <typename T>
	static T GetCommandListOriginal(
		ID3D12GraphicsCommandList *commandList, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::ResolveCached(
			commandList, slot, fallback, name,
			DX12CommandListHookState::CommandListOriginals,
			ARRAYSIZE(DX12CommandListHookState::CommandListOriginals),
			&DX12CommandListHookState::VtableBaseCache);
	}
};


class DX12CommandListHooks
{
public:
};


