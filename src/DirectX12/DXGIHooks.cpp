#include "DXGIHooks.h"

#include <dxgi1_4.h>

#include "DX12BindingTracker.h"
#include "DX12CommandListRuntime.h"
#include "DX12DeviceHooks.h"
#include "DX12FrameAnalysis.h"
#include "DX12HookManager.h"
#include "DX12Input.h"
#include "DX12ModRuntime.h"
#include "DX12Overlay.h"
#include "DX12Profiling.h"
#include "DX12ShaderHunt.h"
#include "DX12ShaderDump.h"
#include "DX12State.h"

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
static PFN_CREATE_DXGI_FACTORY gOrigCreateDXGIFactory = nullptr;
static PFN_CREATE_DXGI_FACTORY gOrigCreateDXGIFactory1 = nullptr;
static PFN_CREATE_DXGI_FACTORY2 gOrigCreateDXGIFactory2 = nullptr;

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

	if (fallback)
		return fallback;

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

static void LogPresentStage(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr = S_OK)
{
	// Early-present crashes often leave only buffered logs behind. Flush these
	// low-volume stage markers so the next run shows the exact boundary reached.
	DX12LogDebugJsonFuncFlush("DX12PresentStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
		api ? api : "", stage ? stage : "", DX12GetPresentCount(), swapChain, hr);
}

static void HookSwapChain(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return;

	DX12VTableHook swapChainHooks[] = {
		{8, reinterpret_cast<void**>(&gOrigPresent), HookedPresent, "IDXGISwapChain::Present"},
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
	HookDeviceFromDXGIArgument(device);
	auto original = GetCreateSwapChainOriginal(factory);
	HRESULT hr = original ? original(factory, device, desc, swapChain) : DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGIFactory::CreateSwapChain",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		HookSwapChain(*swapChain);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(
	IDXGIFactory2 *factory, IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	HookDeviceFromDXGIArgument(device);
	auto original = GetCreateSwapChainForHwndOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, fullscreenDesc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogJsonFunc("IDXGIFactory2::CreateSwapChainForHwnd",
		"\"device\":\"%p\",\"hwnd\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, window, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		HookSwapChain(*swapChain);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForCoreWindow(
	IDXGIFactory2 *factory, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
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
	DX12_PROFILE_SCOPE(Present);
	DX12PollInput();
	DX12Profiling::BeginFrame();
	const bool dumpFrame = DX12FrameAnalysisEndCapture();
	const bool dumpShaders = DX12ShaderDumpEndCapture();
	PFN_PRESENT original = GetPresentOriginal(swapChain);
	if (!original)
		return DXGI_ERROR_INVALID_CALL;
	if (DX12HuntShouldDrawOverlay())
		DX12UpdateOverlayWindowForSwapChain(swapChain);
	LogPresentStage("Present", "beforeOriginal", swapChain);
	HRESULT hr = original(swapChain, syncInterval, flags);
	LogPresentStage("Present", "afterOriginal", swapChain, hr);
	DX12IncrementPresentCount();
	DX12FlushLog();
	LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
	DX12ModBeginFrame();

	// Recalculate the hot-path skip-all flag once per frame.  When set, every
	// recording hook (Draw, Dispatch, SetPipelineState, SetRoot*, IASet*, etc.)
	// skips ALL tracking work and calls the original directly with a single
	// branch.  This collapses the common "inject but idle" path to near-zero
	// overhead.
	DX12HotPathUpdate();
	LogPresentStage("Present", "afterHotPathUpdate", swapChain, hr);

	DX12CommandListRuntimeBumpTrackingGeneration();

	if (dumpFrame) {
		DX12FrameAnalysisLogJsonFunc("FrameCaptureEnd",
			"\"present\":%ld", DX12GetPresentCount());
		DX12DumpFrameAnalysis();
		DX12FrameAnalysisEnd();
	} else if (dumpShaders) {
		DX12DumpCapturedFrameShaders();
	} else if (DX12FrameAnalysisIsCaptureRequested()) {
		DX12BindingBeginFrame();
		DX12FrameAnalysisBeginCapture();
		DX12FrameAnalysisLogJsonFunc("FrameCaptureBegin",
			"\"present\":%ld", DX12GetPresentCount());
	} else if (DX12ShaderDumpIsCaptureRequested()) {
		DX12BindingBeginFrame();
		DX12ShaderDumpBeginCapture();
	} else if (DX12FrameAnalysisIsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
	           DX12ModHasActiveTextureOverrides()) {
		// Binding tracking is needed by an active subsystem — reset per-frame
		// state.  When nothing is active we skip this entirely so the binding
		// tracker stays idle and does not consume CPU every frame.
		DX12BindingBeginFrame();
		LogPresentStage("Present", "afterBindingBeginFrame", swapChain, hr);
	}
	// else: no capture, no hunt, no texture overrides — skip binding frame
	//       reset to save CPU on the Present path.

	// 增量更新 visited 列表：移除帧内未见过的哈希，添加帧内新出现的哈希
	DX12HuntEndFrame();

	DX12Profiling::EndFrame();
	LogPresentStage("Present", "end", swapChain, hr);
	return hr;
}

static HRESULT STDMETHODCALLTYPE HookedPresent1(
	IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS *presentParameters)
{
	DX12PollInput();
	DX12Profiling::BeginFrame();
	const bool dumpFrame = DX12FrameAnalysisEndCapture();
	const bool dumpShaders = DX12ShaderDumpEndCapture();
	PFN_PRESENT1 original = GetPresent1Original(swapChain);
	if (!original)
		return DXGI_ERROR_INVALID_CALL;
	if (DX12HuntShouldDrawOverlay())
		DX12UpdateOverlayWindowForSwapChain(swapChain);
	LogPresentStage("Present1", "beforeOriginal", swapChain);
	HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
	LogPresentStage("Present1", "afterOriginal", swapChain, hr);
	DX12IncrementPresentCount();
	DX12FlushLog();
	LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
	DX12ModBeginFrame();

	// Recalculate the hot-path skip-all flag once per frame.
	DX12HotPathUpdate();
	LogPresentStage("Present1", "afterHotPathUpdate", swapChain, hr);

	// Bump the global tracking generation so every command list's cached
	// tracking predicates are invalidated once per frame.
	DX12CommandListRuntimeBumpTrackingGeneration();

	if (dumpFrame) {
		DX12FrameAnalysisLogJsonFunc("FrameCaptureEnd",
			"\"present\":%ld", DX12GetPresentCount());
		DX12DumpFrameAnalysis();
		DX12FrameAnalysisEnd();
	} else if (dumpShaders) {
		DX12DumpCapturedFrameShaders();
	} else if (DX12FrameAnalysisIsCaptureRequested()) {
		DX12BindingBeginFrame();
		DX12FrameAnalysisBeginCapture();
		DX12FrameAnalysisLogJsonFunc("FrameCaptureBegin",
			"\"present\":%ld", DX12GetPresentCount());
	} else if (DX12ShaderDumpIsCaptureRequested()) {
		DX12BindingBeginFrame();
		DX12ShaderDumpBeginCapture();
	} else if (DX12FrameAnalysisIsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
	           DX12ModHasActiveTextureOverrides()) {
		// Binding tracking is needed by an active subsystem — reset per-frame
		// state.  When nothing is active we skip this entirely so the binding
		// tracker stays idle and does not consume CPU every frame.
		DX12BindingBeginFrame();
		LogPresentStage("Present1", "afterBindingBeginFrame", swapChain, hr);
	}
	// else: no capture, no hunt, no texture overrides — skip binding frame
	//       reset to save CPU on the Present path.

	// 增量更新 visited 列表：移除帧内未见过的哈希，添加帧内新出现的哈希
	DX12HuntEndFrame();

	DX12Profiling::EndFrame();
	LogPresentStage("Present1", "end", swapChain, hr);
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
