#pragma once

#include "Hooks/DX12CommandFlow.hpp"

class DX12HookRegistry
{
public:
	SRWLOCK lock;
	CNktHookLib hookManager;
	std::unordered_set<void*> hookedFunctions;
	std::unordered_map<void*, void*> originalFunctions;
	volatile LONG hooksFinalized = 0;
	// Vtable method hooks are never installed in wrapper-only mode.  Keeping
	// the counter lets original-resolution cache raw vtable entries as positive
	// results (zero means no method code in d3d12.dll/dxgi.dll was patched).
	volatile LONG vtableHooksInstalled = 0;

	DX12HookRegistry()
	{
		InitializeSRWLock(&lock);
		// Install patches with all other threads suspended. Nektra's default
		// inline hook rewrites the target's entry bytes non-atomically; if the
		// game executes the function during the rewrite, the torn call can
		// corrupt a wait/sync/init sequence and leave the game in a broken
		// state (observed as permanent 100ms timeout polling, GPU pegged at
		// 99%, flicker) - randomly, depending on whether a thread happened to
		// be inside the patched function at install time. Suspending threads
		// during installation removes that race for every export hook.
		hookManager.SetSuspendThreadsWhileHooking(TRUE);
	}

	static DX12HookRegistry &Get()
	{
		static DX12HookRegistry state;
		return state;
	}

	static bool RememberHook(void *target)
	{
		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		bool inserted = state.hookedFunctions.insert(target).second;
		ReleaseSRWLockExclusive(&state.lock);
		return inserted;
	}

	static void RememberOriginal(void *target, void *original)
	{
		if (!target || !original)
			return;

		DX12HookRegistry &state = Get();
		AcquireSRWLockExclusive(&state.lock);
		state.originalFunctions[target] = original;
		ReleaseSRWLockExclusive(&state.lock);
	}

	static DWORD HookFunction(void **original, void *target, void *hook, const char *name)
	{
		if (!target || !hook)
			return ERROR_PROC_NOT_FOUND;
		if (!RememberHook(target))
			return ERROR_SUCCESS;

		SIZE_T hookId = 0;
		DWORD result = Get().hookManager.Hook(&hookId, original, target, hook);
		if (result == ERROR_SUCCESS && original)
			RememberOriginal(target, *original);
		DX12LogManager::Fields(name ? name : "HookInstall",
			"\"event\":\"HookInstall\",\"status\":\"%s\",\"target\":\"%p\",\"original\":\"%p\",\"result\":\"0x%lx\"",
			result == ERROR_SUCCESS ? "installed" : "failed",
			target, original ? *original : nullptr, result);
		return result;
	}

	static void FinalizeHooks()
	{
		InterlockedExchange(&Get().hooksFinalized, 1);
	}

	static bool AnyVtableHooksInstalled()
	{
		return InterlockedCompareExchange(&Get().vtableHooksInstalled, 0, 0) != 0;
	}

	static void *GetOriginalFunction(void *target)
	{
		if (!target)
			return nullptr;

		// Wrapper mode never patches vtables: an unhooked vtable entry IS
		// the original implementation, and the registry only ever contains
		// entries for actually patched targets. Skipping the lookup removes
		// a shared SRW lock acquire plus an unordered_map find from every
		// hot hook call (CopyDescriptors / Create*View run thousands of
		// times per frame).
		if (!AnyVtableHooksInstalled())
			return nullptr;

		DX12HookRegistry &state = Get();
		AcquireSRWLockShared(&state.lock);
		auto it = state.originalFunctions.find(target);
		void *original = it != state.originalFunctions.end() ? it->second : nullptr;
		ReleaseSRWLockShared(&state.lock);
		return original;
	}

	static void UnhookAll()
	{
		Get().hookManager.UnhookAll();
	}

};


class DX12HookManager
{
public:
	static DWORD InstallExportHook(HMODULE module, const char *exportName, void **original, void *hook)
	{
		if (!module || !exportName)
			return ERROR_PROC_NOT_FOUND;

		return DX12HookRegistry::HookFunction(original, GetProcAddress(module, exportName), hook, exportName);
	}
};


class DX12CommandListHookSlotTypes
{
public:
	class ID3D12GraphicsCommandList
	{
	public:
		static constexpr UINT Close = 9;
		static constexpr UINT Reset = 10;
		static constexpr UINT ClearState = 11;
		static constexpr UINT DrawInstanced = 12;
		static constexpr UINT DrawIndexedInstanced = 13;
		static constexpr UINT Dispatch = 14;
		static constexpr UINT CopyBufferRegion = 15;
		static constexpr UINT CopyTextureRegion = 16;
		static constexpr UINT CopyResource = 17;
		static constexpr UINT CopyTiles = 18;
		static constexpr UINT ResolveSubresource = 19;
		static constexpr UINT IaSetPrimitiveTopology = 20;
		static constexpr UINT RsSetViewports = 21;
		static constexpr UINT RsSetScissorRects = 22;
		static constexpr UINT OmSetBlendFactor = 23;
		static constexpr UINT OmSetStencilRef = 24;
		static constexpr UINT SetPipelineState = 25;
		static constexpr UINT ResourceBarrier = 26;
		static constexpr UINT ExecuteBundle = 27;
		static constexpr UINT SetDescriptorHeaps = 28;
		static constexpr UINT SetComputeRootSignature = 29;
		static constexpr UINT SetGraphicsRootSignature = 30;
		static constexpr UINT SetComputeRootDescriptorTable = 31;
		static constexpr UINT SetGraphicsRootDescriptorTable = 32;
		static constexpr UINT SetComputeRoot32BitConstant = 33;
		static constexpr UINT SetGraphicsRoot32BitConstant = 34;
		static constexpr UINT SetComputeRoot32BitConstants = 35;
		static constexpr UINT SetGraphicsRoot32BitConstants = 36;
		static constexpr UINT SetComputeRootConstantBufferView = 37;
		static constexpr UINT SetGraphicsRootConstantBufferView = 38;
		static constexpr UINT SetComputeRootShaderResourceView = 39;
		static constexpr UINT SetGraphicsRootShaderResourceView = 40;
		static constexpr UINT SetComputeRootUnorderedAccessView = 41;
		static constexpr UINT SetGraphicsRootUnorderedAccessView = 42;
		static constexpr UINT IaSetIndexBuffer = 43;
		static constexpr UINT IaSetVertexBuffers = 44;
		static constexpr UINT SoSetTargets = 45;
		static constexpr UINT OmSetRenderTargets = 46;
		static constexpr UINT ClearDepthStencilView = 47;
		static constexpr UINT ClearRenderTargetView = 48;
		static constexpr UINT ClearUnorderedAccessViewUint = 49;
		static constexpr UINT ClearUnorderedAccessViewFloat = 50;
		static constexpr UINT DiscardResource = 51;
		static constexpr UINT BeginQuery = 52;
		static constexpr UINT EndQuery = 53;
		static constexpr UINT ResolveQueryData = 54;
		static constexpr UINT SetPredication = 55;
		static constexpr UINT SetMarker = 56;
		static constexpr UINT BeginEvent = 57;
		static constexpr UINT EndEvent = 58;
		static constexpr UINT ExecuteIndirect = 59;
	};

	class ID3D12CommandQueue
	{
	public:
		static constexpr UINT UpdateTileMappings = 8;
		static constexpr UINT CopyTileMappings = 9;
		static constexpr UINT ExecuteCommandLists = 10;
		static constexpr UINT SetMarker = 11;
		static constexpr UINT BeginEvent = 12;
		static constexpr UINT EndEvent = 13;
		static constexpr UINT Signal = 14;
		static constexpr UINT Wait = 15;
		static constexpr UINT GetTimestampFrequency = 16;
		static constexpr UINT GetClockCalibration = 17;
	};

	class ID3D12Device
	{
	public:
		static constexpr UINT CreateCommandQueue = 8;
		static constexpr UINT CreateCommandAllocator = 9;
		static constexpr UINT CreateCommandList = 12;
	};

	class ID3D12Device4
	{
	public:
		static constexpr UINT CreateCommandList1 = 52;
	};
};



enum D3D12ProcessGateRole
{
	D3D12ProcessGateRoleReject = 0,
	D3D12ProcessGateRoleBootstrapOnly = 1,
	D3D12ProcessGateRoleTargetRuntime = 2
};

class D3D12ProcessGateDecision
{
public:
	bool allowed = false;
	D3D12ProcessGateRole role = D3D12ProcessGateRoleReject;
	wchar_t reason[256] = {};
	wchar_t modulePath[MAX_PATH] = {};
	wchar_t moduleDirectory[MAX_PATH] = {};
	wchar_t processPath[MAX_PATH] = {};
	wchar_t processDirectory[MAX_PATH] = {};
	wchar_t configPath[MAX_PATH] = {};
	wchar_t target[MAX_PATH] = {};
	wchar_t loader[MAX_PATH] = {};
};

class D3D12ProcessGateUtils
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module)
	{
		D3D12ProcessGateDecision decision = Evaluate(module);
		DebugDecision(decision);
		return decision.role != D3D12ProcessGateRoleReject;
	}

	static D3D12ProcessGateDecision Evaluate(HINSTANCE module)
	{
		D3D12ProcessGateDecision decision;
		if (!ReadPaths(module, &decision)) {
			CopyReason(&decision, L"path discovery failed");
			return decision;
		}

		if (ReadLoaderConfig(&decision)) {
			if (MatchesConfiguredTarget(decision.processPath, decision.target)) {
				decision.allowed = true;
				decision.role = D3D12ProcessGateRoleTargetRuntime;
				CopyReason(&decision, L"matched configured loader target");
			} else if (MatchesConfiguredLoader(decision.processPath, decision.loader) ||
					(IsSameDirectory(decision.moduleDirectory, decision.processDirectory) &&
						IsBootstrapProcess(decision.processPath))) {
				decision.allowed = true;
				decision.role = D3D12ProcessGateRoleBootstrapOnly;
				CopyReason(&decision, L"matched configured loader bootstrap");
			} else {
				CopyReason(&decision, L"rejected by configured loader target");
			}
			return decision;
		}

		if (IsSameDirectory(decision.moduleDirectory, decision.processDirectory)) {
			decision.allowed = true;
			decision.role = D3D12ProcessGateRoleTargetRuntime;
			CopyReason(&decision, L"same directory proxy fallback");
		} else {
			CopyReason(&decision, L"no configured target and not same directory");
		}
		return decision;
	}

	static bool ReadPaths(HINSTANCE module, D3D12ProcessGateDecision *decision)
	{
		wchar_t *moduleBase = nullptr;
		wchar_t *processBase = nullptr;

		if (!decision)
			return false;
		if (!GetModuleFileNameW(module, decision->modulePath, MAX_PATH))
			return false;
		if (!GetModuleFileNameW(nullptr, decision->processPath, MAX_PATH))
			return false;

		wcsncpy_s(decision->moduleDirectory, MAX_PATH, decision->modulePath, _TRUNCATE);
		wcsncpy_s(decision->processDirectory, MAX_PATH, decision->processPath, _TRUNCATE);

		moduleBase = wcsrchr(decision->moduleDirectory, L'\\');
		processBase = wcsrchr(decision->processDirectory, L'\\');
		if (!moduleBase || !processBase)
			return false;

		*moduleBase = L'\0';
		*processBase = L'\0';

		wcsncpy_s(decision->configPath, MAX_PATH, decision->moduleDirectory, _TRUNCATE);
		PathAppendW(decision->configPath, L"d3dx.ini");
		return true;
	}

	static bool ReadLoaderConfig(D3D12ProcessGateDecision *decision)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		DWORD fileSize = 0;
		DWORD readSize = 0;
		char *buffer = nullptr;
		const char *section = nullptr;
		char targetUtf8[MAX_PATH] = {};
		char loaderUtf8[MAX_PATH] = {};
		bool foundTarget = false;
		bool foundLoader = false;

		if (!decision)
			return false;

		file = CreateFileW(decision->configPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;

		fileSize = GetFileSize(file, nullptr);
		if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
			goto out_close;

		buffer = new char[fileSize + 1];
		if (!buffer)
			goto out_close;

		if (!ReadFile(file, buffer, fileSize, &readSize, nullptr) || fileSize != readSize)
			goto out_free;
		buffer[fileSize] = '\0';

		section = find_ini_section_lite(buffer, "loader");
		if (!section)
			goto out_free;
		if (find_ini_setting_lite(section, "target", targetUtf8, MAX_PATH) &&
				MultiByteToWideChar(CP_UTF8, 0, targetUtf8, -1, decision->target, MAX_PATH)) {
			TrimQuotes(decision->target);
			foundTarget = decision->target[0] != L'\0';
		}

		if (find_ini_setting_lite(section, "loader", loaderUtf8, MAX_PATH) &&
				MultiByteToWideChar(CP_UTF8, 0, loaderUtf8, -1, decision->loader, MAX_PATH)) {
			TrimQuotes(decision->loader);
			foundLoader = decision->loader[0] != L'\0';
		}

	out_free:
		delete [] buffer;
	out_close:
		CloseHandle(file);
		return foundTarget || foundLoader;
	}

	static bool MatchesConfiguredTarget(const wchar_t *processPath, const wchar_t *target)
	{
		size_t processLength = 0;
		size_t targetLength = 0;

		if (!processPath || !target || target[0] == L'\0')
			return false;

		processLength = wcslen(processPath);
		targetLength = wcslen(target);
		if (processLength < targetLength)
			return false;

		if (target[0] != L'\\' && target[1] != L':' && processLength > targetLength) {
			if (processPath[processLength - targetLength - 1] != L'\\')
				return false;
		}

		return _wcsicmp(processPath + processLength - targetLength, target) == 0;
	}

	static bool IsSameDirectory(const wchar_t *left, const wchar_t *right)
	{
		if (!left || !right)
			return false;
		return _wcsicmp(left, right) == 0;
	}

	static bool MatchesConfiguredLoader(const wchar_t *processPath, const wchar_t *loader)
	{
		const wchar_t *baseName = nullptr;

		if (!processPath || !loader || loader[0] == L'\0')
			return false;

		baseName = wcsrchr(processPath, L'\\');
		if (baseName)
			baseName++;
		else
			baseName = processPath;

		return _wcsicmp(baseName, loader) == 0;
	}

	static bool IsBootstrapProcess(const wchar_t *processPath)
	{
		const wchar_t *baseName = nullptr;

		if (!processPath)
			return false;

		baseName = wcsrchr(processPath, L'\\');
		if (baseName)
			baseName++;
		else
			baseName = processPath;

		return _wcsicmp(baseName, L"Run.exe") == 0;
	}

	static const wchar_t *RoleName(D3D12ProcessGateRole role)
	{
		switch (role) {
		case D3D12ProcessGateRoleBootstrapOnly:
			return L"bootstrap";
		case D3D12ProcessGateRoleTargetRuntime:
			return L"target";
		default:
			return L"reject";
		}
	}

	static void TrimQuotes(wchar_t *text)
	{
		size_t length = 0;

		if (!text)
			return;

		length = wcslen(text);
		if (length >= 2 && text[0] == L'"' && text[length - 1] == L'"') {
			memmove(text, text + 1, (length - 2) * sizeof(wchar_t));
			text[length - 2] = L'\0';
		}
	}

	static void CopyReason(D3D12ProcessGateDecision *decision, const wchar_t *reason)
	{
		if (!decision || !reason)
			return;
		wcsncpy_s(decision->reason, 256, reason, _TRUNCATE);
	}

	static void DebugDecision(const D3D12ProcessGateDecision &decision)
	{
		wchar_t line[1024] = {};
		swprintf_s(line, 1024,
			L"[Bunny-D3D12Gate] %s role=%s: pid=%lu process=%s module=%s target=%s loader=%s config=%s reason=%s\n",
			decision.allowed ? L"allow" : L"deny",
			RoleName(decision.role),
			GetCurrentProcessId(),
			decision.processPath,
			decision.modulePath,
			decision.target,
			decision.loader,
			decision.configPath,
			decision.reason);
		OutputDebugStringW(line);
	}
};


class D3D12ProxyRuntime
{
public:
	static HMODULE LoadRealD3D12();
	static void UnloadRealD3D12();
	static FARPROC GetRealProc(const char *name);
	static FARPROC GetRealProc(WORD ordinal);
	static UINT_PTR CallRealProc(
		const char *name,
		UINT_PTR a1 = 0, UINT_PTR a2 = 0, UINT_PTR a3 = 0, UINT_PTR a4 = 0,
		UINT_PTR a5 = 0, UINT_PTR a6 = 0, UINT_PTR a7 = 0, UINT_PTR a8 = 0);
	static UINT_PTR CallRealProc(
		WORD ordinal,
		UINT_PTR a1 = 0, UINT_PTR a2 = 0, UINT_PTR a3 = 0, UINT_PTR a4 = 0,
		UINT_PTR a5 = 0, UINT_PTR a6 = 0, UINT_PTR a7 = 0, UINT_PTR a8 = 0);
	static void Initialize(HINSTANCE module);
	static void Shutdown();
};


class D3D12ProxySignatures
{
public:
	using CreateDevice = HRESULT(WINAPI *)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
	using GetDebugInterface = HRESULT(WINAPI *)(REFIID, void**);
	using SerializeRootSignature = HRESULT(WINAPI *)(
		const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
	using CreateRootSignatureDeserializer = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void**);
	using SerializeVersionedRootSignature = HRESULT(WINAPI *)(
		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
	using CreateVersionedRootSignatureDeserializer = HRESULT(WINAPI *)(LPCVOID, SIZE_T, REFIID, void**);
	using EnableExperimentalFeatures = HRESULT(WINAPI *)(UINT, const IID*, void*, UINT*);
	using GetInterface = HRESULT(WINAPI *)(REFCLSID, REFIID, void**);
	using Generic = UINT_PTR(WINAPI *)(
		UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR,
		UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
};



class D3D12ProxyState
{
public:
	inline static HMODULE RealD3D12 = nullptr;
	inline static D3D12ProxySignatures::CreateDevice OrigCreateDevice = nullptr;
	inline static D3D12ProxySignatures::GetDebugInterface OrigGetDebugInterface = nullptr;
	inline static D3D12ProxySignatures::SerializeRootSignature OrigSerializeRootSignature = nullptr;
	inline static D3D12ProxySignatures::CreateRootSignatureDeserializer OrigCreateRootSignatureDeserializer = nullptr;
	inline static D3D12ProxySignatures::SerializeVersionedRootSignature OrigSerializeVersionedRootSignature = nullptr;
	inline static D3D12ProxySignatures::CreateVersionedRootSignatureDeserializer OrigCreateVersionedRootSignatureDeserializer = nullptr;
	inline static D3D12ProxySignatures::EnableExperimentalFeatures OrigEnableExperimentalFeatures = nullptr;
	inline static D3D12ProxySignatures::GetInterface OrigGetInterface = nullptr;
};


class D3D12ProxyTargetHelper
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module);
};
