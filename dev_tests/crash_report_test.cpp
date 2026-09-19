// Crash self-diagnosis test (TIMING-only feature).
//
// Proves, in a real process crash rather than by inspection:
//   1. the report filter appends an exception record, faulting registers and a
//      symbolized stack to the log next to the module,
//   2. the breadcrumb ring names the wrapper scope that was in flight,
//   3. a minidump is written next to the log,
//   4. the handler installed *before* ours still runs (chaining), so a game's
//      own crash reporter is not displaced,
//   5. the report survives the asynchronous log writer: with a writer that
//      parks on request the report lands in d3d12_log.txt intact, and with a
//      writer that cannot be parked the report keeps out of that file instead
//      of being overwritten by the writer's own stale file position.
//
// The parent re-executes itself as the crashing child, so the fault, the report
// and the verification never share an address space.
#include "../src/DirectX12/Core/Diag/DX12CrashReport.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	// 0: no competing writer (report goes to the log)
	// 1: mock log writer that parks on request (report goes to the log)
	// 2: mock log writer that refuses to park (report gets its own file)
	enum Scenario
	{
		NoWriter = 0,
		ParkedWriter = 1,
		StuckWriter = 2,
	};

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

	// Every field the report promises, in the order a reader would look for it.
	bool LooksLikeFullReport(const std::string &text)
	{
		return Contains(text, "func=DX12Crash,event=detected") &&
			Contains(text, "code=0xc0000005") &&
			Contains(text, "access=read") &&
			Contains(text, "func=DX12CrashRecord,") &&
			Contains(text, "func=DX12CrashContext,") &&
			Contains(text, "func=DX12CrashStack,") &&
			Contains(text, "CrashChild") &&
			Contains(text, "func=DX12CrashThread,") &&
			Contains(text, "scope=Api.DrawIndexedInstanced") &&
			Contains(text, "scope=Create.ResourceCommittedWrapper.Original") &&
			Contains(text, "crashThread=true") &&
			Contains(text, "func=DX12CrashDump,status=ok");
	}

	// ------------------------------------------------------------------
	// Mock log writer: stands in for the DX12DiagLog writer thread, i.e. a
	// FILE* opened once (truncating) that keeps writing at a file position of
	// its own. That position is what erased the real 13:26 report.
	// ------------------------------------------------------------------
	FILE *gMockLog = nullptr;
	volatile LONG gMockStop = 0;

	void MockLogStart(const std::wstring &logPath)
	{
		_wfopen_s(&gMockLog, logPath.c_str(), L"w");
		if (gMockLog) {
			fputs("time=0,func=Log,message=mock log writer owns this file\n", gMockLog);
			fflush(gMockLog);
		}
	}

	DWORD WINAPI MockWriterThread(void *)
	{
		while (InterlockedCompareExchange(&gMockStop, 0, 0) == 0) {
			if (gMockLog) {
				fputs("time=0,func=DX12LongWait,event=blocked,api=WaitForSingleObject,"
					"threadId=1,elapsedMs=1250,waitMs=-1,count=1\n", gMockLog);
				fflush(gMockLog);
			}
			Sleep(10);
		}
		return 0;
	}

	// Scenario 1: the writer parks and hands the file over.
	bool MockLogQuiesce()
	{
		InterlockedExchange(&gMockStop, 1);
		Sleep(30); // bounded: the mock writer polls every 10ms
		if (gMockLog) {
			fflush(gMockLog);
			fclose(gMockLog);
			gMockLog = nullptr;
		}
		return true;
	}

	// Scenario 2: the writer is stuck and cannot be parked.
	bool MockLogQuiesceRefused()
	{
		return false;
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
		// filter still ran". The sleep gives a writer that was *not* parked the
		// same window the real one had while Windows Error Reporting collected
		// its dump, so a missing handover shows up as a clobbered report instead
		// of passing by timing luck.
		wchar_t marker[MAX_PATH] = {};
		GetEnvironmentVariableW(L"DX12_CRASH_TEST_MARKER", marker, ARRAYSIZE(marker));
		FILE *file = nullptr;
		if (_wfopen_s(&file, marker, L"wb") == 0 && file) {
			fputs("previous-handler-ran", file);
			fclose(file);
		}
		Sleep(400);
		// Returning EXECUTE_HANDLER terminates the process without involving
		// Windows Error Reporting.
		return EXCEPTION_EXECUTE_HANDLER;
	}

	int RunChild(int scenario)
	{
		// No "program has stopped working" dialog if the chain ever breaks.
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		SetUnhandledExceptionFilter(PreviousHandler);

		wchar_t logPath[MAX_PATH] = {};
		GetEnvironmentVariableW(L"DX12_CRASH_TEST_LOG", logPath, ARRAYSIZE(logPath));
		if (scenario != NoWriter) {
			MockLogStart(logPath);
			HANDLE writer = CreateThread(nullptr, 0, MockWriterThread, nullptr, 0, nullptr);
			if (writer)
				CloseHandle(writer);
			DX12CrashReportSetLogQuiesce(
				scenario == ParkedWriter ? &MockLogQuiesce : &MockLogQuiesceRefused);
		}
		DX12CrashReportInstall();

		// Stand in for the wrapper scopes that were in flight.
		DX12CrashBreadcrumb("Api.DrawIndexedInstanced");
		DX12CrashBreadcrumb("Create.ResourceCommittedWrapper.Original");
		CrashChild();
		return 0;
	}

	struct RunResult
	{
		std::string log;
		std::string separateReport;
		bool dumpFound = false;
		bool markerFound = false;
		DWORD exitCode = 0;
	};

	void DeleteStale(const std::wstring &directory, const wchar_t *pattern)
	{
		WIN32_FIND_DATAW found = {};
		HANDLE search = FindFirstFileW((directory + L"\\" + pattern).c_str(), &found);
		if (search == INVALID_HANDLE_VALUE)
			return;
		do {
			DeleteFileW((directory + L"\\" + found.cFileName).c_str());
		} while (FindNextFileW(search, &found));
		FindClose(search);
	}

	// Runs one scenario and reads back everything it produced.
	RunResult RunScenario(const std::wstring &directory, int scenario)
	{
		const std::wstring logPath = directory + L"\\d3d12_log.txt";
		const std::wstring markerPath = directory + L"\\previous-handler.txt";
		DeleteFileW(logPath.c_str());
		DeleteFileW(markerPath.c_str());
		DeleteStale(directory, L"d3d12_crash_*.dmp");
		DeleteStale(directory, L"d3d12_crash_*.txt");

		SetEnvironmentVariableW(L"DX12_CRASH_TEST_MARKER", markerPath.c_str());
		SetEnvironmentVariableW(L"DX12_CRASH_TEST_LOG", logPath.c_str());

		wchar_t mode[16] = {};
		swprintf(mode, ARRAYSIZE(mode), L" %d", scenario);
		std::wstring command = L"\"" + ModulePath() + L"\" --crash" + mode;
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		RunResult result;
		if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
				nullptr, directory.c_str(), &startup, &process)) {
			std::printf("FAIL cannot start the crashing child (error %lu)\n", GetLastError());
			++gFailures;
			return result;
		}
		WaitForSingleObject(process.hProcess, 60000);
		GetExitCodeProcess(process.hProcess, &result.exitCode);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		result.log = ReadTextFile(logPath);
		WIN32_FIND_DATAW found = {};
		HANDLE search = FindFirstFileW((directory + L"\\d3d12_crash_*.txt").c_str(), &found);
		if (search != INVALID_HANDLE_VALUE) {
			result.separateReport = ReadTextFile(directory + L"\\" + found.cFileName);
			FindClose(search);
		}
		search = FindFirstFileW((directory + L"\\d3d12_crash_*.dmp").c_str(), &found);
		if (search != INVALID_HANDLE_VALUE) {
			result.dumpFound = ((static_cast<unsigned long long>(found.nFileSizeHigh) << 32) |
				found.nFileSizeLow) > 0;
			FindClose(search);
		}
		result.markerFound = !ReadTextFile(markerPath).empty();
		return result;
	}

	int RunParent(const std::wstring &directory)
	{
		// 1. Baseline: no competing writer, report appended to the log.
		const RunResult plain = RunScenario(directory, NoWriter);
		std::printf("scenario no_writer exit code 0x%08lx\n", plain.exitCode);
		Check(LooksLikeFullReport(plain.log), "baseline: full report in d3d12_log.txt");
		Check(plain.dumpFound, "baseline: minidump file exists and is not empty");
		Check(plain.markerFound, "baseline: the handler installed before ours still ran");
		Check(plain.exitCode != 0, "baseline: the crash terminated the child process");
		Check(plain.separateReport.empty(), "baseline: no separate report file is written");

		// 2. Log writer parks on request: the report still lands in the log.
		const RunResult parked = RunScenario(directory, ParkedWriter);
		std::printf("scenario parked_writer exit code 0x%08lx\n", parked.exitCode);
		Check(LooksLikeFullReport(parked.log),
			"parked writer: report is complete in d3d12_log.txt");
		Check(Contains(parked.log, "mode=log"), "parked writer: report says mode=log");
		Check(parked.separateReport.empty(), "parked writer: no separate report file is written");

		// 3. Log writer cannot be parked: the report must not share its file,
		//    because that writer keeps writing at a position of its own.
		const RunResult stuck = RunScenario(directory, StuckWriter);
		std::printf("scenario stuck_writer exit code 0x%08lx\n", stuck.exitCode);
		Check(LooksLikeFullReport(stuck.separateReport),
			"stuck writer: complete report in d3d12_crash_*.txt");
		Check(Contains(stuck.separateReport, "mode=separate_file"),
			"stuck writer: report says mode=separate_file");
		Check(!Contains(stuck.log, "func=DX12Crash,event=detected"),
			"stuck writer: report kept out of the log it cannot own");
		Check(stuck.dumpFound, "stuck writer: minidump is still written");

		if (gFailures == 0) {
			std::printf("ALL PASS: crash report, symbolized stack, breadcrumbs, minidump, "
				"handler chaining, log-writer handover\n");
			return 0;
		}
		std::printf("%d check(s) failed\n", gFailures);
		return 1;
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--crash") == 0)
		return RunChild(argc > 2 ? std::atoi(argv[2]) : 0);
	return RunParent(DirectoryOf(ModulePath()));
}
