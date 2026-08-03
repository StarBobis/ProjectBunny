#pragma once

#include "Resources/DX12ResourceDump.hpp"
#include "Diagnostics/DX12HangWatchdog.hpp"

// Deferred initial mod load (startup hang workaround). Declared here so every
// TU that compiles DX12ResourceRuntime.hpp sees them regardless of include
// order; the (extern) definitions live in DX12ModState.inl /
// DX12ModConfiguration.inl, which are only included by DX12ModRuntime.cpp.
extern DWORD gDeferredInitialModLoadDelayMs;
extern void DX12ModScheduleInitialLoad(const wchar_t *configPath);
extern void DX12ModMaybeRunDeferredInitialLoad();

class DX12ResourceHookSlotTypes
{
public:
	class ID3D12Resource
	{
	public:
		// IUnknown: slots 0-2
		// ID3D12Object: slots 3-6
		// ID3D12DeviceChild: slot 7 (GetDevice)
		// ID3D12Pageable: no methods
		// ID3D12Resource methods start at slot 8
		static constexpr UINT Map = 8;
		static constexpr UINT Unmap = 9;
	};
};


class DX12ResourceTrackerHookCallbacks
{
public:
	static HRESULT STDMETHODCALLTYPE HookedCreateDescriptorHeap(
		ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **heap);
	static HRESULT STDMETHODCALLTYPE HookedCreateRootSignature(
		ID3D12Device *device, UINT nodeMask, const void *blob, SIZE_T blobLength,
		REFIID riid, void **rootSignature);
	static void STDMETHODCALLTYPE HookedCreateConstantBufferView(
		ID3D12Device *device, const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateShaderResourceView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateUnorderedAccessView(
		ID3D12Device *device, ID3D12Resource *resource, ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateRenderTargetView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateDepthStencilView(
		ID3D12Device *device, ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCreateSampler(
		ID3D12Device *device, const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void STDMETHODCALLTYPE HookedCopyDescriptors(
		ID3D12Device *device, UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes, UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void STDMETHODCALLTYPE HookedCopyDescriptorsSimple(
		ID3D12Device *device, UINT numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource(
		ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource(
		ID3D12Device *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource(
		ID3D12Device *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateFence(
		ID3D12Device *device, UINT64 initialValue, D3D12_FENCE_FLAGS flags,
		REFIID riid, void **fence);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource1(
		ID3D12Device4 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource1(
		ID3D12Device4 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource2(
		ID3D12Device8 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource1(
		ID3D12Device8 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES initialState,
		const D3D12_CLEAR_VALUE *optimizedClearValue, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateCommittedResource3(
		ID3D12Device10 *device, const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC1 *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreatePlacedResource2(
		ID3D12Device10 *device, ID3D12Heap *heap, UINT64 heapOffset,
		const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initialLayout,
		const D3D12_CLEAR_VALUE *optimizedClearValue, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
	static HRESULT STDMETHODCALLTYPE HookedCreateReservedResource2(
		ID3D12Device10 *device, const D3D12_RESOURCE_DESC *desc,
		D3D12_BARRIER_LAYOUT initialLayout, const D3D12_CLEAR_VALUE *optimizedClearValue,
		ID3D12ProtectedResourceSession *protectedSession, UINT32 numCastableFormats,
		const DXGI_FORMAT *castableFormats, REFIID riid, void **resource);
};



class DX12ResourceTrackerHookDiagnostics
{
public:
	static void LogResourceHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugFields("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogResourceHookCallWhenTracking(const char *api, const void *object)
	{
		if (DX12HotPathState::skipAll &&
				!DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
			return;
		LogResourceHookCall(api, object);
	}
};



class DX12ResourceTrackerHookRegistry
{
public:
	template <typename T>
	static T GetDeviceOriginal(void *device, UINT slot, T fallback, const char *name)
	{
		return DX12OriginalFunctionRegistry::Resolve(device, slot, fallback, name);
	}
};



class DX12ResourceTrackerHookRuntime
{
public:
	static const D3D12_UNORDERED_ACCESS_VIEW_DESC *PrepareUavDesc(
		ID3D12Resource *resource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_UNORDERED_ACCESS_VIEW_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC *PrepareResourceDesc(
		const D3D12_RESOURCE_DESC *desc,
		D3D12_RESOURCE_DESC *storage,
		const char *source);
	static const D3D12_RESOURCE_DESC1 *PrepareResourceDesc1(
		const D3D12_RESOURCE_DESC1 *desc,
		D3D12_RESOURCE_DESC1 *storage,
		const char *source);
	static bool ShouldRecordCreatedResource(const D3D12_RESOURCE_DESC *desc);
	static void RecordCreatedDescriptorHeap(
		HRESULT hr, ID3D12Device *device, const D3D12_DESCRIPTOR_HEAP_DESC *desc, void **heap);
	static void RecordCreatedRootSignature(
		HRESULT hr, UINT nodeMask, const void *blob, SIZE_T blobLength, void **rootSignature);
	static void RecordCreatedConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedShaderResourceView(
		ID3D12Device *device,
		ID3D12Resource *resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedUnorderedAccessView(
		ID3D12Device *device,
		ID3D12Resource *resource,
		ID3D12Resource *counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedRenderTargetView(
		ID3D12Resource *resource,
		const D3D12_RENDER_TARGET_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedDepthStencilView(
		ID3D12Resource *resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCreatedSampler(
		const D3D12_SAMPLER_DESC *desc,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptor);
	static void RecordCopiedDescriptors(
		ID3D12Device *device,
		UINT numDestDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *destDescriptorRangeStarts,
		const UINT *destDescriptorRangeSizes,
		UINT numSrcDescriptorRanges,
		const D3D12_CPU_DESCRIPTOR_HANDLE *srcDescriptorRangeStarts,
		const UINT *srcDescriptorRangeSizes,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCopiedDescriptorsSimple(
		ID3D12Device *device,
		UINT numDescriptors,
		D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
		D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapsType);
	static void RecordCreatedResource(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static PlacedResourceInfo MakePlacedResourceInfo(ID3D12Heap *heap, UINT64 heapOffset);
	static void LogResourceCreateStage(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource);
	static void LogResourceCreateStage1(
		const char *api,
		const char *stage,
		ID3D12Device *device,
		HRESULT hr,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		D3D12_RESOURCE_STATES initialState,
		void **resource);
	static void RecordCreatedResource1(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static void RecordCreatedResourceWithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);
	static void RecordCreatedResource1WithLayout(
		HRESULT hr,
		void **resource,
		const D3D12_RESOURCE_DESC1 *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_BARRIER_LAYOUT initialLayout);

	static void InstallMapUnmapHooks(ID3D12Resource *resource);

	// Wrapper-mode Map/Unmap interception. ResourceWrapper calls these so
	// buffer content capture keeps working without patching the shared
	// ID3D12Resource code (RenderDoc-style, DLSS FG compatible).
	static HRESULT WrappedResourceMap(
		ID3D12Resource *resource, UINT subresource, const D3D12_RANGE *readRange, void **data);
	static void WrappedResourceUnmap(
		ID3D12Resource *resource, UINT subresource,
		const D3D12_RANGE *writtenRange, const void *caller);
};


class DX12ResourceTrackerHookSignatures
{
public:
	using CreateDescriptorHeap = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
	using CreateRootSignature = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);
	using CreateConstantBufferView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateShaderResourceView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateUnorderedAccessView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateRenderTargetView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateDepthStencilView = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CreateSampler = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	using CopyDescriptors = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*,
		UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	using CopyDescriptorsSimple = void(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE,
		D3D12_DESCRIPTOR_HEAP_TYPE);
	using CreateCommittedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		REFIID, void**);
	using CreatePlacedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*,
		D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateReservedResource = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
		const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateFence = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device*, UINT64, D3D12_FENCE_FLAGS, REFIID, void**);
	using CreateCommittedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device4*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreateReservedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device4*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
		const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreateCommittedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device8*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, REFIID, void**);
	using CreatePlacedResource1 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device8*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
		D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	using CreateCommittedResource3 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
		const D3D12_RESOURCE_DESC1*, D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*,
		ID3D12ProtectedResourceSession*, UINT32, const DXGI_FORMAT*, REFIID, void**);
	using CreatePlacedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*,
		D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE*, UINT32, const DXGI_FORMAT*,
		REFIID, void**);
	using CreateReservedResource2 = HRESULT(STDMETHODCALLTYPE *)(
		ID3D12Device10*, const D3D12_RESOURCE_DESC*, D3D12_BARRIER_LAYOUT,
		const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, UINT32,
		const DXGI_FORMAT*, REFIID, void**);
};


class DX12ResourceTrackerHookSlotTypes
{
public:
	class ID3D12Device
	{
	public:
		static constexpr UINT CreateDescriptorHeap = 14;
		static constexpr UINT CreateRootSignature = 16;
		static constexpr UINT CreateConstantBufferView = 17;
		static constexpr UINT CreateShaderResourceView = 18;
		static constexpr UINT CreateUnorderedAccessView = 19;
		static constexpr UINT CreateRenderTargetView = 20;
		static constexpr UINT CreateDepthStencilView = 21;
		static constexpr UINT CreateSampler = 22;
		static constexpr UINT CopyDescriptors = 23;
		static constexpr UINT CopyDescriptorsSimple = 24;
		static constexpr UINT CreateCommittedResource = 27;
		static constexpr UINT CreatePlacedResource = 29;
		static constexpr UINT CreateReservedResource = 30;
		static constexpr UINT CreateFence = 36;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommittedResource1 = 53;
		static constexpr UINT CreateReservedResource1 = 55;
	};

	class ID3D12Device8
	{
	public:
		static constexpr UINT CreateCommittedResource2 = 69;
		static constexpr UINT CreatePlacedResource1 = 70;
	};

	class ID3D12Device10
	{
	public:
		static constexpr UINT CreateCommittedResource3 = 76;
		static constexpr UINT CreatePlacedResource2 = 77;
		static constexpr UINT CreateReservedResource2 = 78;
	};
};

class DX12ResourceTrackerHookState
{
public:
	inline static DX12ResourceTrackerHookSignatures::CreateDescriptorHeap OrigCreateDescriptorHeap = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateRootSignature OrigCreateRootSignature = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateConstantBufferView OrigCreateConstantBufferView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateShaderResourceView OrigCreateShaderResourceView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateUnorderedAccessView OrigCreateUnorderedAccessView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateRenderTargetView OrigCreateRenderTargetView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateDepthStencilView OrigCreateDepthStencilView = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateSampler OrigCreateSampler = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CopyDescriptors OrigCopyDescriptors = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CopyDescriptorsSimple OrigCopyDescriptorsSimple = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource OrigCreateCommittedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource OrigCreatePlacedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource OrigCreateReservedResource = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateFence OrigCreateFence = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource1 OrigCreateCommittedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource1 OrigCreateReservedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource2 OrigCreateCommittedResource2 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource1 OrigCreatePlacedResource1 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateCommittedResource3 OrigCreateCommittedResource3 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreatePlacedResource2 OrigCreatePlacedResource2 = nullptr;
	inline static DX12ResourceTrackerHookSignatures::CreateReservedResource2 OrigCreateReservedResource2 = nullptr;

	// Per-resource Map/Unmap originals for buffer content capture
	struct {
		void *Map = nullptr;
		void *Unmap = nullptr;
	} inline static ResourceOriginals;
};


class DXGIHooks
{
public:
	static void Install();
	static void Shutdown();
};

class DX12WaitHooks
{
public:
	static void Install();
	static void Shutdown();
};



namespace Bunny {

class LoaderConfig
{
public:
	std::wstring target;
	std::wstring module;
	std::wstring entryPoint = L"CBTProc";
	std::wstring launch;
	std::wstring workingDirectory;
	bool checkVersion = true;
	bool requireAdmin = false;
	bool waitForTarget = true;
	int hookProc = 5;
	int delay = 0;
};

class RuntimeConfig
{
public:
	bool dx12ProxyMode = true;
	bool dx12SafeMode = false;
	bool enableOverlay = false;
	int dx12ConfigInitializationDelayMs = 2000;
	std::wstring configPath;
};

class MigotoConfig {
public:
	bool Load(const wchar_t *path);

	const IniDocument &Ini() const { return mIni; }
	const LoaderConfig &Loader() const { return mLoader; }
	const RuntimeConfig &Runtime() const { return mRuntime; }

	const std::wstring &Path() const { return mPath; }
	const std::wstring &Error() const { return mError; }

public:
	void ReadLoaderConfig();
	void ReadRuntimeConfig();

	IniDocument mIni;
	LoaderConfig mLoader;
	RuntimeConfig mRuntime;
	std::wstring mPath;
	std::wstring mError;
};

class MigotoConfigLocator
{
public:
	static std::wstring FindDefaultConfigPath(HINSTANCE module);
};

}


namespace Bunny {

class MigotoConfigReader
{
public:
static void ReadString(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, std::wstring *value)
{
	std::wstring text;
	if (ini.GetString(section, key, &text) && value)
		*value = text;
}

static void ReadBool(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, bool *value)
{
	bool parsed = false;
	if (ini.GetBool(section, key, &parsed) && value)
		*value = parsed;
}

static void ReadInt(
	const IniDocument &ini, const wchar_t *section, const wchar_t *key, int *value)
{
	int parsed = 0;
	if (ini.GetInt(section, key, &parsed) && value)
		*value = parsed;
}

};

inline bool MigotoConfig::Load(const wchar_t *path)
{
	mPath = path ? path : L"";
	mError.clear();
	mLoader = LoaderConfig();
	mRuntime = RuntimeConfig();
	mRuntime.configPath = mPath;

	if (!mIni.LoadFromFile(mPath.c_str())) {
		mError = mIni.Error();
		return false;
	}

	ReadLoaderConfig();
	ReadRuntimeConfig();
	return true;
}

inline void MigotoConfig::ReadLoaderConfig()
{
	MigotoConfigReader::ReadString(mIni, L"Loader", L"target", &mLoader.target);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"module", &mLoader.module);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"entry_point", &mLoader.entryPoint);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"launch", &mLoader.launch);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_dir", &mLoader.workingDirectory);
	MigotoConfigReader::ReadString(mIni, L"Loader", L"working_directory", &mLoader.workingDirectory);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"check_version", &mLoader.checkVersion);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"require_admin", &mLoader.requireAdmin);
	MigotoConfigReader::ReadBool(mIni, L"Loader", L"wait_for_target", &mLoader.waitForTarget);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"hook_proc", &mLoader.hookProc);
	MigotoConfigReader::ReadInt(mIni, L"Loader", L"delay", &mLoader.delay);
}

inline void MigotoConfig::ReadRuntimeConfig()
{
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReader::ReadBool(mIni, L"DX12", L"overlay", &mRuntime.enableOverlay);
	MigotoConfigReader::ReadInt(mIni, L"DX12", L"config_initialization_delay_ms", &mRuntime.dx12ConfigInitializationDelayMs);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_proxy_mode", &mRuntime.dx12ProxyMode);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_safe_mode", &mRuntime.dx12SafeMode);
	MigotoConfigReader::ReadBool(mIni, L"System", L"dx12_overlay", &mRuntime.enableOverlay);
	MigotoConfigReader::ReadInt(mIni, L"System", L"dx12_config_initialization_delay_ms", &mRuntime.dx12ConfigInitializationDelayMs);
	if (mRuntime.dx12ConfigInitializationDelayMs < 0)
		mRuntime.dx12ConfigInitializationDelayMs = 0;
}

inline std::wstring MigotoConfigLocator::FindDefaultConfigPath(HINSTANCE module)
{
	wchar_t path[MAX_PATH] = {};
	wchar_t cwdPath[MAX_PATH] = {};

	if (GetCurrentDirectoryW(MAX_PATH, cwdPath)) {
		PathAppendW(cwdPath, L"d3dx.ini");
		if (PathFileExistsW(cwdPath))
			return cwdPath;
	}

	if (module && GetModuleFileNameW(module, path, MAX_PATH)) {
		PathRemoveFileSpecW(path);
		PathAppendW(path, L"d3dx.ini");
		if (PathFileExistsW(path))
			return path;
	}

	return L"d3dx.ini";
}

} // namespace Bunny



class DX12Runtime
{
public:
	using LoadRealD3D12Fn = HMODULE (*)();

	inline static bool Initialized = false;
	inline static Bunny::MigotoConfig Config;
	inline static bool ConfigLoaded = false;
	inline static LoadRealD3D12Fn LoadRealD3D12 = nullptr;

	static DWORD WINAPI WorkerThread(void*)
	{
		DX12LogManager::Text("DX12 worker thread started\n");
		if (LoadRealD3D12)
			LoadRealD3D12();

		for (int i = 0; i < 200; ++i) {
			DXGIHooks::Install();
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
			// Wait-object instrumentation hooks kernel32 exports process-wide;
			// it exists to attribute stall stacks in TIMING builds and must not
			// ship in normal Release (DX11 has no equivalent global wait hook).
			// Safe to install since Nektra now suspends all threads while
			// patching (DX12HookRegistry ctor), eliminating the torn-patch race
			// that previously corrupted waits (permanent 100ms timeout polling,
			// GPU pegged, flicker).
			// DX12WaitHooks::Install(); // CONTROLLED-EXPERIMENT: disabled
#endif
			Sleep(50);
		}

		return 0;
	}

	static void Initialize(HINSTANCE module, LoadRealD3D12Fn loadRealD3D12)
	{
		if (Initialized)
			return;
		Initialized = true;
		LoadRealD3D12 = loadRealD3D12;

		DX12RuntimeState::SetModule(module);
		DX12LogManager::Open();
		DX12Profiling::Init();
		DX12LogManager::Text(
			"\n*** 3DMigoto DX12 runtime initialized - build descriptor-churn+ia-reuse-v2 ***\n");
		DX12HangWatchdog::Start();

		std::wstring configPath = Bunny::MigotoConfigLocator::FindDefaultConfigPath(module);
		ConfigLoaded = Config.Load(configPath.c_str());
		if (ConfigLoaded) {
			DX12ModSetSafeMode(Config.Runtime().dx12SafeMode);
			gDeferredInitialModLoadDelayMs =
				static_cast<DWORD>(Config.Runtime().dx12ConfigInitializationDelayMs);
			DX12LogManager::Fields("DX12Config",
				"\"path\":\"%S\",\"safeMode\":%s,\"overlay\":%s,\"configInitializationDelayMs\":%lu,\"overlayStarted\":true",
				Config.Path().c_str(),
				Config.Runtime().dx12SafeMode ? "true" : "false",
				Config.Runtime().enableOverlay ? "true" : "false",
				static_cast<unsigned long>(gDeferredInitialModLoadDelayMs));
			DX12ModScheduleInitialLoad(Config.Path().c_str());
			DX12OverlayStatusState::SetStatus(
				L"DX12 mod load deferred until startup reload");
			DX12LogManager::Fields("DX12ModRuntimeDeferredLoad",
				"\"status\":\"scheduled\",\"path\":\"%S\",\"delayMs\":%lu",
				Config.Path().c_str(),
				static_cast<unsigned long>(gDeferredInitialModLoadDelayMs));
		} else {
			DX12LogManager::Fields("DX12Config",
				"\"path\":\"%S\",\"status\":\"missing_or_invalid\",\"error\":\"%S\"",
				configPath.c_str(), Config.Error().c_str());
		}
		DX12HotPathState::Update();

		// Start persistent resource/descriptor tracking queue (wrapper mode)
		DX12ResourceTrackerHooks::Install(nullptr);

		// CreateThread requires a DWORD WINAPI thread entry. Keep the entry
		// as a class method so runtime startup state stays in one module.
		HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		DX12HookRegistry::FinalizeHooks();

		DX12LogManager::EndReleaseStartupLogging();
	}

	static void Shutdown()
	{
		DX12LogManager::Text("*** 3DMigoto DX12 runtime shutting down ***\n");
		DX12NativeOverlayShutdown();
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12WaitHooks::Shutdown();
#endif
		DXGIHooks::Shutdown();
		DX12CommandListHookState::ReleaseKnownCommandQueue();
		DX12HookRegistry::UnhookAll();
		DX12LogManager::Close();
		DX12LogUtils::Close();
		LoadRealD3D12 = nullptr;
		Initialized = false;
	}
};

inline void DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode mode)
{
	const LONG value = mode == DX12RuntimeMode::Hunting ?
		static_cast<LONG>(DX12RuntimeMode::Hunting) :
		static_cast<LONG>(DX12RuntimeMode::Mod);
	const LONG old = InterlockedExchange(&runtimeMode, value);
	if (old != value) {
		DX12LogManager::Fields("DX12RuntimeMode",
			"\"mode\":\"%s\"",
			mode == DX12RuntimeMode::Hunting ? "Hunting" : "Mod");
	}
}



enum class DX12UiActionBits : LONG
{
	HuntToggle = 1 << 0,
	HuntPreviousVS = 1 << 1,
	HuntNextVS = 1 << 2,
	HuntPreviousPS = 1 << 3,
	HuntNextPS = 1 << 4,
	HuntPreviousCS = 1 << 5,
	HuntNextCS = 1 << 6,
	HuntPreviousIB = 1 << 7,
	HuntNextIB = 1 << 8,
	HuntPreviousVB = 1 << 9,
	HuntNextVB = 1 << 10,
	HuntCopy = 1 << 11,
	HuntReset = 1 << 12,
	FrameAnalysis = 1 << 13,
	ShaderDump = 1 << 14,
	ModReload = 1 << 15,
	Profiling = 1 << 16,
};

class DX12UiActions
{
public:
	inline static volatile LONG PendingActions = 0;

	static void Request(DX12UiActionBits action)
	{
		InterlockedOr(&PendingActions, static_cast<LONG>(action));
	}

	static void RequestHuntToggle() { Request(DX12UiActionBits::HuntToggle); }
	static void RequestHuntPreviousVS() { Request(DX12UiActionBits::HuntPreviousVS); }
	static void RequestHuntNextVS() { Request(DX12UiActionBits::HuntNextVS); }
	static void RequestHuntPreviousPS() { Request(DX12UiActionBits::HuntPreviousPS); }
	static void RequestHuntNextPS() { Request(DX12UiActionBits::HuntNextPS); }
	static void RequestHuntPreviousCS() { Request(DX12UiActionBits::HuntPreviousCS); }
	static void RequestHuntNextCS() { Request(DX12UiActionBits::HuntNextCS); }
	static void RequestHuntPreviousIB() { Request(DX12UiActionBits::HuntPreviousIB); }
	static void RequestHuntNextIB() { Request(DX12UiActionBits::HuntNextIB); }
	static void RequestHuntPreviousVB() { Request(DX12UiActionBits::HuntPreviousVB); }
	static void RequestHuntNextVB() { Request(DX12UiActionBits::HuntNextVB); }
	static void RequestHuntCopySelectedHash() { Request(DX12UiActionBits::HuntCopy); }
	static void RequestHuntResetSelection() { Request(DX12UiActionBits::HuntReset); }
	static void RequestFrameAnalysis() { Request(DX12UiActionBits::FrameAnalysis); }
	static void RequestShaderDump() { Request(DX12UiActionBits::ShaderDump); }
	// The overlay runs on its own Win32 thread. Reloading changes the live
	// command/descriptor state, so defer it to the render thread's Present
	// boundary just like every other UI action.
	static void RequestModReload() { Request(DX12UiActionBits::ModReload); }
	static void RequestProfilingToggle() { Request(DX12UiActionBits::Profiling); }

	static bool Consume()
	{
		const LONG actions = InterlockedExchange(&PendingActions, 0);
		if (!actions)
			return false;

		const bool huntToggleRequested = HasAction(actions, DX12UiActionBits::HuntToggle);

		if (huntToggleRequested)
			DX12HuntToggle();
		if (!huntToggleRequested && DX12HuntIsEnabled()) {
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVS))
				DX12HuntPreviousVS();
			if (HasAction(actions, DX12UiActionBits::HuntNextVS))
				DX12HuntNextVS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousPS))
				DX12HuntPreviousPS();
			if (HasAction(actions, DX12UiActionBits::HuntNextPS))
				DX12HuntNextPS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousCS))
				DX12HuntPreviousCS();
			if (HasAction(actions, DX12UiActionBits::HuntNextCS))
				DX12HuntNextCS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousIB))
				DX12HuntPreviousIB();
			if (HasAction(actions, DX12UiActionBits::HuntNextIB))
				DX12HuntNextIB();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVB))
				DX12HuntPreviousVB();
			if (HasAction(actions, DX12UiActionBits::HuntNextVB))
				DX12HuntNextVB();
			if (HasAction(actions, DX12UiActionBits::HuntCopy))
				DX12HuntCopySelectedHash();
			if (HasAction(actions, DX12UiActionBits::HuntReset))
				DX12HuntResetSelection();
		}
		if (HasAction(actions, DX12UiActionBits::FrameAnalysis))
			RunFrameAnalysisRequest();
		if (HasAction(actions, DX12UiActionBits::ShaderDump))
			DX12RequestShaderDump();
		if (HasAction(actions, DX12UiActionBits::ModReload))
			DX12Input::RequestModReload();
		// Profiling stays enabled and is no longer toggled by a hotkey.
		return true;
	}

	static bool HasAction(LONG actions, DX12UiActionBits action)
	{
		return (actions & static_cast<LONG>(action)) != 0;
	}

	static void RunFrameAnalysisRequest()
	{
		DX12LogManager::Fields("DX12UiAction", "\"action\":\"FrameAnalysisRequest\"");
		if (!DX12RuntimeState::IsHuntingMode()) {
			DX12LogManager::Fields("FrameAnalysisBeginFailed", "\"reason\":\"requires_hunting_mode\"");
			DX12OverlayStatusState::SetWarning(L"Enable Hunting before Frame Dump");
			return;
		}
		if (DX12FrameAnalysis::Begin()) {
			DX12FrameAnalysis::LogJsonFunc("FrameAnalysisArmed", nullptr);
			DX12FrameAnalysis::RequestCapture();
			DX12OverlayStatusState::SetStatus(L"Frame analysis armed");
		} else {
			DX12LogManager::Fields("FrameAnalysisBeginFailed", "\"reason\":\"create_directory_failed\"");
			DX12OverlayStatusState::SetError(L"Frame analysis failed: cannot create directory");
		}
	}
};
