// DeviceWrapper / D3D12DeviceWrapper plus WrapExisting / TryWrapAuxiliaryAs helpers.
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"
#include "DX12CommandWrappers.h"
#include "DX12ResourceWrappers.h"
#include "DX12PipelineWrappers.h"
#include "DX12CommandListWrapper.h"

namespace DX12WrapperDetail
{

class DeviceWrapper
	{
	public:
		// Vertex-limit raising for placed buffers has a two-sided contract:
		// allocation queries and CreatePlacedResource* must see the exact same
		// adjusted Width. Adjusting creation alone can overlap the next resource
		// in the heap; adjusting allocation alone leaves the resource undersized.
		// Keeping both sides paired mirrors DX11's CreateBuffer-time ByteWidth
		// override and preserves one resource identity for UAV and IA use.
		static const D3D12_RESOURCE_DESC *PrepareResourceDescArray(
			const D3D12_RESOURCE_DESC *descs, UINT count,
			std::vector<D3D12_RESOURCE_DESC> *storage, const char *source)
		{
			if (!descs || !count || !storage)
				return descs;
			storage->assign(descs, descs + count);
			bool adjusted = false;
			for (D3D12_RESOURCE_DESC &desc : *storage)
				adjusted |= DX12ModAdjustBufferResourceDesc(&desc, source);
			return adjusted ? storage->data() : descs;
		}

		static const D3D12_RESOURCE_DESC1 *PrepareResourceDescArray1(
			const D3D12_RESOURCE_DESC1 *descs, UINT count,
			std::vector<D3D12_RESOURCE_DESC1> *storage, const char *source)
		{
			if (!descs || !count || !storage)
				return descs;
			storage->assign(descs, descs + count);
			bool adjusted = false;
			for (D3D12_RESOURCE_DESC1 &desc : *storage)
				adjusted |= DX12ModAdjustBufferResourceDesc1(&desc, source);
			return adjusted ? storage->data() : descs;
		}

		// Wrapper-only interception: child objects (command queue, command
		// list, allocator, resource, ...) are wrapped with COM proxy objects
		// instead of inline-patching shared d3d12.dll code. This keeps DLSS
		// Frame Generation's own D3D12 objects free of our hooks and avoids
		// detour-chain conflicts with Streamline.

		static bool TryWrapCommandAllocator(REFIID riid, void **object)
		{
			if (!IsD3D12CommandAllocatorIid(riid))
				return false;
			return TryWrapDeviceChild<CommandAllocatorWrapper, ID3D12CommandAllocator>(
				riid, object,
				[](CommandAllocatorWrapper *wrapper, REFIID) {
					return static_cast<ID3D12CommandAllocator*>(wrapper);
				});
		}

		static bool TryWrapFence(REFIID riid, void **object)
		{
			if (!IsD3D12FenceIid(riid))
				return false;
			return TryWrapDeviceChild<FenceWrapper, ID3D12Fence>(
				riid, object,
				[](FenceWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetFenceWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapDescriptorHeap(REFIID riid, void **object)
		{
			if (!IsD3D12DescriptorHeapIid(riid))
				return false;
			return TryWrapDeviceChild<DescriptorHeapWrapper, ID3D12DescriptorHeap>(
				riid, object,
				[](DescriptorHeapWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetDescriptorHeapWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapResource(REFIID riid, void **object)
		{
			if (!IsD3D12ResourceIid(riid))
				return false;
			return TryWrapDeviceChild<ResourceWrapper, ID3D12Resource>(
				riid, object,
				[](ResourceWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetResourceWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapCommandQueue(REFIID riid, void **object)
		{
			if (!IsD3D12CommandQueueIid(riid))
				return false;
			return TryWrapDeviceChild<CommandQueueWrapper, ID3D12CommandQueue>(
				riid, object,
				[](CommandQueueWrapper *wrapper, REFIID) {
					return static_cast<ID3D12CommandQueue*>(wrapper);
				});
		}

		static bool TryWrapGraphicsCommandList(REFIID riid, void **object)
		{
			if (!IsD3D12GraphicsCommandListIid(riid))
				return false;
			return TryWrapDeviceChild<GraphicsCommandListWrapper, ID3D12GraphicsCommandList>(
				riid, object,
				[](GraphicsCommandListWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetGraphicsCommandListWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapHeap(REFIID riid, void **object)
		{
			if (!IsD3D12HeapIid(riid))
				return false;
			return TryWrapDeviceChild<HeapWrapper, ID3D12Heap>(
				riid, object,
				[](HeapWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetHeapWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapPipelineState(REFIID riid, void **object)
		{
			if (!IsD3D12PipelineStateIid(riid))
				return false;
			return TryWrapDeviceChild<PipelineStateWrapper, ID3D12PipelineState>(
				riid, object,
				[](PipelineStateWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetPipelineStateWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapRootSignature(REFIID riid, void **object)
		{
			if (!IsD3D12RootSignatureIid(riid))
				return false;
			return TryWrapDeviceChild<RootSignatureWrapper, ID3D12RootSignature>(
				riid, object,
				[](RootSignatureWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetRootSignatureWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapQueryHeap(REFIID riid, void **object)
		{
			if (!IsD3D12QueryHeapIid(riid))
				return false;
			return TryWrapDeviceChild<QueryHeapWrapper, ID3D12QueryHeap>(
				riid, object,
				[](QueryHeapWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetQueryHeapWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapCommandSignature(REFIID riid, void **object)
		{
			if (!IsD3D12CommandSignatureIid(riid))
				return false;
			return TryWrapDeviceChild<CommandSignatureWrapper, ID3D12CommandSignature>(
				riid, object,
				[](CommandSignatureWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetCommandSignatureWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapLifetimeTracker(REFIID riid, void **object)
		{
			if (!IsD3D12LifetimeTrackerIid(riid))
				return false;
			return TryWrapDeviceChild<LifetimeTrackerWrapper, ID3D12LifetimeTracker>(
				riid, object,
				[](LifetimeTrackerWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetLifetimeTrackerWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapProtectedResourceSession(REFIID riid, void **object)
		{
			if (!IsD3D12ProtectedResourceSessionIid(riid))
				return false;
			return TryWrapDeviceChild<ProtectedResourceSessionWrapper, ID3D12ProtectedResourceSession>(
				riid, object,
				[](ProtectedResourceSessionWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetProtectedResourceSessionWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapMetaCommand(REFIID riid, void **object)
		{
			if (!IsD3D12MetaCommandIid(riid))
				return false;
			return TryWrapDeviceChild<MetaCommandWrapper, ID3D12MetaCommand>(
				riid, object,
				[](MetaCommandWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetMetaCommandWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapStateObject(REFIID riid, void **object)
		{
			if (!IsD3D12StateObjectIid(riid))
				return false;
			return TryWrapDeviceChild<StateObjectWrapper, ID3D12StateObject>(
				riid, object,
				[](StateObjectWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetStateObjectWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapPipelineLibrary(REFIID riid, void **object)
		{
			if (!IsD3D12PipelineLibraryIid(riid))
				return false;
			return TryWrapDeviceChild<PipelineLibraryWrapper, ID3D12PipelineLibrary>(
				riid, object,
				[](PipelineLibraryWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetPipelineLibraryWrapperInterface(interfaceIid);
				});
		}

		static bool TryWrapShaderCacheSession(REFIID riid, void **object)
		{
			if (!IsD3D12ShaderCacheSessionIid(riid))
				return false;
			return TryWrapDeviceChild<ShaderCacheSessionWrapper, ID3D12ShaderCacheSession>(
				riid, object,
				[](ShaderCacheSessionWrapper *wrapper, REFIID interfaceIid) {
					return wrapper->GetShaderCacheSessionWrapperInterface(interfaceIid);
				});
		}
	};

	class D3D12DeviceWrapper final : public ID3D12Device14, public WrapperBase
	{
	public:
		explicit D3D12DeviceWrapper(ID3D12Device *device)
			: WrapperBase(device)
		{
			mDevice = device;
			RegisterWrapperPointer(static_cast<ID3D12Device*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device2*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device3*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device4*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device5*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device6*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device7*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device8*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device9*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device10*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device11*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device12*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device13*>(this));
			RegisterWrapperPointer(static_cast<ID3D12Device14*>(this));
			DX12LogManager::Fields("DX12Wrapper",
				"kind=device,stage=create,wrapper=%p,object=%p",
				this, device);
		}

		HRESULT QueryDevice1(ID3D12Device1 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice2(ID3D12Device2 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice3(ID3D12Device3 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice4(ID3D12Device4 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice5(ID3D12Device5 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice6(ID3D12Device6 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice7(ID3D12Device7 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice8(ID3D12Device8 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice9(ID3D12Device9 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice10(ID3D12Device10 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice11(ID3D12Device11 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice12(ID3D12Device12 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice13(ID3D12Device13 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		HRESULT QueryDevice14(ID3D12Device14 **device)
		{
			return mDevice->QueryInterface(IID_PPV_ARGS(device));
		}

		void *GetDeviceWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(ID3D12Device14)))
				return static_cast<ID3D12Device14*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device13)))
				return static_cast<ID3D12Device13*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device12)))
				return static_cast<ID3D12Device12*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device11)))
				return static_cast<ID3D12Device11*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device10)))
				return static_cast<ID3D12Device10*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device9)))
				return static_cast<ID3D12Device9*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device8)))
				return static_cast<ID3D12Device8*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device7)))
				return static_cast<ID3D12Device7*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device6)))
				return static_cast<ID3D12Device6*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device5)))
				return static_cast<ID3D12Device5*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device4)))
				return static_cast<ID3D12Device4*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device3)))
				return static_cast<ID3D12Device3*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device2)))
				return static_cast<ID3D12Device2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device1)))
				return static_cast<ID3D12Device1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Device)))
				return static_cast<ID3D12Device*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Object)))
				return static_cast<ID3D12Object*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsD3D12DeviceIid(riid) || IsEqualIID(riid, __uuidof(ID3D12Object))) {
				if (!IsEqualIID(riid, __uuidof(IUnknown)) && !DoesObjectSupportIid(mDevice, riid))
					return E_NOINTERFACE;
				*ppvObject = GetDeviceWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mDevice->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvObject);
			return hr;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(++mRefCount);
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			return ReleaseWithRegistry(mRefCount);
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryPrivateDataWrapper, Original);
			return mDevice->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscPrivateDataWrapper, Original);
			return mDevice->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscPrivateDataWrapper, Original);
			return mDevice->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscPrivateDataWrapper, Original);
			return mDevice->SetName(Name);
		}

		UINT STDMETHODCALLTYPE GetNodeCount() override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryMiscWrapper, Original);
			return mDevice->GetNodeCount();
		}

		HRESULT STDMETHODCALLTYPE CreateCommandQueue(
			const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(CommandQueueWrapper, Original);
				hr = mDevice->CreateCommandQueue(pDesc, riid, ppCommandQueue);
			}
			DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandQueue",
				"\"hr\":\"0x%lx\",\"queue\":\"%p\"", hr, ppCommandQueue ? *ppCommandQueue : nullptr);
			{
				DX12_CREATE_TIMING_SCOPE(CommandQueueWrapper, Wrap);
				if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue)
					DeviceWrapper::TryWrapCommandQueue(riid, ppCommandQueue);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(CommandAllocatorWrapper, Original);
				hr = mDevice->CreateCommandAllocator(type, riid, ppCommandAllocator);
			}
			DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandAllocator",
				"\"hr\":\"0x%lx\",\"allocator\":\"%p\"", hr,
				ppCommandAllocator ? *ppCommandAllocator : nullptr);
			{
				DX12_CREATE_TIMING_SCOPE(CommandAllocatorWrapper, Wrap);
				if (SUCCEEDED(hr) && ppCommandAllocator && *ppCommandAllocator)
					DeviceWrapper::TryWrapCommandAllocator(riid, ppCommandAllocator);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(
			const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override
		{
			if (DX12InternalReplayState::IsActive())
				return mDevice->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState);

			DX12GraphicsPsoReplacement replacement;
			const D3D12_GRAPHICS_PIPELINE_STATE_DESC *activeDesc = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(GraphicsPsoWrapper, Prepare);
				if (pDesc) {
					replacement.desc = *pDesc;
					replacement.desc.pRootSignature =
						DX12WrapperRuntimeUtils::UnwrapRootSignature(pDesc->pRootSignature);
					activeDesc = &replacement.desc;
					if (DX12ModReplaceShaderBytecode("vs", pDesc->VS, &replacement.vs, &replacement.vsBytes)) {
						replacement.desc.VS = replacement.vs;
						replacement.changed = true;
					}
					if (DX12ModReplaceShaderBytecode("ps", pDesc->PS, &replacement.ps, &replacement.psBytes)) {
						replacement.desc.PS = replacement.ps;
						replacement.changed = true;
					}
				}
			}

			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(GraphicsPsoWrapper, Original);
				hr = mDevice->CreateGraphicsPipelineState(activeDesc, riid, ppPipelineState);
			}
			DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateGraphicsPipelineState",
				"\"hr\":\"0x%lx\",\"pso\":\"%p\"", hr, ppPipelineState ? *ppPipelineState : nullptr);
			{
				DX12_CREATE_TIMING_SCOPE(GraphicsPsoWrapper, Record);
				if (SUCCEEDED(hr) && activeDesc) {
					ID3D12PipelineState *created =
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr;
					if (pDesc)
						DX12ModRecordGraphicsPipelineState(mDevice, created, activeDesc);
					DX12RecordGraphicsPipelineState(created, activeDesc);
				}
			}
			{
				DX12_CREATE_TIMING_SCOPE(GraphicsPsoWrapper, Wrap);
				if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
					DeviceWrapper::TryWrapPipelineState(riid, ppPipelineState);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateComputePipelineState(
			const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) override
		{
			if (DX12InternalReplayState::IsActive())
				return mDevice->CreateComputePipelineState(pDesc, riid, ppPipelineState);

			DX12ComputePsoReplacement replacement;
			const D3D12_COMPUTE_PIPELINE_STATE_DESC *activeDesc = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(ComputePsoWrapper, Prepare);
				if (pDesc) {
					replacement.desc = *pDesc;
					replacement.desc.pRootSignature =
						DX12WrapperRuntimeUtils::UnwrapRootSignature(pDesc->pRootSignature);
					activeDesc = &replacement.desc;
					if (DX12ModReplaceShaderBytecode("cs", pDesc->CS, &replacement.cs, &replacement.csBytes)) {
						replacement.desc.CS = replacement.cs;
						replacement.changed = true;
					}
				}
			}

			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(ComputePsoWrapper, Original);
				hr = mDevice->CreateComputePipelineState(activeDesc, riid, ppPipelineState);
			}
			DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateComputePipelineState",
				"\"hr\":\"0x%lx\",\"pso\":\"%p\"", hr, ppPipelineState ? *ppPipelineState : nullptr);
			{
				DX12_CREATE_TIMING_SCOPE(ComputePsoWrapper, Record);
				if (SUCCEEDED(hr) && activeDesc) {
					ID3D12PipelineState *created =
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr;
					if (pDesc)
						DX12ModRecordComputePipelineState(mDevice, created, activeDesc);
					DX12RecordComputePipelineState(created, activeDesc);
				}
			}
			{
				DX12_CREATE_TIMING_SCOPE(ComputePsoWrapper, Wrap);
				if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
					DeviceWrapper::TryWrapPipelineState(riid, ppPipelineState);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateCommandList(
			UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator,
			ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList) override
		{
			ID3D12CommandAllocator *allocator = nullptr;
			ID3D12PipelineState *initialState = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Prepare);
				allocator = DX12WrapperRuntimeUtils::UnwrapCommandAllocator(pCommandAllocator);
				initialState = DX12WrapperRuntimeUtils::UnwrapPipelineState(pInitialState);
			}
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Original);
				hr = mDevice->CreateCommandList(
					nodeMask, type, allocator, initialState, riid, ppCommandList);
			}
			DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateCommandList",
				"\"hr\":\"0x%lx\",\"commandList\":\"%p\"", hr, ppCommandList ? *ppCommandList : nullptr);
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Record);
				if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) {
					ID3D12GraphicsCommandList *baseList = nullptr;
					if (SUCCEEDED(static_cast<IUnknown*>(*ppCommandList)->QueryInterface(IID_PPV_ARGS(&baseList)))) {
						DX12CommandListLifecycle::Register(baseList, initialState);
						baseList->Release();
					}
				}
			}
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Wrap);
				if (SUCCEEDED(hr) && ppCommandList && *ppCommandList)
					DeviceWrapper::TryWrapGraphicsCommandList(riid, ppCommandList);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
			D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryCapabilityWrapper, Original);
			return mDevice->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
		}

		HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(
			const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorHeapWrapper, Original);
				hr = mDevice->CreateDescriptorHeap(pDescriptorHeapDesc, riid, ppvHeap);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorHeapWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(
					hr, mDevice, pDescriptorHeapDesc, ppvHeap);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorHeapWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapDescriptorHeap(riid, ppvHeap);
			}
			return hr;
		}

		UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryDescriptorWrapper, Original);
			return mDevice->GetDescriptorHandleIncrementSize(DescriptorHeapType);
		}

		HRESULT STDMETHODCALLTYPE CreateRootSignature(
			UINT nodeMask, const void *pBlobWithRootSignature, SIZE_T blobLengthInBytes,
			REFIID riid, void **ppvRootSignature) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(RootSignatureWrapper, Original);
				hr = mDevice->CreateRootSignature(
					nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature);
			}
			{
				DX12_CREATE_TIMING_SCOPE(RootSignatureWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(
					hr, nodeMask, pBlobWithRootSignature, blobLengthInBytes, ppvRootSignature);
			}
			{
				DX12_CREATE_TIMING_SCOPE(RootSignatureWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvRootSignature && *ppvRootSignature)
					DeviceWrapper::TryWrapRootSignature(riid, ppvRootSignature);
			}
			return hr;
		}

		void STDMETHODCALLTYPE CreateConstantBufferView(
			const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorCbvWrapper, Original);
				mDevice->CreateConstantBufferView(pDesc, DestDescriptor);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorCbvWrapper, Record);
				DX12DescriptorHeapShadowUtils::RecordCreatedConstantBufferViewIfChanged(
					pDesc, DestDescriptor);
			}
		}

		void STDMETHODCALLTYPE CreateShaderResourceView(
			ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc,
			D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			ID3D12Resource *resource = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorSrvWrapper, Prepare);
				resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorSrvWrapper, Original);
				mDevice->CreateShaderResourceView(resource, pDesc, DestDescriptor);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorSrvWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(
					mDevice, resource, pDesc, DestDescriptor);
			}
		}

		void STDMETHODCALLTYPE CreateUnorderedAccessView(
			ID3D12Resource *pResource, ID3D12Resource *pCounterResource,
			const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc,
			D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			ID3D12Resource *resource = nullptr;
			ID3D12Resource *counterResource = nullptr;
			D3D12_UNORDERED_ACCESS_VIEW_DESC adjustedDesc = {};
			const D3D12_UNORDERED_ACCESS_VIEW_DESC *descToUse = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorUavWrapper, Prepare);
				resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
				counterResource = DX12WrapperRuntimeUtils::UnwrapResource(pCounterResource);
				descToUse = DX12ResourceTrackerHookRuntime::PrepareUavDesc(
					resource, pDesc, &adjustedDesc, "CreateUnorderedAccessView");
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorUavWrapper, Original);
				mDevice->CreateUnorderedAccessView(resource, counterResource, descToUse, DestDescriptor);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorUavWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedUnorderedAccessView(
					mDevice, resource, counterResource, descToUse, DestDescriptor);
			}
		}

		void STDMETHODCALLTYPE CreateRenderTargetView(
			ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc,
			D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			ID3D12Resource *resource = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorRtvWrapper, Prepare);
				resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorRtvWrapper, Original);
				mDevice->CreateRenderTargetView(resource, pDesc, DestDescriptor);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorRtvWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedRenderTargetView(resource, pDesc, DestDescriptor);
			}
		}

		void STDMETHODCALLTYPE CreateDepthStencilView(
			ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc,
			D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			ID3D12Resource *resource = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorDsvWrapper, Prepare);
				resource = DX12WrapperRuntimeUtils::UnwrapResource(pResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorDsvWrapper, Original);
				mDevice->CreateDepthStencilView(resource, pDesc, DestDescriptor);
			}
			{
				DX12_CREATE_TIMING_SCOPE(DescriptorDsvWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedDepthStencilView(resource, pDesc, DestDescriptor);
			}
		}

		void STDMETHODCALLTYPE CreateSampler(
			const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			{
				DX12_CREATE_TIMING_SCOPE(SamplerWrapper, Original);
				mDevice->CreateSampler(pDesc, DestDescriptor);
			}
			{
				DX12_CREATE_TIMING_SCOPE(SamplerWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedSampler(pDesc, DestDescriptor);
			}
		}

		void STDMETHODCALLTYPE CopyDescriptors(
			UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts,
			const UINT *pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
			const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts,
			const UINT *pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override
		{
			{
				DX12_CREATE_TIMING_SCOPE(CopyDescriptorsWrapper, Original);
				mDevice->CopyDescriptors(
					NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
					DescriptorHeapsType);
			}
			{
				DX12_CREATE_TIMING_SCOPE(CopyDescriptorsWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
					mDevice, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
					NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes,
					DescriptorHeapsType);
			}
		}

		void STDMETHODCALLTYPE CopyDescriptorsSimple(
			UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
			D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
			D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override
		{
			{
				DX12_CREATE_TIMING_SCOPE(CopyDescriptorsSimpleWrapper, Original);
				mDevice->CopyDescriptorsSimple(
					NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);
			}
			if (NumDescriptors &&
					DestDescriptorRangeStart.ptr != SrcDescriptorRangeStart.ptr &&
					(DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
						DX12ResourceTrackingPolicyUtils::NeedsDumpOrHuntingTracking())) {
				DX12_CREATE_TIMING_SCOPE(CopyDescriptorsSimpleWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCopiedDescriptorsSimple(
					NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart,
					DescriptorHeapsType,
					mDevice->GetDescriptorHandleIncrementSize(DescriptorHeapsType));
			}
		}

		D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(
			UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryAllocationWrapper, Original);
			std::vector<D3D12_RESOURCE_DESC> adjustedDescs;
			const D3D12_RESOURCE_DESC *descsToUse = DeviceWrapper::PrepareResourceDescArray(
				pResourceDescs, numResourceDescs, &adjustedDescs,
				"GetResourceAllocationInfo");
			return mDevice->GetResourceAllocationInfo(
				visibleMask, numResourceDescs, descsToUse);
		}

		D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(
			UINT nodeMask, D3D12_HEAP_TYPE heapType) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryCapabilityWrapper, Original);
			return mDevice->GetCustomHeapProperties(nodeMask, heapType);
		}

		HRESULT STDMETHODCALLTYPE CreateCommittedResource(
			const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
			const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource,
			void **ppvResource) override
		{
			if (DX12InternalReplayState::IsActive())
				return mDevice->CreateCommittedResource(
					pHeapProperties, HeapFlags, pDesc, InitialResourceState,
					pOptimizedClearValue, riidResource, ppvResource);
			D3D12_RESOURCE_DESC adjustedDesc = {};
			const D3D12_RESOURCE_DESC *descToUse = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Prepare);
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
					pDesc, &adjustedDesc, "CreateCommittedResource");
			}
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Original);
				hr = mDevice->CreateCommittedResource(
					pHeapProperties, HeapFlags, descToUse, InitialResourceState,
					pOptimizedClearValue, riidResource, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialResourceState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateHeap(
			const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(HeapWrapper, Original);
				hr = mDevice->CreateHeap(pDesc, riid, ppvHeap);
			}
			{
				DX12_CREATE_TIMING_SCOPE(HeapWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapHeap(riid, ppvHeap);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreatePlacedResource(
			ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc,
			D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			REFIID riid, void **ppvResource) override
		{
			ID3D12Heap *heap = nullptr;
			D3D12_RESOURCE_DESC adjustedDesc = {};
			const D3D12_RESOURCE_DESC *descToUse = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Prepare);
				heap = DX12WrapperRuntimeUtils::UnwrapHeap(pHeap);
				// Must remain paired with GetResourceAllocationInfo above.
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
					pDesc, &adjustedDesc, "CreatePlacedResource");
			}
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Original);
				hr = mDevice->CreatePlacedResource(
					heap, HeapOffset, descToUse, InitialState,
					pOptimizedClearValue, riid, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Record);
				PlacedResourceInfo placedInfo =
					DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, HeapOffset);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, descToUse, nullptr, &placedInfo, InitialState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateReservedResource(
			const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid,
			void **ppvResource) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Original);
				hr = mDevice->CreateReservedResource(
					pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, pDesc, nullptr, nullptr, InitialState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateSharedHandle(
			ID3D12DeviceChild *pObject, const SECURITY_ATTRIBUTES *pAttributes,
			DWORD Access, LPCWSTR Name, HANDLE *pHandle) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscSharedHandleWrapper, Original);
			return mDevice->CreateSharedHandle(
				reinterpret_cast<ID3D12DeviceChild*>(DX12Wrapper::Unwrap(pObject)),
				pAttributes, Access, Name, pHandle);
		}

		HRESULT STDMETHODCALLTYPE OpenSharedHandle(
			HANDLE NTHandle, REFIID riid, void **ppvObj) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscSharedHandleWrapper, Original);
			HRESULT hr = mDevice->OpenSharedHandle(NTHandle, riid, ppvObj);
			if (SUCCEEDED(hr) && ppvObj && *ppvObj)
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvObj);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE *pNTHandle) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscSharedHandleWrapper, Original);
			return mDevice->OpenSharedHandleByName(Name, Access, pNTHandle);
		}

		HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable *const *ppObjects) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceResidencyWrapper, Original);
			std::vector<ID3D12Pageable*> unwrapped;
			ID3D12Pageable *const *activeObjects = ppObjects;
			if (ppObjects && NumObjects) {
				unwrapped.resize(NumObjects);
				for (UINT i = 0; i < NumObjects; ++i)
					unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
				activeObjects = unwrapped.data();
			}
			return mDevice->MakeResident(NumObjects, activeObjects);
		}

		HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable *const *ppObjects) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceResidencyWrapper, Original);
			std::vector<ID3D12Pageable*> unwrapped;
			ID3D12Pageable *const *activeObjects = ppObjects;
			if (ppObjects && NumObjects) {
				unwrapped.resize(NumObjects);
				for (UINT i = 0; i < NumObjects; ++i)
					unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
				activeObjects = unwrapped.data();
			}
			return mDevice->Evict(NumObjects, activeObjects);
		}

		HRESULT STDMETHODCALLTYPE CreateFence(
			UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(FenceWrapper, Original);
				hr = mDevice->CreateFence(InitialValue, Flags, riid, ppFence);
			}
			{
				DX12_CREATE_TIMING_SCOPE(FenceWrapper, Wrap);
				if (SUCCEEDED(hr) && ppFence && *ppFence)
					DeviceWrapper::TryWrapFence(riid, ppFence);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryMiscWrapper, Original);
			return mDevice->GetDeviceRemovedReason();
		}

		void STDMETHODCALLTYPE GetCopyableFootprints(
			const D3D12_RESOURCE_DESC *pResourceDesc, UINT FirstSubresource,
			UINT NumSubresources, UINT64 BaseOffset,
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows,
			UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryLayoutWrapper, Original);
			mDevice->GetCopyableFootprints(
				pResourceDesc, FirstSubresource, NumSubresources, BaseOffset,
				pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes);
		}

		HRESULT STDMETHODCALLTYPE CreateQueryHeap(
			const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap) override
		{
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(QueryHeapWrapper, Original);
				hr = mDevice->CreateQueryHeap(pDesc, riid, ppvHeap);
			}
			{
				DX12_CREATE_TIMING_SCOPE(QueryHeapWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
					DeviceWrapper::TryWrapQueryHeap(riid, ppvHeap);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscPowerWrapper, Original);
			return mDevice->SetStablePowerState(Enable);
		}

		HRESULT STDMETHODCALLTYPE CreateCommandSignature(
			const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature,
			REFIID riid, void **ppvCommandSignature) override
		{
			ID3D12RootSignature *rootSignature = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(CommandSignatureWrapper, Prepare);
				rootSignature = DX12WrapperRuntimeUtils::UnwrapRootSignature(pRootSignature);
			}
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(CommandSignatureWrapper, Original);
				hr = mDevice->CreateCommandSignature(
					pDesc, rootSignature, riid, ppvCommandSignature);
			}
			{
				DX12_CREATE_TIMING_SCOPE(CommandSignatureWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvCommandSignature && *ppvCommandSignature)
					DeviceWrapper::TryWrapCommandSignature(riid, ppvCommandSignature);
			}
			return hr;
		}

		void STDMETHODCALLTYPE GetResourceTiling(
			ID3D12Resource *pTiledResource, UINT *pNumTilesForEntireResource,
			D3D12_PACKED_MIP_INFO *pPackedMipDesc,
			D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips,
			UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet,
			D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryTilingWrapper, Original);
			mDevice->GetResourceTiling(
				DX12WrapperRuntimeUtils::UnwrapResource(pTiledResource),
				pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips,
				pNumSubresourceTilings, FirstSubresourceTilingToGet,
				pSubresourceTilingsForNonPackedMips);
		}

		LUID STDMETHODCALLTYPE GetAdapterLuid() override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryMiscWrapper, Original);
			return mDevice->GetAdapterLuid();
		}

		HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(
			const void *pLibraryBlob, SIZE_T BlobLength, REFIID riid, void **ppPipelineLibrary) override
		{
			ID3D12Device1 *device1 = nullptr;
			HRESULT hr = QueryDevice1(&device1);
			if (FAILED(hr))
				return hr;
			{
				DX12_CREATE_TIMING_SCOPE(PipelineLibraryWrapper, Original);
				hr = device1->CreatePipelineLibrary(pLibraryBlob, BlobLength, riid, ppPipelineLibrary);
			}
			{
				DX12_CREATE_TIMING_SCOPE(PipelineLibraryWrapper, Wrap);
				if (SUCCEEDED(hr) && ppPipelineLibrary && *ppPipelineLibrary)
					DeviceWrapper::TryWrapPipelineLibrary(riid, ppPipelineLibrary);
			}
			device1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(
			ID3D12Fence *const *ppFences, const UINT64 *pFenceValues, UINT NumFences,
			D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscPowerWrapper, Original);
			ID3D12Device1 *device1 = nullptr;
			HRESULT hr = QueryDevice1(&device1);
			if (FAILED(hr))
				return hr;
			std::vector<ID3D12Fence*> unwrapped;
			ID3D12Fence *const *activeFences = ppFences;
			if (ppFences && NumFences) {
				unwrapped.resize(NumFences);
				for (UINT i = 0; i < NumFences; ++i)
					unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapFence(ppFences[i]);
				activeFences = unwrapped.data();
			}
			hr = device1->SetEventOnMultipleFenceCompletion(
				activeFences, pFenceValues, NumFences, Flags, hEvent);
			device1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE SetResidencyPriority(
			UINT NumObjects, ID3D12Pageable *const *ppObjects,
			const D3D12_RESIDENCY_PRIORITY *pPriorities) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceResidencyWrapper, Original);
			ID3D12Device1 *device1 = nullptr;
			HRESULT hr = QueryDevice1(&device1);
			if (FAILED(hr))
				return hr;
			std::vector<ID3D12Pageable*> unwrapped;
			ID3D12Pageable *const *activeObjects = ppObjects;
			if (ppObjects && NumObjects) {
				unwrapped.resize(NumObjects);
				for (UINT i = 0; i < NumObjects; ++i)
					unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
				activeObjects = unwrapped.data();
			}
			hr = device1->SetResidencyPriority(NumObjects, activeObjects, pPriorities);
			device1->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreatePipelineState(
			const D3D12_PIPELINE_STATE_STREAM_DESC *pDesc, REFIID riid,
			void **ppPipelineState) override
		{
			ID3D12Device2 *device2 = nullptr;
			HRESULT hr = E_FAIL;
			{
				DX12_CREATE_TIMING_SCOPE(PsoStreamWrapper, Prepare);
				hr = QueryDevice2(&device2);
			}
			if (FAILED(hr))
				return hr;
			{
				DX12_CREATE_TIMING_SCOPE(PsoStreamWrapper, Original);
				hr = device2->CreatePipelineState(pDesc, riid, ppPipelineState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(PsoStreamWrapper, Record);
				if (SUCCEEDED(hr) && pDesc)
					DX12RecordPipelineStateStream(
						ppPipelineState ? static_cast<ID3D12PipelineState*>(*ppPipelineState) : nullptr,
						pDesc);
			}
			{
				DX12_CREATE_TIMING_SCOPE(PsoStreamWrapper, Wrap);
				if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState)
					DeviceWrapper::TryWrapPipelineState(riid, ppPipelineState);
			}
			device2->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(
			const void *pAddress, REFIID riid, void **ppvHeap) override
		{
			DX12_CREATE_TIMING_SCOPE(HeapWrapper, Original);
			ID3D12Device3 *device3 = nullptr;
			HRESULT hr = QueryDevice3(&device3);
			if (FAILED(hr))
				return hr;
			hr = device3->OpenExistingHeapFromAddress(pAddress, riid, ppvHeap);
			if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
				DeviceWrapper::TryWrapHeap(riid, ppvHeap);
			device3->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(
			HANDLE hFileMapping, REFIID riid, void **ppvHeap) override
		{
			DX12_CREATE_TIMING_SCOPE(HeapWrapper, Original);
			ID3D12Device3 *device3 = nullptr;
			HRESULT hr = QueryDevice3(&device3);
			if (FAILED(hr))
				return hr;
			hr = device3->OpenExistingHeapFromFileMapping(hFileMapping, riid, ppvHeap);
			if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
				DeviceWrapper::TryWrapHeap(riid, ppvHeap);
			device3->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE EnqueueMakeResident(
			D3D12_RESIDENCY_FLAGS Flags, UINT NumObjects, ID3D12Pageable *const *ppObjects,
			ID3D12Fence *pFenceToSignal, UINT64 FenceValueToSignal) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceResidencyWrapper, Original);
			ID3D12Device3 *device3 = nullptr;
			HRESULT hr = QueryDevice3(&device3);
			if (FAILED(hr))
				return hr;
			std::vector<ID3D12Pageable*> unwrapped;
			ID3D12Pageable *const *activeObjects = ppObjects;
			if (ppObjects && NumObjects) {
				unwrapped.resize(NumObjects);
				for (UINT i = 0; i < NumObjects; ++i)
					unwrapped[i] = DX12WrapperRuntimeUtils::UnwrapPageable(ppObjects[i]);
				activeObjects = unwrapped.data();
			}
			hr = device3->EnqueueMakeResident(
				Flags, NumObjects, activeObjects,
				DX12WrapperRuntimeUtils::UnwrapFence(pFenceToSignal), FenceValueToSignal);
			device3->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateCommandList1(
			UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags,
			REFIID riid, void **ppCommandList) override
		{
			ID3D12Device4 *device4 = nullptr;
			HRESULT hr = QueryDevice4(&device4);
			if (FAILED(hr))
				return hr;
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Original);
				hr = device4->CreateCommandList1(nodeMask, type, flags, riid, ppCommandList);
			}
			DX12FrameAnalysis::LogJsonFunc("ID3D12Device4::CreateCommandList1",
				"\"hr\":\"0x%lx\",\"commandList\":\"%p\"", hr,
				ppCommandList ? *ppCommandList : nullptr);
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Record);
				if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) {
					ID3D12GraphicsCommandList *baseList = nullptr;
					if (SUCCEEDED(static_cast<IUnknown*>(*ppCommandList)->QueryInterface(IID_PPV_ARGS(&baseList)))) {
						DX12CommandListLifecycle::Register(baseList, nullptr);
						baseList->Release();
					}
				}
			}
			{
				DX12_CREATE_TIMING_SCOPE(CommandListWrapper, Wrap);
				if (SUCCEEDED(hr) && ppCommandList && *ppCommandList)
					DeviceWrapper::TryWrapGraphicsCommandList(riid, ppCommandList);
			}
			device4->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession(
			const D3D12_PROTECTED_RESOURCE_SESSION_DESC *pDesc, REFIID riid,
			void **ppSession) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscProtectedSessionWrapper, Original);
			ID3D12Device4 *device4 = nullptr;
			HRESULT hr = QueryDevice4(&device4);
			if (FAILED(hr))
				return hr;
			hr = device4->CreateProtectedResourceSession(pDesc, riid, ppSession);
			if (SUCCEEDED(hr) && ppSession && *ppSession)
				DeviceWrapper::TryWrapProtectedResourceSession(riid, ppSession);
			device4->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateCommittedResource1(
			const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
			const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			ID3D12ProtectedResourceSession *pProtectedSession, REFIID riidResource,
			void **ppvResource) override
		{
			ID3D12Device4 *device4 = nullptr;
			HRESULT hr = QueryDevice4(&device4);
			if (FAILED(hr))
				return hr;
			D3D12_RESOURCE_DESC adjustedDesc = {};
			const D3D12_RESOURCE_DESC *descToUse = pDesc;
			ID3D12ProtectedResourceSession *protectedSession = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Prepare);
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc(
					pDesc, &adjustedDesc, "CreateCommittedResource1");
				protectedSession =
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Original);
				hr = device4->CreateCommittedResource1(
					pHeapProperties, HeapFlags, descToUse, InitialResourceState,
					pOptimizedClearValue, protectedSession, riidResource, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialResourceState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
			}
			device4->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateHeap1(
			const D3D12_HEAP_DESC *pDesc, ID3D12ProtectedResourceSession *pProtectedSession,
			REFIID riid, void **ppvHeap) override
		{
			DX12_CREATE_TIMING_SCOPE(HeapWrapper, Original);
			ID3D12Device4 *device4 = nullptr;
			HRESULT hr = QueryDevice4(&device4);
			if (FAILED(hr))
				return hr;
			hr = device4->CreateHeap1(
				pDesc, DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession),
				riid, ppvHeap);
			if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
				DeviceWrapper::TryWrapHeap(riid, ppvHeap);
			device4->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateReservedResource1(
			const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			ID3D12ProtectedResourceSession *pProtectedSession, REFIID riid,
			void **ppvResource) override
		{
			ID3D12Device4 *device4 = nullptr;
			HRESULT hr = QueryDevice4(&device4);
			if (FAILED(hr))
				return hr;
			ID3D12ProtectedResourceSession *protectedSession = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Prepare);
				protectedSession =
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Original);
				hr = device4->CreateReservedResource1(
					pDesc, InitialState, pOptimizedClearValue,
					protectedSession, riid, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource(
					hr, ppvResource, pDesc, nullptr, nullptr, InitialState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
			}
			device4->Release();
			return hr;
		}

		D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo1(
			UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs,
			D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryAllocationWrapper, Original);
			ID3D12Device4 *device4 = nullptr;
			HRESULT hr = QueryDevice4(&device4);
			if (FAILED(hr))
				return D3D12_RESOURCE_ALLOCATION_INFO();
			std::vector<D3D12_RESOURCE_DESC> adjustedDescs;
			const D3D12_RESOURCE_DESC *descsToUse = DeviceWrapper::PrepareResourceDescArray(
				pResourceDescs, numResourceDescs, &adjustedDescs,
				"GetResourceAllocationInfo1");
			D3D12_RESOURCE_ALLOCATION_INFO info = device4->GetResourceAllocationInfo1(
				visibleMask, numResourceDescs, descsToUse, pResourceAllocationInfo1);
			device4->Release();
			return info;
		}

		HRESULT STDMETHODCALLTYPE CreateLifetimeTracker(
			ID3D12LifetimeOwner *pOwner, REFIID riid, void **ppvTracker) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscLifetimeWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			HRESULT hr = QueryDevice5(&device5);
			if (FAILED(hr))
				return hr;
			hr = device5->CreateLifetimeTracker(pOwner, riid, ppvTracker);
			if (SUCCEEDED(hr) && ppvTracker && *ppvTracker)
				DeviceWrapper::TryWrapLifetimeTracker(riid, ppvTracker);
			device5->Release();
			return hr;
		}

		void STDMETHODCALLTYPE RemoveDevice() override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscLifetimeWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			if (SUCCEEDED(QueryDevice5(&device5))) {
				device5->RemoveDevice();
				device5->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE EnumerateMetaCommands(
			UINT *pNumMetaCommands, D3D12_META_COMMAND_DESC *pDescs) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryMetaCommandWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			HRESULT hr = QueryDevice5(&device5);
			if (FAILED(hr))
				return hr;
			hr = device5->EnumerateMetaCommands(pNumMetaCommands, pDescs);
			device5->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE EnumerateMetaCommandParameters(
			REFGUID CommandId, D3D12_META_COMMAND_PARAMETER_STAGE Stage,
			UINT *pTotalStructureSizeInBytes, UINT *pParameterCount,
			D3D12_META_COMMAND_PARAMETER_DESC *pParameterDescs) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryMetaCommandWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			HRESULT hr = QueryDevice5(&device5);
			if (FAILED(hr))
				return hr;
			hr = device5->EnumerateMetaCommandParameters(
				CommandId, Stage, pTotalStructureSizeInBytes, pParameterCount, pParameterDescs);
			device5->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateMetaCommand(
			REFGUID CommandId, UINT NodeMask, const void *pCreationParametersData,
			SIZE_T CreationParametersDataSizeInBytes, REFIID riid,
			void **ppMetaCommand) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscMetaCommandWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			HRESULT hr = QueryDevice5(&device5);
			if (FAILED(hr))
				return hr;
			hr = device5->CreateMetaCommand(
				CommandId, NodeMask, pCreationParametersData,
				CreationParametersDataSizeInBytes, riid, ppMetaCommand);
			if (SUCCEEDED(hr) && ppMetaCommand && *ppMetaCommand)
				DeviceWrapper::TryWrapMetaCommand(riid, ppMetaCommand);
			device5->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateStateObject(
			const D3D12_STATE_OBJECT_DESC *pDesc, REFIID riid, void **ppStateObject) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscStateObjectWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			HRESULT hr = QueryDevice5(&device5);
			if (FAILED(hr))
				return hr;
			D3D12_STATE_OBJECT_DESC descStorage = {};
			std::vector<D3D12_STATE_SUBOBJECT> subobjectStorage;
			std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> globalRootSignatureStorage;
			std::vector<D3D12_LOCAL_ROOT_SIGNATURE> localRootSignatureStorage;
			std::vector<D3D12_EXISTING_COLLECTION_DESC> existingCollectionStorage;
			std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associationStorage;
			const D3D12_STATE_OBJECT_DESC *activeDesc = pDesc;
			DX12WrapperRuntimeUtils::UnwrapStateObjectDesc(
				pDesc,
				&descStorage,
				&subobjectStorage,
				&globalRootSignatureStorage,
				&localRootSignatureStorage,
				&existingCollectionStorage,
				&associationStorage,
				&activeDesc);
			hr = device5->CreateStateObject(activeDesc, riid, ppStateObject);
			if (SUCCEEDED(hr) && ppStateObject && *ppStateObject)
				DeviceWrapper::TryWrapStateObject(riid, ppStateObject);
			device5->Release();
			return hr;
		}

		void STDMETHODCALLTYPE GetRaytracingAccelerationStructurePrebuildInfo(
			const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *pDesc,
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *pInfo) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryRaytracingWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			if (SUCCEEDED(QueryDevice5(&device5))) {
				device5->GetRaytracingAccelerationStructurePrebuildInfo(pDesc, pInfo);
				device5->Release();
			}
		}

		D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE CheckDriverMatchingIdentifier(
			D3D12_SERIALIZED_DATA_TYPE SerializedDataType,
			const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER *pIdentifierToCheck) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryRaytracingWrapper, Original);
			ID3D12Device5 *device5 = nullptr;
			HRESULT hr = QueryDevice5(&device5);
			if (FAILED(hr))
				return D3D12_DRIVER_MATCHING_IDENTIFIER_UNRECOGNIZED;
			D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS status =
				device5->CheckDriverMatchingIdentifier(SerializedDataType, pIdentifierToCheck);
			device5->Release();
			return status;
		}

		HRESULT STDMETHODCALLTYPE SetBackgroundProcessingMode(
			D3D12_BACKGROUND_PROCESSING_MODE Mode, D3D12_MEASUREMENTS_ACTION MeasurementsAction,
			HANDLE hEventToSignalUponCompletion, BOOL *pbFurtherMeasurementsDesired) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscBackgroundProcessingWrapper, Original);
			ID3D12Device6 *device6 = nullptr;
			HRESULT hr = QueryDevice6(&device6);
			if (FAILED(hr))
				return hr;
			hr = device6->SetBackgroundProcessingMode(
				Mode, MeasurementsAction, hEventToSignalUponCompletion,
				pbFurtherMeasurementsDesired);
			device6->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE AddToStateObject(
			const D3D12_STATE_OBJECT_DESC *pAddition, ID3D12StateObject *pStateObjectToGrowFrom,
			REFIID riid, void **ppNewStateObject) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscStateObjectWrapper, Original);
			ID3D12Device7 *device7 = nullptr;
			HRESULT hr = QueryDevice7(&device7);
			if (FAILED(hr))
				return hr;
			D3D12_STATE_OBJECT_DESC descStorage = {};
			std::vector<D3D12_STATE_SUBOBJECT> subobjectStorage;
			std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> globalRootSignatureStorage;
			std::vector<D3D12_LOCAL_ROOT_SIGNATURE> localRootSignatureStorage;
			std::vector<D3D12_EXISTING_COLLECTION_DESC> existingCollectionStorage;
			std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> associationStorage;
			const D3D12_STATE_OBJECT_DESC *activeAddition = pAddition;
			DX12WrapperRuntimeUtils::UnwrapStateObjectDesc(
				pAddition,
				&descStorage,
				&subobjectStorage,
				&globalRootSignatureStorage,
				&localRootSignatureStorage,
				&existingCollectionStorage,
				&associationStorage,
				&activeAddition);
			hr = device7->AddToStateObject(
				activeAddition,
				DX12WrapperRuntimeUtils::UnwrapStateObject(pStateObjectToGrowFrom),
				riid,
				ppNewStateObject);
			if (SUCCEEDED(hr) && ppNewStateObject && *ppNewStateObject)
				DeviceWrapper::TryWrapStateObject(riid, ppNewStateObject);
			device7->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession1(
			const D3D12_PROTECTED_RESOURCE_SESSION_DESC1 *pDesc, REFIID riid,
			void **ppSession) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscProtectedSessionWrapper, Original);
			ID3D12Device7 *device7 = nullptr;
			HRESULT hr = QueryDevice7(&device7);
			if (FAILED(hr))
				return hr;
			hr = device7->CreateProtectedResourceSession1(pDesc, riid, ppSession);
			if (SUCCEEDED(hr) && ppSession && *ppSession)
				DeviceWrapper::TryWrapProtectedResourceSession(riid, ppSession);
			device7->Release();
			return hr;
		}

		D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo2(
			UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC1 *pResourceDescs,
			D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryAllocationWrapper, Original);
			ID3D12Device8 *device8 = nullptr;
			HRESULT hr = QueryDevice8(&device8);
			if (FAILED(hr))
				return D3D12_RESOURCE_ALLOCATION_INFO();
			std::vector<D3D12_RESOURCE_DESC1> adjustedDescs;
			const D3D12_RESOURCE_DESC1 *descsToUse = DeviceWrapper::PrepareResourceDescArray1(
				pResourceDescs, numResourceDescs, &adjustedDescs,
				"GetResourceAllocationInfo2");
			D3D12_RESOURCE_ALLOCATION_INFO info = device8->GetResourceAllocationInfo2(
				visibleMask, numResourceDescs, descsToUse, pResourceAllocationInfo1);
			device8->Release();
			return info;
		}

		HRESULT STDMETHODCALLTYPE CreateCommittedResource2(
			const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
			const D3D12_RESOURCE_DESC1 *pDesc, D3D12_RESOURCE_STATES InitialResourceState,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			ID3D12ProtectedResourceSession *pProtectedSession, REFIID riidResource,
			void **ppvResource) override
		{
			ID3D12Device8 *device8 = nullptr;
			HRESULT hr = QueryDevice8(&device8);
			if (FAILED(hr))
				return hr;
			D3D12_RESOURCE_DESC1 adjustedDesc = {};
			const D3D12_RESOURCE_DESC1 *descToUse = pDesc;
			ID3D12ProtectedResourceSession *protectedSession = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Prepare);
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
					pDesc, &adjustedDesc, "CreateCommittedResource2");
				protectedSession =
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Original);
				hr = device8->CreateCommittedResource2(
					pHeapProperties, HeapFlags, descToUse, InitialResourceState,
					pOptimizedClearValue, protectedSession, riidResource, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialResourceState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
			}
			device8->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreatePlacedResource1(
			ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1 *pDesc,
			D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			REFIID riid, void **ppvResource) override
		{
			ID3D12Device8 *device8 = nullptr;
			HRESULT hr = QueryDevice8(&device8);
			if (FAILED(hr))
				return hr;
			ID3D12Heap *heap = nullptr;
			D3D12_RESOURCE_DESC1 adjustedDesc = {};
			const D3D12_RESOURCE_DESC1 *descToUse = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Prepare);
				heap = DX12WrapperRuntimeUtils::UnwrapHeap(pHeap);
				// Must remain paired with GetResourceAllocationInfo2.
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
					pDesc, &adjustedDesc, "CreatePlacedResource1");
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Original);
				hr = device8->CreatePlacedResource1(
					heap, HeapOffset, descToUse, InitialState,
					pOptimizedClearValue, riid, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Record);
				PlacedResourceInfo placedInfo =
					DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, HeapOffset);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1(
					hr, ppvResource, descToUse, nullptr, &placedInfo, InitialState);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
			}
			device8->Release();
			return hr;
		}

		void STDMETHODCALLTYPE CreateSamplerFeedbackUnorderedAccessView(
			ID3D12Resource *pTargetedResource, ID3D12Resource *pFeedbackResource,
			D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			DX12_CREATE_TIMING_SCOPE(DescriptorUavWrapper, Original);
			ID3D12Device8 *device8 = nullptr;
			if (SUCCEEDED(QueryDevice8(&device8))) {
				device8->CreateSamplerFeedbackUnorderedAccessView(
					DX12WrapperRuntimeUtils::UnwrapResource(pTargetedResource),
					DX12WrapperRuntimeUtils::UnwrapResource(pFeedbackResource),
					DestDescriptor);
				device8->Release();
			}
		}

		void STDMETHODCALLTYPE GetCopyableFootprints1(
			const D3D12_RESOURCE_DESC1 *pResourceDesc, UINT FirstSubresource,
			UINT NumSubresources, UINT64 BaseOffset,
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows,
			UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes) override
		{
			ID3D12Device8 *device8 = nullptr;
			if (SUCCEEDED(QueryDevice8(&device8))) {
				DX12_CREATE_TIMING_SCOPE(DeviceQueryLayoutWrapper, Original);
				device8->GetCopyableFootprints1(
					pResourceDesc, FirstSubresource, NumSubresources, BaseOffset,
					pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes);
				device8->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE CreateShaderCacheSession(
			const D3D12_SHADER_CACHE_SESSION_DESC *pDesc, REFIID riid,
			void **ppvSession) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscShaderCacheWrapper, Original);
			ID3D12Device9 *device9 = nullptr;
			HRESULT hr = QueryDevice9(&device9);
			if (FAILED(hr))
				return hr;
			hr = device9->CreateShaderCacheSession(pDesc, riid, ppvSession);
			if (SUCCEEDED(hr) && ppvSession && *ppvSession)
				DeviceWrapper::TryWrapShaderCacheSession(riid, ppvSession);
			device9->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE ShaderCacheControl(
			D3D12_SHADER_CACHE_KIND_FLAGS Kinds, D3D12_SHADER_CACHE_CONTROL_FLAGS Control) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceMiscShaderCacheWrapper, Original);
			ID3D12Device9 *device9 = nullptr;
			HRESULT hr = QueryDevice9(&device9);
			if (FAILED(hr))
				return hr;
			hr = device9->ShaderCacheControl(Kinds, Control);
			device9->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateCommandQueue1(
			const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID CreatorID, REFIID riid,
			void **ppCommandQueue) override
		{
			ID3D12Device9 *device9 = nullptr;
			HRESULT hr = QueryDevice9(&device9);
			if (FAILED(hr))
				return hr;
			{
				DX12_CREATE_TIMING_SCOPE(CommandQueueWrapper, Original);
				hr = device9->CreateCommandQueue1(pDesc, CreatorID, riid, ppCommandQueue);
			}
			{
				DX12_CREATE_TIMING_SCOPE(CommandQueueWrapper, Wrap);
				if (SUCCEEDED(hr) && ppCommandQueue && *ppCommandQueue)
					DeviceWrapper::TryWrapCommandQueue(riid, ppCommandQueue);
			}
			device9->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateCommittedResource3(
			const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags,
			const D3D12_RESOURCE_DESC1 *pDesc, D3D12_BARRIER_LAYOUT InitialLayout,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			ID3D12ProtectedResourceSession *pProtectedSession, UINT32 NumCastableFormats,
			const DXGI_FORMAT *pCastableFormats, REFIID riidResource,
			void **ppvResource) override
		{
			ID3D12Device10 *device10 = nullptr;
			HRESULT hr = QueryDevice10(&device10);
			if (FAILED(hr))
				return hr;
			D3D12_RESOURCE_DESC1 adjustedDesc = {};
			const D3D12_RESOURCE_DESC1 *descToUse = pDesc;
			ID3D12ProtectedResourceSession *protectedSession = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Prepare);
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
					pDesc, &adjustedDesc, "CreateCommittedResource3");
				protectedSession =
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Original);
				hr = device10->CreateCommittedResource3(
					pHeapProperties, HeapFlags, descToUse, InitialLayout, pOptimizedClearValue,
					protectedSession, NumCastableFormats, pCastableFormats,
					riidResource, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
					hr, ppvResource, descToUse, pHeapProperties, nullptr, InitialLayout);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceCommittedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riidResource, ppvResource);
			}
			device10->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreatePlacedResource2(
			ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1 *pDesc,
			D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			UINT32 NumCastableFormats, const DXGI_FORMAT *pCastableFormats,
			REFIID riid, void **ppvResource) override
		{
			ID3D12Device10 *device10 = nullptr;
			HRESULT hr = QueryDevice10(&device10);
			if (FAILED(hr))
				return hr;
			ID3D12Heap *heap = nullptr;
			D3D12_RESOURCE_DESC1 adjustedDesc = {};
			const D3D12_RESOURCE_DESC1 *descToUse = pDesc;
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Prepare);
				heap = DX12WrapperRuntimeUtils::UnwrapHeap(pHeap);
				// Must remain paired with GetResourceAllocationInfo3.
				descToUse = DX12ResourceTrackerHookRuntime::PrepareResourceDesc1(
					pDesc, &adjustedDesc, "CreatePlacedResource2");
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Original);
				hr = device10->CreatePlacedResource2(
					heap, HeapOffset, descToUse, InitialLayout, pOptimizedClearValue,
					NumCastableFormats, pCastableFormats, riid, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Record);
				PlacedResourceInfo placedInfo =
					DX12ResourceTrackerHookRuntime::MakePlacedResourceInfo(heap, HeapOffset);
				DX12ResourceTrackerHookRuntime::RecordCreatedResource1WithLayout(
					hr, ppvResource, descToUse, nullptr, &placedInfo, InitialLayout);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourcePlacedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
			}
			device10->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateReservedResource2(
			const D3D12_RESOURCE_DESC *pDesc, D3D12_BARRIER_LAYOUT InitialLayout,
			const D3D12_CLEAR_VALUE *pOptimizedClearValue,
			ID3D12ProtectedResourceSession *pProtectedSession, UINT32 NumCastableFormats,
			const DXGI_FORMAT *pCastableFormats, REFIID riid,
			void **ppvResource) override
		{
			ID3D12Device10 *device10 = nullptr;
			HRESULT hr = QueryDevice10(&device10);
			if (FAILED(hr))
				return hr;
			ID3D12ProtectedResourceSession *protectedSession = nullptr;
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Prepare);
				protectedSession =
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedSession);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Original);
				hr = device10->CreateReservedResource2(
					pDesc, InitialLayout, pOptimizedClearValue, protectedSession,
					NumCastableFormats, pCastableFormats, riid, ppvResource);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedResourceWithLayout(
					hr, ppvResource, pDesc, nullptr, nullptr, InitialLayout);
			}
			{
				DX12_CREATE_TIMING_SCOPE(ResourceReservedWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvResource && *ppvResource)
					DeviceWrapper::TryWrapResource(riid, ppvResource);
			}
			device10->Release();
			return hr;
		}

		void STDMETHODCALLTYPE CreateSampler2(
			const D3D12_SAMPLER_DESC2 *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			DX12_CREATE_TIMING_SCOPE(SamplerWrapper, Original);
			ID3D12Device11 *device11 = nullptr;
			if (SUCCEEDED(QueryDevice11(&device11))) {
				device11->CreateSampler2(pDesc, DestDescriptor);
				device11->Release();
			}
		}

		D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo3(
			UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC1 *pResourceDescs,
			const UINT32 *pNumCastableFormats, const DXGI_FORMAT *const *ppCastableFormats,
			D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) override
		{
			DX12_CREATE_TIMING_SCOPE(DeviceQueryAllocationWrapper, Original);
			ID3D12Device12 *device12 = nullptr;
			HRESULT hr = QueryDevice12(&device12);
			if (FAILED(hr))
				return D3D12_RESOURCE_ALLOCATION_INFO();
			std::vector<D3D12_RESOURCE_DESC1> adjustedDescs;
			const D3D12_RESOURCE_DESC1 *descsToUse = DeviceWrapper::PrepareResourceDescArray1(
				pResourceDescs, numResourceDescs, &adjustedDescs,
				"GetResourceAllocationInfo3");
			D3D12_RESOURCE_ALLOCATION_INFO info = device12->GetResourceAllocationInfo3(
				visibleMask, numResourceDescs, descsToUse, pNumCastableFormats,
				ppCastableFormats, pResourceAllocationInfo1);
			device12->Release();
			return info;
		}

		HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress1(
			const void *pAddress, SIZE_T size, REFIID riid, void **ppvHeap) override
		{
			DX12_CREATE_TIMING_SCOPE(HeapWrapper, Original);
			ID3D12Device13 *device13 = nullptr;
			HRESULT hr = QueryDevice13(&device13);
			if (FAILED(hr))
				return hr;
			hr = device13->OpenExistingHeapFromAddress1(pAddress, size, riid, ppvHeap);
			if (SUCCEEDED(hr) && ppvHeap && *ppvHeap)
				DeviceWrapper::TryWrapHeap(riid, ppvHeap);
			device13->Release();
			return hr;
		}

		HRESULT STDMETHODCALLTYPE CreateRootSignatureFromSubobjectInLibrary(
			UINT nodeMask, const void *pLibraryBlob, SIZE_T blobLengthInBytes,
			LPCWSTR subobjectName, REFIID riid, void **ppvRootSignature) override
		{
			ID3D12Device14 *device14 = nullptr;
			HRESULT hr = QueryDevice14(&device14);
			if (FAILED(hr))
				return hr;
			{
				DX12_CREATE_TIMING_SCOPE(RootSignatureWrapper, Original);
				hr = device14->CreateRootSignatureFromSubobjectInLibrary(
					nodeMask, pLibraryBlob, blobLengthInBytes, subobjectName, riid, ppvRootSignature);
			}
			{
				DX12_CREATE_TIMING_SCOPE(RootSignatureWrapper, Record);
				DX12ResourceTrackerHookRuntime::RecordCreatedRootSignature(
					hr, nodeMask, pLibraryBlob, blobLengthInBytes, ppvRootSignature);
			}
			{
				DX12_CREATE_TIMING_SCOPE(RootSignatureWrapper, Wrap);
				if (SUCCEEDED(hr) && ppvRootSignature && *ppvRootSignature)
					DeviceWrapper::TryWrapRootSignature(riid, ppvRootSignature);
			}
			device14->Release();
			return hr;
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12Device *mDevice = nullptr;
	};



	template <typename TWrapper, typename TObject, typename TInterface>
	bool WrapExisting(TObject *object, TInterface **outInterface)
	{
		if (!object || !outInterface)
			return false;

		// Fast path: reuse an existing wrapper without any creation lock.
		{
			WrapperBase *existing = WrapperBase::FindByIdentity(object);
			ScopedWrapperReference existingReference(existing);
			if (existing) {
				TWrapper *wrapper = dynamic_cast<TWrapper*>(existing);
				if (!wrapper)
					return false;
				*outInterface = static_cast<TInterface*>(wrapper);
				reinterpret_cast<IUnknown*>(*outInterface)->AddRef();
				return true;
			}
		}

		IUnknown *identity = GetIdentity(object);
		std::lock_guard<std::recursive_mutex> creationLock(
			WrapperCreationShardFor(identity));
		if (identity)
			identity->Release();

		// Re-check under the shard before creating.
		WrapperBase *existing = WrapperBase::FindByIdentity(object);
		ScopedWrapperReference existingReference(existing);
		if (existing) {
			TWrapper *wrapper = dynamic_cast<TWrapper*>(existing);
			if (!wrapper)
				return false;
			*outInterface = static_cast<TInterface*>(wrapper);
			reinterpret_cast<IUnknown*>(*outInterface)->AddRef();
			return true;
		}

		*outInterface = static_cast<TInterface*>(new TWrapper(object));
		return true;
	}

	template <typename TWrapper, typename TQueried>
	bool TryWrapAuxiliaryAs(REFIID riid, void **object)
	{
		if (!object || !*object)
			return false;

		IUnknown *originalObject = static_cast<IUnknown*>(*object);
		WrapperBase *existingByPointer = WrapperBase::FindByWrapperPointer(originalObject);
		ScopedWrapperReference existingByPointerReference(existingByPointer);
		if (existingByPointer) {
			TWrapper *existingWrapper = dynamic_cast<TWrapper*>(existingByPointer);
			if (!existingWrapper)
				return false;
			void *existingInterface = existingWrapper->GetAuxiliaryWrapperInterface(riid);
			if (!existingInterface)
				return false;
			IUnknown *previousObject = originalObject;
			*object = existingInterface;
			reinterpret_cast<IUnknown*>(*object)->AddRef();
			previousObject->Release();
			return true;
		}

		TQueried *queried = nullptr;
		if (FAILED(originalObject->QueryInterface(IID_PPV_ARGS(&queried))))
			return false;

		// Serialize find-then-create per identity shard; the lock-free
		// by-pointer path above already handled the common repeat case.
		IUnknown *creationIdentity = GetIdentity(queried);
		std::lock_guard<std::recursive_mutex> creationLock(
			WrapperCreationShardFor(creationIdentity));
		if (creationIdentity)
			creationIdentity->Release();

		WrapperBase *existingByIdentity = WrapperBase::FindByIdentity(queried);

		ScopedWrapperReference existingByIdentityReference(existingByIdentity);
		if (existingByIdentity) {
			TWrapper *existingWrapper = dynamic_cast<TWrapper*>(existingByIdentity);
			if (existingWrapper) {
				void *existingInterface = existingWrapper->GetAuxiliaryWrapperInterface(riid);
				if (existingInterface) {
					*object = existingInterface;
					reinterpret_cast<IUnknown*>(*object)->AddRef();
					queried->Release();
					originalObject->Release();
					return true;
				}
			}
		}

		TWrapper *wrapper = new TWrapper(queried);
		void *wrapperInterface = wrapper->GetAuxiliaryWrapperInterface(riid);
		if (!wrapperInterface) {
			queried->Release();
			wrapper->Release();
			return false;
		}

		*object = wrapperInterface;
		queried->Release();
		originalObject->Release();
		return true;
	}

} // namespace DX12WrapperDetail
