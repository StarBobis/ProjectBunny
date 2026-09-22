// Graphics command list wrapper (draw/dispatch hook forwarding).
// Split from DX12Wrapper.cpp; COM wrapper class definitions (inline).
#pragma once

#include "DX12WrapperCore.h"

namespace DX12WrapperDetail
{

class GraphicsCommandListWrapper final : public ID3D12GraphicsCommandList10, public WrapperBase
	{
	public:
		template <typename Interface>
		class CachedCommandListInterface
		{
		public:
			Interface *pointer = nullptr;
			HRESULT result = E_NOINTERFACE;
			bool queried = false;
		};

		explicit GraphicsCommandListWrapper(ID3D12GraphicsCommandList *commandList)
			: WrapperBase(commandList)
		{
			mCommandList = commandList;
		// Embed RuntimeState in the wrapper for O(1) access, as DX11 does.
		DX12CommandListRuntime::RegisterDirectState(commandList, &mRuntimeState);
		// Cache device pointer once at construction to eliminate per-draw QI.
		commandList->GetDevice(IID_PPV_ARGS(&mRuntimeState.device));
			RegisterWrapperPointer(static_cast<ID3D12Object*>(this));
			RegisterWrapperPointer(static_cast<ID3D12DeviceChild*>(this));
			RegisterWrapperPointer(static_cast<ID3D12CommandList*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList1*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList2*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList3*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList4*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList5*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList6*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList7*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList8*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList9*>(this));
			RegisterWrapperPointer(static_cast<ID3D12GraphicsCommandList10*>(this));

		// Resolve pre-hook original function pointers to bypass vtable hooks.
		// Eliminates double interception: wrapper callbacks call originals
		// directly instead of going through hooked vtable -> second hook cycle.
		// Resolve hot vtable slots in one batch and populate the L2 cache.
		// Only the first call reaches the global registry for a vtable.
		// This removes repeated vtable walks and registry lookups.
		{
			static const UINT kHotSlots[] = {
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetPrimitiveTopology,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstant,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstants,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers,
				DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteIndirect,
			};
			DX12OriginalFunctionRegistry::BulkResolveVtableSlots(
				static_cast<void*>(commandList),
				kHotSlots,
				ARRAYSIZE(kHotSlots),
				DX12CommandListHookState::CommandListOriginals,
				ARRAYSIZE(DX12CommandListHookState::CommandListOriginals),
				&DX12CommandListHookState::VtableBaseCache);
		}

		mOrigDrawInstanced = ResolveOriginalDrawInstanced();
		mOrigDrawIndexedInstanced = ResolveOriginalDrawIndexedInstanced();
		mOrigDispatch = ResolveOriginalDispatch();
		mOrigIASetPrimitiveTopology = ResolveOriginalIASetPrimitiveTopology();
		mOrigSetPipelineState = ResolveOriginalSetPipelineState();
		mOrigResourceBarrier = ResolveOriginalResourceBarrier();
		mOrigSetDescriptorHeaps = ResolveOriginalSetDescriptorHeaps();
		mOrigSetComputeRootSignature = ResolveOriginalSetComputeRootSignature();
		mOrigSetGraphicsRootSignature = ResolveOriginalSetGraphicsRootSignature();
		mOrigSetComputeRootDescriptorTable = ResolveOriginalSetComputeRootDescriptorTable();
		mOrigSetGraphicsRootDescriptorTable = ResolveOriginalSetGraphicsRootDescriptorTable();
		mOrigSetComputeRoot32BitConstant = ResolveOriginalSetComputeRoot32BitConstant();
		mOrigSetGraphicsRoot32BitConstant = ResolveOriginalSetGraphicsRoot32BitConstant();
		mOrigSetComputeRoot32BitConstants = ResolveOriginalSetComputeRoot32BitConstants();
		mOrigSetGraphicsRoot32BitConstants = ResolveOriginalSetGraphicsRoot32BitConstants();
		mOrigSetComputeRootConstantBufferView = ResolveOriginalSetComputeRootConstantBufferView();
		mOrigSetGraphicsRootConstantBufferView = ResolveOriginalSetGraphicsRootConstantBufferView();
		mOrigSetComputeRootShaderResourceView = ResolveOriginalSetComputeRootShaderResourceView();
		mOrigSetGraphicsRootShaderResourceView = ResolveOriginalSetGraphicsRootShaderResourceView();
		mOrigSetComputeRootUnorderedAccessView = ResolveOriginalSetComputeRootUnorderedAccessView();
		mOrigSetGraphicsRootUnorderedAccessView = ResolveOriginalSetGraphicsRootUnorderedAccessView();
		mOrigIASetIndexBuffer = ResolveOriginalIASetIndexBuffer();
		mOrigIASetVertexBuffers = ResolveOriginalIASetVertexBuffers();
		mOrigExecuteIndirect = ResolveOriginalExecuteIndirect();

		DX12_LOG("DX12Wrapper",
			"kind=graphics_command_list,stage=create,wrapper=%p,object=%p",
			this, commandList);
	}

	~GraphicsCommandListWrapper()
	{
		ReleaseCachedCommandListInterface(&mCommandList1Interface);
		ReleaseCachedCommandListInterface(&mCommandList2Interface);
		ReleaseCachedCommandListInterface(&mCommandList3Interface);
		ReleaseCachedCommandListInterface(&mCommandList4Interface);
		ReleaseCachedCommandListInterface(&mCommandList5Interface);
		ReleaseCachedCommandListInterface(&mCommandList6Interface);
		ReleaseCachedCommandListInterface(&mCommandList7Interface);
		ReleaseCachedCommandListInterface(&mCommandList8Interface);
		ReleaseCachedCommandListInterface(&mCommandList9Interface);
		ReleaseCachedCommandListInterface(&mCommandList10Interface);
		if (mCommandList) {
			DX12CommandListLifecycle::Unregister(mCommandList);
			DX12CommandListRuntime::UnregisterDirectState(mCommandList);
		}
		if (mRuntimeState.device) {
			DX12_LOG("DX12Wrapper",
				"kind=graphics_command_list,stage=release_cached_device,wrapper=%p,object=%p,device=%p",
				this, mCommandList, mRuntimeState.device);
			mRuntimeState.device->Release();
			mRuntimeState.device = nullptr;
		}
	}

	DX12CommandListHookSignatures::DrawInstanced ResolveOriginalDrawInstanced()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::DrawInstanced>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawInstanced,
			DX12CommandListHookState::OrigDrawInstanced,
			"DrawInstanced");
	}

	DX12CommandListHookSignatures::DrawIndexedInstanced ResolveOriginalDrawIndexedInstanced()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::DrawIndexedInstanced>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::DrawIndexedInstanced,
			DX12CommandListHookState::OrigDrawIndexedInstanced,
			"DrawIndexedInstanced");
	}

	DX12CommandListHookSignatures::Dispatch ResolveOriginalDispatch()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::Dispatch>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::Dispatch,
			DX12CommandListHookState::OrigDispatch,
			"Dispatch");
	}

	DX12CommandListHookSignatures::IaSetPrimitiveTopology ResolveOriginalIASetPrimitiveTopology()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::IaSetPrimitiveTopology>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetPrimitiveTopology,
			DX12CommandListHookState::OrigIASetPrimitiveTopology,
			"IASetPrimitiveTopology");
	}

	DX12CommandListHookSignatures::SetPipelineState ResolveOriginalSetPipelineState()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetPipelineState>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetPipelineState,
			DX12CommandListHookState::OrigSetPipelineState,
			"SetPipelineState");
	}

	DX12CommandListHookSignatures::ResourceBarrier ResolveOriginalResourceBarrier()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::ResourceBarrier>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ResourceBarrier,
			DX12CommandListHookState::OrigResourceBarrier,
			"ResourceBarrier");
	}

	DX12CommandListHookSignatures::SetDescriptorHeaps ResolveOriginalSetDescriptorHeaps()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetDescriptorHeaps>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetDescriptorHeaps,
			DX12CommandListHookState::OrigSetDescriptorHeaps,
			"SetDescriptorHeaps");
	}

	DX12CommandListHookSignatures::SetRootSignature ResolveOriginalSetComputeRootSignature()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<DX12CommandListHookSignatures::SetRootSignature>(
			mCommandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootSignature,
			DX12CommandListHookState::OrigSetComputeRootSignature, "SetComputeRootSignature");
	}

	DX12CommandListHookSignatures::SetRootSignature ResolveOriginalSetGraphicsRootSignature()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<DX12CommandListHookSignatures::SetRootSignature>(
			mCommandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootSignature,
			DX12CommandListHookState::OrigSetGraphicsRootSignature, "SetGraphicsRootSignature");
	}

	DX12CommandListHookSignatures::SetRootDescriptorTable ResolveOriginalSetComputeRootDescriptorTable()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<DX12CommandListHookSignatures::SetRootDescriptorTable>(
			mCommandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootDescriptorTable,
			DX12CommandListHookState::OrigSetComputeRootDescriptorTable, "SetComputeRootDescriptorTable");
	}

	DX12CommandListHookSignatures::SetRootDescriptorTable ResolveOriginalSetGraphicsRootDescriptorTable()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<DX12CommandListHookSignatures::SetRootDescriptorTable>(
			mCommandList, DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable,
			DX12CommandListHookState::OrigSetGraphicsRootDescriptorTable, "SetGraphicsRootDescriptorTable");
	}

	DX12CommandListHookSignatures::SetRoot32BitConstant ResolveOriginalSetComputeRoot32BitConstant()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRoot32BitConstant>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstant,
			DX12CommandListHookState::OrigSetComputeRoot32BitConstant,
			"SetComputeRoot32BitConstant");
	}

	DX12CommandListHookSignatures::SetRoot32BitConstant ResolveOriginalSetGraphicsRoot32BitConstant()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRoot32BitConstant>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant,
			DX12CommandListHookState::OrigSetGraphicsRoot32BitConstant,
			"SetGraphicsRoot32BitConstant");
	}

	DX12CommandListHookSignatures::SetRoot32BitConstants ResolveOriginalSetComputeRoot32BitConstants()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRoot32BitConstants>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRoot32BitConstants,
			DX12CommandListHookState::OrigSetComputeRoot32BitConstants,
			"SetComputeRoot32BitConstants");
	}

	DX12CommandListHookSignatures::SetRoot32BitConstants ResolveOriginalSetGraphicsRoot32BitConstants()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRoot32BitConstants>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants,
			DX12CommandListHookState::OrigSetGraphicsRoot32BitConstants,
			"SetGraphicsRoot32BitConstants");
	}

	DX12CommandListHookSignatures::SetRootGpuVa ResolveOriginalSetComputeRootConstantBufferView()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRootGpuVa>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootConstantBufferView,
			DX12CommandListHookState::OrigSetComputeRootConstantBufferView,
			"SetComputeRootConstantBufferView");
	}

	DX12CommandListHookSignatures::SetRootGpuVa ResolveOriginalSetGraphicsRootConstantBufferView()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRootGpuVa>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView,
			DX12CommandListHookState::OrigSetGraphicsRootConstantBufferView,
			"SetGraphicsRootConstantBufferView");
	}

	DX12CommandListHookSignatures::SetRootGpuVa ResolveOriginalSetComputeRootShaderResourceView()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRootGpuVa>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootShaderResourceView,
			DX12CommandListHookState::OrigSetComputeRootShaderResourceView,
			"SetComputeRootShaderResourceView");
	}

	DX12CommandListHookSignatures::SetRootGpuVa ResolveOriginalSetGraphicsRootShaderResourceView()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRootGpuVa>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView,
			DX12CommandListHookState::OrigSetGraphicsRootShaderResourceView,
			"SetGraphicsRootShaderResourceView");
	}

	DX12CommandListHookSignatures::SetRootGpuVa ResolveOriginalSetComputeRootUnorderedAccessView()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRootGpuVa>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView,
			DX12CommandListHookState::OrigSetComputeRootUnorderedAccessView,
			"SetComputeRootUnorderedAccessView");
	}

	DX12CommandListHookSignatures::SetRootGpuVa ResolveOriginalSetGraphicsRootUnorderedAccessView()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::SetRootGpuVa>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView,
			DX12CommandListHookState::OrigSetGraphicsRootUnorderedAccessView,
			"SetGraphicsRootUnorderedAccessView");
	}

	DX12CommandListHookSignatures::IaSetIndexBuffer ResolveOriginalIASetIndexBuffer()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::IaSetIndexBuffer>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetIndexBuffer,
			DX12CommandListHookState::OrigIASetIndexBuffer,
			"IASetIndexBuffer");
	}

	DX12CommandListHookSignatures::IaSetVertexBuffers ResolveOriginalIASetVertexBuffers()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::IaSetVertexBuffers>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::IaSetVertexBuffers,
			DX12CommandListHookState::OrigIASetVertexBuffers,
			"IASetVertexBuffers");
	}

	DX12CommandListHookSignatures::ExecuteIndirect ResolveOriginalExecuteIndirect()
	{
		return DX12CommandListHookRegistry::GetCommandListOriginal<
			DX12CommandListHookSignatures::ExecuteIndirect>(
			mCommandList,
			DX12CommandListHookSlotTypes::ID3D12GraphicsCommandList::ExecuteIndirect,
			DX12CommandListHookState::OrigExecuteIndirect,
			"ExecuteIndirect");
	}

		template <typename Interface>
		Interface *GetCachedCommandListInterface(
			CachedCommandListInterface<Interface> *cached)
		{
			// Internal forwarding only: the wrapper owns this cached
			// reference for its whole lifetime, so no AddRef is needed.
			if (!cached->queried) {
				cached->queried = true;
				cached->result = mCommandList->QueryInterface(
					__uuidof(Interface),
					reinterpret_cast<void**>(&cached->pointer));
			}
			if (FAILED(cached->result))
				return nullptr;
			return cached->pointer;
		}

		template <typename Interface>
		static void ReleaseCachedCommandListInterface(
			CachedCommandListInterface<Interface> *cached)
		{
			if (cached->pointer) {
				cached->pointer->Release();
				cached->pointer = nullptr;
			}
		}

		ID3D12GraphicsCommandList1 *CommandList1Raw()
		{
			return GetCachedCommandListInterface(&mCommandList1Interface);
		}

		ID3D12GraphicsCommandList2 *CommandList2Raw()
		{
			return GetCachedCommandListInterface(&mCommandList2Interface);
		}

		ID3D12GraphicsCommandList3 *CommandList3Raw()
		{
			return GetCachedCommandListInterface(&mCommandList3Interface);
		}

		ID3D12GraphicsCommandList4 *CommandList4Raw()
		{
			return GetCachedCommandListInterface(&mCommandList4Interface);
		}

		ID3D12GraphicsCommandList5 *CommandList5Raw()
		{
			return GetCachedCommandListInterface(&mCommandList5Interface);
		}

		ID3D12GraphicsCommandList6 *CommandList6Raw()
		{
			return GetCachedCommandListInterface(&mCommandList6Interface);
		}

		ID3D12GraphicsCommandList7 *CommandList7Raw()
		{
			return GetCachedCommandListInterface(&mCommandList7Interface);
		}

		ID3D12GraphicsCommandList8 *CommandList8Raw()
		{
			return GetCachedCommandListInterface(&mCommandList8Interface);
		}

		ID3D12GraphicsCommandList9 *CommandList9Raw()
		{
			return GetCachedCommandListInterface(&mCommandList9Interface);
		}

		ID3D12GraphicsCommandList10 *CommandList10Raw()
		{
			return GetCachedCommandListInterface(&mCommandList10Interface);
		}

		void *GetGraphicsCommandListWrapperInterface(REFIID riid)
		{
			if (IsEqualIID(riid, __uuidof(IUnknown)) ||
				IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList10)))
				return static_cast<ID3D12GraphicsCommandList10*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList9)))
				return static_cast<ID3D12GraphicsCommandList9*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList8)))
				return static_cast<ID3D12GraphicsCommandList8*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList7)))
				return static_cast<ID3D12GraphicsCommandList7*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList6)))
				return static_cast<ID3D12GraphicsCommandList6*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList5)))
				return static_cast<ID3D12GraphicsCommandList5*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList4)))
				return static_cast<ID3D12GraphicsCommandList4*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList3)))
				return static_cast<ID3D12GraphicsCommandList3*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList2)))
				return static_cast<ID3D12GraphicsCommandList2*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList1)))
				return static_cast<ID3D12GraphicsCommandList1*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12GraphicsCommandList)))
				return static_cast<ID3D12GraphicsCommandList*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12CommandList)))
				return static_cast<ID3D12CommandList*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12DeviceChild)))
				return static_cast<ID3D12DeviceChild*>(this);
			if (IsEqualIID(riid, __uuidof(ID3D12Object)))
				return static_cast<ID3D12Object*>(this);
			return nullptr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
		{
			if (!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;

			if (IsEqualIID(riid, __uuidof(IUnknown)) || IsSupportedWrapperIid(mCommandList, riid, IsD3D12GraphicsCommandListWrapperIid)) {
				*ppvObject = GetGraphicsCommandListWrapperInterface(riid);
				if (*ppvObject) {
					AddRef();
					return S_OK;
				}
			}

			HRESULT hr = mCommandList->QueryInterface(riid, ppvObject);
			if (SUCCEEDED(hr))
				TryWrapKnownD3D12Result(riid, ppvObject);
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
			return mCommandList->GetPrivateData(guid, pDataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override
		{
			return mCommandList->SetPrivateData(guid, DataSize, pData);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override
		{
			return mCommandList->SetPrivateDataInterface(guid, pData);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mCommandList->SetName(Name);
		}

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppvDevice) override
		{
			HRESULT hr = mCommandList->GetDevice(riid, ppvDevice);
			if (SUCCEEDED(hr))
				DX12Wrapper::WrapD3D12DeviceResult(riid, ppvDevice);
			return hr;
		}

		D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType() override
		{
			return mCommandList->GetType();
		}

		HRESULT STDMETHODCALLTYPE Close() override
		{
		// RAII scope: the old END line sat AFTER the return and never ran.
		DX12_PROF_STAGE_SCOPE(CloseCmdList);
			return DX12WrapperRuntimeUtils::CloseCommandList(mCommandList, mOrigClose);
		}

		HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialState) override
		{
		// RAII scope: the old END line sat AFTER the return and never ran.
		DX12_PROF_STAGE_SCOPE(ResetCmdList);
			HRESULT hr = DX12WrapperRuntimeUtils::ResetCommandList(
				mCommandList, &mRuntimeState, pAllocator, pInitialState, mOrigReset);
			return hr;
		}

		void STDMETHODCALLTYPE ClearState(ID3D12PipelineState *pPipelineState) override
		{
		DX12_PROF_STAGE_BEGIN(ClearState);
			mCommandList->ClearState(DX12WrapperRuntimeUtils::UnwrapPipelineState(pPipelineState));
		DX12_PROF_STAGE_END(ClearState);
		}

		void STDMETHODCALLTYPE DrawInstanced(
			UINT VertexCountPerInstance, UINT InstanceCount,
			UINT StartVertexLocation, UINT StartInstanceLocation) override
		{
		// Fast-path: no draw-level work -> call driver directly, bypass hook entirely.
		// MASK_DRAW_WORK==0 implies skipAll==1 (Update() publishes them
		// together), so the single flags read covers both gates.
			const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
			// A pending draw-SRV deferred restore forces the slow path so it
			// flushes before this draw observes descriptor-heap state.
			if (!(drawFlags & (DX12HotPathState::MASK_DRAW_WORK |
					DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE)) &&
				mOrigDrawInstanced) {
			DX12_PROF_API_SCOPE(DrawInstanced);
			mOrigDrawInstanced(mCommandList, VertexCountPerInstance, InstanceCount,
				StartVertexLocation, StartInstanceLocation);
			return;
		}
		// Per-PSO noop fast path: the compiled draw decision proves this PSO
		// can never match any override, so forward without entering the
		// generic draw interpreter (see DX12DrawHookFlow::TryForwardNoopDraw).
		if (mOrigDrawInstanced &&
			DX12DrawHookFlow::TryForwardNoopDraw(
				&mRuntimeState, drawFlags,
				[this, VertexCountPerInstance, InstanceCount,
					StartVertexLocation, StartInstanceLocation]() {
					mOrigDrawInstanced(mCommandList, VertexCountPerInstance,
						InstanceCount, StartVertexLocation, StartInstanceLocation);
				})) {
			return;
		}
		DX12WrapperRuntimeUtils::DrawInstanced(
			mCommandList,
			&mRuntimeState,
			VertexCountPerInstance,
			InstanceCount,
			StartVertexLocation,
			StartInstanceLocation,
			mOrigDrawInstanced,
			drawFlags);
	}

	void STDMETHODCALLTYPE DrawIndexedInstanced(
		UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
		INT BaseVertexLocation, UINT StartInstanceLocation) override
	{
		const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
		if (!(drawFlags & (DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE)) &&
			mOrigDrawIndexedInstanced) {
			DX12_PROF_API_SCOPE(DrawIndexedInstanced);
			mOrigDrawIndexedInstanced(mCommandList, IndexCountPerInstance, InstanceCount,
				StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
			return;
		}
		// Per-PSO noop fast path (see DX12DrawHookFlow::TryForwardNoopDraw).
		if (mOrigDrawIndexedInstanced &&
			DX12DrawHookFlow::TryForwardNoopDraw(
				&mRuntimeState, drawFlags,
				[this, IndexCountPerInstance, InstanceCount,
					StartIndexLocation, BaseVertexLocation, StartInstanceLocation]() {
					mOrigDrawIndexedInstanced(mCommandList, IndexCountPerInstance,
						InstanceCount, StartIndexLocation, BaseVertexLocation,
						StartInstanceLocation);
				})) {
			return;
		}
		DX12WrapperRuntimeUtils::DrawIndexedInstanced(
			mCommandList,
			&mRuntimeState,
			IndexCountPerInstance,
			InstanceCount,
			StartIndexLocation,
			BaseVertexLocation,
			StartInstanceLocation,
			mOrigDrawIndexedInstanced,
			drawFlags);
	}

	void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
	{
		const UINT64 dispatchFlags = DX12HotPathState::HookWorkFlags;
		if ((!(dispatchFlags & (DX12HotPathState::MASK_DISPATCH_WORK |
				DX12HotPathState::FLAG_DRAWSRV_PENDING_RESTORE))) && mOrigDispatch) {
			DX12_PROF_API_SCOPE(Dispatch);
			mOrigDispatch(mCommandList, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
			return;
		}
		DX12WrapperRuntimeUtils::Dispatch(
			mCommandList,
			&mRuntimeState,
			ThreadGroupCountX,
			ThreadGroupCountY,
			ThreadGroupCountZ,
			mOrigDispatch,
			dispatchFlags);
	}

		void STDMETHODCALLTYPE CopyBufferRegion(
			ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
			UINT64 SrcOffset, UINT64 NumBytes) override
		{
			DX12_PROF_API_SCOPE(CopyBufferRegion);
			const UINT64 flags = DX12HotPathState::HookWorkFlags;
			if (DX12HotPathState::NeedsPreSkinProbe(flags))
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			ID3D12Resource *dstResource =
				DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer);
			ID3D12Resource *srcResource =
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer);
			mCommandList->CopyBufferRegion(
				dstResource,
				DstOffset,
				srcResource,
				SrcOffset,
				NumBytes);
			// Async content capture: queue a GPU readback of the
			// destination range instead of propagating CPU shadow content
			// synchronously. The readback executes after this submit, so
			// it always observes the post-copy bytes.
			if (DX12HotPathState::NeedsIaBufferContentTracking(flags)) {
				// Synchronously invalidate the destination's content generation:
				// the IA hash cache key uses the per-resource content generation
				// (not the command-list iaContentEpoch above), and the async
				// readback only refreshes it a frame later. Without this bump a
				// GPU-uploaded vertex/index buffer keeps a stale hash for one
				// frame -> intermittent wrong IA replacement / model flicker.
				DX12InvalidateBufferContent(dstResource);
				DX12AsyncReadbackQueueBufferCapture(
					dstResource, DstOffset, NumBytes);
			}
		}

		void STDMETHODCALLTYPE CopyTextureRegion(
			const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ,
			const D3D12_TEXTURE_COPY_LOCATION *pSrc, const D3D12_BOX *pSrcBox) override
		{
			DX12_PROF_API_SCOPE(CopyTextureRegion);
			// One flags read for both gates, matching CopyBufferRegion and
			// CopyResource: the word is a seq_cst load of a shared cache line.
			const UINT64 copyFlags = DX12HotPathState::HookWorkFlags;
			if (DX12HotPathState::NeedsPreSkinProbe(copyFlags))
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			D3D12_TEXTURE_COPY_LOCATION dstStorage = {};
			D3D12_TEXTURE_COPY_LOCATION srcStorage = {};
			const D3D12_TEXTURE_COPY_LOCATION *activeDst = pDst;
				const D3D12_TEXTURE_COPY_LOCATION *activeSrc = pSrc;
				DX12WrapperRuntimeUtils::UnwrapTextureCopyLocation(pDst, &dstStorage, &activeDst);
				DX12WrapperRuntimeUtils::UnwrapTextureCopyLocation(pSrc, &srcStorage, &activeSrc);
			// DX11-style gating: only maintain the texture hash cache on
			// copies when a consumer (override matching / hunt / capture)
			// can actually read it. The ungated path hashed the full top
			// mip and took a global lock on every streaming copy.
			// Coverage is content-based, not upload-shape based: any write
			// to the top mip marks the texture wanted; the readback pipeline
			// captures it and retries until the hash publishes, so the copy
			// shape (placed-footprint / full-cover / partial) no longer
			// decides whether a texture can ever be matched by content hash.
			if (DX12HotPathState::NeedsTextureHashTracking(copyFlags) &&
				activeDst->Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX &&
				activeDst->SubresourceIndex == 0 &&
				activeDst->pResource) {
				DX12MarkTextureHashWanted(activeDst->pResource);
			}
			mCommandList->CopyTextureRegion(activeDst, DstX, DstY, DstZ, activeSrc, pSrcBox);
			}

		void STDMETHODCALLTYPE CopyResource(ID3D12Resource *pDstResource, ID3D12Resource *pSrcResource) override
		{
		DX12_PROF_API_SCOPE(CopyResource);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsPreSkinProbe(flags))
			DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
		DX12_PROF_STAGE_BEGIN(CopyResourceOp);
			ID3D12Resource *dstResource =
				DX12WrapperRuntimeUtils::UnwrapResource(pDstResource);
			ID3D12Resource *srcResource =
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource);
			mCommandList->CopyResource(
				dstResource,
				srcResource);
			// Async content capture: queue a GPU readback of the whole
			// destination buffer instead of CPU shadow propagation. The
			// readback executes after this submit and observes the
			// post-copy bytes.
			if (DX12HotPathState::NeedsIaBufferContentTracking(flags) &&
				dstResource && srcResource) {
				const D3D12_RESOURCE_DESC dstDesc = dstResource->GetDesc();
				const D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();
				if (dstDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
				    srcDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
					DX12AsyncReadbackQueueBufferCapture(
						dstResource, 0, dstDesc.Width);
				}
			}
		// Texture->texture CopyResource writes the top mip too: mark the
		// destination wanted so whole-resource uploads get content hashes
		// (previously only CopyTextureRegion uploads were covered).
		if (DX12HotPathState::NeedsTextureHashTracking(flags) && dstResource) {
			const D3D12_RESOURCE_DESC dstDesc = dstResource->GetDesc();
			if (dstDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
				DX12MarkTextureHashWanted(dstResource);
		}
		DX12_PROF_STAGE_END(CopyResourceOp);
		}

		void STDMETHODCALLTYPE CopyTiles(
			ID3D12Resource *pTiledResource,
			const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
			const D3D12_TILE_REGION_SIZE *pTileRegionSize, ID3D12Resource *pBuffer,
			UINT64 BufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS Flags) override
		{
			DX12_PROF_API_SCOPE(CopyTiles);
			if (DX12HotPathState::NeedsPreSkinProbe(
					DX12HotPathState::HookWorkFlags))
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			mCommandList->CopyTiles(
				DX12WrapperRuntimeUtils::UnwrapResource(pTiledResource),
				pTileRegionStartCoordinate, pTileRegionSize,
				DX12WrapperRuntimeUtils::UnwrapResource(pBuffer), BufferStartOffsetInBytes, Flags);
		}

		void STDMETHODCALLTYPE ResolveSubresource(
			ID3D12Resource *pDstResource, UINT DstSubresource,
			ID3D12Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override
		{
			DX12_PROF_API_SCOPE(ResolveSubresource);
			if (DX12HotPathState::NeedsPreSkinProbe(
					DX12HotPathState::HookWorkFlags))
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			mCommandList->ResolveSubresource(
				DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), DstSubresource,
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), SrcSubresource, Format);
		}

		void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) override
		{
			DX12_PROF_STAGE_BEGIN(IaSetTopology);
			DX12_PROF_API_SCOPE(IASetPrimitiveTopology);
			if (DX12HotPathState::skipAll) {
				if (mOrigIASetPrimitiveTopology)
					mOrigIASetPrimitiveTopology(mCommandList, PrimitiveTopology);
				else
					mCommandList->IASetPrimitiveTopology(PrimitiveTopology);
			} else {
				const bool changed = DX12CommandListStateFlow::CapturePrimitiveTopology(
					mCommandList, &mRuntimeState, PrimitiveTopology);
				if (changed && mOrigIASetPrimitiveTopology)
					mOrigIASetPrimitiveTopology(mCommandList, PrimitiveTopology);
				else if (changed)
					mCommandList->IASetPrimitiveTopology(PrimitiveTopology);
			}
			DX12_PROF_STAGE_END(IaSetTopology);
		}

		void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT *pViewports) override
		{
		DX12_PROF_STAGE_BEGIN(RSSetViewports);
			mCommandList->RSSetViewports(NumViewports, pViewports);
		DX12_PROF_STAGE_END(RSSetViewports);
		}

		void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D12_RECT *pRects) override
		{
		DX12_PROF_STAGE_BEGIN(RSSetScissorRects);
			mCommandList->RSSetScissorRects(NumRects, pRects);
		DX12_PROF_STAGE_END(RSSetScissorRects);
		}

		void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT BlendFactor[4]) override
		{
		DX12_PROF_STAGE_BEGIN(OMSetBlendFactor);
			mCommandList->OMSetBlendFactor(BlendFactor);
		DX12_PROF_STAGE_END(OMSetBlendFactor);
		}

		void STDMETHODCALLTYPE OMSetStencilRef(UINT StencilRef) override
		{
		DX12_PROF_STAGE_BEGIN(OMSetStencilRef);
			mCommandList->OMSetStencilRef(StencilRef);
		DX12_PROF_STAGE_END(OMSetStencilRef);
		}

		void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState *pPipelineState) override
		{
			if (DX12HotPathState::skipAll && mOrigSetPipelineState) {
				DX12_PROF_API_SCOPE(SetPipelineState);
				// Wrapper mode hands the game wrapped child objects; the
				// skip-all fast path must still unwrap them before the
				// original call, or D3D12Core receives our proxy pointer.
				mOrigSetPipelineState(
					mCommandList,
					DX12WrapperRuntimeUtils::UnwrapPipelineState(pPipelineState));
				return;
			}
			DX12WrapperRuntimeUtils::SetPipelineState(
				mCommandList, &mRuntimeState, pPipelineState, mOrigSetPipelineState);
		}

		void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers) override
		{
			if (DX12HotPathState::skipAll && mOrigResourceBarrier) {
				DX12_PROF_API_SCOPE(ResourceBarrier);
				// Wrapper mode wraps every resource handed to the game,
				// including swap chain back buffers (GetBuffer). Barriers
				// therefore reference wrapped resources, which D3D12Core
				// must never see: unwrap before the original call or it
				// trips ValidateTransitionBarrierDesc / hangs the driver
				// (observed white-screen startup hang in ZenlessZoneZero).
				std::vector<D3D12_RESOURCE_BARRIER> &unwrappedBarriers =
					mRuntimeState.barrierScratch;
				const D3D12_RESOURCE_BARRIER *activeBarriers = pBarriers;
				DX12WrapperRuntimeUtils::UnwrapResourceBarrierArray(
					pBarriers, NumBarriers, &unwrappedBarriers, &activeBarriers);
				mOrigResourceBarrier(mCommandList, NumBarriers, activeBarriers);
				return;
			}
			DX12WrapperRuntimeUtils::ResourceBarrier(mCommandList, &mRuntimeState, NumBarriers, pBarriers, mOrigResourceBarrier);
		}

		void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *pCommandList) override
		{
		DX12_PROF_STAGE_BEGIN(ExecuteBundle);
			ID3D12GraphicsCommandList *bundle =
				DX12WrapperRuntimeUtils::UnwrapGraphicsCommandList(pCommandList);
			// The bundle's tables were recorded against the game's heap:
			// restore it before the bundle observes descriptor-heap state.
			DX12ModFlushDrawSrvReplacement(mCommandList);
			// A bundle recorded with a mirror session needs its session heap
			// bound on this list while it executes (parent replay).
			DX12ModApplyBundleMirrorForExecuteBundle(mCommandList, bundle);
			DX12CommandListCommandRecorder::RecordExecuteBundle(bundle, nullptr);
			mCommandList->ExecuteBundle(bundle);
			DX12ModRestoreBundleMirrorAfterExecuteBundle(mCommandList, bundle);
			DX12CommandListLifecycle::InvalidateAfterExecuteBundle(mCommandList);
		DX12_PROF_STAGE_END(ExecuteBundle);
		}

		void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap *const *ppDescriptorHeaps) override
		{
			DX12_PROF_API_SCOPE(SetDescriptorHeaps);
			// The game re-establishes heap state itself here: drop any
			// deferred draw-SRV restore silently (restoring first would be
			// overwritten by this very call, and the spec invalidates all
			// root table bindings on a heap change either way).
			DX12ModDropDrawSrvReplacementForHeapChange(mCommandList);
			// Four heaps fit on the stack; larger calls use heap storage.
			ID3D12DescriptorHeap *stackUnwrapped[4];
			ID3D12DescriptorHeap *const *activeHeaps = ppDescriptorHeaps;
			std::vector<ID3D12DescriptorHeap*> heapUnwrapped;
			if (ppDescriptorHeaps && NumDescriptorHeaps) {
				ID3D12DescriptorHeap **dest = stackUnwrapped;
				if (NumDescriptorHeaps > 4) {
					heapUnwrapped.resize(NumDescriptorHeaps);
					dest = heapUnwrapped.data();
				}
				for (UINT i = 0; i < NumDescriptorHeaps; ++i)
					dest[i] = DX12WrapperRuntimeUtils::UnwrapDescriptorHeap(ppDescriptorHeaps[i]);
				activeHeaps = dest;
			}
			if (!DX12WrapperRuntimeUtils::ShouldSkipComputeBinding()) {
				DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
					mCommandList, &mRuntimeState, NumDescriptorHeaps, activeHeaps);
			}
			// Bundles with a mirror session must reference the session heap:
			// swap it in for the emitted call (the trackers captured the
			// game's logical heaps above). activeHeaps points to the mutable
			// unwrapped copy whenever a rewrite is possible.
			if (activeHeaps && NumDescriptorHeaps)
				DX12ModRewriteBundleDescriptorHeaps(
					mCommandList, NumDescriptorHeaps,
					const_cast<ID3D12DescriptorHeap **>(activeHeaps));

			if (mOrigSetDescriptorHeaps)
				mOrigSetDescriptorHeaps(
					mCommandList, NumDescriptorHeaps, activeHeaps);
			else
				mCommandList->SetDescriptorHeaps(
					NumDescriptorHeaps, activeHeaps);
		}

		void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *pRootSignature) override
		{
			DX12_PROF_STAGE_BEGIN(RootSignature);
			DX12WrapperRuntimeUtils::SetComputeRootSignature(mCommandList,
				&mRuntimeState, pRootSignature, mOrigSetComputeRootSignature);
			DX12_PROF_STAGE_END(RootSignature);
		}

		void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *pRootSignature) override
		{
			DX12_PROF_STAGE_BEGIN(RootSignature);
			DX12WrapperRuntimeUtils::SetGraphicsRootSignature(mCommandList,
				&mRuntimeState, pRootSignature, mOrigSetGraphicsRootSignature);
			DX12_PROF_STAGE_END(RootSignature);
		}

		void STDMETHODCALLTYPE SetComputeRootDescriptorTable(
			UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
		{
			DX12_PROF_STAGE_BEGIN(RootDescTable);
			DX12WrapperRuntimeUtils::SetComputeRootDescriptorTable(
				mCommandList, &mRuntimeState, RootParameterIndex, BaseDescriptor, mOrigSetComputeRootDescriptorTable);
			DX12_PROF_STAGE_END(RootDescTable);
		}

		void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(
			UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
		{
			DX12_PROF_STAGE_BEGIN(RootDescTable);
			DX12WrapperRuntimeUtils::SetGraphicsRootDescriptorTable(
				mCommandList, &mRuntimeState, RootParameterIndex, BaseDescriptor, mOrigSetGraphicsRootDescriptorTable);
			DX12_PROF_STAGE_END(RootDescTable);
		}

		void STDMETHODCALLTYPE SetComputeRoot32BitConstant(
			UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override
		{
			DX12_PROF_API_SCOPE(SetComputeRootConstants);
			if (!DX12WrapperRuntimeUtils::ShouldSkipComputeBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
					mCommandList, &mRuntimeState,
					RootParameterIndex, SrcData,
					DestOffsetIn32BitValues, false);
			}
			if (mOrigSetComputeRoot32BitConstant)
				mOrigSetComputeRoot32BitConstant(
					mCommandList,
					RootParameterIndex,
					SrcData,
					DestOffsetIn32BitValues);
			else
				mCommandList->SetComputeRoot32BitConstant(
					RootParameterIndex,
					SrcData,
					DestOffsetIn32BitValues);
		}

		void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(
			UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override
		{
			DX12_PROF_API_SCOPE(SetGraphicsRootConstants);
			if (!DX12WrapperRuntimeUtils::ShouldSkipGraphicsBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
					mCommandList, &mRuntimeState,
					RootParameterIndex, SrcData,
					DestOffsetIn32BitValues, true);
			}
			if (mOrigSetGraphicsRoot32BitConstant)
				mOrigSetGraphicsRoot32BitConstant(
					mCommandList,
					RootParameterIndex,
					SrcData,
					DestOffsetIn32BitValues);
			else
				mCommandList->SetGraphicsRoot32BitConstant(
					RootParameterIndex,
					SrcData,
					DestOffsetIn32BitValues);
		}

		void STDMETHODCALLTYPE SetComputeRoot32BitConstants(
			UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues) override
		{
			DX12_PROF_API_SCOPE(SetComputeRootConstants);
			if (!DX12WrapperRuntimeUtils::ShouldSkipComputeBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
					mCommandList, &mRuntimeState,
					RootParameterIndex, Num32BitValuesToSet,
					pSrcData, DestOffsetIn32BitValues, false);
			}
			if (mOrigSetComputeRoot32BitConstants)
				mOrigSetComputeRoot32BitConstants(
					mCommandList,
					RootParameterIndex,
					Num32BitValuesToSet,
					pSrcData,
					DestOffsetIn32BitValues);
			else
				mCommandList->SetComputeRoot32BitConstants(
					RootParameterIndex,
					Num32BitValuesToSet,
					pSrcData,
					DestOffsetIn32BitValues);
		}

		void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(
			UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues) override
		{
			DX12_PROF_API_SCOPE(SetGraphicsRootConstants);
			if (!DX12WrapperRuntimeUtils::ShouldSkipGraphicsBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
					mCommandList, &mRuntimeState,
					RootParameterIndex, Num32BitValuesToSet,
					pSrcData, DestOffsetIn32BitValues, true);
			}
			if (mOrigSetGraphicsRoot32BitConstants)
				mOrigSetGraphicsRoot32BitConstants(
					mCommandList,
					RootParameterIndex,
					Num32BitValuesToSet,
					pSrcData,
					DestOffsetIn32BitValues);
			else
				mCommandList->SetGraphicsRoot32BitConstants(
					RootParameterIndex,
					Num32BitValuesToSet,
					pSrcData,
					DestOffsetIn32BitValues);
		}

		void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
		{
		DX12_PROF_STAGE_BEGIN(SetComputeCBV);
			if (!DX12WrapperRuntimeUtils::ShouldSkipComputeBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
					mCommandList, &mRuntimeState,
					RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV,
					BufferLocation, false,
					"ID3D12GraphicsCommandList::SetComputeRootConstantBufferView");
			}
			if (mOrigSetComputeRootConstantBufferView)
				mOrigSetComputeRootConstantBufferView(
					mCommandList, RootParameterIndex, BufferLocation);
			else
				mCommandList->SetComputeRootConstantBufferView(
					RootParameterIndex, BufferLocation);
		DX12_PROF_STAGE_END(SetComputeCBV);
		}

		void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
		{
		DX12_PROF_STAGE_BEGIN(SetGraphicsCBV);
			if (!DX12WrapperRuntimeUtils::ShouldSkipGraphicsBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
					mCommandList, &mRuntimeState,
					RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV,
					BufferLocation, true,
					"ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView");
			}
			if (mOrigSetGraphicsRootConstantBufferView)
				mOrigSetGraphicsRootConstantBufferView(
					mCommandList, RootParameterIndex, BufferLocation);
			else
				mCommandList->SetGraphicsRootConstantBufferView(
					RootParameterIndex, BufferLocation);
		DX12_PROF_STAGE_END(SetGraphicsCBV);
		}

		void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
		{
		DX12_PROF_STAGE_BEGIN(SetComputeSRV);
			if (!DX12WrapperRuntimeUtils::ShouldSkipComputeBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
					mCommandList, &mRuntimeState,
					RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV,
					BufferLocation, false,
					"ID3D12GraphicsCommandList::SetComputeRootShaderResourceView");
			}
			if (mOrigSetComputeRootShaderResourceView)
				mOrigSetComputeRootShaderResourceView(
					mCommandList, RootParameterIndex, BufferLocation);
			else
				mCommandList->SetComputeRootShaderResourceView(
					RootParameterIndex, BufferLocation);
		DX12_PROF_STAGE_END(SetComputeSRV);
		}

		void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
		{
		DX12_PROF_STAGE_BEGIN(SetGraphicsSRV);
			if (!DX12WrapperRuntimeUtils::ShouldSkipGraphicsBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
					mCommandList, &mRuntimeState,
					RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV,
					BufferLocation, true,
					"ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView");
			}
			if (mOrigSetGraphicsRootShaderResourceView)
				mOrigSetGraphicsRootShaderResourceView(
					mCommandList, RootParameterIndex, BufferLocation);
			else
				mCommandList->SetGraphicsRootShaderResourceView(
					RootParameterIndex, BufferLocation);
		DX12_PROF_STAGE_END(SetGraphicsSRV);
		}

		void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
		{
		DX12_PROF_STAGE_BEGIN(SetComputeUAV);
			if (!DX12WrapperRuntimeUtils::ShouldSkipComputeBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
					mCommandList, &mRuntimeState,
					RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV,
					BufferLocation, false,
					"ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView");
			}
			if (mOrigSetComputeRootUnorderedAccessView)
				mOrigSetComputeRootUnorderedAccessView(
					mCommandList, RootParameterIndex, BufferLocation);
			else
				mCommandList->SetComputeRootUnorderedAccessView(
					RootParameterIndex, BufferLocation);
		DX12_PROF_STAGE_END(SetComputeUAV);
		}

		void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
		{
		DX12_PROF_STAGE_BEGIN(SetGraphicsUAV);
			if (!DX12WrapperRuntimeUtils::ShouldSkipGraphicsBinding()) {
				DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
					mCommandList, &mRuntimeState,
					RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV,
					BufferLocation, true,
					"ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView");
			}
			if (mOrigSetGraphicsRootUnorderedAccessView)
				mOrigSetGraphicsRootUnorderedAccessView(
					mCommandList, RootParameterIndex, BufferLocation);
			else
				mCommandList->SetGraphicsRootUnorderedAccessView(
					RootParameterIndex, BufferLocation);
		DX12_PROF_STAGE_END(SetGraphicsUAV);
		}

		void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *pView) override
		{
			if (DX12HotPathState::skipAll && mOrigIASetIndexBuffer) {
				DX12_PROF_API_SCOPE(IASetIndexBuffer);
				mOrigIASetIndexBuffer(mCommandList, pView);
				return;
			}
		DX12_PROF_STAGE_BEGIN(IASetIndexBuf);
			DX12WrapperRuntimeUtils::IASetIndexBuffer(
				mCommandList, &mRuntimeState, pView, mOrigIASetIndexBuffer);
		DX12_PROF_STAGE_END(IASetIndexBuf);
		}

		void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews) override
		{
			if (DX12HotPathState::skipAll && mOrigIASetVertexBuffers) {
				DX12_PROF_API_SCOPE(IASetVertexBuffers);
				mOrigIASetVertexBuffers(mCommandList, StartSlot, NumViews, pViews);
				return;
			}
		DX12_PROF_STAGE_BEGIN(IASetVertexBuf);
			DX12WrapperRuntimeUtils::IASetVertexBuffers(
				mCommandList, &mRuntimeState, StartSlot, NumViews, pViews, mOrigIASetVertexBuffers);
		DX12_PROF_STAGE_END(IASetVertexBuf);
		}

		void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) override
		{
		DX12_PROF_STAGE_BEGIN(SOSetTargets);
			mCommandList->SOSetTargets(StartSlot, NumViews, pViews);
		DX12_PROF_STAGE_END(SOSetTargets);
		}

		void STDMETHODCALLTYPE OMSetRenderTargets(
			UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
			BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) override
		{
			// Plain forward. This override used to shadow the bound render
			// targets, viewports and scissor rects into DX12SimulateContext;
			// nothing ever read that shadow (the custom element replay rebinds
			// the back buffer directly in DX12CommandListExecutor), so it only
			// cost a std::min, an ID3D12Device::GetDescriptorHandleIncrementSize
			// COM call on the single-handle-range path, and up to eight stores
			// on every call.
			mCommandList->OMSetRenderTargets(
				NumRenderTargetDescriptors, pRenderTargetDescriptors,
				RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
		}

		void STDMETHODCALLTYPE ClearDepthStencilView(
			D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags,
			FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT *pRects) override
		{
			mCommandList->ClearDepthStencilView(
				DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects);
		}

		void STDMETHODCALLTYPE ClearRenderTargetView(
			D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4],
			UINT NumRects, const D3D12_RECT *pRects) override
		{
			mCommandList->ClearRenderTargetView(RenderTargetView, ColorRGBA, NumRects, pRects);
		}

		void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(
			D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
			D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource,
			const UINT Values[4], UINT NumRects, const D3D12_RECT *pRects) override
		{
			DX12_PROF_API_SCOPE(ClearUnorderedAccessView);
			// The GPU handle resolves against the currently bound heap:
			// flush a deferred draw-SRV restore first.
			DX12ModFlushDrawSrvReplacement(mCommandList);
			if (DX12HotPathState::NeedsPreSkinProbe(
					DX12HotPathState::HookWorkFlags))
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			mCommandList->ClearUnorderedAccessViewUint(
				ViewGPUHandleInCurrentHeap, ViewCPUHandle,
				DX12WrapperRuntimeUtils::UnwrapResource(pResource), Values, NumRects, pRects);
		}

		void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(
			D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
			D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource,
			const FLOAT Values[4], UINT NumRects, const D3D12_RECT *pRects) override
		{
			DX12_PROF_API_SCOPE(ClearUnorderedAccessView);
			// The GPU handle resolves against the currently bound heap:
			// flush a deferred draw-SRV restore first.
			DX12ModFlushDrawSrvReplacement(mCommandList);
			if (DX12HotPathState::NeedsPreSkinProbe(
					DX12HotPathState::HookWorkFlags))
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			mCommandList->ClearUnorderedAccessViewFloat(
				ViewGPUHandleInCurrentHeap, ViewCPUHandle,
				DX12WrapperRuntimeUtils::UnwrapResource(pResource), Values, NumRects, pRects);
		}

		void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *pResource, const D3D12_DISCARD_REGION *pRegion) override
		{
		DX12_PROF_STAGE_BEGIN(DiscardResource);
			mCommandList->DiscardResource(DX12WrapperRuntimeUtils::UnwrapResource(pResource), pRegion);
		DX12_PROF_STAGE_END(DiscardResource);
		}

		void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override
		{
		DX12_PROF_STAGE_BEGIN(BeginQuery);
			mCommandList->BeginQuery(DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, Index);
		DX12_PROF_STAGE_END(BeginQuery);
		}

		void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override
		{
		DX12_PROF_STAGE_BEGIN(EndQuery);
			mCommandList->EndQuery(DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, Index);
		DX12_PROF_STAGE_END(EndQuery);
		}

		void STDMETHODCALLTYPE ResolveQueryData(
			ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex,
			UINT NumQueries, ID3D12Resource *pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) override
		{
			mCommandList->ResolveQueryData(
				DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, StartIndex, NumQueries,
				DX12WrapperRuntimeUtils::UnwrapResource(pDestinationBuffer), AlignedDestinationBufferOffset);
		}

		void STDMETHODCALLTYPE SetPredication(
			ID3D12Resource *pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) override
		{
			mCommandList->SetPredication(
				DX12WrapperRuntimeUtils::UnwrapResource(pBuffer), AlignedBufferOffset, Operation);
		}

		void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void *pData, UINT Size) override
		{
		DX12_PROF_STAGE_BEGIN(SetMarker);
			mCommandList->SetMarker(Metadata, pData, Size);
		DX12_PROF_STAGE_END(SetMarker);
		}

		void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *pData, UINT Size) override
		{
		DX12_PROF_STAGE_BEGIN(BeginEvent);
			mCommandList->BeginEvent(Metadata, pData, Size);
		DX12_PROF_STAGE_END(BeginEvent);
		}

		void STDMETHODCALLTYPE EndEvent() override
		{
		DX12_PROF_STAGE_BEGIN(EndEvent);
			mCommandList->EndEvent();
		DX12_PROF_STAGE_END(EndEvent);
		}

		void STDMETHODCALLTYPE ExecuteIndirect(
			ID3D12CommandSignature *pCommandSignature, UINT MaxCommandCount,
			ID3D12Resource *pArgumentBuffer, UINT64 ArgumentBufferOffset,
			ID3D12Resource *pCountBuffer, UINT64 CountBufferOffset) override
		{
			DX12WrapperRuntimeUtils::ExecuteIndirect(
				mCommandList,
				pCommandSignature,
				MaxCommandCount,
				pArgumentBuffer,
				ArgumentBufferOffset,
				pCountBuffer,
				CountBufferOffset,
				mOrigExecuteIndirect);
		}

		void STDMETHODCALLTYPE AtomicCopyBufferUINT(
			ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
			UINT64 SrcOffset, UINT Dependencies, ID3D12Resource *const *ppDependentResources,
			const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges) override
		{
			ID3D12GraphicsCommandList1 *commandList1 = CommandList1Raw();
			if (!commandList1)
				return;
			std::vector<ID3D12Resource*> dependentResources;
			ID3D12Resource *const *activeDependentResources = ppDependentResources;
			DX12WrapperRuntimeUtils::UnwrapResourceArray(
				ppDependentResources, Dependencies, &dependentResources, &activeDependentResources);
			commandList1->AtomicCopyBufferUINT(
				DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset,
				Dependencies, activeDependentResources, pDependentSubresourceRanges);
		}

		void STDMETHODCALLTYPE AtomicCopyBufferUINT64(
			ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
			UINT64 SrcOffset, UINT Dependencies, ID3D12Resource *const *ppDependentResources,
			const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges) override
		{
			ID3D12GraphicsCommandList1 *commandList1 = CommandList1Raw();
			if (!commandList1)
				return;
			std::vector<ID3D12Resource*> dependentResources;
			ID3D12Resource *const *activeDependentResources = ppDependentResources;
			DX12WrapperRuntimeUtils::UnwrapResourceArray(
				ppDependentResources, Dependencies, &dependentResources, &activeDependentResources);
			commandList1->AtomicCopyBufferUINT64(
				DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset,
				Dependencies, activeDependentResources, pDependentSubresourceRanges);
		}

		void STDMETHODCALLTYPE OMSetDepthBounds(FLOAT Min, FLOAT Max) override
		{
		DX12_PROF_STAGE_BEGIN(OMSetDepthBounds);
			ID3D12GraphicsCommandList1 *commandList1 = CommandList1Raw();
			if (commandList1) {
				commandList1->OMSetDepthBounds(Min, Max);
			}
		DX12_PROF_STAGE_END(OMSetDepthBounds);
		}

		void STDMETHODCALLTYPE SetSamplePositions(
			UINT NumSamplesPerPixel, UINT NumPixels, D3D12_SAMPLE_POSITION *pSamplePositions) override
		{
			ID3D12GraphicsCommandList1 *commandList1 = CommandList1Raw();
			if (commandList1) {
				commandList1->SetSamplePositions(NumSamplesPerPixel, NumPixels, pSamplePositions);
			}
		}

		void STDMETHODCALLTYPE ResolveSubresourceRegion(
			ID3D12Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
			ID3D12Resource *pSrcResource, UINT SrcSubresource, RECT *pSrcRect,
			DXGI_FORMAT Format, D3D12_RESOLVE_MODE ResolveMode) override
		{
			ID3D12GraphicsCommandList1 *commandList1 = CommandList1Raw();
			if (!commandList1)
				return;
			commandList1->ResolveSubresourceRegion(
				DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), DstSubresource, DstX, DstY,
				DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), SrcSubresource,
				pSrcRect, Format, ResolveMode);
		}

		void STDMETHODCALLTYPE SetViewInstanceMask(UINT Mask) override
		{
		DX12_PROF_STAGE_BEGIN(SetViewInstanceMask);
			ID3D12GraphicsCommandList1 *commandList1 = CommandList1Raw();
			if (commandList1) {
				commandList1->SetViewInstanceMask(Mask);
			}
		DX12_PROF_STAGE_END(SetViewInstanceMask);
		}

		void STDMETHODCALLTYPE WriteBufferImmediate(
			UINT Count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *pParams,
			const D3D12_WRITEBUFFERIMMEDIATE_MODE *pModes) override
		{
			ID3D12GraphicsCommandList2 *commandList2 = CommandList2Raw();
			if (commandList2) {
				commandList2->WriteBufferImmediate(Count, pParams, pModes);
			}
		}

		void STDMETHODCALLTYPE SetProtectedResourceSession(ID3D12ProtectedResourceSession *pProtectedResourceSession) override
		{
		DX12_PROF_STAGE_BEGIN(SetProtectedSession);
			ID3D12GraphicsCommandList3 *commandList3 = CommandList3Raw();
			if (commandList3) {
				commandList3->SetProtectedResourceSession(
					DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedResourceSession));
			}
		DX12_PROF_STAGE_END(SetProtectedSession);
		}

		void STDMETHODCALLTYPE BeginRenderPass(
			UINT NumRenderTargets, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *pRenderTargets,
			const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *pDepthStencil, D3D12_RENDER_PASS_FLAGS Flags) override
		{
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->BeginRenderPass(NumRenderTargets, pRenderTargets, pDepthStencil, Flags);
			}
		}

		void STDMETHODCALLTYPE EndRenderPass() override
		{
		DX12_PROF_STAGE_BEGIN(EndRenderPass);
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->EndRenderPass();
			}
		DX12_PROF_STAGE_END(EndRenderPass);
		}

		void STDMETHODCALLTYPE InitializeMetaCommand(
			ID3D12MetaCommand *pMetaCommand, const void *pInitializationParametersData,
			SIZE_T InitializationParametersDataSizeInBytes) override
		{
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->InitializeMetaCommand(
					DX12WrapperRuntimeUtils::UnwrapMetaCommand(pMetaCommand),
					pInitializationParametersData, InitializationParametersDataSizeInBytes);
			}
		}

		void STDMETHODCALLTYPE ExecuteMetaCommand(
			ID3D12MetaCommand *pMetaCommand, const void *pExecutionParametersData,
			SIZE_T ExecutionParametersDataSizeInBytes) override
		{
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->ExecuteMetaCommand(
					DX12WrapperRuntimeUtils::UnwrapMetaCommand(pMetaCommand),
					pExecutionParametersData, ExecutionParametersDataSizeInBytes);
			}
		}

		void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(
			const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *pDesc,
			UINT NumPostbuildInfoDescs, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pPostbuildInfoDescs) override
		{
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->BuildRaytracingAccelerationStructure(pDesc, NumPostbuildInfoDescs, pPostbuildInfoDescs);
			}
		}

		void STDMETHODCALLTYPE EmitRaytracingAccelerationStructurePostbuildInfo(
			const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pDesc,
			UINT NumSourceAccelerationStructures, const D3D12_GPU_VIRTUAL_ADDRESS *pSourceAccelerationStructureData) override
		{
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->EmitRaytracingAccelerationStructurePostbuildInfo(
					pDesc, NumSourceAccelerationStructures, pSourceAccelerationStructureData);
			}
		}

		void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(
			D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData,
			D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData,
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode) override
		{
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->CopyRaytracingAccelerationStructure(
					DestAccelerationStructureData, SourceAccelerationStructureData, Mode);
			}
		}

		void STDMETHODCALLTYPE SetPipelineState1(ID3D12StateObject *pStateObject) override
		{
		DX12_PROF_STAGE_BEGIN(SetPSO1);
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->SetPipelineState1(DX12WrapperRuntimeUtils::UnwrapStateObject(pStateObject));
			}
		DX12_PROF_STAGE_END(SetPSO1);
		}

		void STDMETHODCALLTYPE DispatchRays(const D3D12_DISPATCH_RAYS_DESC *pDesc) override
		{
		DX12_PROF_STAGE_BEGIN(DispatchRays);
			ID3D12GraphicsCommandList4 *commandList4 = CommandList4Raw();
			if (commandList4) {
				commandList4->DispatchRays(pDesc);
			}
		DX12_PROF_STAGE_END(DispatchRays);
		}

		void STDMETHODCALLTYPE RSSetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER *combiners) override
		{
		DX12_PROF_STAGE_BEGIN(RSSetShadingRate);
			ID3D12GraphicsCommandList5 *commandList5 = CommandList5Raw();
			if (commandList5) {
				commandList5->RSSetShadingRate(baseShadingRate, combiners);
			}
		DX12_PROF_STAGE_END(RSSetShadingRate);
		}

		void STDMETHODCALLTYPE RSSetShadingRateImage(ID3D12Resource *pShadingRateImage) override
		{
		DX12_PROF_STAGE_BEGIN(RSSetShadingRateImg);
			ID3D12GraphicsCommandList5 *commandList5 = CommandList5Raw();
			if (commandList5) {
				commandList5->RSSetShadingRateImage(DX12WrapperRuntimeUtils::UnwrapResource(pShadingRateImage));
			}
		DX12_PROF_STAGE_END(RSSetShadingRateImg);
		}

		void STDMETHODCALLTYPE DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
		{
		DX12_PROF_STAGE_BEGIN(DispatchMesh);
			ID3D12GraphicsCommandList6 *commandList6 = CommandList6Raw();
			if (commandList6) {
				commandList6->DispatchMesh(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
			}
		DX12_PROF_STAGE_END(DispatchMesh);
		}

		void STDMETHODCALLTYPE Barrier(
			UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP *pBarrierGroups) override
		{
			ID3D12GraphicsCommandList7 *commandList7 = CommandList7Raw();
			if (!commandList7)
				return;
			std::vector<D3D12_BARRIER_GROUP> groupStorage;
			std::vector<std::vector<D3D12_TEXTURE_BARRIER>> textureStorage;
			std::vector<std::vector<D3D12_BUFFER_BARRIER>> bufferStorage;
			const D3D12_BARRIER_GROUP *activeBarrierGroups = pBarrierGroups;
			DX12WrapperRuntimeUtils::UnwrapBarrierGroups(
				pBarrierGroups,
				NumBarrierGroups,
				&groupStorage,
				&textureStorage,
				&bufferStorage,
				&activeBarrierGroups);
			commandList7->Barrier(NumBarrierGroups, activeBarrierGroups);
		}

		void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT FrontStencilRef, UINT BackStencilRef) override
		{
		DX12_PROF_STAGE_BEGIN(OMSetFrontBackStencil);
			ID3D12GraphicsCommandList8 *commandList8 = CommandList8Raw();
			if (commandList8) {
				commandList8->OMSetFrontAndBackStencilRef(FrontStencilRef, BackStencilRef);
			}
		DX12_PROF_STAGE_END(OMSetFrontBackStencil);
		}

		void STDMETHODCALLTYPE RSSetDepthBias(FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias) override
		{
		DX12_PROF_STAGE_BEGIN(RSSetDepthBias);
			ID3D12GraphicsCommandList9 *commandList9 = CommandList9Raw();
			if (commandList9) {
				commandList9->RSSetDepthBias(DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
			}
		DX12_PROF_STAGE_END(RSSetDepthBias);
		}

		void STDMETHODCALLTYPE IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue) override
		{
		DX12_PROF_STAGE_BEGIN(IASetIBStripCut);
			ID3D12GraphicsCommandList9 *commandList9 = CommandList9Raw();
			if (commandList9) {
				commandList9->IASetIndexBufferStripCutValue(IBStripCutValue);
			}
		DX12_PROF_STAGE_END(IASetIBStripCut);
		}

		void STDMETHODCALLTYPE SetProgram(const D3D12_SET_PROGRAM_DESC *pDesc) override
		{
		DX12_PROF_STAGE_BEGIN(SetProgram);
			ID3D12GraphicsCommandList10 *commandList10 = CommandList10Raw();
			if (commandList10) {
				commandList10->SetProgram(pDesc);
			}
		DX12_PROF_STAGE_END(SetProgram);
		}

		void STDMETHODCALLTYPE DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC *pDesc) override
		{
		DX12_PROF_STAGE_BEGIN(DispatchGraph);
			ID3D12GraphicsCommandList10 *commandList10 = CommandList10Raw();
			if (commandList10) {
				commandList10->DispatchGraph(pDesc);
			}
		DX12_PROF_STAGE_END(DispatchGraph);
		}

	public:
		std::atomic<long> mRefCount{1};
		ID3D12GraphicsCommandList *mCommandList = nullptr;
		DX12CommandListHookSignatures::CloseCommandList mOrigClose = nullptr;
		DX12CommandListHookSignatures::ResetCommandList mOrigReset = nullptr;
		DX12CommandListHookSignatures::DrawInstanced mOrigDrawInstanced = nullptr;
		DX12CommandListHookSignatures::DrawIndexedInstanced mOrigDrawIndexedInstanced = nullptr;
		DX12CommandListHookSignatures::Dispatch mOrigDispatch = nullptr;
		DX12CommandListHookSignatures::IaSetPrimitiveTopology mOrigIASetPrimitiveTopology = nullptr;
		DX12CommandListHookSignatures::SetPipelineState mOrigSetPipelineState = nullptr;
		DX12CommandListHookSignatures::ResourceBarrier mOrigResourceBarrier = nullptr;
		DX12CommandListHookSignatures::SetDescriptorHeaps mOrigSetDescriptorHeaps = nullptr;
		DX12CommandListHookSignatures::SetRootSignature mOrigSetComputeRootSignature = nullptr;
		DX12CommandListHookSignatures::SetRootSignature mOrigSetGraphicsRootSignature = nullptr;
		DX12CommandListHookSignatures::SetRootDescriptorTable mOrigSetComputeRootDescriptorTable = nullptr;
		DX12CommandListHookSignatures::SetRootDescriptorTable mOrigSetGraphicsRootDescriptorTable = nullptr;
		DX12CommandListHookSignatures::SetRoot32BitConstant mOrigSetComputeRoot32BitConstant = nullptr;
		DX12CommandListHookSignatures::SetRoot32BitConstant mOrigSetGraphicsRoot32BitConstant = nullptr;
		DX12CommandListHookSignatures::SetRoot32BitConstants mOrigSetComputeRoot32BitConstants = nullptr;
		DX12CommandListHookSignatures::SetRoot32BitConstants mOrigSetGraphicsRoot32BitConstants = nullptr;
		DX12CommandListHookSignatures::SetRootGpuVa mOrigSetComputeRootConstantBufferView = nullptr;
		DX12CommandListHookSignatures::SetRootGpuVa mOrigSetGraphicsRootConstantBufferView = nullptr;
		DX12CommandListHookSignatures::SetRootGpuVa mOrigSetComputeRootShaderResourceView = nullptr;
		DX12CommandListHookSignatures::SetRootGpuVa mOrigSetGraphicsRootShaderResourceView = nullptr;
		DX12CommandListHookSignatures::SetRootGpuVa mOrigSetComputeRootUnorderedAccessView = nullptr;
		DX12CommandListHookSignatures::SetRootGpuVa mOrigSetGraphicsRootUnorderedAccessView = nullptr;
		DX12CommandListHookSignatures::IaSetIndexBuffer mOrigIASetIndexBuffer = nullptr;
		DX12CommandListHookSignatures::IaSetVertexBuffers mOrigIASetVertexBuffers = nullptr;
		DX12CommandListHookSignatures::ExecuteIndirect mOrigExecuteIndirect = nullptr;
		CachedCommandListInterface<ID3D12GraphicsCommandList1> mCommandList1Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList2> mCommandList2Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList3> mCommandList3Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList4> mCommandList4Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList5> mCommandList5Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList6> mCommandList6Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList7> mCommandList7Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList8> mCommandList8Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList9> mCommandList9Interface;
		CachedCommandListInterface<ID3D12GraphicsCommandList10> mCommandList10Interface;
		// Embedded RuntimeState avoids a sharded map lookup, as in DX11.
		DX12SimulateContext mRuntimeState;
	};


} // namespace DX12WrapperDetail
