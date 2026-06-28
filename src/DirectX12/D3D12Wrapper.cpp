#include "D3D12Wrapper.h"

#include <Shlwapi.h>

#include "DX12DeviceHooks.h"
#include "DX12Runtime.h"
#include "DX12LogManager.hpp"
#include "DXGIHooks.h"
#include "util_min.h"

static HMODULE gRealD3D12 = nullptr;

static HMODULE LoadRealD3D12();

static bool VerifyIntendedTarget(HINSTANCE module)
{
	wchar_t ourPath[MAX_PATH], exePath[MAX_PATH];
	wchar_t *ourBase = nullptr, *exeBase = nullptr;
	HANDLE file = INVALID_HANDLE_VALUE;
	char *buffer = nullptr;
	DWORD fileSize = 0, readSize = 0;
	const char *section = nullptr;
	char target[MAX_PATH];
	wchar_t targetW[MAX_PATH];
	size_t targetLen = 0, exeLen = 0;
	bool result = false;

	if (!GetModuleFileNameW(module, ourPath, MAX_PATH))
		return false;
	if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		return false;

	ourBase = wcsrchr(ourPath, L'\\');
	exeBase = wcsrchr(exePath, L'\\');
	if (!ourBase || !exeBase)
		return false;

	*(ourBase++) = L'\0';
	*(exeBase++) = L'\0';

	if (!_wcsicmp(ourPath, exePath))
		return true;
	if (wcsstr(ourPath, exePath))
		return true;

	*(exeBase - 1) = L'\\';
	wcsncat_s(ourPath, MAX_PATH, L"\\d3dx.ini", _TRUNCATE);

	file = CreateFileW(ourPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	fileSize = GetFileSize(file, nullptr);
	buffer = new char[fileSize + 1];
	if (!buffer)
		goto out_close;

	if (!ReadFile(file, buffer, fileSize, &readSize, nullptr) || fileSize != readSize)
		goto out_free;
	buffer[fileSize] = '\0';

	section = find_ini_section_lite(buffer, "loader");
	if (!section)
		goto out_free;
	if (!find_ini_setting_lite(section, "target", target, MAX_PATH))
		goto out_free;
	if (!MultiByteToWideChar(CP_UTF8, 0, target, -1, targetW, MAX_PATH))
		goto out_free;

	targetLen = wcslen(targetW);
	exeLen = wcslen(exePath);
	if (exeLen < targetLen)
		goto out_free;
	if (targetW[0] != L'\\' && exeLen > targetLen && exePath[exeLen - targetLen - 1] != L'\\')
		goto out_free;

	result = !_wcsicmp(exePath + exeLen - targetLen, targetW);
	if (result) {
		HMODULE handle = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
			reinterpret_cast<LPCWSTR>(VerifyIntendedTarget), &handle);
	}

out_free:
	delete [] buffer;
out_close:
	CloseHandle(file);
	return result;
}

typedef HRESULT(WINAPI *PFN_D3D12_CREATE_DEVICE_LOCAL)(
	IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_GET_DEBUG_INTERFACE_LOCAL)(REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_LOCAL)(
	const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI *PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE_LOCAL)(
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI *PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL)(
	LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT(WINAPI *PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES_LOCAL)(
	UINT, const IID*, void*, UINT*);
typedef HRESULT(WINAPI *PFN_D3D12_GET_INTERFACE_LOCAL)(REFCLSID, REFIID, void**);
typedef UINT_PTR(WINAPI *PFN_D3D12_GENERIC_LOCAL)(
	UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR,
	UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);

static PFN_D3D12_CREATE_DEVICE_LOCAL gOrigD3D12CreateDevice = nullptr;
static PFN_D3D12_GET_DEBUG_INTERFACE_LOCAL gOrigD3D12GetDebugInterface = nullptr;
static PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_LOCAL gOrigD3D12SerializeRootSignature = nullptr;
static PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL gOrigD3D12CreateRootSignatureDeserializer = nullptr;
static PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE_LOCAL gOrigD3D12SerializeVersionedRootSignature = nullptr;
static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL gOrigD3D12CreateVersionedRootSignatureDeserializer = nullptr;
static PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES_LOCAL gOrigD3D12EnableExperimentalFeatures = nullptr;
static PFN_D3D12_GET_INTERFACE_LOCAL gOrigD3D12GetInterface = nullptr;

static FARPROC GetRealD3D12Proc(const char *name)
{
	HMODULE module = LoadRealD3D12();
	if (!module)
		return nullptr;

	FARPROC proc = GetProcAddress(module, name);
	if (!proc)
		DX12LogManager::Text("Failed to resolve real d3d12 export %s, error=%lu\n", name, GetLastError());
	return proc;
}

static FARPROC GetRealD3D12Proc(WORD ordinal)
{
	HMODULE module = LoadRealD3D12();
	if (!module)
		return nullptr;

	FARPROC proc = GetProcAddress(module, MAKEINTRESOURCEA(ordinal));
	if (!proc)
		DX12LogManager::Text("Failed to resolve real d3d12 ordinal %u, error=%lu\n", ordinal, GetLastError());
	return proc;
}

static UINT_PTR CallRealD3D12Proc(const char *name,
	UINT_PTR a1 = 0, UINT_PTR a2 = 0, UINT_PTR a3 = 0, UINT_PTR a4 = 0,
	UINT_PTR a5 = 0, UINT_PTR a6 = 0, UINT_PTR a7 = 0, UINT_PTR a8 = 0)
{
	FARPROC proc = GetRealD3D12Proc(name);
	if (!proc)
		return static_cast<UINT_PTR>(E_FAIL);

	return reinterpret_cast<PFN_D3D12_GENERIC_LOCAL>(proc)(a1, a2, a3, a4, a5, a6, a7, a8);
}

static UINT_PTR CallRealD3D12Proc(WORD ordinal,
	UINT_PTR a1 = 0, UINT_PTR a2 = 0, UINT_PTR a3 = 0, UINT_PTR a4 = 0,
	UINT_PTR a5 = 0, UINT_PTR a6 = 0, UINT_PTR a7 = 0, UINT_PTR a8 = 0)
{
	FARPROC proc = GetRealD3D12Proc(ordinal);
	if (!proc)
		return static_cast<UINT_PTR>(E_FAIL);

	return reinterpret_cast<PFN_D3D12_GENERIC_LOCAL>(proc)(a1, a2, a3, a4, a5, a6, a7, a8);
}

static HMODULE LoadRealD3D12()
{
	if (gRealD3D12)
		return gRealD3D12;

	wchar_t path[MAX_PATH];
	if (!GetSystemDirectoryW(path, MAX_PATH))
		return nullptr;
	PathAppendW(path, L"d3d12.dll");

	gRealD3D12 = LoadLibraryW(path);
	if (!gRealD3D12) {
		DX12LogManager::Text("Failed to load real d3d12.dll from %S, error=%lu\n", path, GetLastError());
		return nullptr;
	}

	gOrigD3D12CreateDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE_LOCAL>(
		GetProcAddress(gRealD3D12, "D3D12CreateDevice"));
	gOrigD3D12GetDebugInterface = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE_LOCAL>(
		GetProcAddress(gRealD3D12, "D3D12GetDebugInterface"));
	gOrigD3D12SerializeRootSignature = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_LOCAL>(
		GetProcAddress(gRealD3D12, "D3D12SerializeRootSignature"));
	gOrigD3D12CreateRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gRealD3D12, "D3D12CreateRootSignatureDeserializer"));
	gOrigD3D12SerializeVersionedRootSignature =
		reinterpret_cast<PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE_LOCAL>(
			GetProcAddress(gRealD3D12, "D3D12SerializeVersionedRootSignature"));
	gOrigD3D12CreateVersionedRootSignatureDeserializer =
		reinterpret_cast<PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER_LOCAL>(
			GetProcAddress(gRealD3D12, "D3D12CreateVersionedRootSignatureDeserializer"));
	gOrigD3D12EnableExperimentalFeatures =
		reinterpret_cast<PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES_LOCAL>(
			GetProcAddress(gRealD3D12, "D3D12EnableExperimentalFeatures"));
	gOrigD3D12GetInterface = reinterpret_cast<PFN_D3D12_GET_INTERFACE_LOCAL>(
		GetProcAddress(gRealD3D12, "D3D12GetInterface"));

	DX12LogManager::Text("Loaded real d3d12.dll: %p\n", gRealD3D12);
	return gRealD3D12;
}

void DX12Initialize(HINSTANCE module)
{
	BunnyDX12RuntimeInitialize(module, DX12LoadRealD3D12);
}

void DX12Shutdown()
{
	BunnyDX12RuntimeShutdown();
	DX12UnloadRealD3D12();
}

HMODULE DX12LoadRealD3D12()
{
	return LoadRealD3D12();
}

void DX12UnloadRealD3D12()
{
	if (gRealD3D12) {
		FreeLibrary(gRealD3D12);
		gRealD3D12 = nullptr;
	}
}

extern "C" HRESULT WINAPI D3D12CreateDevice(
	IUnknown *adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, REFIID riid, void **device)
{
	LoadRealD3D12();
	DX12InstallDXGIHooks();
	if (!gOrigD3D12CreateDevice)
		return E_FAIL;
	HRESULT hr = gOrigD3D12CreateDevice(adapter, minimumFeatureLevel, riid, device);
	DX12LogManager::JsonFunc("D3D12CreateDevice",
		"\"adapter\":\"%p\",\"minFeature\":\"0x%x\",\"hr\":\"0x%lx\",\"device\":\"%p\"",
		adapter, minimumFeatureLevel, hr, device ? *device : nullptr);
	if (SUCCEEDED(hr) && device && *device)
		DX12HookDevice(static_cast<IUnknown*>(*device));
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void **debug)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12GetDebugInterface\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12GetDebugInterface)
		return E_FAIL;
	HRESULT hr = gOrigD3D12GetDebugInterface(riid, debug);
	DX12LogManager::JsonFunc("D3D12GetDebugInterface",
		"\"hr\":\"0x%lx\",\"debug\":\"%p\"", hr, debug ? *debug : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeRootSignature(
	const D3D12_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version,
	ID3DBlob **blob, ID3DBlob **errorBlob)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12SerializeRootSignature\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12SerializeRootSignature)
		return E_FAIL;
	HRESULT hr = gOrigD3D12SerializeRootSignature(desc, version, blob, errorBlob);
	DX12LogManager::JsonFunc("D3D12SerializeRootSignature",
		"\"version\":%u,\"hr\":\"0x%lx\"", static_cast<UINT>(version), hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12CreateRootSignatureDeserializer\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12CreateRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = gOrigD3D12CreateRootSignatureDeserializer(
		srcData, srcDataSize, riid, rootSignatureDeserializer);
	DX12LogManager::JsonFunc("D3D12CreateRootSignatureDeserializer",
		"\"size\":%llu,\"hr\":\"0x%lx\",\"object\":\"%p\"",
		static_cast<unsigned long long>(srcDataSize), hr,
		rootSignatureDeserializer ? *rootSignatureDeserializer : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeVersionedRootSignature(
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **errorBlob)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12SerializeVersionedRootSignature\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12SerializeVersionedRootSignature)
		return E_FAIL;
	HRESULT hr = gOrigD3D12SerializeVersionedRootSignature(desc, blob, errorBlob);
	DX12LogManager::JsonFunc("D3D12SerializeVersionedRootSignature", "\"hr\":\"0x%lx\"", hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12CreateVersionedRootSignatureDeserializer\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12CreateVersionedRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = gOrigD3D12CreateVersionedRootSignatureDeserializer(
		srcData, srcDataSize, riid, rootSignatureDeserializer);
	DX12LogManager::JsonFunc("D3D12CreateVersionedRootSignatureDeserializer",
		"\"size\":%llu,\"hr\":\"0x%lx\",\"object\":\"%p\"",
		static_cast<unsigned long long>(srcDataSize), hr,
		rootSignatureDeserializer ? *rootSignatureDeserializer : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12EnableExperimentalFeatures(
	UINT numFeatures, const IID *featureIIDs, void *configurationStructs, UINT *configurationStructSizes)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12EnableExperimentalFeatures\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12EnableExperimentalFeatures)
		return E_FAIL;
	HRESULT hr = gOrigD3D12EnableExperimentalFeatures(
		numFeatures, featureIIDs, configurationStructs, configurationStructSizes);
	DX12LogManager::JsonFunc("D3D12EnableExperimentalFeatures",
		"\"features\":%u,\"hr\":\"0x%lx\"", numFeatures, hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID riid, void **object)
{
	LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12GetInterface\",\"this\":\"%p\"", nullptr);
	if (!gOrigD3D12GetInterface)
		return E_NOINTERFACE;
	HRESULT hr = gOrigD3D12GetInterface(clsid, riid, object);
	DX12LogManager::JsonFunc("D3D12GetInterface",
		"\"clsid\":\"%p\",\"riid\":\"%p\",\"hr\":\"0x%lx\",\"object\":\"%p\"",
		&clsid, &riid, hr, object ? *object : nullptr);
	if (SUCCEEDED(hr) && object && *object)
		DX12HookDeviceFactory(static_cast<IUnknown*>(*object));
	return hr;
}

extern "C" UINT_PTR WINAPI D3D12Ordinal99(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc(static_cast<WORD>(99), a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI SetAppCompatStringPointer(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("SetAppCompatStringPointer", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12CoreCreateLayeredDevice(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12CoreCreateLayeredDevice", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12CoreGetLayeredDeviceSize(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12CoreGetLayeredDeviceSize", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12CoreRegisterLayers(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12CoreRegisterLayers", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12DeviceRemovedExtendedData(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12DeviceRemovedExtendedData", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXEventsReplaceBlock(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12PIXEventsReplaceBlock", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXGetThreadInfo(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12PIXGetThreadInfo", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXNotifyWakeFromFenceSignal(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12PIXNotifyWakeFromFenceSignal", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI D3D12PIXReportCounter(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("D3D12PIXReportCounter", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" UINT_PTR WINAPI GetBehaviorValue(
	UINT_PTR a1, UINT_PTR a2, UINT_PTR a3, UINT_PTR a4,
	UINT_PTR a5, UINT_PTR a6, UINT_PTR a7, UINT_PTR a8)
{
	return CallRealD3D12Proc("GetBehaviorValue", a1, a2, a3, a4, a5, a6, a7, a8);
}

extern "C" LRESULT CALLBACK CBTProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(module);
		if (!VerifyIntendedTarget(module))
			return FALSE;
		DX12Initialize(module);
		break;
	case DLL_PROCESS_DETACH:
		DX12Shutdown();
		break;
	}
	return TRUE;
}


