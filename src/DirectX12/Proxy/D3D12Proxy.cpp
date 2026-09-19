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
		DX12_LOG_TEXT("Failed to resolve real d3d12 export %s, error=%lu\n", name, GetLastError());
	return proc;
}

FARPROC D3D12ProxyRuntime::GetRealProc(WORD ordinal)
{
	HMODULE module = D3D12ProxyRuntime::LoadRealD3D12();
	if (!module)
		return nullptr;

	FARPROC proc = GetProcAddress(module, MAKEINTRESOURCEA(ordinal));
	if (!proc)
		DX12_LOG_TEXT("Failed to resolve real d3d12 ordinal %u, error=%lu\n", ordinal, GetLastError());
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
		DX12_LOG_TEXT("Failed to load real d3d12.dll from %S, error=%lu\n", path, GetLastError());
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

	DX12_LOG_TEXT("Loaded real d3d12.dll: %p\n", D3D12ProxyState::RealD3D12);
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
	DX12_PROF_CREATE_SCOPE(ProxyDeviceCreate, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateDevice)
		return E_FAIL;
	DX12_LOG_FLUSH("D3D12CreateDevice",
		"event=enter,threadId=%lu,adapter=%p,minFeature=0x%x,outputRequested=%s",
		GetCurrentThreadId(), adapter, minimumFeatureLevel, device ? "true" : "false");
	HRESULT hr = D3D12ProxyState::OrigCreateDevice(adapter, minimumFeatureLevel, riid, device);
	void *nativeDevice = SUCCEEDED(hr) && device ? *device : nullptr;
	const bool wrapped = SUCCEEDED(hr) && DX12Wrapper::WrapD3D12DeviceResult(riid, device);
	DX12_LOG_FLUSH("D3D12CreateDevice",
		"event=leave,threadId=%lu,hr=0x%lx,native=%p,returned=%p,wrapped=%s",
		GetCurrentThreadId(), hr, nativeDevice, SUCCEEDED(hr) && device ? *device : nullptr,
		wrapped ? "true" : "false");
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void **debug)
{
	DX12_PROF_CREATE_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigGetDebugInterface)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigGetDebugInterface(riid, debug);
	DX12_LOG("D3D12GetDebugInterface",
		"hr=0x%lx,debug=%p", hr, debug ? *debug : nullptr);
	if (SUCCEEDED(hr))
		DX12Wrapper::WrapD3D12DebugInterfaceResult(riid, debug);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeRootSignature(
	const D3D12_ROOT_SIGNATURE_DESC *desc, D3D_ROOT_SIGNATURE_VERSION version,
	ID3DBlob **blob, ID3DBlob **errorBlob)
{
	DX12_PROF_CREATE_SCOPE(RootSignatureSerializeProxy, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigSerializeRootSignature)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigSerializeRootSignature(desc, version, blob, errorBlob);
	DX12_LOG("D3D12SerializeRootSignature",
		"version=%u,hr=0x%lx", static_cast<UINT>(version), hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	DX12_PROF_CREATE_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateRootSignatureDeserializer(
		srcData, srcDataSize, riid, rootSignatureDeserializer);
	DX12_LOG("D3D12CreateRootSignatureDeserializer",
		"size=%llu,hr=0x%lx,object=%p",
		static_cast<unsigned long long>(srcDataSize), hr,
		rootSignatureDeserializer ? *rootSignatureDeserializer : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12SerializeVersionedRootSignature(
	const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **errorBlob)
{
	DX12_PROF_CREATE_SCOPE(RootSignatureSerializeProxy, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigSerializeVersionedRootSignature)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigSerializeVersionedRootSignature(desc, blob, errorBlob);
	DX12_LOG("D3D12SerializeVersionedRootSignature", "hr=0x%lx", hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
	LPCVOID srcData, SIZE_T srcDataSize, REFIID riid, void **rootSignatureDeserializer)
{
	DX12_PROF_CREATE_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigCreateVersionedRootSignatureDeserializer(
		srcData, srcDataSize, riid, rootSignatureDeserializer);
	DX12_LOG("D3D12CreateVersionedRootSignatureDeserializer",
		"size=%llu,hr=0x%lx,object=%p",
		static_cast<unsigned long long>(srcDataSize), hr,
		rootSignatureDeserializer ? *rootSignatureDeserializer : nullptr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12EnableExperimentalFeatures(
	UINT numFeatures, const IID *featureIIDs, void *configurationStructs, UINT *configurationStructSizes)
{
	DX12_PROF_CREATE_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigEnableExperimentalFeatures)
		return E_FAIL;
	HRESULT hr = D3D12ProxyState::OrigEnableExperimentalFeatures(
		numFeatures, featureIIDs, configurationStructs, configurationStructSizes);
	DX12_LOG("D3D12EnableExperimentalFeatures",
		"features=%u,hr=0x%lx", numFeatures, hr);
	return hr;
}

extern "C" HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID riid, void **object)
{
	DX12_PROF_CREATE_SCOPE(ProxyMisc, Original);
	D3D12ProxyRuntime::LoadRealD3D12();
	if (!D3D12ProxyState::OrigGetInterface)
		return E_NOINTERFACE;
	HRESULT hr = D3D12ProxyState::OrigGetInterface(clsid, riid, object);
	DX12_LOG("D3D12GetInterface",
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

namespace
{
	// Only redirect the executable's creation entry points. Process-wide export
	// detours would also wrap Streamline's private devices and reintroduce hook
	// chain conflicts. Fixed storage keeps attach/detach free of allocation.
	struct D3D12ImportRedirect
	{
		void **slot = nullptr;
		void *original = nullptr;
		void *replacement = nullptr;
	};
	D3D12ImportRedirect gImportRedirects[64] = {};
	size_t gImportRedirectCount = 0;

	bool ImageRangeValid(DWORD rva, size_t bytes, DWORD imageSize)
	{
		return rva < imageSize && bytes <= imageSize - rva;
	}

	const char *ImageString(BYTE *base, DWORD rva, DWORD imageSize)
	{
		if (!ImageRangeValid(rva, 1, imageSize))
			return nullptr;
		const char *text = reinterpret_cast<const char*>(base + rva);
		return memchr(text, '\0', imageSize - rva) ? text : nullptr;
	}

	void *CreationImportTarget(BYTE *base, ULONGLONG lookup, DWORD imageSize)
	{
		if (IMAGE_SNAP_BY_ORDINAL64(lookup)) {
			switch (IMAGE_ORDINAL64(lookup)) {
			case 101: return reinterpret_cast<void*>(&D3D12CreateDevice);
			case 102: return reinterpret_cast<void*>(&D3D12GetDebugInterface);
			case 110: return reinterpret_cast<void*>(&D3D12GetInterface);
			default: return nullptr;
			}
		}
		if (lookup > MAXDWORD - sizeof(WORD))
			return nullptr;
		const char *name = ImageString(base, static_cast<DWORD>(lookup) + sizeof(WORD), imageSize);
		if (!name)
			return nullptr;
		if (strcmp(name, "D3D12CreateDevice") == 0)
			return reinterpret_cast<void*>(&D3D12CreateDevice);
		if (strcmp(name, "D3D12GetDebugInterface") == 0)
			return reinterpret_cast<void*>(&D3D12GetDebugInterface);
		if (strcmp(name, "D3D12GetInterface") == 0)
			return reinterpret_cast<void*>(&D3D12GetInterface);
		return nullptr;
	}

	void RedirectMainD3D12Imports()
	{
		BYTE *base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
		if (!base)
			return;
		const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
			return;
		const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			return;
		const DWORD size = nt->OptionalHeader.SizeOfImage;
		if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
			return;
		const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!directory.VirtualAddress || !ImageRangeValid(directory.VirtualAddress, directory.Size, size))
			return;
		wchar_t systemPath[MAX_PATH] = {};
		if (!GetSystemDirectoryW(systemPath, ARRAYSIZE(systemPath)) || !PathAppendW(systemPath, L"d3d12.dll"))
			return;
		HMODULE systemD3D12 = GetModuleHandleW(systemPath);
		if (!systemD3D12)
			return;
		const auto *imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
		for (size_t i = 0; i < directory.Size / sizeof(*imports) && imports[i].Name; ++i) {
			const auto &entry = imports[i];
			const char *name = ImageString(base, entry.Name, size);
			if (!name || _stricmp(name, "d3d12.dll") != 0)
				continue;
			// With no lookup table, a bound IAT contains addresses, not import
			// names. Never guess which entry point an arbitrary address denotes.
			if (!entry.OriginalFirstThunk || !entry.FirstThunk)
				continue;
			for (size_t index = 0; ; ++index) {
				const size_t offset = index * sizeof(IMAGE_THUNK_DATA64);
				if (!ImageRangeValid(entry.OriginalFirstThunk, offset + sizeof(IMAGE_THUNK_DATA64), size) ||
					!ImageRangeValid(entry.FirstThunk, offset + sizeof(IMAGE_THUNK_DATA64), size))
					break;
				const auto *lookup = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry.OriginalFirstThunk);
				if (!lookup[index].u1.AddressOfData)
					break;
				void *target = CreationImportTarget(base, lookup[index].u1.AddressOfData, size);
				if (!target)
					continue;
				void **slot = reinterpret_cast<void**>(base + entry.FirstThunk + offset);
				if (*slot == target)
					continue; // Already bound to our proxy by the Windows loader.
				const ULONGLONG importLookup = lookup[index].u1.AddressOfData;
				const char *importName = IMAGE_SNAP_BY_ORDINAL64(importLookup) ?
					MAKEINTRESOURCEA(IMAGE_ORDINAL64(importLookup)) :
					ImageString(base, static_cast<DWORD>(importLookup) + sizeof(WORD), size);
				void *expected = reinterpret_cast<void*>(GetProcAddress(systemD3D12, importName));
				if (*slot != expected) {
					DX12_LOG_FLUSH("DX12ImportRedirect", "event=foreign_hook_preserved,slot=%p,current=%p", slot, *slot);
					continue;
				}
				if (gImportRedirectCount == ARRAYSIZE(gImportRedirects))
					break;
				DWORD protection = 0;
				if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
					DX12_LOG_FLUSH("DX12ImportRedirect", "event=protect_failed,error=%lu", GetLastError());
					continue;
				}
				void *original = InterlockedCompareExchangePointer(slot, target, expected);
				const bool installed = original == expected;
				if (installed)
					gImportRedirects[gImportRedirectCount++] = {slot, original, target};
				DWORD ignored = 0;
				const BOOL restored = VirtualProtect(slot, sizeof(*slot), protection, &ignored);
				if (!installed) {
					DX12_LOG_FLUSH("DX12ImportRedirect", "event=concurrent_hook_preserved,slot=%p", slot);
					continue;
				}
				DX12_LOG_FLUSH("DX12ImportRedirect",
					"event=installed,slot=%p,original=%p,target=%p,byOrdinal=%s,protectionRestored=%s",
					slot, original, target, IMAGE_SNAP_BY_ORDINAL64(lookup[index].u1.Ordinal) ? "true" : "false",
					restored ? "true" : "false");
			}
		}
		DX12_LOG_FLUSH("DX12ImportRedirect", "event=complete,count=%llu,scope=main_executable_creation_imports",
			static_cast<unsigned long long>(gImportRedirectCount));
	}

	void RestoreMainD3D12Imports()
	{
		while (gImportRedirectCount) {
			const auto &entry = gImportRedirects[--gImportRedirectCount];
			DWORD protection = 0;
			if (!VirtualProtect(entry.slot, sizeof(*entry.slot), PAGE_READWRITE, &protection))
				continue;
			// Do not overwrite a hook installed after ours.
			InterlockedCompareExchangePointer(entry.slot, entry.original, entry.replacement);
			DWORD ignored = 0;
			VirtualProtect(entry.slot, sizeof(*entry.slot), protection, &ignored);
		}
	}
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
		RedirectMainD3D12Imports();
		DX12_LOG_FLUSH("DX12Bootstrap",
			"event=attach,threadId=%lu,proxy=\"%S\",process=\"%S\",reason=\"%S\"",
			GetCurrentThreadId(), decision.modulePath, decision.processPath, decision.reason);
		gD3D12ProxyRuntimeInitialized = true;
		break;
	}
	case DLL_PROCESS_DETACH:
		if (gD3D12ProxyRuntimeInitialized) {
			RestoreMainD3D12Imports();
			// DX11 parity: DestroyDLL() saves the persistent settings before the
			// runtime tears down. A clean game exit is the last chance to
			// capture hotkey state the autosave interval has not written yet.
			DX12ModSavePersistStateOnExit();
			DX12Wrapper::Shutdown();
			D3D12ProxyRuntime::Shutdown();
			gD3D12ProxyRuntimeInitialized = false;
		}
		break;
	}
	return TRUE;
}
