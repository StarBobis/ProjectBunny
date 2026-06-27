#include "DXGIHooks.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include "DX12BindingTracker.h"
#include "DX12CommandListHooks.h"
#include "DX12CommandListRuntime.h"
#include "DX12DeviceHooks.h"
#include "DX12FrameAnalysis.h"
#include "DX12HookManager.h"
#include "DX12NativeOverlay.h"
#include "DX12ModRuntime.h"
#include "DX12Profiling.h"
#include "DX12ShaderHunt.h"
#include "DX12ShaderDump.h"
#include "DX12State.h"
#include "DX12UiActions.h"

typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_SWAP_CHAIN)(
	IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_SWAP_CHAIN_FOR_HWND)(
	IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_SWAP_CHAIN_FOR_CORE_WINDOW)(
	IDXGIFactory2*, IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
	IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_CREATE_SWAP_CHAIN_FOR_COMPOSITION)(
	IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE *PFN_PRESENT)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *PFN_PRESENT1)(
	IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE *PFN_RESIZE_BUFFERS)(
	IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID, void**);
typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT, REFIID, void**);

static bool gDXGIHookAttempted = false;
static SRWLOCK gDXGIHookLock = SRWLOCK_INIT;
static PFN_CREATE_SWAP_CHAIN gOrigCreateSwapChain = nullptr;
static PFN_CREATE_SWAP_CHAIN_FOR_HWND gOrigCreateSwapChainForHwnd = nullptr;
static PFN_CREATE_SWAP_CHAIN_FOR_CORE_WINDOW gOrigCreateSwapChainForCoreWindow = nullptr;
static PFN_CREATE_SWAP_CHAIN_FOR_COMPOSITION gOrigCreateSwapChainForComposition = nullptr;
static PFN_PRESENT gOrigPresent = nullptr;
static PFN_PRESENT1 gOrigPresent1 = nullptr;
static PFN_RESIZE_BUFFERS gOrigResizeBuffers = nullptr;
static PFN_CREATE_DXGI_FACTORY gOrigCreateDXGIFactory = nullptr;
static PFN_CREATE_DXGI_FACTORY gOrigCreateDXGIFactory1 = nullptr;
static PFN_CREATE_DXGI_FACTORY2 gOrigCreateDXGIFactory2 = nullptr;
static SRWLOCK gKnownSwapChainLock = SRWLOCK_INIT;
static IDXGISwapChain *gKnownSwapChain = nullptr;

static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory);
static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory);
static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
	IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapChain);
static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
	IDXGIFactory2 *factory, IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output, IDXGISwapChain1 **swapChain);
static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForCoreWindow(
	IDXGIFactory2 *factory, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain);
static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(
	IDXGIFactory2 *factory, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain);
static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags);
static HRESULT STDMETHODCALLTYPE HookedPresent1(
	IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS *presentParameters);
static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
	IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height,
	DXGI_FORMAT newFormat, UINT swapChainFlags);

static void LogDXGIHookCall(const char *api, const void *object)
{
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12HookCall",
		"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object);
}

static void LogDXGIOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
{
	if (!DX12ShouldLogHookCall(api))
		return;
	DX12LogDebugJsonFunc("DX12FallbackPath",
		"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
		"\"slot\":%u,\"fallback\":\"%p\"",
		api ? api : "", DX12GetPresentCount(), object, slot, fallback);
}

template <typename T>
static T GetDXGIOriginal(void *object, UINT slot, T fallback, const char *name)
{
	if (object) {
		void **vtable = *reinterpret_cast<void***>(object);
		if (vtable) {
			void *target = vtable[slot];
			void *original = DX12GetOriginalFunction(target);
			if (original)
				return reinterpret_cast<T>(original);
		}
	}

	if (fallback) {
		LogDXGIOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
		return fallback;
	}

	DX12LogJsonFunc(name ? name : "DXGI::Unknown",
		"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
		object, slot);
	return nullptr;
}

static PFN_PRESENT GetPresentOriginal(IDXGISwapChain *swapChain)
{
	return GetDXGIOriginal<PFN_PRESENT>(
		swapChain, 8, gOrigPresent, "IDXGISwapChain::Present");
}

static PFN_PRESENT1 GetPresent1Original(IDXGISwapChain1 *swapChain)
{
	return GetDXGIOriginal<PFN_PRESENT1>(
		swapChain, 22, gOrigPresent1, "IDXGISwapChain1::Present1");
}

static PFN_RESIZE_BUFFERS GetResizeBuffersOriginal(IDXGISwapChain *swapChain)
{
	return GetDXGIOriginal<PFN_RESIZE_BUFFERS>(
		swapChain, 13, gOrigResizeBuffers, "IDXGISwapChain::ResizeBuffers");
}

static void LogPresentStage(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr = S_OK)
{
	if (!DX12VerboseDiagnosticsEnabled())
		return;
	DX12LogDebugJsonFuncFlush("DX12PresentStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
		api ? api : "", stage ? stage : "", DX12GetPresentCount(), swapChain, hr);
}

static void LogPresentDeviceRemovedReason(const char *api, IDXGISwapChain *swapChain, HRESULT presentHr)
{
	if (SUCCEEDED(presentHr) || !swapChain)
		return;

	ID3D12Device *device = nullptr;
	HRESULT queryHr = swapChain->GetDevice(IID_PPV_ARGS(&device));
	if (FAILED(queryHr) || !device) {
		DX12LogDebugJsonFuncFlush("DX12DeviceRemoved",
			"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"queryHr\":\"0x%lx\"",
			api ? api : "", DX12GetPresentCount(), swapChain, presentHr, queryHr);
		return;
	}

	const HRESULT reason = device->GetDeviceRemovedReason();
	DX12LogDebugJsonFuncFlush("DX12DeviceRemoved",
		"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"reason\":\"0x%lx\"",
		api ? api : "", DX12GetPresentCount(), swapChain, presentHr, reason);
	device->Release();
}

static void RunPostPresentUi(IDXGISwapChain *swapChain)
{
	DX12NativeOverlayRender(swapChain);
}

static void ConsumeUiActionsForFrame()
{
	if (!DX12UiConsumeActions())
		return;
	DX12HotPathUpdate();
	DX12CommandListRuntimeBumpTrackingGeneration();
}

static void BeginFrameAnalysisCapture(LONG present)
{
	DX12BindingBeginFrame();
	DX12FrameAnalysisBeginCapture();
	DX12CommandListRuntimeBumpTrackingGeneration();
	DX12FrameAnalysisLogJsonFunc("FrameCaptureBegin", "\"present\":%ld", present);
}

static void BeginShaderDumpCapture()
{
	DX12BindingBeginFrame();
	DX12ShaderDumpBeginCapture();
	DX12CommandListRuntimeBumpTrackingGeneration();
}

static void HookSwapChain(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return;
	AcquireSRWLockExclusive(&gKnownSwapChainLock);
	gKnownSwapChain = swapChain;
	ReleaseSRWLockExclusive(&gKnownSwapChainLock);

	DX12VTableHook swapChainHooks[] = {
		{8, reinterpret_cast<void**>(&gOrigPresent), HookedPresent, "IDXGISwapChain::Present"},
		{13, reinterpret_cast<void**>(&gOrigResizeBuffers), HookedResizeBuffers, "IDXGISwapChain::ResizeBuffers"},
	};
	DX12InstallVTableHooks(swapChain, swapChainHooks);

	IDXGISwapChain1 *swapChain1 = nullptr;
	if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
		DX12VTableHook swapChain1Hooks[] = {
			{22, reinterpret_cast<void**>(&gOrigPresent1), HookedPresent1, "IDXGISwapChain1::Present1"},
		};
		DX12InstallVTableHooks(swapChain1, swapChain1Hooks);
		swapChain1->Release();
	}
}

void DX12ActivateHotPathHooks()
{
	IDXGISwapChain *swapChain = nullptr;
	AcquireSRWLockShared(&gKnownSwapChainLock);
	swapChain = gKnownSwapChain;
	ReleaseSRWLockShared(&gKnownSwapChainLock);
	if (swapChain)
		HookSwapChain(swapChain);
	DX12ActivateCommandQueueHotPathHooks();
}

static void HookDeviceFromDXGIArgument(IUnknown *device)
{
	DX12HookDevice(device);
	DX12HookDeviceFromCommandQueue(device);
}

static void HookFactory(IUnknown *factory)
{
	if (!factory)
		return;

	IDXGIFactory *factory0 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory0)))) {
		DX12VTableHook factoryHooks[] = {
			{10, reinterpret_cast<void**>(&gOrigCreateSwapChain), HookedCreateSwapChain,
				"IDXGIFactory::CreateSwapChain"},
		};
		DX12InstallVTableHooks(factory0, factoryHooks);
		factory0->Release();
	}

	IDXGIFactory2 *factory2 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2)))) {
		DX12VTableHook factory2Hooks[] = {
			{15, reinterpret_cast<void**>(&gOrigCreateSwapChainForHwnd),
				HookedCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd"},
			{16, reinterpret_cast<void**>(&gOrigCreateSwapChainForCoreWindow),
				HookedCreateSwapChainForCoreWindow, "IDXGIFactory2::CreateSwapChainForCoreWindow"},
			{24, reinterpret_cast<void**>(&gOrigCreateSwapChainForComposition),
				HookedCreateSwapChainForComposition, "IDXGIFactory2::CreateSwapChainForComposition"},
		};
		DX12InstallVTableHooks(factory2, factory2Hooks);
		factory2->Release();
	}
}

static PFN_CREATE_SWAP_CHAIN GetCreateSwapChainOriginal(IDXGIFactory *factory)
{
	return GetDXGIOriginal<PFN_CREATE_SWAP_CHAIN>(
		factory, 10, gOrigCreateSwapChain, "IDXGIFactory::CreateSwapChain");
}

static PFN_CREATE_SWAP_CHAIN_FOR_HWND GetCreateSwapChainForHwndOriginal(IDXGIFactory2 *factory)
{
	return GetDXGIOriginal<PFN_CREATE_SWAP_CHAIN_FOR_HWND>(
		factory, 15, gOrigCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
}

static PFN_CREATE_SWAP_CHAIN_FOR_CORE_WINDOW GetCreateSwapChainForCoreWindowOriginal(IDXGIFactory2 *factory)
{
	return GetDXGIOriginal<PFN_CREATE_SWAP_CHAIN_FOR_CORE_WINDOW>(
		factory, 16, gOrigCreateSwapChainForCoreWindow,
		"IDXGIFactory2::CreateSwapChainForCoreWindow");
}

static PFN_CREATE_SWAP_CHAIN_FOR_COMPOSITION GetCreateSwapChainForCompositionOriginal(IDXGIFactory2 *factory)
{
	return GetDXGIOriginal<PFN_CREATE_SWAP_CHAIN_FOR_COMPOSITION>(
		factory, 24, gOrigCreateSwapChainForComposition,
		"IDXGIFactory2::CreateSwapChainForComposition");
}

static void HookDXGIFactoryVTables(HMODULE dxgi)
{
	auto createFactory = reinterpret_cast<PFN_CREATE_DXGI_FACTORY>(
		GetProcAddress(dxgi, "CreateDXGIFactory"));
	auto createFactory1 = reinterpret_cast<PFN_CREATE_DXGI_FACTORY>(
		GetProcAddress(dxgi, "CreateDXGIFactory1"));
	auto createFactory2 = reinterpret_cast<PFN_CREATE_DXGI_FACTORY2>(
		GetProcAddress(dxgi, "CreateDXGIFactory2"));

	IDXGIFactory *factory = nullptr;
	if (createFactory1 && SUCCEEDED(createFactory1(IID_PPV_ARGS(&factory)))) {
		HookFactory(factory);
		factory->Release();
		return;
	}

	if (createFactory && SUCCEEDED(createFactory(IID_PPV_ARGS(&factory)))) {
		HookFactory(factory);
		factory->Release();
	}

	IDXGIFactory2 *factory2 = nullptr;
	if (createFactory2 && SUCCEEDED(createFactory2(0, IID_PPV_ARGS(&factory2)))) {
		HookFactory(factory2);
		factory2->Release();
	}
}

static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory)
{
	LogDXGIHookCall("CreateDXGIFactory", nullptr);
	HRESULT hr = gOrigCreateDXGIFactory(riid, factory);
	DX12LogJsonFunc("CreateDXGIFactory",
		"\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory)
{
	LogDXGIHookCall("CreateDXGIFactory1", nullptr);
	HRESULT hr = gOrigCreateDXGIFactory1(riid, factory);
	DX12LogJsonFunc("CreateDXGIFactory1",
		"\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory)
{
	LogDXGIHookCall("CreateDXGIFactory2", nullptr);
	HRESULT hr = gOrigCreateDXGIFactory2(flags, riid, factory);
	DX12LogJsonFunc("CreateDXGIFactory2",
		"\"flags\":\"0x%x\",\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		flags, &riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(
	IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapChain)
{
	LogDXGIHookCall("IDXGIFactory::CreateSwapChain", factory);
	HookDeviceFromDXGIArgument(device);
	auto original = GetCreateSwapChainOriginal(factory);
	HRESULT hr = original ? original(factory, device, desc, swapChain) : DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGIFactory::CreateSwapChain",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain && (!desc || !DX12NativeOverlayIsOverlayWindow(desc->OutputWindow)))
		HookSwapChain(*swapChain);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
	IDXGIFactory2 *factory, IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	LogDXGIHookCall("IDXGIFactory2::CreateSwapChainForHwnd", factory);
	HookDeviceFromDXGIArgument(device);
	auto original = GetCreateSwapChainForHwndOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, fullscreenDesc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGIFactory2::CreateSwapChainForHwnd",
		"\"device\":\"%p\",\"hwnd\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, window, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain && !DX12NativeOverlayIsOverlayWindow(window))
		HookSwapChain(*swapChain);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForCoreWindow(
	IDXGIFactory2 *factory, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	LogDXGIHookCall("IDXGIFactory2::CreateSwapChainForCoreWindow", factory);
	HookDeviceFromDXGIArgument(device);
	auto original = GetCreateSwapChainForCoreWindowOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGIFactory2::CreateSwapChainForCoreWindow",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		HookSwapChain(*swapChain);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForComposition(
	IDXGIFactory2 *factory, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	LogDXGIHookCall("IDXGIFactory2::CreateSwapChainForComposition", factory);
	HookDeviceFromDXGIArgument(device);
	auto original = GetCreateSwapChainForCompositionOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, desc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGIFactory2::CreateSwapChainForComposition",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		HookSwapChain(*swapChain);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
{
	if (DX12IsInternalReplay()) {
		PFN_PRESENT original = GetPresentOriginal(swapChain);
		return original ? original(swapChain, syncInterval, flags) : DXGI_ERROR_INVALID_CALL;
	}

	ConsumeUiActionsForFrame();

	static volatile LONG firstPresentLog = 0;
	if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
		DX12LogJsonFuncFlush("DX12PresentFirst",
			"\"api\":\"Present\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
			swapChain, syncInterval, flags, gDX12HotPathSkipAll);

	if (gDX12HotPathSkipAll) {
		PFN_PRESENT original = GetPresentOriginal(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		if (InterlockedCompareExchange(&firstPresentLog, 2, 1) == 1)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"before_original_skip\",\"swapchain\":\"%p\"",
				swapChain);
		HRESULT hr = original(swapChain, syncInterval, flags);
		if (InterlockedCompareExchange(&firstPresentLog, 3, 2) == 2)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"after_original_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12IncrementPresentCount();
		DX12HuntOnPresent();
		if (DX12GetPresentCount() > 2)
			RunPostPresentUi(swapChain);
		if (InterlockedCompareExchange(&firstPresentLog, 4, 3) == 3)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"before_hotpath_update_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12HotPathUpdate();
		if (InterlockedCompareExchange(&firstPresentLog, 5, 4) == 4)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"return_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		return hr;
	}

	LogDXGIHookCall("IDXGISwapChain::Present", swapChain);
	DX12_PROFILE_SCOPE(Present);

	{
		const LONG draws = InterlockedExchange(&gPerFrameTotalDraws, 0);
		const LONG dispatches = InterlockedExchange(&gPerFrameTotalDispatches, 0);
		if (DX12VerboseDiagnosticsEnabled()) {
			DX12LogJsonFunc("DX12PerFrameCounters",
				"\"draws\":%ld,\"dispatches\":%ld", draws, dispatches);
		}
	}

	DX12Profiling::BeginFrame();
	const bool dumpFrame = DX12FrameAnalysisEndCapture();
	const bool dumpShaders = DX12ShaderDumpEndCapture();
	PFN_PRESENT original = GetPresentOriginal(swapChain);
	if (!original)
		return DXGI_ERROR_INVALID_CALL;
	LogPresentStage("Present", "beforeOriginal", swapChain);
	HRESULT hr = original(swapChain, syncInterval, flags);
	LogPresentStage("Present", "afterOriginal", swapChain, hr);
	LogPresentDeviceRemovedReason("Present", swapChain, hr);
	DX12IncrementPresentCount();
	DX12HuntOnPresent();
	DX12CommandListRuntimeBumpTrackingGeneration();
	RunPostPresentUi(swapChain);

	LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
	DX12ModBeginFrame();

	DX12HotPathUpdate();
	LogPresentStage("Present", "afterHotPathUpdate", swapChain, hr);

	if (dumpFrame) {
		DX12FrameAnalysisLogJsonFunc("FrameCaptureEnd",
			"\"present\":%ld", DX12GetPresentCount());
		DX12DumpFrameAnalysis();
		DX12FrameAnalysisEnd();
	} else if (dumpShaders) {
		DX12DumpCapturedFrameShaders();
	} else if (DX12FrameAnalysisIsCaptureRequested()) {
		BeginFrameAnalysisCapture(DX12GetPresentCount());
	} else if (DX12ShaderDumpIsCaptureRequested()) {
		BeginShaderDumpCapture();
	} else if (DX12FrameAnalysisIsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
	           DX12ModNeedsPreSkinningUavProbe()) {
		DX12BindingBeginFrame();
		LogPresentStage("Present", "afterBindingBeginFrame", swapChain, hr);
	}
	DX12Profiling::EndFrame();
	LogPresentStage("Present", "end", swapChain, hr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedPresent1(
	IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS *presentParameters)
{
	if (DX12IsInternalReplay()) {
		PFN_PRESENT1 original = GetPresent1Original(swapChain);
		return original ? original(swapChain, syncInterval, flags, presentParameters) : DXGI_ERROR_INVALID_CALL;
	}

	ConsumeUiActionsForFrame();

	static volatile LONG firstPresent1Log = 0;
	if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
		DX12LogJsonFuncFlush("DX12PresentFirst",
			"\"api\":\"Present1\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
			swapChain, syncInterval, flags, gDX12HotPathSkipAll);

	if (gDX12HotPathSkipAll) {
		PFN_PRESENT1 original = GetPresent1Original(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		if (InterlockedCompareExchange(&firstPresent1Log, 2, 1) == 1)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"before_original_skip\",\"swapchain\":\"%p\"",
				swapChain);
		HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
		if (InterlockedCompareExchange(&firstPresent1Log, 3, 2) == 2)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"after_original_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12IncrementPresentCount();
		DX12HuntOnPresent();
		if (DX12GetPresentCount() > 2)
			RunPostPresentUi(swapChain);
		if (InterlockedCompareExchange(&firstPresent1Log, 4, 3) == 3)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"before_hotpath_update_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12HotPathUpdate();
		if (InterlockedCompareExchange(&firstPresent1Log, 5, 4) == 4)
			DX12LogJsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"return_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		return hr;
	}

	LogDXGIHookCall("IDXGISwapChain1::Present1", swapChain);

	{
		const LONG draws = InterlockedExchange(&gPerFrameTotalDraws, 0);
		const LONG dispatches = InterlockedExchange(&gPerFrameTotalDispatches, 0);
		if (DX12VerboseDiagnosticsEnabled()) {
			DX12LogJsonFunc("DX12PerFrameCounters",
				"\"draws\":%ld,\"dispatches\":%ld", draws, dispatches);
		}
	}

	DX12Profiling::BeginFrame();
	const bool dumpFrame = DX12FrameAnalysisEndCapture();
	const bool dumpShaders = DX12ShaderDumpEndCapture();
	PFN_PRESENT1 original = GetPresent1Original(swapChain);
	if (!original)
		return DXGI_ERROR_INVALID_CALL;
	LogPresentStage("Present1", "beforeOriginal", swapChain);
	HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
	LogPresentStage("Present1", "afterOriginal", swapChain, hr);
	LogPresentDeviceRemovedReason("Present1", swapChain, hr);
	DX12IncrementPresentCount();
	DX12HuntOnPresent();
	DX12CommandListRuntimeBumpTrackingGeneration();
	RunPostPresentUi(swapChain);

	LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
	DX12ModBeginFrame();

	DX12HotPathUpdate();
	LogPresentStage("Present1", "afterHotPathUpdate", swapChain, hr);

	if (dumpFrame) {
		DX12FrameAnalysisLogJsonFunc("FrameCaptureEnd",
			"\"present\":%ld", DX12GetPresentCount());
		DX12DumpFrameAnalysis();
		DX12FrameAnalysisEnd();
	} else if (dumpShaders) {
		DX12DumpCapturedFrameShaders();
	} else if (DX12FrameAnalysisIsCaptureRequested()) {
		BeginFrameAnalysisCapture(DX12GetPresentCount());
	} else if (DX12ShaderDumpIsCaptureRequested()) {
		BeginShaderDumpCapture();
	} else if (DX12FrameAnalysisIsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
	           DX12ModNeedsPreSkinningUavProbe()) {
		DX12BindingBeginFrame();
		LogPresentStage("Present1", "afterBindingBeginFrame", swapChain, hr);
	}
	DX12Profiling::EndFrame();
	LogPresentStage("Present1", "end", swapChain, hr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
	IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height,
	DXGI_FORMAT newFormat, UINT swapChainFlags)
{
	LogDXGIHookCall("IDXGISwapChain::ResizeBuffers", swapChain);
	if (!DX12IsInternalReplay())
		DX12NativeOverlayInvalidate(swapChain);
	auto original = GetResizeBuffersOriginal(swapChain);
	HRESULT hr = original ? original(swapChain, bufferCount, width, height, newFormat, swapChainFlags) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGISwapChain::ResizeBuffers",
		"\"swapchain\":\"%p\",\"buffers\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":\"0x%x\",\"hr\":\"0x%lx\"",
		swapChain, bufferCount, width, height, static_cast<UINT>(newFormat), swapChainFlags, hr);
	return hr;
}

void DX12InstallDXGIHooks()
{
	AcquireSRWLockExclusive(&gDXGIHookLock);
	if (gDXGIHookAttempted) {
		ReleaseSRWLockExclusive(&gDXGIHookLock);
		return;
	}
	gDXGIHookAttempted = true;
	ReleaseSRWLockExclusive(&gDXGIHookLock);

	HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
	if (!dxgi) {
		static volatile LONG deferredLogs = 0;
		if (InterlockedIncrement(&deferredLogs) <= 4)
			DX12Log("dxgi.dll is not loaded yet; DXGI hook deferred\n");
		AcquireSRWLockExclusive(&gDXGIHookLock);
		gDXGIHookAttempted = false;
		ReleaseSRWLockExclusive(&gDXGIHookLock);
		return;
	}

	HookDXGIFactoryVTables(dxgi);
	DX12InstallExportHook(dxgi, "CreateDXGIFactory",
		reinterpret_cast<void**>(&gOrigCreateDXGIFactory), HookedCreateDXGIFactory);
	DX12InstallExportHook(dxgi, "CreateDXGIFactory1",
		reinterpret_cast<void**>(&gOrigCreateDXGIFactory1), HookedCreateDXGIFactory1);
	DX12InstallExportHook(dxgi, "CreateDXGIFactory2",
		reinterpret_cast<void**>(&gOrigCreateDXGIFactory2), HookedCreateDXGIFactory2);
}
