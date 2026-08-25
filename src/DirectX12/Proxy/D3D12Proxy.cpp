#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <Shlwapi.h>
#include <crtdbg.h>
#include <stdlib.h>

bool D3D12ProxyTargetHelper::VerifyIntendedTarget(HINSTANCE module)
{
	return D3D12ProcessGateUtils::VerifyIntendedTarget(module);
}

static bool gD3D12ProxyRuntimeInitialized = false;
static SRWLOCK gD3D12ProxyLoadLock = SRWLOCK_INIT;

FARPROC D3D12ProxyRuntime::GetRealProc(const char *name)
{
	HMODULE module = D3D12ProxyRuntime::LoadRealD3D12();
	if (!module)
		return nullptr;

	FARPROC proc = GetProcAddress(module, name);
	if (!proc)
		DX12LogManager::Text("Failed to resolve real d3d12 export %s, error=%lu\n", name, GetLastError());
	return proc;
}

FARPROC D3D12ProxyRuntime::GetRealProc(WORD ordinal)
{
	HMODULE module = D3D12ProxyRuntime::LoadRealD3D12();
	if (!module)
		return nullptr;

	FARPROC proc = GetProcAddress(module, MAKEINTRESOURCEA(ordinal));
	if (!proc)
		DX12LogManager::Text("Failed to resolve real d3d12 ordinal %u, error=%lu\n", ordinal, GetLastError());
	return proc;
}

UINT_PTR D3D12ProxyRuntime::CallRealProc(const char *name,
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	FARPROC proc = GetRealProc(name);
	if (!proc)
		return static_cast<UINT_PTR>(E_FAIL);

	return reinterpret_cast<D3D12ProxySignatures::Generic>(proc)(a1, a2, a3, a4, a5, a6, a7, a8);
}

UINT_PTR D3D12ProxyRuntime::CallRealProc(WORD ordinal,
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	FARPROC proc = GetRealProc(ordinal);
	if (!proc)
		return static_cast<UINT_PTR>(E_FAIL);

	return reinterpret_cast<D3D12ProxySignatures::Generic>(proc)(a1, a2, a3, a4, a5, a6, a7, a8);
}

HMODULE D3D12ProxyRuntime::LoadRealD3D12()
{
	AcquireSRWLockExclusive(&gD3D12ProxyLoadLock);
	if (D3D12ProxyState::RealD3D12) {
		HMODULE module = D3D12ProxyState::RealD3D12;
		ReleaseSRWLockExclusive(&gD3D12ProxyLoadLock);
		return module;
	}

	wchar_t path[MAX_PATH];
	if (!GetSystemDirectoryW(path, MAX_PATH)) {
		ReleaseSRWLockExclusive(&gD3D12ProxyLoadLock);
		return nullptr;
	}
	PathAppendW(path, L"d3d12.dll");

	HMODULE module = LoadLibraryW(path);
	if (!module) {
		DX12LogManager::Text("Failed to load real d3d12.dll from %S, error=%lu\n", path, GetLastError());
		ReleaseSRWLockExclusive(&gD3D12ProxyLoadLock);
		return nullptr;
	}

	D3D12ProxyState::OrigCreateDevice = reinterpret_cast<D3D12ProxySignatures::CreateDevice>(
		GetProcAddress(module, "D3D12CreateDevice"));
	D3D12ProxyState::OrigGetDebugInterface = reinterpret_cast<D3D12ProxySignatures::GetDebugInterface>(
		GetProcAddress(module, "D3D12GetDebugInterface"));
	D3D12ProxyState::OrigSerializeRootSignature = reinterpret_cast<D3D12ProxySignatures::SerializeRootSignature>(
		GetProcAddress(module, "D3D12SerializeRootSignature"));
	D3D12ProxyState::OrigCreateRootSignatureDeserializer =
		reinterpret_cast<D3D12ProxySignatures::CreateRootSignatureDeserializer>(
			GetProcAddress(module, "D3D12CreateRootSignatureDeserializer"));
	D3D12ProxyState::OrigSerializeVersionedRootSignature =
		reinterpret_cast<D3D12ProxySignatures::SerializeVersionedRootSignature>(
		GetProcAddress(module, "D3D12SerializeVersionedRootSignature"));
	D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer =
		reinterpret_cast<D3D12ProxySignatures::CreateVersionedRootSignatureDeserializer>(
			GetProcAddress(module, "D3D12CreateVersionedRootSignatureDeserializer"));
	D3D12ProxyState::OrigEnableExperimentalFeatures =
		reinterpret_cast<D3D12ProxySignatures::EnableExperimentalFeatures>(
		GetProcAddress(module, "D3D12EnableExperimentalFeatures"));
	D3D12ProxyState::OrigGetInterface = reinterpret_cast<D3D12ProxySignatures::GetInterface>(
		GetProcAddress(module, "D3D12GetInterface"));
	D3D12ProxyState::RealD3D12 = module;

	DX12LogManager::Text("Loaded real d3d12.dll: %p\n", D3D12ProxyState::RealD3D12);
	ReleaseSRWLockExclusive(&gD3D12ProxyLoadLock);
	return module;
}

void D3D12ProxyRuntime::Initialize(HINSTANCE module)
{
	DX12Runtime::Initialize(module, D3D12ProxyRuntime::LoadRealD3D12);
}

void D3D12ProxyRuntime::Shutdown()
{
	DX12Runtime::Shutdown();
	UnloadRealD3D12();
}

void D3D12ProxyRuntime::UnloadRealD3D12()
{
	AcquireSRWLockExclusive(&gD3D12ProxyLoadLock);
	if (D3D12ProxyState::RealD3D12) {
		FreeLibrary(D3D12ProxyState::RealD3D12);
		D3D12ProxyState::RealD3D12 = nullptr;
	}
	ReleaseSRWLockExclusive(&gD3D12ProxyLoadLock);
}

extern "C" HRESULT WINAPI D3D12CreateDevice(
	IUnknown *adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, REFIID riid, void **device)
{
	DX12_CREATE_TIMING_SCOPE(ProxyDeviceCreate, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateDevice)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateDevice(adapter, minimumFeatureLevel, riid, device);
	DX12LogManager::Fields("D3D12CreateDevice",
		"adapter=%p,minFeature=0x%x,hr=0x%lx,device=%p",
		adapter, minimumFeatureLevel, hr, device ? *device : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapD3D12DeviceResult(riid, device);
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void **debug)
{
	DX12_CREATE_TIMING_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigGetDebugInterface)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigGetDebugInterface(riid, debug);
	DX12LogManager::Fields("D3D12GetDebugInterface",
		"hr=0x%lx,debug=%p", hr, debug ? *debug : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapD3D12DebugInterfaceResult(riid, debug);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeRootSignature(
	const D3D12_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version,
	ID3DBlob **blob, ID3DBlob **errorBlob)
{
	DX12_CREATE_TIMING_SCOPE(RootSignatureSerializeProxy, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigSerializeRootSignature)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigSerializeRootSignature(desc, version, blob, errorBlob);
	DX12LogManager::Fields("D3D12SerializeRootSignature",
		"version=%u,hr=0x%lx", static_cast<UINT>(version), hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	DX12_CREATE_TIMING_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateRootSignatureDeserializer(
		srcData, srcDataSize, riid, rootSignatureDeserializer);
	DX12LogManager::Fields("D3D12CreateRootSignatureDeserializer",
		"size=%llu,hr=0x%lx,object=%p",
		static_cast<unsigned long long>(srcDataSize), hr,
		rootSignatureDeserializer ? *rootSignatureDeserializer : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeVersionedRootSignature(
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **errorBlob)
{
	DX12_CREATE_TIMING_SCOPE(RootSignatureSerializeProxy, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigSerializeVersionedRootSignature)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigSerializeVersionedRootSignature(desc, blob, errorBlob);
	DX12LogManager::Fields("D3D12SerializeVersionedRootSignature", "hr=0x%lx", hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	DX12_CREATE_TIMING_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer(
		srcData, srcDataSize, riid, rootSignatureDeserializer);
	DX12LogManager::Fields("D3D12CreateVersionedRootSignatureDeserializer",
		"size=%llu,hr=0x%lx,object=%p",
		static_cast<unsigned long long>(srcDataSize), hr,
		rootSignatureDeserializer ? *rootSignatureDeserializer : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12EnableExperimentalFeatures(
	UINT numFeatures, const IID *featureIIDs, void *configurationStructs, UINT *configurationStructSizes)
{
	DX12_CREATE_TIMING_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigEnableExperimentalFeatures)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigEnableExperimentalFeatures(
		numFeatures, featureIIDs, configurationStructs, configurationStructSizes);
	DX12LogManager::Fields("D3D12EnableExperimentalFeatures",
		"features=%u,hr=0x%lx", numFeatures, hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID riid, void **object)
{
	DX12_CREATE_TIMING_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigGetInterface)
		return E_NOINTERFACE;
	HRESULT hr = D3D12ProxyState::OrigGetInterface(clsid, riid, object);
	DX12LogManager::Fields("D3D12GetInterface",
		"clsid=%p,riid=%p,hr=0x%lx,object=%p",
		&clsid, &riid, hr, object ? *object : nullptr);
	if (SUCCEEDED(hr) && !DX12Wrapper::WrapD3D12DeviceFactoryResult(riid, object))
		DX12Wrapper::WrapD3D12AuxiliaryResult(riid, object);
	return hr;
}

extern "C" UINT_PTR WINAPI D3D12Ordinal99(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc(static_cast<WORD>(99), a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI SetAppCompatStringPointer(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("SetAppCompatStringPointer", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12CoreCreateLayeredDevice(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12CoreCreateLayeredDevice", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12CoreGetLayeredDeviceSize(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12CoreGetLayeredDeviceSize", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12CoreRegisterLayers(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12CoreRegisterLayers", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12DeviceRemovedExtendedData(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12DeviceRemovedExtendedData", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXEventsReplaceBlock(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12PIXEventsReplaceBlock", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXGetThreadInfo(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12PIXGetThreadInfo", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXNotifyWakeFromFenceSignal(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12PIXNotifyWakeFromFenceSignal", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXReportCounter(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("D3D12PIXReportCounter", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI GetBehaviorValue(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return D3D12ProxyRuntime::CallRealProc("GetBehaviorValue", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" LRESULT CALLBACK CBTProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH:
	{
		D3D12ProcessGateDecision decision;
		DisableThreadLibraryCalls(module);

		decision = D3D12ProcessGateUtils::Evaluate(module);
		D3D12ProcessGateUtils::DebugDecision(decision);
		if (decision.role != D3D12ProcessGateRoleTargetRuntime) {
			// Global CBT hook injection can load this DLL into shell/launcher
			// processes.  Match DX11's harmless-pass-through behavior for those
			// processes: keep CBTProc callable, but do not initialize D3D12 hooks.
			break;
		}

		D3D12ProxyRuntime::Initialize(module);
		gD3D12ProxyRuntimeInitialized = true;
		break;
	}
	case DLL_PROCESS_DETACH:
		if (gD3D12ProxyRuntimeInitialized) {
			DX12Wrapper::Shutdown();
			D3D12ProxyRuntime::Shutdown();
			gD3D12ProxyRuntimeInitialized = false;
		}
		break;
	}
	return TRUE;
}
