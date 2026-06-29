#include "DXGIHooks.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>

#include "DX12BindingTracker.hpp"
#include "DX12CommandListHooks.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12DeviceHooks.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12HookManager.hpp"
#include "DX12NativeOverlay.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12HotPathState.hpp"
#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12UiActions.hpp"

class DXGIHookSignatures
{
public:
	using CreateSwapChain = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	using CreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
	using CreateSwapChainForCoreWindow = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory2*, IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
		IDXGIOutput*, IDXGISwapChain1**);
	using CreateSwapChainForComposition = HRESULT(STDMETHODCALLTYPE *)(
		IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
	using Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain*, UINT, UINT);
	using Present1 = HRESULT(STDMETHODCALLTYPE *)(
		IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	using ResizeBuffers = HRESULT(STDMETHODCALLTYPE *)(
		IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	using CreateDXGIFactory = HRESULT(WINAPI *)(REFIID, void**);
	using CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT, REFIID, void**);
};

class DXGIHookState
{
public:
	inline static bool HookAttempted = false;
	inline static SRWLOCK HookLock = SRWLOCK_INIT;
	inline static DXGIHookSignatures::CreateSwapChain OrigCreateSwapChain = nullptr;
	inline static DXGIHookSignatures::CreateSwapChainForHwnd OrigCreateSwapChainForHwnd = nullptr;
	inline static DXGIHookSignatures::CreateSwapChainForCoreWindow OrigCreateSwapChainForCoreWindow = nullptr;
	inline static DXGIHookSignatures::CreateSwapChainForComposition OrigCreateSwapChainForComposition = nullptr;
	inline static DXGIHookSignatures::Present OrigPresent = nullptr;
	inline static DXGIHookSignatures::Present1 OrigPresent1 = nullptr;
	inline static DXGIHookSignatures::ResizeBuffers OrigResizeBuffers = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory1 = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory2 OrigCreateDXGIFactory2 = nullptr;
	inline static SRWLOCK KnownSwapChainLock = SRWLOCK_INIT;
	inline static IDXGISwapChain *KnownSwapChain = nullptr;
};

class DXGIHookCallbacks
{
public:
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
};

class DXGIHookUtils
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogOriginalFallback(const char *api, const void *object, UINT slot, const void *fallback)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12FallbackPath",
			"\"kind\":\"original_lookup\",\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\","
			"\"slot\":%u,\"fallback\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object, slot, fallback);
	}

	template <typename T>
	static T GetOriginal(void *object, UINT slot, T fallback, const char *name)
	{
		if (object) {
			void **vtable = *reinterpret_cast<void***>(object);
			if (vtable) {
				void *target = vtable[slot];
				void *original = DX12HookRegistry::GetOriginalFunction(target);
				if (original)
					return reinterpret_cast<T>(original);
			}
		}

		if (fallback) {
			LogOriginalFallback(name, object, slot, reinterpret_cast<const void*>(fallback));
			return fallback;
		}

		DX12LogManager::JsonFunc(name ? name : "DXGI::Unknown",
			"\"event\":\"MissingOriginal\",\"this\":\"%p\",\"slot\":%u",
			object, slot);
		return nullptr;
	}

	static DXGIHookSignatures::Present GetPresentOriginal(IDXGISwapChain *swapChain);
	static DXGIHookSignatures::Present1 GetPresent1Original(IDXGISwapChain1 *swapChain);
	static DXGIHookSignatures::ResizeBuffers GetResizeBuffersOriginal(IDXGISwapChain *swapChain);
	static void LogPresentStage(
		const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr = S_OK);
	static void LogPresentDeviceRemovedReason(const char *api, IDXGISwapChain *swapChain, HRESULT presentHr);
	static void RunPostPresentUi(IDXGISwapChain *swapChain);
	static void ConsumeUiActionsForFrame();
	static void BeginFrameAnalysisCapture(LONG present);
	static void BeginShaderDumpCapture();
	static void HookSwapChain(IDXGISwapChain *swapChain);
	static void HookDeviceFromDXGIArgument(IUnknown *device);
	static void HookFactory(IUnknown *factory);
	static DXGIHookSignatures::CreateSwapChain GetCreateSwapChainOriginal(IDXGIFactory *factory);
	static DXGIHookSignatures::CreateSwapChainForHwnd GetCreateSwapChainForHwndOriginal(IDXGIFactory2 *factory);
	static DXGIHookSignatures::CreateSwapChainForCoreWindow GetCreateSwapChainForCoreWindowOriginal(IDXGIFactory2 *factory);
	static DXGIHookSignatures::CreateSwapChainForComposition GetCreateSwapChainForCompositionOriginal(IDXGIFactory2 *factory);
	static void HookDXGIFactoryVTables(HMODULE dxgi);
};

DXGIHookSignatures::Present DXGIHookUtils::GetPresentOriginal(IDXGISwapChain *swapChain)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::Present>(
		swapChain, 8, DXGIHookState::OrigPresent, "IDXGISwapChain::Present");
}

DXGIHookSignatures::Present1 DXGIHookUtils::GetPresent1Original(IDXGISwapChain1 *swapChain)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::Present1>(
		swapChain, 22, DXGIHookState::OrigPresent1, "IDXGISwapChain1::Present1");
}

DXGIHookSignatures::ResizeBuffers DXGIHookUtils::GetResizeBuffersOriginal(IDXGISwapChain *swapChain)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::ResizeBuffers>(
		swapChain, 13, DXGIHookState::OrigResizeBuffers, "IDXGISwapChain::ResizeBuffers");
}

void DXGIHookUtils::LogPresentStage(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr)
{
	if (!DX12LogManager::VerboseDiagnosticsEnabled())
		return;
	DX12LogManager::DebugJsonFuncFlush("DX12PresentStage",
		"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
		api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), swapChain, hr);
}

void DXGIHookUtils::LogPresentDeviceRemovedReason(const char *api, IDXGISwapChain *swapChain, HRESULT presentHr)
{
	if (SUCCEEDED(presentHr) || !swapChain)
		return;

	ID3D12Device *device = nullptr;
	HRESULT queryHr = swapChain->GetDevice(IID_PPV_ARGS(&device));
	if (FAILED(queryHr) || !device) {
		DX12LogManager::DebugJsonFuncFlush("DX12DeviceRemoved",
			"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"queryHr\":\"0x%lx\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, queryHr);
		return;
	}

	const HRESULT reason = device->GetDeviceRemovedReason();
	DX12LogManager::DebugJsonFuncFlush("DX12DeviceRemoved",
		"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"reason\":\"0x%lx\"",
		api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, reason);
	device->Release();
}

void DXGIHookUtils::RunPostPresentUi(IDXGISwapChain *swapChain)
{
	DX12NativeOverlayRender(swapChain);
}

void DXGIHookUtils::ConsumeUiActionsForFrame()
{
	if (!DX12UiActions::Consume())
		return;
	DX12HotPathState::Update();
	DX12CommandListRuntime::BumpTrackingGeneration();
}

void DXGIHookUtils::BeginFrameAnalysisCapture(LONG present)
{
	DX12BindingTracker::BeginFrame();
	DX12FrameAnalysis::BeginCapture();
	DX12CommandListRuntime::BumpTrackingGeneration();
	DX12FrameAnalysis::LogJsonFunc("FrameCaptureBegin", "\"present\":%ld", present);
}

void DXGIHookUtils::BeginShaderDumpCapture()
{
	DX12BindingTracker::BeginFrame();
	DX12ShaderDumpBeginCapture();
	DX12CommandListRuntime::BumpTrackingGeneration();
}

void DXGIHookUtils::HookSwapChain(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return;
	AcquireSRWLockExclusive(&DXGIHookState::KnownSwapChainLock);
	DXGIHookState::KnownSwapChain = swapChain;
	ReleaseSRWLockExclusive(&DXGIHookState::KnownSwapChainLock);

	DX12VTableHook swapChainHooks[] = {
		{8, reinterpret_cast<void**>(&DXGIHookState::OrigPresent), DXGIHookCallbacks::HookedPresent, "IDXGISwapChain::Present"},
		{13, reinterpret_cast<void**>(&DXGIHookState::OrigResizeBuffers), DXGIHookCallbacks::HookedResizeBuffers, "IDXGISwapChain::ResizeBuffers"},
	};
	DX12HookManager::InstallVTableHooks(swapChain, swapChainHooks);

	IDXGISwapChain1 *swapChain1 = nullptr;
	if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
		DX12VTableHook swapChain1Hooks[] = {
			{22, reinterpret_cast<void**>(&DXGIHookState::OrigPresent1), DXGIHookCallbacks::HookedPresent1, "IDXGISwapChain1::Present1"},
		};
		DX12HookManager::InstallVTableHooks(swapChain1, swapChain1Hooks);
		swapChain1->Release();
	}
}

void DXGIHooks::ActivateHotPathHooks()
{
	IDXGISwapChain *swapChain = nullptr;
	AcquireSRWLockShared(&DXGIHookState::KnownSwapChainLock);
	swapChain = DXGIHookState::KnownSwapChain;
	ReleaseSRWLockShared(&DXGIHookState::KnownSwapChainLock);
	if (swapChain)
		DXGIHookUtils::HookSwapChain(swapChain);
	DX12CommandListHooks::ActivateCommandQueueHotPathHooks();
}

void DXGIHookUtils::HookDeviceFromDXGIArgument(IUnknown *device)
{
	DX12DeviceHooks::HookDevice(device);
	DX12DeviceHooks::HookDeviceFromCommandQueue(device);
}

void DXGIHookUtils::HookFactory(IUnknown *factory)
{
	if (!factory)
		return;

	IDXGIFactory *factory0 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory0)))) {
		DX12VTableHook factoryHooks[] = {
			{10, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChain), DXGIHookCallbacks::HookedCreateSwapChain,
				"IDXGIFactory::CreateSwapChain"},
		};
		DX12HookManager::InstallVTableHooks(factory0, factoryHooks);
		factory0->Release();
	}

	IDXGIFactory2 *factory2 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2)))) {
		DX12VTableHook factory2Hooks[] = {
			{15, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChainForHwnd),
				DXGIHookCallbacks::HookedCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd"},
			{16, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChainForCoreWindow),
				DXGIHookCallbacks::HookedCreateSwapChainForCoreWindow, "IDXGIFactory2::CreateSwapChainForCoreWindow"},
			{24, reinterpret_cast<void**>(&DXGIHookState::OrigCreateSwapChainForComposition),
				DXGIHookCallbacks::HookedCreateSwapChainForComposition, "IDXGIFactory2::CreateSwapChainForComposition"},
		};
		DX12HookManager::InstallVTableHooks(factory2, factory2Hooks);
		factory2->Release();
	}
}

DXGIHookSignatures::CreateSwapChain DXGIHookUtils::GetCreateSwapChainOriginal(IDXGIFactory *factory)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::CreateSwapChain>(
		factory, 10, DXGIHookState::OrigCreateSwapChain, "IDXGIFactory::CreateSwapChain");
}

DXGIHookSignatures::CreateSwapChainForHwnd DXGIHookUtils::GetCreateSwapChainForHwndOriginal(IDXGIFactory2 *factory)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::CreateSwapChainForHwnd>(
		factory, 15, DXGIHookState::OrigCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
}

DXGIHookSignatures::CreateSwapChainForCoreWindow DXGIHookUtils::GetCreateSwapChainForCoreWindowOriginal(IDXGIFactory2 *factory)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::CreateSwapChainForCoreWindow>(
		factory, 16, DXGIHookState::OrigCreateSwapChainForCoreWindow,
		"IDXGIFactory2::CreateSwapChainForCoreWindow");
}

DXGIHookSignatures::CreateSwapChainForComposition DXGIHookUtils::GetCreateSwapChainForCompositionOriginal(IDXGIFactory2 *factory)
{
	return DXGIHookUtils::GetOriginal<DXGIHookSignatures::CreateSwapChainForComposition>(
		factory, 24, DXGIHookState::OrigCreateSwapChainForComposition,
		"IDXGIFactory2::CreateSwapChainForComposition");
}

void DXGIHookUtils::HookDXGIFactoryVTables(HMODULE dxgi)
{
	auto createFactory = reinterpret_cast<DXGIHookSignatures::CreateDXGIFactory>(
		GetProcAddress(dxgi, "CreateDXGIFactory"));
	auto createFactory1 = reinterpret_cast<DXGIHookSignatures::CreateDXGIFactory>(
		GetProcAddress(dxgi, "CreateDXGIFactory1"));
	auto createFactory2 = reinterpret_cast<DXGIHookSignatures::CreateDXGIFactory2>(
		GetProcAddress(dxgi, "CreateDXGIFactory2"));

	IDXGIFactory *factory = nullptr;
	if (createFactory1 && SUCCEEDED(createFactory1(IID_PPV_ARGS(&factory)))) {
		DXGIHookUtils::HookFactory(factory);
		factory->Release();
		return;
	}

	if (createFactory && SUCCEEDED(createFactory(IID_PPV_ARGS(&factory)))) {
		DXGIHookUtils::HookFactory(factory);
		factory->Release();
	}

	IDXGIFactory2 *factory2 = nullptr;
	if (createFactory2 && SUCCEEDED(createFactory2(0, IID_PPV_ARGS(&factory2)))) {
		DXGIHookUtils::HookFactory(factory2);
		factory2->Release();
	}
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory(REFIID riid, void **factory)
{
	DXGIHookUtils::LogHookCall("CreateDXGIFactory", nullptr);
	HRESULT hr = DXGIHookState::OrigCreateDXGIFactory(riid, factory);
	DX12LogManager::JsonFunc("CreateDXGIFactory",
		"\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		DXGIHookUtils::HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory1(REFIID riid, void **factory)
{
	DXGIHookUtils::LogHookCall("CreateDXGIFactory1", nullptr);
	HRESULT hr = DXGIHookState::OrigCreateDXGIFactory1(riid, factory);
	DX12LogManager::JsonFunc("CreateDXGIFactory1",
		"\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		&riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		DXGIHookUtils::HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

HRESULT WINAPI DXGIHookCallbacks::HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory)
{
	DXGIHookUtils::LogHookCall("CreateDXGIFactory2", nullptr);
	HRESULT hr = DXGIHookState::OrigCreateDXGIFactory2(flags, riid, factory);
	DX12LogManager::JsonFunc("CreateDXGIFactory2",
		"\"flags\":\"0x%x\",\"riid\":\"%p\",\"hr\":\"0x%lx\",\"factory\":\"%p\"",
		flags, &riid, hr, factory ? *factory : nullptr);
	if (SUCCEEDED(hr) && factory)
		DXGIHookUtils::HookFactory(static_cast<IUnknown*>(*factory));
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChain(
	IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapChain)
{
	DXGIHookUtils::LogHookCall("IDXGIFactory::CreateSwapChain", factory);
	DXGIHookUtils::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookUtils::GetCreateSwapChainOriginal(factory);
	HRESULT hr = original ? original(factory, device, desc, swapChain) : DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory::CreateSwapChain",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain && (!desc || !DX12NativeOverlayIsOverlayWindow(desc->OutputWindow)))
		DXGIHookUtils::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChainForHwnd(
	IDXGIFactory2 *factory, IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	DXGIHookUtils::LogHookCall("IDXGIFactory2::CreateSwapChainForHwnd", factory);
	DXGIHookUtils::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookUtils::GetCreateSwapChainForHwndOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, fullscreenDesc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory2::CreateSwapChainForHwnd",
		"\"device\":\"%p\",\"hwnd\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, window, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain && !DX12NativeOverlayIsOverlayWindow(window))
		DXGIHookUtils::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChainForCoreWindow(
	IDXGIFactory2 *factory, IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	DXGIHookUtils::LogHookCall("IDXGIFactory2::CreateSwapChainForCoreWindow", factory);
	DXGIHookUtils::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookUtils::GetCreateSwapChainForCoreWindowOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, window, desc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory2::CreateSwapChainForCoreWindow",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		DXGIHookUtils::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedCreateSwapChainForComposition(
	IDXGIFactory2 *factory, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc,
	IDXGIOutput *output, IDXGISwapChain1 **swapChain)
{
	DXGIHookUtils::LogHookCall("IDXGIFactory2::CreateSwapChainForComposition", factory);
	DXGIHookUtils::HookDeviceFromDXGIArgument(device);
	auto original = DXGIHookUtils::GetCreateSwapChainForCompositionOriginal(factory);
	HRESULT hr = original ?
		original(factory, device, desc, output, swapChain) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGIFactory2::CreateSwapChainForComposition",
		"\"device\":\"%p\",\"hr\":\"0x%lx\",\"swapchain\":\"%p\"",
		device, hr, swapChain ? *swapChain : nullptr);
	if (SUCCEEDED(hr) && swapChain)
		DXGIHookUtils::HookSwapChain(*swapChain);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
{
	if (DX12InternalReplayState::IsActive()) {
		DXGIHookSignatures::Present original = DXGIHookUtils::GetPresentOriginal(swapChain);
		return original ? original(swapChain, syncInterval, flags) : DXGI_ERROR_INVALID_CALL;
	}

	DXGIHookUtils::ConsumeUiActionsForFrame();

	static volatile LONG firstPresentLog = 0;
	if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
		DX12LogManager::JsonFuncFlush("DX12PresentFirst",
			"\"api\":\"Present\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
			swapChain, syncInterval, flags, DX12HotPathState::Get().skipAll);

	if (DX12HotPathState::Get().skipAll) {
		DXGIHookSignatures::Present original = DXGIHookUtils::GetPresentOriginal(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		if (InterlockedCompareExchange(&firstPresentLog, 2, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"before_original_skip\",\"swapchain\":\"%p\"",
				swapChain);
		HRESULT hr = original(swapChain, syncInterval, flags);
		if (InterlockedCompareExchange(&firstPresentLog, 3, 2) == 2)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"after_original_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		if (DX12RuntimeState::GetPresentCount() > 2)
			DXGIHookUtils::RunPostPresentUi(swapChain);
		if (InterlockedCompareExchange(&firstPresentLog, 4, 3) == 3)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"before_hotpath_update_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12HotPathState::Update();
		if (InterlockedCompareExchange(&firstPresentLog, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"return_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		return hr;
	}

	DXGIHookUtils::LogHookCall("IDXGISwapChain::Present", swapChain);
	DX12_PROFILE_SCOPE(Present);

	{
		const LONG draws = DX12RuntimeState::TakeFrameDraws();
		const LONG dispatches = DX12RuntimeState::TakeFrameDispatches();
		if (DX12LogManager::VerboseDiagnosticsEnabled()) {
			DX12LogManager::JsonFunc("DX12PerFrameCounters",
				"\"draws\":%ld,\"dispatches\":%ld", draws, dispatches);
		}
	}

	DX12Profiling::BeginFrame();
	const bool dumpFrame = DX12FrameAnalysis::EndCapture();
	const bool dumpShaders = DX12ShaderDumpEndCapture();
	DXGIHookSignatures::Present original = DXGIHookUtils::GetPresentOriginal(swapChain);
	if (!original)
		return DXGI_ERROR_INVALID_CALL;
	DXGIHookUtils::LogPresentStage("Present", "beforeOriginal", swapChain);
	HRESULT hr = original(swapChain, syncInterval, flags);
	DXGIHookUtils::LogPresentStage("Present", "afterOriginal", swapChain, hr);
	DXGIHookUtils::LogPresentDeviceRemovedReason("Present", swapChain, hr);
	DX12RuntimeState::IncrementPresentCount();
	DX12HuntOnPresent();
	DX12CommandListRuntime::BumpTrackingGeneration();
	DXGIHookUtils::RunPostPresentUi(swapChain);

	DXGIHookUtils::LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
	DX12ModBeginFrame();

	DX12HotPathState::Update();
	DXGIHookUtils::LogPresentStage("Present", "afterHotPathUpdate", swapChain, hr);

	if (dumpFrame) {
		DX12FrameAnalysis::LogJsonFunc("FrameCaptureEnd",
			"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		DX12DumpFrameAnalysis();
		DX12FrameAnalysis::End();
	} else if (dumpShaders) {
		DX12DumpCapturedFrameShaders();
	} else if (DX12FrameAnalysis::IsCaptureRequested()) {
		DXGIHookUtils::BeginFrameAnalysisCapture(DX12RuntimeState::GetPresentCount());
	} else if (DX12ShaderDumpIsCaptureRequested()) {
		DXGIHookUtils::BeginShaderDumpCapture();
	} else if (DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
	           DX12ModNeedsPreSkinningUavProbe()) {
		DX12BindingTracker::BeginFrame();
		DXGIHookUtils::LogPresentStage("Present", "afterBindingBeginFrame", swapChain, hr);
	}
	DX12Profiling::EndFrame();
	DXGIHookUtils::LogPresentStage("Present", "end", swapChain, hr);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedPresent1(
	IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS *presentParameters)
{
	if (DX12InternalReplayState::IsActive()) {
		DXGIHookSignatures::Present1 original = DXGIHookUtils::GetPresent1Original(swapChain);
		return original ? original(swapChain, syncInterval, flags, presentParameters) : DXGI_ERROR_INVALID_CALL;
	}

	DXGIHookUtils::ConsumeUiActionsForFrame();

	static volatile LONG firstPresent1Log = 0;
	if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
		DX12LogManager::JsonFuncFlush("DX12PresentFirst",
			"\"api\":\"Present1\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
			swapChain, syncInterval, flags, DX12HotPathState::Get().skipAll);

	if (DX12HotPathState::Get().skipAll) {
		DXGIHookSignatures::Present1 original = DXGIHookUtils::GetPresent1Original(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		if (InterlockedCompareExchange(&firstPresent1Log, 2, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"before_original_skip\",\"swapchain\":\"%p\"",
				swapChain);
		HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
		if (InterlockedCompareExchange(&firstPresent1Log, 3, 2) == 2)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"after_original_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		if (DX12RuntimeState::GetPresentCount() > 2)
			DXGIHookUtils::RunPostPresentUi(swapChain);
		if (InterlockedCompareExchange(&firstPresent1Log, 4, 3) == 3)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"before_hotpath_update_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12HotPathState::Update();
		if (InterlockedCompareExchange(&firstPresent1Log, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"return_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		return hr;
	}

	DXGIHookUtils::LogHookCall("IDXGISwapChain1::Present1", swapChain);

	{
		const LONG draws = DX12RuntimeState::TakeFrameDraws();
		const LONG dispatches = DX12RuntimeState::TakeFrameDispatches();
		if (DX12LogManager::VerboseDiagnosticsEnabled()) {
			DX12LogManager::JsonFunc("DX12PerFrameCounters",
				"\"draws\":%ld,\"dispatches\":%ld", draws, dispatches);
		}
	}

	DX12Profiling::BeginFrame();
	const bool dumpFrame = DX12FrameAnalysis::EndCapture();
	const bool dumpShaders = DX12ShaderDumpEndCapture();
	DXGIHookSignatures::Present1 original = DXGIHookUtils::GetPresent1Original(swapChain);
	if (!original)
		return DXGI_ERROR_INVALID_CALL;
	DXGIHookUtils::LogPresentStage("Present1", "beforeOriginal", swapChain);
	HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
	DXGIHookUtils::LogPresentStage("Present1", "afterOriginal", swapChain, hr);
	DXGIHookUtils::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
	DX12RuntimeState::IncrementPresentCount();
	DX12HuntOnPresent();
	DX12CommandListRuntime::BumpTrackingGeneration();
	DXGIHookUtils::RunPostPresentUi(swapChain);

	DXGIHookUtils::LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
	DX12ModBeginFrame();

	DX12HotPathState::Update();
	DXGIHookUtils::LogPresentStage("Present1", "afterHotPathUpdate", swapChain, hr);

	if (dumpFrame) {
		DX12FrameAnalysis::LogJsonFunc("FrameCaptureEnd",
			"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		DX12DumpFrameAnalysis();
		DX12FrameAnalysis::End();
	} else if (dumpShaders) {
		DX12DumpCapturedFrameShaders();
	} else if (DX12FrameAnalysis::IsCaptureRequested()) {
		DXGIHookUtils::BeginFrameAnalysisCapture(DX12RuntimeState::GetPresentCount());
	} else if (DX12ShaderDumpIsCaptureRequested()) {
		DXGIHookUtils::BeginShaderDumpCapture();
	} else if (DX12FrameAnalysis::IsCapturing() || DX12ShaderDumpIsCapturingFrame() ||
	           DX12ModNeedsPreSkinningUavProbe()) {
		DX12BindingTracker::BeginFrame();
		DXGIHookUtils::LogPresentStage("Present1", "afterBindingBeginFrame", swapChain, hr);
	}
	DX12Profiling::EndFrame();
	DXGIHookUtils::LogPresentStage("Present1", "end", swapChain, hr);
	return hr;
}

HRESULT STDMETHODCALLTYPE DXGIHookCallbacks::HookedResizeBuffers(
	IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height,
	DXGI_FORMAT newFormat, UINT swapChainFlags)
{
	DXGIHookUtils::LogHookCall("IDXGISwapChain::ResizeBuffers", swapChain);
	if (!DX12InternalReplayState::IsActive())
		DX12NativeOverlayInvalidate(swapChain);
	auto original = DXGIHookUtils::GetResizeBuffersOriginal(swapChain);
	HRESULT hr = original ? original(swapChain, bufferCount, width, height, newFormat, swapChainFlags) :
		DXGI_ERROR_INVALID_CALL;
	DX12LogManager::JsonFunc("IDXGISwapChain::ResizeBuffers",
		"\"swapchain\":\"%p\",\"buffers\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":\"0x%x\",\"hr\":\"0x%lx\"",
		swapChain, bufferCount, width, height, static_cast<UINT>(newFormat), swapChainFlags, hr);
	return hr;
}

void DXGIHooks::Install()
{
	AcquireSRWLockExclusive(&DXGIHookState::HookLock);
	if (DXGIHookState::HookAttempted) {
		ReleaseSRWLockExclusive(&DXGIHookState::HookLock);
		return;
	}
	DXGIHookState::HookAttempted = true;
	ReleaseSRWLockExclusive(&DXGIHookState::HookLock);

	HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
	if (!dxgi) {
		static volatile LONG deferredLogs = 0;
		if (InterlockedIncrement(&deferredLogs) <= 4)
			DX12LogManager::Text("dxgi.dll is not loaded yet; DXGI hook deferred\n");
		AcquireSRWLockExclusive(&DXGIHookState::HookLock);
		DXGIHookState::HookAttempted = false;
		ReleaseSRWLockExclusive(&DXGIHookState::HookLock);
		return;
	}

	DXGIHookUtils::HookDXGIFactoryVTables(dxgi);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory), DXGIHookCallbacks::HookedCreateDXGIFactory);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory1",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory1), DXGIHookCallbacks::HookedCreateDXGIFactory1);
	DX12HookManager::InstallExportHook(dxgi, "CreateDXGIFactory2",
		reinterpret_cast<void**>(&DXGIHookState::OrigCreateDXGIFactory2), DXGIHookCallbacks::HookedCreateDXGIFactory2);
}




