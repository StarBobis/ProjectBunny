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

			DX12LogManager::Fields("DX12Wrapper",
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
				DX12LogManager::Fields("DX12Wrapper",
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
			HRESULT QueryCachedCommandListInterface(
				CachedCommandListInterface<Interface> *cached,
				Interface **commandList)
			{
				if (!commandList)
					return E_POINTER;
				*commandList = nullptr;
				// COM requires an object's supported interface set to remain stable.
				// Keep one cached reference and preserve the caller's AddRef contract.
				if (!cached->queried) {
					cached->queried = true;
					cached->result = mCommandList->QueryInterface(
						__uuidof(Interface),
						reinterpret_cast<void**>(&cached->pointer));
				}
				if (FAILED(cached->result))
					return cached->result;
				cached->pointer->AddRef();
				*commandList = cached->pointer;
				return S_OK;
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

			HRESULT QueryCommandList1(ID3D12GraphicsCommandList1 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList1Interface, commandList);
			}

			HRESULT QueryCommandList2(ID3D12GraphicsCommandList2 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList2Interface, commandList);
			}

			HRESULT QueryCommandList3(ID3D12GraphicsCommandList3 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList3Interface, commandList);
			}

			HRESULT QueryCommandList4(ID3D12GraphicsCommandList4 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList4Interface, commandList);
			}

			HRESULT QueryCommandList5(ID3D12GraphicsCommandList5 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList5Interface, commandList);
			}

			HRESULT QueryCommandList6(ID3D12GraphicsCommandList6 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList6Interface, commandList);
			}

			HRESULT QueryCommandList7(ID3D12GraphicsCommandList7 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList7Interface, commandList);
			}

			HRESULT QueryCommandList8(ID3D12GraphicsCommandList8 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList8Interface, commandList);
			}

			HRESULT QueryCommandList9(ID3D12GraphicsCommandList9 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList9Interface, commandList);
			}

			HRESULT QueryCommandList10(ID3D12GraphicsCommandList10 **commandList)
			{
				return QueryCachedCommandListInterface(&mCommandList10Interface, commandList);
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
			DX12_HOTPATH_TIME_BEGIN(CloseCmdListTotal);
				return DX12WrapperRuntimeUtils::CloseCommandList(mCommandList, mOrigClose);
			DX12_HOTPATH_TIME_END(CloseCmdListTotal);
			}

			HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialState) override
			{
			DX12_HOTPATH_TIME_BEGIN(ResetCmdListTotal);
				HRESULT hr = DX12WrapperRuntimeUtils::ResetCommandList(
					mCommandList, &mRuntimeState, pAllocator, pInitialState, mOrigReset);
				return hr;
			DX12_HOTPATH_TIME_END(ResetCmdListTotal);
			}

			void STDMETHODCALLTYPE ClearState(ID3D12PipelineState *pPipelineState) override
			{
			DX12_HOTPATH_TIME_BEGIN(ClearStateTotal);
				mCommandList->ClearState(DX12WrapperRuntimeUtils::UnwrapPipelineState(pPipelineState));
			DX12_HOTPATH_TIME_END(ClearStateTotal);
			}

			void STDMETHODCALLTYPE DrawInstanced(
				UINT VertexCountPerInstance, UINT InstanceCount,
				UINT StartVertexLocation, UINT StartInstanceLocation) override
			{
			// Fast-path: no draw-level work -> call driver directly, bypass hook entirely.
				const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
				const bool residentDrawReject =
					DX12HotPathState::CanResidentDrawFastReject(
						drawFlags, &mRuntimeState);
				if ((DX12HotPathState::skipAll || !(drawFlags & DX12HotPathState::MASK_DRAW_WORK) ||
					residentDrawReject) && mOrigDrawInstanced) {
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiDrawInstanced);
				mOrigDrawInstanced(mCommandList, VertexCountPerInstance, InstanceCount,
					StartVertexLocation, StartInstanceLocation);
				return;
			}
			DX12WrapperRuntimeUtils::DrawInstanced(
				mCommandList,
				&mRuntimeState,
				VertexCountPerInstance,
				InstanceCount,
				StartVertexLocation,
				StartInstanceLocation,
				mOrigDrawInstanced);
		}

		void STDMETHODCALLTYPE DrawIndexedInstanced(
			UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
			INT BaseVertexLocation, UINT StartInstanceLocation) override
		{
			const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
			const bool residentDrawReject =
				DX12HotPathState::CanResidentDrawFastReject(
					drawFlags, &mRuntimeState);
			if ((DX12HotPathState::skipAll || !(drawFlags & DX12HotPathState::MASK_DRAW_WORK) ||
				residentDrawReject) && mOrigDrawIndexedInstanced) {
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiDrawIndexedInstanced);
				mOrigDrawIndexedInstanced(mCommandList, IndexCountPerInstance, InstanceCount,
					StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
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
				mOrigDrawIndexedInstanced);
		}

		void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
		{
			const UINT64 dispatchFlags = DX12HotPathState::HookWorkFlags;
			const bool residentDispatchReject =
				(dispatchFlags & DX12HotPathState::MASK_DISPATCH_WORK) &&
				DX12HotPathState::CanResidentDispatchFastReject(
					dispatchFlags, &mRuntimeState);
			if ((!(dispatchFlags & DX12HotPathState::MASK_DISPATCH_WORK) ||
				residentDispatchReject) && mOrigDispatch) {
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiDispatch);
				mOrigDispatch(mCommandList, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
				return;
			}
			DX12WrapperRuntimeUtils::Dispatch(
				mCommandList,
				&mRuntimeState,
				ThreadGroupCountX,
				ThreadGroupCountY,
				ThreadGroupCountZ,
				mOrigDispatch);
		}

			void STDMETHODCALLTYPE CopyBufferRegion(
				ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
				UINT64 SrcOffset, UINT64 NumBytes) override
			{
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiCopyBufferRegion);
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
				if (!DX12CopyBufferContentRange(
						dstResource,
						DstOffset,
						srcResource,
						SrcOffset,
						NumBytes)) {
					DX12InvalidateBufferContent(dstResource);
				}
			}

			void STDMETHODCALLTYPE CopyTextureRegion(
				const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ,
				const D3D12_TEXTURE_COPY_LOCATION *pSrc, const D3D12_BOX *pSrcBox) override
			{
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiCopyTextureRegion);
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
				D3D12_TEXTURE_COPY_LOCATION dstStorage = {};
				D3D12_TEXTURE_COPY_LOCATION srcStorage = {};
				const D3D12_TEXTURE_COPY_LOCATION *activeDst = pDst;
					const D3D12_TEXTURE_COPY_LOCATION *activeSrc = pSrc;
					DX12WrapperRuntimeUtils::UnwrapTextureCopyLocation(pDst, &dstStorage, &activeDst);
					DX12WrapperRuntimeUtils::UnwrapTextureCopyLocation(pSrc, &srcStorage, &activeSrc);
					DX12TrackTextureHashFromCopyTextureRegion(
						activeDst, DstX, DstY, DstZ, activeSrc, pSrcBox);
					mCommandList->CopyTextureRegion(activeDst, DstX, DstY, DstZ, activeSrc, pSrcBox);
				}

			void STDMETHODCALLTYPE CopyResource(ID3D12Resource *pDstResource, ID3D12Resource *pSrcResource) override
			{
			DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
				DX12OuterTimingProbeUtils::ApiCopyResource);
			DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
			DX12_HOTPATH_TIME_BEGIN(CopyResourceOpTotal);
				ID3D12Resource *dstResource =
					DX12WrapperRuntimeUtils::UnwrapResource(pDstResource);
				ID3D12Resource *srcResource =
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource);
				mCommandList->CopyResource(
					dstResource,
					srcResource);
				if (dstResource && srcResource) {
					const D3D12_RESOURCE_DESC dstDesc = dstResource->GetDesc();
					const D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();
					if (dstDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					    srcDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
					    !DX12CopyBufferContent(dstResource, srcResource)) {
						DX12InvalidateBufferContent(dstResource);
					}
				}
			DX12_HOTPATH_TIME_END(CopyResourceOpTotal);
			}

			void STDMETHODCALLTYPE CopyTiles(
				ID3D12Resource *pTiledResource,
				const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
				const D3D12_TILE_REGION_SIZE *pTileRegionSize, ID3D12Resource *pBuffer,
				UINT64 BufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS Flags) override
			{
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiCopyTiles);
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
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiResolveSubresource);
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
				mCommandList->ResolveSubresource(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), DstSubresource,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), SrcSubresource, Format);
			}

			void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) override
			{
				DX12_HOTPATH_TIME_BEGIN(IaSetTopologyTotal);
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiIASetPrimitiveTopology);
				if (DX12HotPathState::skipAll) {
					if (mOrigIASetPrimitiveTopology)
						mOrigIASetPrimitiveTopology(mCommandList, PrimitiveTopology);
					else
						mCommandList->IASetPrimitiveTopology(PrimitiveTopology);
				} else {
					DX12CommandListStateFlow::CapturePrimitiveTopology(
						mCommandList, &mRuntimeState, PrimitiveTopology);
					if (mOrigIASetPrimitiveTopology)
						mOrigIASetPrimitiveTopology(mCommandList, PrimitiveTopology);
					else
						mCommandList->IASetPrimitiveTopology(PrimitiveTopology);
				}
				DX12_HOTPATH_TIME_END(IaSetTopologyTotal);
			}

			void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT *pViewports) override
			{
			DX12_HOTPATH_TIME_BEGIN(RSSetViewportsTotal);
				mCommandList->RSSetViewports(NumViewports, pViewports);
			DX12_HOTPATH_TIME_END(RSSetViewportsTotal);
			}

			void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D12_RECT *pRects) override
			{
			DX12_HOTPATH_TIME_BEGIN(RSSetScissorRectsTotal);
				mCommandList->RSSetScissorRects(NumRects, pRects);
			DX12_HOTPATH_TIME_END(RSSetScissorRectsTotal);
			}

			void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT BlendFactor[4]) override
			{
			DX12_HOTPATH_TIME_BEGIN(OMSetBlendFactorTotal);
				mCommandList->OMSetBlendFactor(BlendFactor);
			DX12_HOTPATH_TIME_END(OMSetBlendFactorTotal);
			}

			void STDMETHODCALLTYPE OMSetStencilRef(UINT StencilRef) override
			{
			DX12_HOTPATH_TIME_BEGIN(OMSetStencilRefTotal);
				mCommandList->OMSetStencilRef(StencilRef);
			DX12_HOTPATH_TIME_END(OMSetStencilRefTotal);
			}

			void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState *pPipelineState) override
			{
				if (DX12HotPathState::skipAll && mOrigSetPipelineState) {
					DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
						DX12OuterTimingProbeUtils::ApiSetPipelineState);
					mOrigSetPipelineState(mCommandList, pPipelineState);
					return;
				}
				DX12WrapperRuntimeUtils::SetPipelineState(
					mCommandList, &mRuntimeState, pPipelineState, mOrigSetPipelineState);
			}

			void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers) override
			{
				if (DX12HotPathState::skipAll && mOrigResourceBarrier) {
					DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
						DX12OuterTimingProbeUtils::ApiResourceBarrier);
					mOrigResourceBarrier(mCommandList, NumBarriers, pBarriers);
					return;
				}
				DX12WrapperRuntimeUtils::ResourceBarrier(mCommandList, &mRuntimeState, NumBarriers, pBarriers, mOrigResourceBarrier);
			}

			void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList *pCommandList) override
			{
			DX12_HOTPATH_TIME_BEGIN(ExecuteBundleTotal);
				mCommandList->ExecuteBundle(DX12WrapperRuntimeUtils::UnwrapGraphicsCommandList(pCommandList));
				DX12CommandListLifecycle::InvalidateAfterExecuteBundle(mCommandList);
			DX12_HOTPATH_TIME_END(ExecuteBundleTotal);
			}

			void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap *const *ppDescriptorHeaps) override
			{
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiSetDescriptorHeaps);
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

				if (mOrigSetDescriptorHeaps)
					mOrigSetDescriptorHeaps(
						mCommandList, NumDescriptorHeaps, activeHeaps);
				else
					mCommandList->SetDescriptorHeaps(
						NumDescriptorHeaps, activeHeaps);
			}

			void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature *pRootSignature) override
			{
				DX12_HOTPATH_TIME_BEGIN(RootSignatureTotal);
				DX12WrapperRuntimeUtils::SetComputeRootSignature(mCommandList,
					&mRuntimeState, pRootSignature, mOrigSetComputeRootSignature);
				DX12_HOTPATH_TIME_END(RootSignatureTotal);
			}

			void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature *pRootSignature) override
			{
				DX12_HOTPATH_TIME_BEGIN(RootSignatureTotal);
				DX12WrapperRuntimeUtils::SetGraphicsRootSignature(mCommandList,
					&mRuntimeState, pRootSignature, mOrigSetGraphicsRootSignature);
				DX12_HOTPATH_TIME_END(RootSignatureTotal);
			}

			void STDMETHODCALLTYPE SetComputeRootDescriptorTable(
				UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
			{
				DX12_HOTPATH_TIME_BEGIN(RootDescTableTotal);
				DX12WrapperRuntimeUtils::SetComputeRootDescriptorTable(
					mCommandList, &mRuntimeState, RootParameterIndex, BaseDescriptor, mOrigSetComputeRootDescriptorTable);
				DX12_HOTPATH_TIME_END(RootDescTableTotal);
			}

			void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(
				UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override
			{
				DX12_HOTPATH_TIME_BEGIN(RootDescTableTotal);
				DX12WrapperRuntimeUtils::SetGraphicsRootDescriptorTable(
					mCommandList, &mRuntimeState, RootParameterIndex, BaseDescriptor, mOrigSetGraphicsRootDescriptorTable);
				DX12_HOTPATH_TIME_END(RootDescTableTotal);
			}

			void STDMETHODCALLTYPE SetComputeRoot32BitConstant(
				UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override
			{
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiSetComputeRootConstants);
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
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiSetGraphicsRootConstants);
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
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiSetComputeRootConstants);
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
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiSetGraphicsRootConstants);
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
			DX12_HOTPATH_TIME_BEGIN(SetComputeCBVTotal);
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
			DX12_HOTPATH_TIME_END(SetComputeCBVTotal);
			}

			void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetGraphicsCBVTotal);
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
			DX12_HOTPATH_TIME_END(SetGraphicsCBVTotal);
			}

			void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetComputeSRVTotal);
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
			DX12_HOTPATH_TIME_END(SetComputeSRVTotal);
			}

			void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetGraphicsSRVTotal);
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
			DX12_HOTPATH_TIME_END(SetGraphicsSRVTotal);
			}

			void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetComputeUAVTotal);
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
			DX12_HOTPATH_TIME_END(SetComputeUAVTotal);
			}

			void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetGraphicsUAVTotal);
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
			DX12_HOTPATH_TIME_END(SetGraphicsUAVTotal);
			}

			void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *pView) override
			{
			DX12_HOTPATH_TIME_BEGIN(IASetIndexBufTotal);
				if (DX12HotPathState::skipAll && mOrigIASetIndexBuffer) {
					DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
						DX12OuterTimingProbeUtils::ApiIASetIndexBuffer);
					mOrigIASetIndexBuffer(mCommandList, pView);
					return;
				}
				DX12WrapperRuntimeUtils::IASetIndexBuffer(
					mCommandList, &mRuntimeState, pView, mOrigIASetIndexBuffer);
			DX12_HOTPATH_TIME_END(IASetIndexBufTotal);
			}

			void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews) override
			{
			DX12_HOTPATH_TIME_BEGIN(IASetVertexBufTotal);
				if (DX12HotPathState::skipAll && mOrigIASetVertexBuffers) {
					DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
						DX12OuterTimingProbeUtils::ApiIASetVertexBuffers);
					mOrigIASetVertexBuffers(mCommandList, StartSlot, NumViews, pViews);
					return;
				}
				DX12WrapperRuntimeUtils::IASetVertexBuffers(
					mCommandList, &mRuntimeState, StartSlot, NumViews, pViews, mOrigIASetVertexBuffers);
			DX12_HOTPATH_TIME_END(IASetVertexBufTotal);
			}

			void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) override
			{
			DX12_HOTPATH_TIME_BEGIN(SOSetTargetsTotal);
				mCommandList->SOSetTargets(StartSlot, NumViews, pViews);
			DX12_HOTPATH_TIME_END(SOSetTargetsTotal);
			}

			void STDMETHODCALLTYPE OMSetRenderTargets(
				UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
				BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) override
			{
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
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiClearUnorderedAccessView);
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
				DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
					DX12OuterTimingProbeUtils::ApiClearUnorderedAccessView);
				DX12CommandListRuntime::BumpIaContentEpoch(&mRuntimeState);
				mCommandList->ClearUnorderedAccessViewFloat(
					ViewGPUHandleInCurrentHeap, ViewCPUHandle,
					DX12WrapperRuntimeUtils::UnwrapResource(pResource), Values, NumRects, pRects);
			}

			void STDMETHODCALLTYPE DiscardResource(ID3D12Resource *pResource, const D3D12_DISCARD_REGION *pRegion) override
			{
			DX12_HOTPATH_TIME_BEGIN(DiscardResourceTotal);
				mCommandList->DiscardResource(DX12WrapperRuntimeUtils::UnwrapResource(pResource), pRegion);
			DX12_HOTPATH_TIME_END(DiscardResourceTotal);
			}

			void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override
			{
			DX12_HOTPATH_TIME_BEGIN(BeginQueryTotal);
				mCommandList->BeginQuery(DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, Index);
			DX12_HOTPATH_TIME_END(BeginQueryTotal);
			}

			void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override
			{
			DX12_HOTPATH_TIME_BEGIN(EndQueryTotal);
				mCommandList->EndQuery(DX12WrapperRuntimeUtils::UnwrapQueryHeap(pQueryHeap), Type, Index);
			DX12_HOTPATH_TIME_END(EndQueryTotal);
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
			DX12_HOTPATH_TIME_BEGIN(SetMarkerTotal);
				mCommandList->SetMarker(Metadata, pData, Size);
			DX12_HOTPATH_TIME_END(SetMarkerTotal);
			}

			void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void *pData, UINT Size) override
			{
			DX12_HOTPATH_TIME_BEGIN(BeginEventTotal);
				mCommandList->BeginEvent(Metadata, pData, Size);
			DX12_HOTPATH_TIME_END(BeginEventTotal);
			}

			void STDMETHODCALLTYPE EndEvent() override
			{
			DX12_HOTPATH_TIME_BEGIN(EndEventTotal);
				mCommandList->EndEvent();
			DX12_HOTPATH_TIME_END(EndEventTotal);
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
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (FAILED(QueryCommandList1(&commandList1)))
					return;
				std::vector<ID3D12Resource*> dependentResources;
				ID3D12Resource *const *activeDependentResources = ppDependentResources;
				DX12WrapperRuntimeUtils::UnwrapResourceArray(
					ppDependentResources, Dependencies, &dependentResources, &activeDependentResources);
				commandList1->AtomicCopyBufferUINT(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset,
					Dependencies, activeDependentResources, pDependentSubresourceRanges);
				commandList1->Release();
			}

			void STDMETHODCALLTYPE AtomicCopyBufferUINT64(
				ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer,
				UINT64 SrcOffset, UINT Dependencies, ID3D12Resource *const *ppDependentResources,
				const D3D12_SUBRESOURCE_RANGE_UINT64 *pDependentSubresourceRanges) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (FAILED(QueryCommandList1(&commandList1)))
					return;
				std::vector<ID3D12Resource*> dependentResources;
				ID3D12Resource *const *activeDependentResources = ppDependentResources;
				DX12WrapperRuntimeUtils::UnwrapResourceArray(
					ppDependentResources, Dependencies, &dependentResources, &activeDependentResources);
				commandList1->AtomicCopyBufferUINT64(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstBuffer), DstOffset,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcBuffer), SrcOffset,
					Dependencies, activeDependentResources, pDependentSubresourceRanges);
				commandList1->Release();
			}

			void STDMETHODCALLTYPE OMSetDepthBounds(FLOAT Min, FLOAT Max) override
			{
			DX12_HOTPATH_TIME_BEGIN(OMSetDepthBoundsTotal);
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (SUCCEEDED(QueryCommandList1(&commandList1))) {
					commandList1->OMSetDepthBounds(Min, Max);
					commandList1->Release();
				}
			DX12_HOTPATH_TIME_END(OMSetDepthBoundsTotal);
			}

			void STDMETHODCALLTYPE SetSamplePositions(
				UINT NumSamplesPerPixel, UINT NumPixels, D3D12_SAMPLE_POSITION *pSamplePositions) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (SUCCEEDED(QueryCommandList1(&commandList1))) {
					commandList1->SetSamplePositions(NumSamplesPerPixel, NumPixels, pSamplePositions);
					commandList1->Release();
				}
			}

			void STDMETHODCALLTYPE ResolveSubresourceRegion(
				ID3D12Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
				ID3D12Resource *pSrcResource, UINT SrcSubresource, RECT *pSrcRect,
				DXGI_FORMAT Format, D3D12_RESOLVE_MODE ResolveMode) override
			{
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (FAILED(QueryCommandList1(&commandList1)))
					return;
				commandList1->ResolveSubresourceRegion(
					DX12WrapperRuntimeUtils::UnwrapResource(pDstResource), DstSubresource, DstX, DstY,
					DX12WrapperRuntimeUtils::UnwrapResource(pSrcResource), SrcSubresource,
					pSrcRect, Format, ResolveMode);
				commandList1->Release();
			}

			void STDMETHODCALLTYPE SetViewInstanceMask(UINT Mask) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetViewInstanceMaskTotal);
				ID3D12GraphicsCommandList1 *commandList1 = nullptr;
				if (SUCCEEDED(QueryCommandList1(&commandList1))) {
					commandList1->SetViewInstanceMask(Mask);
					commandList1->Release();
				}
			DX12_HOTPATH_TIME_END(SetViewInstanceMaskTotal);
			}

			void STDMETHODCALLTYPE WriteBufferImmediate(
				UINT Count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *pParams,
				const D3D12_WRITEBUFFERIMMEDIATE_MODE *pModes) override
			{
				ID3D12GraphicsCommandList2 *commandList2 = nullptr;
				if (SUCCEEDED(QueryCommandList2(&commandList2))) {
					commandList2->WriteBufferImmediate(Count, pParams, pModes);
					commandList2->Release();
				}
			}

			void STDMETHODCALLTYPE SetProtectedResourceSession(ID3D12ProtectedResourceSession *pProtectedResourceSession) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetProtectedSessionTotal);
				ID3D12GraphicsCommandList3 *commandList3 = nullptr;
				if (SUCCEEDED(QueryCommandList3(&commandList3))) {
					commandList3->SetProtectedResourceSession(
						DX12WrapperRuntimeUtils::UnwrapProtectedResourceSession(pProtectedResourceSession));
					commandList3->Release();
				}
			DX12_HOTPATH_TIME_END(SetProtectedSessionTotal);
			}

			void STDMETHODCALLTYPE BeginRenderPass(
				UINT NumRenderTargets, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *pRenderTargets,
				const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *pDepthStencil, D3D12_RENDER_PASS_FLAGS Flags) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->BeginRenderPass(NumRenderTargets, pRenderTargets, pDepthStencil, Flags);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE EndRenderPass() override
			{
			DX12_HOTPATH_TIME_BEGIN(EndRenderPassTotal);
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->EndRenderPass();
					commandList4->Release();
				}
			DX12_HOTPATH_TIME_END(EndRenderPassTotal);
			}

			void STDMETHODCALLTYPE InitializeMetaCommand(
				ID3D12MetaCommand *pMetaCommand, const void *pInitializationParametersData,
				SIZE_T InitializationParametersDataSizeInBytes) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->InitializeMetaCommand(
						DX12WrapperRuntimeUtils::UnwrapMetaCommand(pMetaCommand),
						pInitializationParametersData, InitializationParametersDataSizeInBytes);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE ExecuteMetaCommand(
				ID3D12MetaCommand *pMetaCommand, const void *pExecutionParametersData,
				SIZE_T ExecutionParametersDataSizeInBytes) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->ExecuteMetaCommand(
						DX12WrapperRuntimeUtils::UnwrapMetaCommand(pMetaCommand),
						pExecutionParametersData, ExecutionParametersDataSizeInBytes);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(
				const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *pDesc,
				UINT NumPostbuildInfoDescs, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pPostbuildInfoDescs) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->BuildRaytracingAccelerationStructure(pDesc, NumPostbuildInfoDescs, pPostbuildInfoDescs);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE EmitRaytracingAccelerationStructurePostbuildInfo(
				const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *pDesc,
				UINT NumSourceAccelerationStructures, const D3D12_GPU_VIRTUAL_ADDRESS *pSourceAccelerationStructureData) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->EmitRaytracingAccelerationStructurePostbuildInfo(
						pDesc, NumSourceAccelerationStructures, pSourceAccelerationStructureData);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(
				D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData,
				D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData,
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode) override
			{
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->CopyRaytracingAccelerationStructure(
						DestAccelerationStructureData, SourceAccelerationStructureData, Mode);
					commandList4->Release();
				}
			}

			void STDMETHODCALLTYPE SetPipelineState1(ID3D12StateObject *pStateObject) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetPSO1Total);
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->SetPipelineState1(DX12WrapperRuntimeUtils::UnwrapStateObject(pStateObject));
					commandList4->Release();
				}
			DX12_HOTPATH_TIME_END(SetPSO1Total);
			}

			void STDMETHODCALLTYPE DispatchRays(const D3D12_DISPATCH_RAYS_DESC *pDesc) override
			{
			DX12_HOTPATH_TIME_BEGIN(DispatchRaysTotal);
				ID3D12GraphicsCommandList4 *commandList4 = nullptr;
				if (SUCCEEDED(QueryCommandList4(&commandList4))) {
					commandList4->DispatchRays(pDesc);
					commandList4->Release();
				}
			DX12_HOTPATH_TIME_END(DispatchRaysTotal);
			}

			void STDMETHODCALLTYPE RSSetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER *combiners) override
			{
			DX12_HOTPATH_TIME_BEGIN(RSSetShadingRateTotal);
				ID3D12GraphicsCommandList5 *commandList5 = nullptr;
				if (SUCCEEDED(QueryCommandList5(&commandList5))) {
					commandList5->RSSetShadingRate(baseShadingRate, combiners);
					commandList5->Release();
				}
			DX12_HOTPATH_TIME_END(RSSetShadingRateTotal);
			}

			void STDMETHODCALLTYPE RSSetShadingRateImage(ID3D12Resource *pShadingRateImage) override
			{
			DX12_HOTPATH_TIME_BEGIN(RSSetShadingRateImgTotal);
				ID3D12GraphicsCommandList5 *commandList5 = nullptr;
				if (SUCCEEDED(QueryCommandList5(&commandList5))) {
					commandList5->RSSetShadingRateImage(DX12WrapperRuntimeUtils::UnwrapResource(pShadingRateImage));
					commandList5->Release();
				}
			DX12_HOTPATH_TIME_END(RSSetShadingRateImgTotal);
			}

			void STDMETHODCALLTYPE DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override
			{
			DX12_HOTPATH_TIME_BEGIN(DispatchMeshTotal);
				ID3D12GraphicsCommandList6 *commandList6 = nullptr;
				if (SUCCEEDED(QueryCommandList6(&commandList6))) {
					commandList6->DispatchMesh(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
					commandList6->Release();
				}
			DX12_HOTPATH_TIME_END(DispatchMeshTotal);
			}

			void STDMETHODCALLTYPE Barrier(
				UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP *pBarrierGroups) override
			{
				ID3D12GraphicsCommandList7 *commandList7 = nullptr;
				if (FAILED(QueryCommandList7(&commandList7)))
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
				DX12WrapperRuntimeUtils::LogCommandListStage("Barrier", "before", mCommandList);
				commandList7->Barrier(NumBarrierGroups, activeBarrierGroups);
				DX12WrapperRuntimeUtils::LogCommandListStage("Barrier", "after", mCommandList);
				commandList7->Release();
			}

			void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT FrontStencilRef, UINT BackStencilRef) override
			{
			DX12_HOTPATH_TIME_BEGIN(OMSetFrontBackStencilTotal);
				ID3D12GraphicsCommandList8 *commandList8 = nullptr;
				if (SUCCEEDED(QueryCommandList8(&commandList8))) {
					commandList8->OMSetFrontAndBackStencilRef(FrontStencilRef, BackStencilRef);
					commandList8->Release();
				}
			DX12_HOTPATH_TIME_END(OMSetFrontBackStencilTotal);
			}

			void STDMETHODCALLTYPE RSSetDepthBias(FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias) override
			{
			DX12_HOTPATH_TIME_BEGIN(RSSetDepthBiasTotal);
				ID3D12GraphicsCommandList9 *commandList9 = nullptr;
				if (SUCCEEDED(QueryCommandList9(&commandList9))) {
					commandList9->RSSetDepthBias(DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
					commandList9->Release();
				}
			DX12_HOTPATH_TIME_END(RSSetDepthBiasTotal);
			}

			void STDMETHODCALLTYPE IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue) override
			{
			DX12_HOTPATH_TIME_BEGIN(IASetIBStripCutTotal);
				ID3D12GraphicsCommandList9 *commandList9 = nullptr;
				if (SUCCEEDED(QueryCommandList9(&commandList9))) {
					commandList9->IASetIndexBufferStripCutValue(IBStripCutValue);
					commandList9->Release();
				}
			DX12_HOTPATH_TIME_END(IASetIBStripCutTotal);
			}

			void STDMETHODCALLTYPE SetProgram(const D3D12_SET_PROGRAM_DESC *pDesc) override
			{
			DX12_HOTPATH_TIME_BEGIN(SetProgramTotal);
				ID3D12GraphicsCommandList10 *commandList10 = nullptr;
				if (SUCCEEDED(QueryCommandList10(&commandList10))) {
					commandList10->SetProgram(pDesc);
					commandList10->Release();
				}
			DX12_HOTPATH_TIME_END(SetProgramTotal);
			}

			void STDMETHODCALLTYPE DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC *pDesc) override
			{
			DX12_HOTPATH_TIME_BEGIN(DispatchGraphTotal);
				ID3D12GraphicsCommandList10 *commandList10 = nullptr;
				if (SUCCEEDED(QueryCommandList10(&commandList10))) {
					commandList10->DispatchGraph(pDesc);
					commandList10->Release();
				}
			DX12_HOTPATH_TIME_END(DispatchGraphTotal);
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
			DX12CommandListRuntimeState mRuntimeState;
		};
