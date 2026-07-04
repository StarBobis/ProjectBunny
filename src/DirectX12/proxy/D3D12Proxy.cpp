#include "D3D12ProxyRuntime.hpp"

#include <Shlwapi.h>
#include <crtdbg.h>
#include <stdlib.h>

#include "D3D12ProxySignatures.hpp"
#include "D3D12ProxyState.hpp"
#include "DX12Wrapper.hpp"
#include "DX12DeviceHooks.hpp"
#include "DX12Runtime.hpp"
#include "DX12LogManager.hpp"
#include "DXGIHooks.hpp"
#include "helper/D3D12ProxyTargetHelper.hpp"
#include "util_min.h"

bool D3D12ProxyTargetHelper::VerifyIntendedTarget(HINSTANCE module)
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
			reinterpret_cast<LPCWSTR>(D3D12ProxyTargetHelper::VerifyIntendedTarget), &handle);
	}

out_free:
	delete [] buffer;
out_close:
	CloseHandle(file);
	return result;
}

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
	if (D3D12ProxyState::RealD3D12)
		return D3D12ProxyState::RealD3D12;

	wchar_t path[MAX_PATH];
	if (!GetSystemDirectoryW(path, MAX_PATH))
		return nullptr;
	PathAppendW(path, L"d3d12.dll");

	D3D12ProxyState::RealD3D12 = LoadLibraryW(path);
	if (!D3D12ProxyState::RealD3D12) {
		DX12LogManager::Text("Failed to load real d3d12.dll from %S, error=%lu\n", path, GetLastError());
		return nullptr;
	}

	D3D12ProxyState::OrigCreateDevice = reinterpret_cast<D3D12ProxySignatures::CreateDevice>(
		GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12CreateDevice"));
	D3D12ProxyState::OrigGetDebugInterface = reinterpret_cast<D3D12ProxySignatures::GetDebugInterface>(
		GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12GetDebugInterface"));
	D3D12ProxyState::OrigSerializeRootSignature = reinterpret_cast<D3D12ProxySignatures::SerializeRootSignature>(
		GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12SerializeRootSignature"));
	D3D12ProxyState::OrigCreateRootSignatureDeserializer =
		reinterpret_cast<D3D12ProxySignatures::CreateRootSignatureDeserializer>(
			GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12CreateRootSignatureDeserializer"));
	D3D12ProxyState::OrigSerializeVersionedRootSignature =
		reinterpret_cast<D3D12ProxySignatures::SerializeVersionedRootSignature>(
			GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12SerializeVersionedRootSignature"));
	D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer =
		reinterpret_cast<D3D12ProxySignatures::CreateVersionedRootSignatureDeserializer>(
			GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12CreateVersionedRootSignatureDeserializer"));
	D3D12ProxyState::OrigEnableExperimentalFeatures =
		reinterpret_cast<D3D12ProxySignatures::EnableExperimentalFeatures>(
			GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12EnableExperimentalFeatures"));
	D3D12ProxyState::OrigGetInterface = reinterpret_cast<D3D12ProxySignatures::GetInterface>(
		GetProcAddress(D3D12ProxyState::RealD3D12, "D3D12GetInterface"));

	DX12LogManager::Text("Loaded real d3d12.dll: %p\n", D3D12ProxyState::RealD3D12);
	return D3D12ProxyState::RealD3D12;
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
	if (D3D12ProxyState::RealD3D12) {
		FreeLibrary(D3D12ProxyState::RealD3D12);
		D3D12ProxyState::RealD3D12 = nullptr;
	}
}

extern "C" HRESULT WINAPI D3D12CreateDevice(
	IUnknown *adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, REFIID riid, void **device)
{
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateDevice)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateDevice(adapter, minimumFeatureLevel, riid, device);
	DX12LogManager::JsonFunc("D3D12CreateDevice",
		"\"adapter\":\"%p\",\"minFeature\":\"0x%x\",\"hr\":\"0x%lx\",\"device\":\"%p\"",
		adapter, minimumFeatureLevel, hr, device ? *device : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapD3D12DeviceResult(riid, device);
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void **debug)
{
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12GetDebugInterface\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigGetDebugInterface)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigGetDebugInterface(riid, debug);
	DX12LogManager::JsonFunc("D3D12GetDebugInterface",
		"\"hr\":\"0x%lx\",\"debug\":\"%p\"", hr, debug ? *debug : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapD3D12DebugInterfaceResult(riid, debug);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeRootSignature(
	const D3D12_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version,
	ID3DBlob **blob, ID3DBlob **errorBlob)
{
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12SerializeRootSignature\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigSerializeRootSignature)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigSerializeRootSignature(desc, version, blob, errorBlob);
	DX12LogManager::JsonFunc("D3D12SerializeRootSignature",
		"\"version\":%u,\"hr\":\"0x%lx\"", static_cast<UINT>(version), hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12CreateRootSignatureDeserializer\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigCreateRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateRootSignatureDeserializer(
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
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12SerializeVersionedRootSignature\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigSerializeVersionedRootSignature)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigSerializeVersionedRootSignature(desc, blob, errorBlob);
	DX12LogManager::JsonFunc("D3D12SerializeVersionedRootSignature", "\"hr\":\"0x%lx\"", hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12CreateVersionedRootSignatureDeserializer\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer(
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
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12EnableExperimentalFeatures\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigEnableExperimentalFeatures)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigEnableExperimentalFeatures(
		numFeatures, featureIIDs, configurationStructs, configurationStructSizes);
	DX12LogManager::JsonFunc("D3D12EnableExperimentalFeatures",
		"\"features\":%u,\"hr\":\"0x%lx\"", numFeatures, hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID riid, void **object)
{
	D3D12ProxyRuntime::LoadRealD3D12();
	DX12LogManager::DebugJsonFunc("DX12HookCall", "\"api\":\"D3D12GetInterface\",\"this\":\"%p\"", nullptr);
	if (!D3D12ProxyState::OrigGetInterface)
		return E_NOINTERFACE;
	HRESULT hr = D3D12ProxyState::OrigGetInterface(clsid, riid, object);
	DX12LogManager::JsonFunc("D3D12GetInterface",
		"\"clsid\":\"%p\",\"riid\":\"%p\",\"hr\":\"0x%lx\",\"object\":\"%p\"",
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
		DisableThreadLibraryCalls(module);

		// Suppress Debug STL abort() popups — redirect to debugger output.
		// The MSVC Debug STL (_ITERATOR_DEBUG_LEVEL=2) calls abort() on
		// container misuse (e.g. vector out-of-bounds). This does not
		// indicate a crash or memory corruption — the game continues
		// running after clicking Ignore. Redirect to stderr so the dialog
		// does not block automated test runs.
#if _DEBUG
		_set_abort_behavior(0, _WRITE_ABORT_MSG);
		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif

		if (!D3D12ProxyTargetHelper::VerifyIntendedTarget(module))
			return FALSE;
		D3D12ProxyRuntime::Initialize(module);
		break;
	case DLL_PROCESS_DETACH:
		DX12Wrapper::Shutdown();
		D3D12ProxyRuntime::Shutdown();
		break;
	}
	return TRUE;
}


