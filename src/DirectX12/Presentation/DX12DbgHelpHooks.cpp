// DX12DbgHelpHooks: keeps symbol resolution offline.
//
// Root cause of a game-freeze class: engines that symbolize a native stack
// trace on demand (observed: the moment DLSS frame generation is toggled)
// call into dbghelp with a search path that carries a srv*/symsrv* symbol
// server token. dbghelp then routes the lookup through symsrv.dll, which
// fetches the PDB over the network with wininet - on the calling thread, with
// no usable timeout when the route to the symbol server is blackholed. The
// render thread parked inside wininet freezes the game for minutes (observed
// as a permanent hang; the freeze watchdog snapshot showed
// ntdll!NtWaitForSingleObject <- wininet <- symsrv <- dbghelp <- UnityPlayer
// on the render thread while every other thread was idle and healthy).
//
// Two layers, both process-wide and build-independent:
//
// 1. dbghelp entry points (SymInitialize/SymSetSearchPath, ANSI and wide) get
//    their search-path argument sanitized: srv*/symsrv*/URL tokens are
//    stripped, local-only entries (including single-asterisk srv caches)
//    survive. Local PDB resolution is untouched.
//
// 2. symsrv.dll's retrieval exports are stubbed to fail fast. This is the
//    load-bearing layer: the engine was observed to configure its symbol
//    session before this proxy DLL existed (or on a process handle we cannot
//    enumerate), so no path-level interception can reliably reach the stored
//    configuration. Every network fetch funnels through the SymbolServer*
//    retrieval family; making those read as "not in store" removes the
//    network from the equation regardless of who configured what, when.
//
// A kernel32 LoadLibrary* hook fires both layers no matter how late the two
// DLLs load, the symbol-path environment variables are scrubbed once for
// callers that pass NULL and let dbghelp read the environment, and the
// process's loaded-module list is swept at install time and again whenever
// the game freezes.
//
// Why the module sweep is load-bearing: the freeze snapshot showed
//   ntdll!NtWaitForSingleObject <- wininet <- symsrv <- dbghelp <- UnityPlayer
// with symsrv resolved from "C:\Program Files (x86)\Windows Kits\10\
// Debuggers\x64\" - the engine loads its own dbghelp copy from the debuggers
// directory and that copy pulls its sibling symsrv in through a path that
// never reaches the hooked kernel32 exports. Hook-on-LoadLibrary alone left
// that symsrv un-stubbed, so the network fetch happened anyway:
//   thread 2092 (the render thread) parked inside wininet forever while the
//   game's present counter stopped at 4381.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "../Resources/DX12RuntimeControl.h"

#include <tlhelp32.h>
#include <wchar.h>

namespace
{
	// Sanitized-path scratch buffers. Symbol-path calls are one-time init
	// work, never hot-path; one buffer per width plus one lock is plenty.
	// The lock is held across the forwarded call so the callee's copy of the
	// string cannot be overwritten by a racing sanitize.
	wchar_t gSanitizedPathW[4096];
	char gSanitizedPathA[4096];
	SRWLOCK gPathLock = SRWLOCK_INIT;

	// One log line per distinct dll file: the engine loads its dbghelp several
	// times per session (each stack-trace request), and a scan would otherwise
	// repeat the same finding on every sweep.
	constexpr unsigned int kMaxReportedModules = 16;
	wchar_t gReportedModules[kMaxReportedModules][MAX_PATH] = {};
	unsigned int gReportedModuleCount = 0;
	SRWLOCK gReportLock = SRWLOCK_INIT;

	bool ModuleFileIs(const wchar_t *path, const wchar_t *name)
	{
		const wchar_t *file = wcsrchr(path, L'\\');
		file = file ? file + 1 : path;
		return _wcsicmp(file, name) == 0;
	}

	// dbghelp ships as dbghelp.dll and (on some SDK versions) dbghelp_acp.dll;
	// both resolve the same way, so match the family rather than one name.
	bool FileNameStartsWithDll(const wchar_t *path, const wchar_t *prefix)
	{
		const wchar_t *file = wcsrchr(path, L'\\');
		file = file ? file + 1 : path;
		const size_t length = wcslen(file);
		const size_t prefixLength = wcslen(prefix);
		if (length < prefixLength + 4)
			return false;
		return _wcsnicmp(file, prefix, prefixLength) == 0 &&
			_wcsicmp(file + length - 4, L".dll") == 0;
	}

	bool IsDbgHelpFileName(const wchar_t *path)
	{
		return FileNameStartsWithDll(path, L"dbghelp");
	}

	bool IsSymSrvFileName(const wchar_t *path)
	{
		return FileNameStartsWithDll(path, L"symsrv");
	}

	void ReportModuleOnce(const char *event, const wchar_t *path)
	{
		AcquireSRWLockExclusive(&gReportLock);
		bool seen = false;
		for (unsigned int i = 0; i < gReportedModuleCount; ++i) {
			if (_wcsicmp(gReportedModules[i], path) == 0) {
				seen = true;
				break;
			}
		}
		if (!seen && gReportedModuleCount < kMaxReportedModules) {
			wcsncpy_s(gReportedModules[gReportedModuleCount], path, _TRUNCATE);
			++gReportedModuleCount;
		}
		ReleaseSRWLockExclusive(&gReportLock);
		if (!seen)
			DX12_LOG_FLUSH("DX12DbgHelpHooks",
				"\"event\":\"%s\",\"module\":\"%S\"", event, path);
	}

	size_t CountCharW(const wchar_t *begin, size_t length, wchar_t needle)
	{
		size_t count = 0;
		for (size_t i = 0; i < length; ++i) {
			if (begin[i] == needle)
				++count;
		}
		return count;
	}

	bool ContainsSubsequenceW(const wchar_t *begin, size_t length,
		const wchar_t *needle, size_t needleLength)
	{
		if (needleLength > length)
			return false;
		for (size_t i = 0; i + needleLength <= length; ++i) {
			if (wcsncmp(begin + i, needle, needleLength) == 0)
				return true;
		}
		return false;
	}

	// A dbghelp search path is a ';'-separated token list. Network access
	// comes from exactly three token shapes:
	//   srv*<cache>*<store>     - second asterisk means a store follows
	//   symsrv*<dll>*<cache>*<store>
	//   any bare URL token
	// srv*<cache> with a single asterisk is a local cache and stays.
	bool TokenTriggersNetworkW(const wchar_t *begin, size_t length)
	{
		if (length >= 4 && _wcsnicmp(begin, L"srv*", 4) == 0)
			return CountCharW(begin, length, L'*') >= 2;
		if (length >= 7 && _wcsnicmp(begin, L"symsrv*", 7) == 0)
			return CountCharW(begin, length, '*') >= 3;
		return ContainsSubsequenceW(begin, length, L"://", 3);
	}

	bool TokenTriggersNetworkA(const char *begin, size_t length)
	{
		size_t networkAsterisks = 0;
		if (length >= 4 && _strnicmp(begin, "srv*", 4) == 0) {
			networkAsterisks = 2;
		} else if (length >= 7 && _strnicmp(begin, "symsrv*", 7) == 0) {
			networkAsterisks = 3;
		} else {
			// A bare URL token is not a valid search path entry on its own,
			// but keep the walk conservative: anything http(s) is network.
			if (length >= 3) {
				for (size_t i = 0; i + 3 <= length; ++i) {
					if (strncmp(begin + i, "://", 3) == 0)
						return true;
				}
			}
			return false;
		}
		size_t asterisks = 0;
		for (size_t i = 0; i < length; ++i) {
			if (begin[i] == '*')
				++asterisks;
		}
		return asterisks >= networkAsterisks;
	}

	// Copies path into the matching scratch buffer with every network token
	// removed. Requires gPathLock held. Returns true when at least one token
	// was stripped; the result (possibly empty) is in gSanitizedPath*.
	bool SanitizeSearchPathWLocked(const wchar_t *path)
	{
		wchar_t *out = gSanitizedPathW;
		size_t remaining = ARRAYSIZE(gSanitizedPathW);
		bool changed = false;
		const wchar_t *cursor = path;
		while (cursor && *cursor) {
			const wchar_t *semi = wcschr(cursor, L';');
			const size_t length = semi ?
				static_cast<size_t>(semi - cursor) : wcslen(cursor);
			if (TokenTriggersNetworkW(cursor, length)) {
				changed = true;
			} else if (length > 0 && remaining > length + 1) {
				if (out != gSanitizedPathW) {
					*out++ = L';';
					--remaining;
				}
				wmemcpy(out, cursor, length);
				out += length;
				remaining -= length;
			}
			cursor = semi ? semi + 1 : nullptr;
		}
		*out = L'\0';
		return changed;
	}

	bool SanitizeSearchPathALocked(const char *path)
	{
		char *out = gSanitizedPathA;
		size_t remaining = ARRAYSIZE(gSanitizedPathA);
		bool changed = false;
		const char *cursor = path;
		while (cursor && *cursor) {
			const char *semi = strchr(cursor, ';');
			const size_t length = semi ?
				static_cast<size_t>(semi - cursor) : strlen(cursor);
			if (TokenTriggersNetworkA(cursor, length)) {
				changed = true;
			} else if (length > 0 && remaining > length + 1) {
				if (out != gSanitizedPathA) {
					*out++ = ';';
					--remaining;
				}
				memcpy(out, cursor, length);
				out += length;
				remaining -= length;
			}
			cursor = semi ? semi + 1 : nullptr;
		}
		*out = '\0';
		return changed;
	}

	// dbghelp falls back to these when SymInitialize gets a NULL search path.
	// Stripping server tokens here closes the last path that reaches symsrv.
	void ScrubEnvironmentVariable(const wchar_t *name)
	{
		wchar_t value[4096];
		const DWORD length = GetEnvironmentVariableW(
			name, value, ARRAYSIZE(value));
		if (!length || length >= ARRAYSIZE(value) - 1)
			return;
		AcquireSRWLockExclusive(&gPathLock);
		const bool stripped = SanitizeSearchPathWLocked(value);
		if (stripped) {
			// Nothing local left: delete the variable entirely rather than
			// leave an empty one (dbghelp treats empty as "use default").
			SetEnvironmentVariableW(name,
				gSanitizedPathW[0] ? gSanitizedPathW : nullptr);
		}
		ReleaseSRWLockExclusive(&gPathLock);
	}
}

class DX12DbgHelpHookState
{
public:
	using SymInitializeWFn = BOOL (WINAPI *)(HANDLE, PCWSTR, BOOL);
	using SymInitializeAFn = BOOL (WINAPI *)(HANDLE, PCSTR, BOOL);
	using SymSetSearchPathWFn = BOOL (WINAPI *)(HANDLE, PCWSTR);
	using SymSetSearchPathAFn = BOOL (WINAPI *)(HANDLE, PCSTR);
	using LoadLibraryExWFn = HMODULE (WINAPI *)(LPCWSTR, HANDLE, DWORD);
	using LoadLibraryWFn = HMODULE (WINAPI *)(LPCWSTR);
	using LoadLibraryAFn = HMODULE (WINAPI *)(LPCSTR);

	inline static SymInitializeWFn OrigSymInitializeW = nullptr;
	inline static SymInitializeAFn OrigSymInitialize = nullptr;
	inline static SymSetSearchPathWFn OrigSymSetSearchPathW = nullptr;
	inline static SymSetSearchPathAFn OrigSymSetSearchPath = nullptr;
	inline static LoadLibraryExWFn OrigLoadLibraryExW = nullptr;
	inline static LoadLibraryWFn OrigLoadLibraryW = nullptr;
	inline static LoadLibraryAFn OrigLoadLibraryA = nullptr;

	inline static SRWLOCK InstallLock = SRWLOCK_INIT;
	inline static bool LibraryHookAttempted = false;
	// Per-module, not per-process: the engine keeps its own dbghelp and symsrv
	// copies in the Windows Kits debuggers directory next to the System32
	// ones, and either copy can appear first. Keying the one-shot guard on the
	// module base keeps every copy patched exactly once.
	inline static HMODULE HookedDbgHelp[4] = {};
	inline static unsigned int HookedDbgHelpCount = 0;
	inline static HMODULE HookedSymSrv[4] = {};
	inline static unsigned int HookedSymSrvCount = 0;
	inline static SRWLOCK ModuleHookLock = SRWLOCK_INIT;

	// Trampoline storage for the blocked symsrv exports. Nektra's Hook()
	// declares lplpCallOriginal as a required out parameter and rejects a null
	// pointer with ERROR_INVALID_PARAMETER (0x57) - which is exactly how the
	// first attempt at this stub ended up installing nothing ("blocked=0") and
	// leaving the network path live. One slot per (module, export); the array
	// is process-lifetime because the hooks are never removed.
	static constexpr unsigned int kMaxSymsrvExports = 16;
	static constexpr unsigned int kMaxSymSrvModules = 4;
	struct SymSrvTrampolines
	{
		void *original[kMaxSymsrvExports] = {};
		unsigned int used = 0;
	};
	inline static SymSrvTrampolines Trampolines[kMaxSymSrvModules];
	inline static unsigned int TrampolineSetCount = 0;
};

class DX12DbgHelpHookCallbacks
{
public:
	static BOOL WINAPI HookedSymInitializeW(HANDLE process, PCWSTR path,
		BOOL invadeProcess)
	{
		if (!path)
			return DX12DbgHelpHookState::OrigSymInitializeW(
				process, path, invadeProcess);
		// Scrub into a caller-local copy under the lock, then release before
		// calling dbghelp: the scratch buffers are shared, and no outbound
		// call may run while a lock this module owns is held.
		wchar_t sanitized[4096] = {};
		AcquireSRWLockExclusive(&gPathLock);
		const bool stripped = SanitizeSearchPathWLocked(path);
		if (stripped)
			wcsncpy_s(sanitized, gSanitizedPathW, _TRUNCATE);
		ReleaseSRWLockExclusive(&gPathLock);
		// Nothing local left: NULL puts dbghelp on its default (local) search
		// instead of an empty path that means "search nowhere".
		const wchar_t *forward = stripped ?
			(sanitized[0] ? sanitized : nullptr) : path;
		const BOOL ok = DX12DbgHelpHookState::OrigSymInitializeW(
			process, forward, invadeProcess);
		if (stripped)
			DX12_LOG_FLUSH("DX12DbgHelpHooks",
				"\"event\":\"sanitize\",\"api\":\"SymInitializeW\","
				"\"original\":\"%S\",\"sanitized\":\"%S\"",
				path, sanitized);
		return ok;
	}

	static BOOL WINAPI HookedSymInitialize(HANDLE process, PCSTR path,
		BOOL invadeProcess)
	{
		if (!path)
			return DX12DbgHelpHookState::OrigSymInitialize(
				process, path, invadeProcess);
		char sanitized[4096] = {};
		AcquireSRWLockExclusive(&gPathLock);
		const bool stripped = SanitizeSearchPathALocked(path);
		if (stripped)
			strncpy_s(sanitized, gSanitizedPathA, _TRUNCATE);
		ReleaseSRWLockExclusive(&gPathLock);
		const char *forward = stripped ?
			(sanitized[0] ? sanitized : nullptr) : path;
		const BOOL ok = DX12DbgHelpHookState::OrigSymInitialize(
			process, forward, invadeProcess);
		if (stripped)
			DX12_LOG_FLUSH("DX12DbgHelpHooks",
				"\"event\":\"sanitize\",\"api\":\"SymInitialize\","
				"\"original\":\"%s\",\"sanitized\":\"%s\"",
				path, sanitized);
		return ok;
	}

	static BOOL WINAPI HookedSymSetSearchPathW(HANDLE process, PCWSTR path)
	{
		if (!path)
			return DX12DbgHelpHookState::OrigSymSetSearchPathW(process, path);
		wchar_t sanitized[4096] = {};
		AcquireSRWLockExclusive(&gPathLock);
		const bool stripped = SanitizeSearchPathWLocked(path);
		if (stripped)
			wcsncpy_s(sanitized, gSanitizedPathW, _TRUNCATE);
		ReleaseSRWLockExclusive(&gPathLock);
		const wchar_t *forward = stripped ?
			(sanitized[0] ? sanitized : nullptr) : path;
		const BOOL ok = DX12DbgHelpHookState::OrigSymSetSearchPathW(
			process, forward);
		if (stripped)
			DX12_LOG_FLUSH("DX12DbgHelpHooks",
				"\"event\":\"sanitize\",\"api\":\"SymSetSearchPathW\","
				"\"original\":\"%S\",\"sanitized\":\"%S\"",
				path, sanitized);
		return ok;
	}

	static BOOL WINAPI HookedSymSetSearchPath(HANDLE process, PCSTR path)
	{
		if (!path)
			return DX12DbgHelpHookState::OrigSymSetSearchPath(process, path);
		char sanitized[4096] = {};
		AcquireSRWLockExclusive(&gPathLock);
		const bool stripped = SanitizeSearchPathALocked(path);
		if (stripped)
			strncpy_s(sanitized, gSanitizedPathA, _TRUNCATE);
		ReleaseSRWLockExclusive(&gPathLock);
		const char *forward = stripped ?
			(sanitized[0] ? sanitized : nullptr) : path;
		const BOOL ok = DX12DbgHelpHookState::OrigSymSetSearchPath(
			process, forward);
		if (stripped)
			DX12_LOG_FLUSH("DX12DbgHelpHooks",
				"\"event\":\"sanitize\",\"api\":\"SymSetSearchPath\","
				"\"original\":\"%s\",\"sanitized\":\"%s\"",
				path, sanitized);
		return ok;
	}

	static void InstallSymbolModuleHooks(HMODULE module, const char *api)
	{
		(void)api;
		// The sweep keys on the same predicates; the LoadLibrary path is only
		// the low-latency route to it, so it reuses the sweeper instead of
		// duplicating the name table.
		if (!module)
			return;
		DX12DbgHelpHooks::Rescan();
	}

	static HMODULE WINAPI HookedLoadLibraryExW(LPCWSTR name, HANDLE file,
		DWORD flags)
	{
		HMODULE module = DX12DbgHelpHookState::OrigLoadLibraryExW(
			name, file, flags);
		if (module)
			InstallSymbolModuleHooks(module, "LoadLibraryExW");
		return module;
	}

	static HMODULE WINAPI HookedLoadLibraryW(LPCWSTR name)
	{
		HMODULE module = DX12DbgHelpHookState::OrigLoadLibraryW(name);
		if (module)
			InstallSymbolModuleHooks(module, "LoadLibraryW");
		return module;
	}

	static HMODULE WINAPI HookedLoadLibraryA(LPCSTR name)
	{
		HMODULE module = DX12DbgHelpHookState::OrigLoadLibraryA(name);
		if (module)
			InstallSymbolModuleHooks(module, "LoadLibraryA");
		return module;
	}
};

// One stub serves every blocked symsrv export: on x64 the caller owns stack
// cleanup, so a zero rax with a "file not found" last-error reads as a plain
// store miss to dbghelp no matter which retrieval variant was called.
static ULONG_PTR WINAPI SymSrvBlockedStub()
{
	SetLastError(ERROR_FILE_NOT_FOUND);
	return 0;
}

void DX12DbgHelpHooks::InstallSymSrvHooks(HMODULE symsrv)
{
	if (!symsrv)
		return;
	AcquireSRWLockExclusive(&DX12DbgHelpHookState::ModuleHookLock);
	bool alreadyHooked = false;
	for (unsigned int i = 0; i < DX12DbgHelpHookState::HookedSymSrvCount; ++i) {
		if (DX12DbgHelpHookState::HookedSymSrv[i] == symsrv) {
			alreadyHooked = true;
			break;
		}
	}
	const bool tracked =
		DX12DbgHelpHookState::HookedSymSrvCount <
		ARRAYSIZE(DX12DbgHelpHookState::HookedSymSrv);
	if (!alreadyHooked && tracked)
		DX12DbgHelpHookState::HookedSymSrv[
			DX12DbgHelpHookState::HookedSymSrvCount++] = symsrv;
	if (!alreadyHooked &&
	    DX12DbgHelpHookState::TrampolineSetCount >=
		DX12DbgHelpHookState::kMaxSymSrvModules)
		alreadyHooked = true;
	DX12DbgHelpHookState::SymSrvTrampolines *trampolines =
		alreadyHooked ? nullptr :
		&DX12DbgHelpHookState::Trampolines[
			DX12DbgHelpHookState::TrampolineSetCount++];
	ReleaseSRWLockExclusive(&DX12DbgHelpHookState::ModuleHookLock);
	if (!trampolines)
		return;

	// Every retrieval/store/ping entry point: anything that could touch the
	// network. Resolution helpers (GetIndexString, DeltaName, IsStore) stay
	// live - they are pure computation and some callers need them to reason
	// about local stores.
	static const char *kBlockedExports[] = {
		"SymbolServer",
		"SymbolServerW",
		"SymbolServerWEx",
		"SymbolServerWithChecksumsW",
		"SymbolServerByIndex",
		"SymbolServerByIndexW",
		"SymbolServerByIndexAndChecksumsW",
		"SymbolServerGetSupplement",
		"SymbolServerGetSupplementW",
		"SymbolServerStoreFile",
		"SymbolServerStoreFileW",
		"SymbolServerStoreSupplement",
		"SymbolServerStoreSupplementW",
		"SymbolServerPing",
		"SymbolServerPingW",
		"SymbolServerPingWEx",
	};
	unsigned int installed = 0;
	unsigned int resolvable = 0;
	for (size_t i = 0;
	     i < ARRAYSIZE(kBlockedExports) &&
	     trampolines->used < DX12DbgHelpHookState::kMaxSymsrvExports;
	     ++i) {
		void *target = reinterpret_cast<void*>(
			GetProcAddress(symsrv, kBlockedExports[i]));
		if (!target)
			continue;
		++resolvable;
		void **slot = &trampolines->original[trampolines->used];
		const DWORD result = DX12HookRegistry::HookFunction(slot, target,
			reinterpret_cast<void*>(&SymSrvBlockedStub), kBlockedExports[i]);
		++trampolines->used;
		if (result == ERROR_SUCCESS)
			++installed;
	}
	DX12_LOG_FLUSH("DX12DbgHelpHooks",
		"\"event\":\"symsrv_hooks_installed\",\"module\":\"%p\","
		"\"resolvable\":%u,\"exports\":%u,\"blocked\":%u",
		reinterpret_cast<void*>(symsrv), resolvable,
		static_cast<unsigned>(ARRAYSIZE(kBlockedExports)), installed);
}

void DX12DbgHelpHooks::InstallDbgHelpHooks(HMODULE dbghelp)
{
	if (!dbghelp)
		return;
	AcquireSRWLockExclusive(&DX12DbgHelpHookState::ModuleHookLock);
	bool alreadyHooked = false;
	for (unsigned int i = 0; i < DX12DbgHelpHookState::HookedDbgHelpCount; ++i) {
		if (DX12DbgHelpHookState::HookedDbgHelp[i] == dbghelp) {
			alreadyHooked = true;
			break;
		}
	}
	const bool tracked =
		DX12DbgHelpHookState::HookedDbgHelpCount <
		ARRAYSIZE(DX12DbgHelpHookState::HookedDbgHelp);
	if (!alreadyHooked && tracked)
		DX12DbgHelpHookState::HookedDbgHelp[
			DX12DbgHelpHookState::HookedDbgHelpCount++] = dbghelp;
	ReleaseSRWLockExclusive(&DX12DbgHelpHookState::ModuleHookLock);
	if (alreadyHooked)
		return;

	DX12HookManager::InstallExportHook(dbghelp, "SymInitializeW",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigSymInitializeW),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedSymInitializeW));
	DX12HookManager::InstallExportHook(dbghelp, "SymInitialize",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigSymInitialize),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedSymInitialize));
	DX12HookManager::InstallExportHook(dbghelp, "SymSetSearchPathW",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigSymSetSearchPathW),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedSymSetSearchPathW));
	DX12HookManager::InstallExportHook(dbghelp, "SymSetSearchPath",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigSymSetSearchPath),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedSymSetSearchPath));
	DX12_LOG_FLUSH("DX12DbgHelpHooks",
		"\"event\":\"dbghelp_hooks_installed\",\"module\":\"%p\"",
		reinterpret_cast<void*>(dbghelp));

	// The engine may have initialized its symbol session before this proxy
	// DLL existed (d3d12 loads after Unity's early init), baking a srv* path
	// into dbghelp that the set-time hooks above will never see again. Read
	// the live session back, strip anything network-bound, and write it.
	// SymGetSearchPath is invoked directly (not hooked): reads need no
	// interception, and a copy-through (get -> set) stays safe because the
	// set passes through the sanitizing hook. Fails quietly when no session
	// exists yet - the per-call hooks cover that ordering instead.
	auto getSearchPathW = reinterpret_cast<BOOL (WINAPI *)(HANDLE, PWSTR, DWORD)>(
		GetProcAddress(dbghelp, "SymGetSearchPathW"));
	if (DX12DbgHelpHookState::OrigSymSetSearchPathW && getSearchPathW) {
		wchar_t current[4096] = {};
		if (getSearchPathW(GetCurrentProcess(), current,
				ARRAYSIZE(current)) &&
		    current[0]) {
			wchar_t sanitizedCopy[4096] = {};
			AcquireSRWLockExclusive(&gPathLock);
			const bool stripped = SanitizeSearchPathWLocked(current);
			BOOL reset = FALSE;
			if (stripped) {
				reset = DX12DbgHelpHookState::OrigSymSetSearchPathW(
					GetCurrentProcess(),
					gSanitizedPathW[0] ? gSanitizedPathW : nullptr);
				wcsncpy_s(sanitizedCopy, gSanitizedPathW, _TRUNCATE);
			}
			ReleaseSRWLockExclusive(&gPathLock);
			if (stripped)
				DX12_LOG_FLUSH("DX12DbgHelpHooks",
					"\"event\":\"reset_session_path\","
					"\"original\":\"%S\",\"sanitized\":\"%S\","
					"\"resetResult\":\"%ld\"",
					current, sanitizedCopy,
					static_cast<long>(reset));
			else
				DX12_LOG_FLUSH("DX12DbgHelpHooks",
					"\"event\":\"session_path_clean\","
					"\"current\":\"%S\"", current);
		} else {
			DX12_LOG_FLUSH("DX12DbgHelpHooks",
				"\"event\":\"no_active_session\",\"module\":\"%p\"",
				reinterpret_cast<void*>(dbghelp));
		}
	}
}

// Sweeps the process module list and applies both layers to every dbghelp and
// symsrv copy. LoadLibrary interception is not sufficient on its own: the
// engine's dbghelp lives in the Windows Kits debuggers directory and pulls its
// sibling symsrv in through the loader, bypassing the hooked kernel32 exports,
// which left the retrieval stubs uninstalled and the render thread blocked in
// wininet. The snapshot is drained into local storage before anything is
// patched, so the walk itself takes no lock, and registration is keyed on the
// module base - repeated sweeps cost one snapshot and no hook churn.
void DX12DbgHelpHooks::Rescan()
{
	static SRWLOCK scanLock = SRWLOCK_INIT;
	// The LoadLibrary hook can fire from any thread at any time, including
	// while the freeze watchdog sweeps; skipping a concurrent sweep is safe
	// because whoever holds the lock applies the same findings.
	if (!TryAcquireSRWLockExclusive(&scanLock))
		return;

	// Loader entries only: reading the module list out of a kernel snapshot
	// never touches the loader lock, unlike LdrEnumerateLoadedModules.
	constexpr unsigned int kMaxTrackedModules = 32;
	HMODULE dbgHelpModules[kMaxTrackedModules] = {};
	HMODULE symSrvModules[kMaxTrackedModules] = {};
	unsigned int dbgHelpCount = 0;
	unsigned int symSrvCount = 0;

	const HANDLE snapshot = CreateToolhelp32Snapshot(
		TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if (snapshot != INVALID_HANDLE_VALUE) {
		MODULEENTRY32W entry = {};
		entry.dwSize = sizeof(entry);
		if (Module32FirstW(snapshot, &entry)) {
			do {
				if (IsSymSrvFileName(entry.szExePath)) {
					if (symSrvCount < kMaxTrackedModules)
						symSrvModules[symSrvCount++] = entry.hModule;
				} else if (IsDbgHelpFileName(entry.szExePath)) {
					if (dbgHelpCount < kMaxTrackedModules)
						dbgHelpModules[dbgHelpCount++] = entry.hModule;
				}
			} while (Module32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
	}

	// GetProcAddress takes the loader lock, so it must never run while another
	// thread is suspended (the stack snapshot suspends every thread, including
	// one that may hold that lock). Both callers honour that: the watchdog
	// re-sweeps before it captures anything.
	for (unsigned int i = 0; i < symSrvCount; ++i) {
		wchar_t path[MAX_PATH] = {};
		if (GetModuleFileNameW(symSrvModules[i], path, ARRAYSIZE(path)))
			ReportModuleOnce("symsrv_load_detected", path);
		InstallSymSrvHooks(symSrvModules[i]);
	}
	for (unsigned int i = 0; i < dbgHelpCount; ++i) {
		wchar_t path[MAX_PATH] = {};
		if (GetModuleFileNameW(dbgHelpModules[i], path, ARRAYSIZE(path)))
			ReportModuleOnce("dbghelp_load_detected", path);
		InstallDbgHelpHooks(dbgHelpModules[i]);
	}
	ReleaseSRWLockExclusive(&scanLock);
}

void DX12DbgHelpHooks::Install()
{
	AcquireSRWLockExclusive(&DX12DbgHelpHookState::InstallLock);
	if (DX12DbgHelpHookState::LibraryHookAttempted) {
		ReleaseSRWLockExclusive(&DX12DbgHelpHookState::InstallLock);
		return;
	}
	DX12DbgHelpHookState::LibraryHookAttempted = true;
	ReleaseSRWLockExclusive(&DX12DbgHelpHookState::InstallLock);

	// Close the environment fallback before any dbghelp client can use it.
	ScrubEnvironmentVariable(L"_NT_SYMBOL_PATH");
	ScrubEnvironmentVariable(L"_NT_ALT_SYMBOL_PATH");

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32)
		kernel32 = LoadLibraryW(L"kernel32.dll");
	if (!kernel32)
		return;
	// kernel32 forwards these to KernelBase; GetProcAddress follows the
	// forwarder, so patching the export covers every caller in the process.
	DX12HookManager::InstallExportHook(kernel32, "LoadLibraryExW",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigLoadLibraryExW),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedLoadLibraryExW));
	DX12HookManager::InstallExportHook(kernel32, "LoadLibraryW",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigLoadLibraryW),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedLoadLibraryW));
	DX12HookManager::InstallExportHook(kernel32, "LoadLibraryA",
		reinterpret_cast<void**>(&DX12DbgHelpHookState::OrigLoadLibraryA),
		reinterpret_cast<void*>(
			DX12DbgHelpHookCallbacks::HookedLoadLibraryA));

	// Whatever is already mapped - System32 copies, the debuggers-directory
	// copies the engine prefers, or both - is patched now rather than waiting
	// for a load event that may never pass through the hooks above.
	Rescan();
}

void DX12DbgHelpHooks::Shutdown()
{
}
