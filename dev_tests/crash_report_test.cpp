// Crash self-diagnosis test (TIMING-only feature).
//
// Proves, in a real process crash rather than by inspection:
//   1. the report filter appends an exception record, faulting registers and a
//      symbolized stack to the log next to the module,
//   2. the breadcrumb ring names the wrapper scope that was in flight,
//   3. a minidump is written next to the log,
//   4. the handler installed *before* ours still runs (chaining), so a game's
//      own crash reporter is not displaced.
//
// The parent re-executes itself as the crashing child, so the fault, the report
// and the verification never share an address space.
#include "../src/DirectX12/Core/Diag/DX12CrashReport.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
	int gFailures = 0;

	void Check(bool value, const char *message)
	{
		if (value) {
			std::printf("PASS %s\n", message);
			return;
		}
		std::printf("FAIL %s\n", message);
		++gFailures;
	}

	std::wstring ModulePath()
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
		return path;
	}

	std::wstring DirectoryOf(const std::wstring &path)
	{
		const size_t slash = path.find_last_of(L'\\');
		return slash == std::wstring::npos ? path : path.substr(0, slash);
	}

	std::string ReadTextFile(const std::wstring &path)
	{
		FILE *file = nullptr;
		if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
			return std::string();
		std::string text;
		char buffer[4096];
		size_t read = 0;
		while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
			text.append(buffer, read);
		fclose(file);
		return text;
	}

	bool Contains(const std::string &text, const char *needle)
	{
		return text.find(needle) != std::string::npos;
	}

	// The crashing child. Never inlined so the report has a symbol to resolve.
	__declspec(noinline) void CrashChild()
	{
		// Volatile, so the compiler cannot fold the fault away.
		const int value = *static_cast<volatile int *>(nullptr);
		std::printf("unreachable %d\n", value);
	}

	LONG WINAPI PreviousHandler(EXCEPTION_POINTERS *)
	{
		// Marker file = "the handler that was installed before the report
		// filter still ran". Returning EXECUTE_HANDLER terminates the process
		// without involving Windows Error Reporting.
		wchar_t marker[MAX_PATH] = {};
		GetEnvironmentVariableW(L"DX12_CRASH_TEST_MARKER", marker, ARRAYSIZE(marker));
		FILE *file = nullptr;
		if (_wfopen_s(&file, marker, L"wb") == 0 && file) {
			fputs("previous-handler-ran", file);
			fclose(file);
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	int RunChild()
	{
		// No "program has stopped working" dialog if the chain ever breaks.
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		SetUnhandledExceptionFilter(PreviousHandler);
		DX12CrashReportInstall();

		// Stand in for the wrapper scopes that were in flight.
		DX12CrashBreadcrumb("Api.DrawIndexedInstanced");
		DX12CrashBreadcrumb("Create.ResourceCommittedWrapper.Original");
		CrashChild();
		return 0;
	}

	int RunParent(const std::wstring &directory)
	{
		const std::wstring logPath = directory + L"\\d3d12_log.txt";
		const std::wstring markerPath = directory + L"\\previous-handler.txt";
		// Old evidence from a previous run must not be able to pass the test.
		DeleteFileW(logPath.c_str());
		DeleteFileW(markerPath.c_str());
		WIN32_FIND_DATAW stale = {};
		HANDLE staleSearch = FindFirstFileW((directory + L"\\d3d12_crash_*.dmp").c_str(), &stale);
		if (staleSearch != INVALID_HANDLE_VALUE) {
			do {
				DeleteFileW((directory + L"\\" + stale.cFileName).c_str());
			} while (FindNextFileW(staleSearch, &stale));
			FindClose(staleSearch);
		}

		SetEnvironmentVariableW(L"DX12_CRASH_TEST_MARKER", markerPath.c_str());
		std::wstring command = L"\"" + ModulePath() + L"\" --crash";
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
				nullptr, directory.c_str(), &startup, &process)) {
			std::printf("FAIL cannot start the crashing child (error %lu)\n", GetLastError());
			return 1;
		}
		WaitForSingleObject(process.hProcess, 60000);
		DWORD exitCode = 0;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		std::printf("child exit code 0x%08lx\n", exitCode);

		const std::string log = ReadTextFile(logPath);
		Check(!log.empty(), "report was written to d3d12_log.txt next to the module");
		Check(Contains(log, "func=DX12Crash,event=detected"), "log records the crash");
		Check(Contains(log, "code=0xc0000005"), "access violation code is in the report");
		Check(Contains(log, "access=read"), "access kind and faulting address are in the report");
		Check(Contains(log, "func=DX12CrashRecord,"), "exception record is itemized");
		Check(Contains(log, "func=DX12CrashContext,"), "faulting registers are dumped");
		Check(Contains(log, "func=DX12CrashStack,"), "faulting stack is walked");
		Check(Contains(log, "CrashChild"), "the crashing function resolves through the local PDB");
		Check(Contains(log, "func=DX12CrashThread,"), "other threads' instruction pointers are recorded");
		Check(Contains(log, "scope=Api.DrawIndexedInstanced") &&
			Contains(log, "scope=Create.ResourceCommittedWrapper.Original"),
			"breadcrumbs name the wrapper scopes that were in flight");
		Check(Contains(log, "crashThread=true"), "breadcrumbs mark the faulting thread");
		Check(Contains(log, "func=DX12CrashDump,status=ok"), "minidump was written");

		WIN32_FIND_DATAW dump = {};
		HANDLE search = FindFirstFileW((directory + L"\\d3d12_crash_*.dmp").c_str(), &dump);
		bool dumpFound = search != INVALID_HANDLE_VALUE;
		if (dumpFound)
			FindClose(search);
		const unsigned long long dumpBytes = dumpFound
			? (static_cast<unsigned long long>(dump.nFileSizeHigh) << 32) | dump.nFileSizeLow
			: 0;
		Check(dumpFound && dumpBytes > 0, "minidump file exists and is not empty");
		Check(!ReadTextFile(markerPath).empty(), "the handler installed before ours still ran");
		Check(exitCode != 0, "the crash terminated the child process");

		if (gFailures == 0) {
			std::printf("ALL PASS: crash report, symbolized stack, breadcrumbs, minidump, handler chaining\n");
			return 0;
		}
		std::printf("%d check(s) failed\n", gFailures);
		return 1;
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--crash") == 0)
		return RunChild();
	return RunParent(DirectoryOf(ModulePath()));
}
