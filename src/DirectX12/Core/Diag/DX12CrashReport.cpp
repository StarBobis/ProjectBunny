// DX12 crash self-diagnosis (TIMING builds only): an unhandled-exception filter
// that leaves a readable report in d3d12_log.txt plus a minidump next to it.
// See DX12CrashReport.h for why this exists and which question it answers.
#include "DX12CrashReport.h"

#if defined(DX12_RELEASE_TIMING)

#include <dbghelp.h>
#include <tlhelp32.h>

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "dbghelp.lib")

namespace
{
	// The stack of a driver entry point is shallow, but a crash under a game's
	// own hook chain is not: 48 frames covers every observed case while keeping
	// the report inside one screenful of log.
	constexpr unsigned kMaxStackFrames = 48;
	constexpr unsigned kBreadcrumbLines = 96;
	constexpr unsigned kMaxThreads = 96;

	LPTOP_LEVEL_EXCEPTION_FILTER gPreviousFilter = nullptr;
	// Non-blocking reporter election. A second faulting thread must not
	// interleave lines into the report or write a second dump - and it must
	// never wait for the first one, because the first one may itself be stuck.
	volatile LONG gReportStarted = 0;
	int gLogFd = -1;
	char gLine[1024];
	DWORD64 gOwnModuleBase = 0;

	// The deployed d3d12.dll sits next to the d3d12_log.txt the user reads and
	// the d3d12.pdb the TIMING build copies there, so the report lands where the
	// evidence already is.
	bool ModuleDirectory(wchar_t *directory, size_t capacity)
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&DX12CrashReportInstall), &module) ||
			!module)
			return false;
		gOwnModuleBase = reinterpret_cast<DWORD64>(module);
		if (!GetModuleFileNameW(module, directory, static_cast<DWORD>(capacity)))
			return false;
		wchar_t *slash = wcsrchr(directory, L'\\');
		if (!slash)
			return false;
		*slash = L'\0';
		return true;
	}

	// "nvwgf2umx.dll+0x14bcce" - the same shape the log already uses for stack
	// frames, so nothing has to be decoded before the module is recognized.
	void DescribeAddress(DWORD64 address, char *text, size_t capacity)
	{
		HMODULE module = nullptr;
		if (address && GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(address)), &module) &&
			module) {
			char path[MAX_PATH] = {};
			GetModuleFileNameA(module, path, ARRAYSIZE(path));
			const char *name = strrchr(path, '\\');
			name = name ? name + 1 : path;
			snprintf(text, capacity, "%s+0x%llx", name,
				static_cast<unsigned long long>(
					address - reinterpret_cast<DWORD64>(module)));
			return;
		}
		snprintf(text, capacity, "0x%llx", static_cast<unsigned long long>(address));
	}

	// Lines use the logger's own "time=...,func=...,key=value" shape so the
	// crash block reads like the rest of d3d12_log.txt and the existing
	// analysis scripts keep working on it.
	void Report(const char *func, const char *format, ...)
	{
		if (gLogFd < 0)
			return;
		int length = snprintf(gLine, sizeof(gLine), "time=%llu,func=%s,",
			static_cast<unsigned long long>(GetTickCount64()), func);
		if (length < 0 || length >= static_cast<int>(sizeof(gLine)))
			return;
		va_list args;
		va_start(args, format);
		const int written =
			vsnprintf(gLine + length, sizeof(gLine) - length, format, args);
		va_end(args);
		if (written < 0)
			return;
		size_t total = static_cast<size_t>(length) + static_cast<size_t>(written);
		if (total > sizeof(gLine) - 2)
			total = sizeof(gLine) - 2;
		gLine[total++] = '\n';
		// One unbuffered write per line: the process can die in the middle of
		// its own report, and a buffered report that never reaches the disk is
		// exactly the failure this file exists to remove.
		_write(gLogFd, gLine, static_cast<unsigned>(total));
	}

	// dbghelp needs the unwind tables of every module on the stack, otherwise
	// StackWalk64 stops at the first frame outside our own DLL - which is the
	// half of the stack (game -> dxgi -> D3D12Core -> driver) that matters.
	void LoadUnwindTables(HANDLE process, const wchar_t *directory)
	{
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS |
			SYMOPT_FAIL_CRITICAL_ERRORS);
		// FALSE: never invade. Symbolizing the whole process here would probe
		// the symbol path for every loaded module, and the DX12 dbghelp guard
		// deliberately keeps that path offline.
		if (!SymInitializeW(process, directory, FALSE)) {
			// A session may already exist (the game's own crash reporter
			// initializes dbghelp first). Keep going on that one.
			SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
		}

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snapshot != INVALID_HANDLE_VALUE) {
			MODULEENTRY32W entry = {};
			entry.dwSize = sizeof(entry);
			if (Module32FirstW(snapshot, &entry)) {
				do {
					SymLoadModuleExW(process, nullptr, entry.szExePath, nullptr,
						reinterpret_cast<DWORD64>(entry.modBaseAddr),
						entry.modBaseSize, nullptr, 0);
				} while (Module32NextW(snapshot, &entry));
			}
			CloseHandle(snapshot);
		}
	}

	// Names only for our own module: its PDB is deployed next to the DLL by the
	// TIMING build, while every other module's PDB is either absent locally or
	// behind the symbol server this runtime keeps offline.
	void SymbolizeOwnFrame(DWORD64 address, char *symbol, size_t symbolCapacity,
		char *line, size_t lineCapacity)
	{
		symbol[0] = '\0';
		line[0] = '\0';
		if (!gOwnModuleBase || address < gOwnModuleBase)
			return;

		char buffer[sizeof(SYMBOL_INFO) + 2048] = {};
		SYMBOL_INFO *info = reinterpret_cast<SYMBOL_INFO*>(buffer);
		info->SizeOfStruct = sizeof(SYMBOL_INFO);
		info->MaxNameLen = 2048;
		DWORD64 displacement = 0;
		if (SymFromAddr(GetCurrentProcess(), address, &displacement, info)) {
			snprintf(symbol, symbolCapacity, "%s+0x%llx", info->Name,
				static_cast<unsigned long long>(displacement));
			IMAGEHLP_LINE64 source = {};
			source.SizeOfStruct = sizeof(source);
			DWORD lineDisplacement = 0;
			if (SymGetLineFromAddr64(GetCurrentProcess(), address, &lineDisplacement, &source)) {
				const char *file = strrchr(source.FileName, '\\');
				snprintf(line, lineCapacity, "%s:%lu",
					file ? file + 1 : source.FileName, source.LineNumber);
			}
		}
	}

	// The faulting thread's own stack, walked with the unwind tables loaded
	// above. This is the half a minidump cannot give on its own: module order
	// plus the wrapper's function names, in the log, without a debugger.
	void ReportStack(HANDLE process, HANDLE thread, const CONTEXT &context, DWORD threadId)
	{
		STACKFRAME64 frame = {};
		frame.AddrPC.Offset = context.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = context.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = context.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;

		CONTEXT walkContext = context;
		unsigned reported = 0;
		for (unsigned index = 0; index < kMaxStackFrames; ++index) {
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
					&walkContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
					nullptr))
				break;
			if (!frame.AddrPC.Offset)
				break;

			char where[192] = {};
			char symbol[512] = {};
			char line[160] = {};
			DescribeAddress(frame.AddrPC.Offset, where, sizeof(where));
			SymbolizeOwnFrame(frame.AddrPC.Offset, symbol, sizeof(symbol), line, sizeof(line));
			Report("DX12CrashStack", "threadId=%lu,frame=%u,address=0x%llx,where=%s,symbol=%s,line=%s",
				threadId, reported,
				static_cast<unsigned long long>(frame.AddrPC.Offset),
				where, symbol[0] ? symbol : "<none>", line[0] ? line : "<none>");
			++reported;
		}
		if (!reported)
			Report("DX12CrashStack", "threadId=%lu,frame=0,status=unwalkable", threadId);
	}

	// Every other thread's instruction pointer: enough to see whether the
	// process was mid-frame, parked on a fence, or equally deep in the driver
	// when the faulting thread died.
	void ReportThreadContexts(DWORD faultingThreadId)
	{
		const DWORD processId = GetCurrentProcessId();
		const DWORD selfId = GetCurrentThreadId();
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return;

		THREADENTRY32 entry = {};
		entry.dwSize = sizeof(entry);
		unsigned reported = 0;
		if (Thread32First(snapshot, &entry)) {
			do {
				if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == selfId)
					continue;
				if (reported >= kMaxThreads)
					break;
				HANDLE thread = OpenThread(
					THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
					FALSE, entry.th32ThreadID);
				if (!thread)
					continue;
				if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
					CONTEXT context = {};
					context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
					if (GetThreadContext(thread, &context)) {
						char where[192] = {};
						DescribeAddress(context.Rip, where, sizeof(where));
						Report("DX12CrashThread", "threadId=%lu,rip=0x%llx,where=%s,faulting=%s",
							entry.th32ThreadID,
							static_cast<unsigned long long>(context.Rip), where,
							entry.th32ThreadID == faultingThreadId ? "true" : "false");
						++reported;
					}
					ResumeThread(thread);
				}
				CloseHandle(thread);
			} while (Thread32Next(snapshot, &entry));
		}
		CloseHandle(snapshot);
	}

	// Which wrapper scope led here. The ring is process-wide, so entries from
	// the faulting thread are flagged and printed last-first: they answer "what
	// was our code doing", the rest show what the other threads were doing.
	void ReportBreadcrumbs(DWORD faultingThreadId)
	{
		using namespace DX12CrashBreadcrumbDetail;
		const unsigned written = gWriteIndex.load(std::memory_order_relaxed);
		const unsigned available = written < kCapacity ? written : kCapacity;
		const unsigned count = available < kBreadcrumbLines ? available : kBreadcrumbLines;
		const DWORD now = GetTickCount();
		Report("DX12CrashBreadcrumb",
			"event=summary,written=%u,kept=%u,faultingThreadId=%lu,capacity=%u",
			written, count, faultingThreadId, kCapacity);
		for (unsigned index = count; index-- > 0;) {
			const unsigned sequence = written - 1 - index;
			const Entry &entry = gEntries[sequence % kCapacity];
			if (!entry.scope)
				continue;
			Report("DX12CrashBreadcrumb",
				"seq=%u,ageMs=%lu,threadId=%lu,crashThread=%s,scope=%s",
				sequence, static_cast<unsigned long>(now - entry.tick), entry.threadId,
				entry.threadId == faultingThreadId ? "true" : "false", entry.scope);
		}
	}

	void WriteDump(EXCEPTION_POINTERS *exception, const wchar_t *directory)
	{
		SYSTEMTIME now = {};
		GetLocalTime(&now);
		wchar_t path[MAX_PATH] = {};
		swprintf(path, ARRAYSIZE(path),
			L"%s\\d3d12_crash_%04u%02u%02u-%02u%02u%02u.dmp", directory,
			now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

		HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			Report("DX12CrashDump", "status=create_failed,error=%lu,path=%S",
				GetLastError(), path);
			return;
		}

		MINIDUMP_EXCEPTION_INFORMATION dumpInfo = {};
		dumpInfo.ThreadId = GetCurrentThreadId();
		dumpInfo.ExceptionPointers = exception;
		dumpInfo.ClientPointers = FALSE;
		// Thread stacks and the objects those stacks reference. The full address
		// space would be a 100+MB file per crash, and the report above already
		// carries the symbolized stack, so the dump is a second opinion.
		const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
			MiniDumpWithUnloadedModules);
		const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
			file, type, &dumpInfo, nullptr, nullptr);
		LARGE_INTEGER size = {};
		GetFileSizeEx(file, &size);
		CloseHandle(file);
		if (ok)
			Report("DX12CrashDump", "status=ok,bytes=%lld,path=%S",
				static_cast<long long>(size.QuadPart), path);
		else
			Report("DX12CrashDump", "status=write_failed,error=%lu,path=%S",
				GetLastError(), path);
	}

	void WriteReport(EXCEPTION_POINTERS *exception)
	{
		wchar_t directory[MAX_PATH] = {};
		if (!ModuleDirectory(directory, ARRAYSIZE(directory)))
			return;

		wchar_t logPath[MAX_PATH] = {};
		swprintf(logPath, ARRAYSIZE(logPath), L"%s\\d3d12_log.txt", directory);
		// Append: the log already holds everything up to the crash, and the
		// report has to survive a process that dies before the writer thread
		// ever drains its queue.
		gLogFd = -1;
		if (_wsopen_s(&gLogFd, logPath,
				_O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY, _SH_DENYNO,
				_S_IREAD | _S_IWRITE) != 0)
			gLogFd = -1;
		if (gLogFd < 0)
			return;

		const DWORD threadId = GetCurrentThreadId();
		Report("DX12Crash", "event=detected,threadId=%lu", threadId);

		unsigned recordIndex = 0;
		for (EXCEPTION_RECORD *record = exception ? exception->ExceptionRecord : nullptr;
			record; record = record->ExceptionRecord, ++recordIndex) {
			char where[192] = {};
			DescribeAddress(reinterpret_cast<DWORD64>(record->ExceptionAddress),
				where, sizeof(where));
			char access[96] = {};
			if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
				record->NumberParameters >= 2) {
				const ULONG_PTR kind = record->ExceptionInformation[0];
				snprintf(access, sizeof(access), ",access=%s,accessAddress=0x%llx",
					kind == 0 ? "read" : (kind == 1 ? "write" : "execute"),
					static_cast<unsigned long long>(record->ExceptionInformation[1]));
			}
			Report("DX12CrashRecord",
				"record=%u,code=0x%08lx,flags=0x%lx,address=0x%llx,where=%s%s",
				recordIndex, record->ExceptionCode, record->ExceptionFlags,
				static_cast<unsigned long long>(
					reinterpret_cast<ULONG_PTR>(record->ExceptionAddress)),
				where, access);
		}

		CONTEXT context = {};
		if (exception && exception->ContextRecord)
			context = *exception->ContextRecord;
		Report("DX12CrashContext",
			"threadId=%lu,rip=0x%llx,rsp=0x%llx,rbp=0x%llx,"
			"rax=0x%llx,rbx=0x%llx,rcx=0x%llx,rdx=0x%llx,"
			"rsi=0x%llx,rdi=0x%llx,r8=0x%llx,r9=0x%llx,"
			"r10=0x%llx,r11=0x%llx,r12=0x%llx,r13=0x%llx,r14=0x%llx,r15=0x%llx",
			threadId,
			static_cast<unsigned long long>(context.Rip),
			static_cast<unsigned long long>(context.Rsp),
			static_cast<unsigned long long>(context.Rbp),
			static_cast<unsigned long long>(context.Rax),
			static_cast<unsigned long long>(context.Rbx),
			static_cast<unsigned long long>(context.Rcx),
			static_cast<unsigned long long>(context.Rdx),
			static_cast<unsigned long long>(context.Rsi),
			static_cast<unsigned long long>(context.Rdi),
			static_cast<unsigned long long>(context.R8),
			static_cast<unsigned long long>(context.R9),
			static_cast<unsigned long long>(context.R10),
			static_cast<unsigned long long>(context.R11),
			static_cast<unsigned long long>(context.R12),
			static_cast<unsigned long long>(context.R13),
			static_cast<unsigned long long>(context.R14),
			static_cast<unsigned long long>(context.R15));

		const HANDLE process = GetCurrentProcess();
		LoadUnwindTables(process, directory);
		if (exception && exception->ContextRecord)
			ReportStack(process, GetCurrentThread(), context, threadId);
		ReportThreadContexts(threadId);
		ReportBreadcrumbs(threadId);

		// Last, because MiniDumpWriteDump suspends every other thread: by now
		// the machine-readable part of the report is already on disk.
		WriteDump(exception, directory);

		Report("DX12Crash", "event=report_done,log=%S", logPath);
		_close(gLogFd);
		gLogFd = -1;
	}

	LONG WINAPI DX12CrashFilter(EXCEPTION_POINTERS *exception)
	{
		if (InterlockedCompareExchange(&gReportStarted, 1, 0) == 0)
			WriteReport(exception);
		// The game's own crash handler (if any) was installed before us: it
		// still decides what happens to the process.
		if (gPreviousFilter)
			return gPreviousFilter(exception);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

void DX12CrashReportInstall()
{
	gPreviousFilter = SetUnhandledExceptionFilter(DX12CrashFilter);
}

#endif // DX12_RELEASE_TIMING
