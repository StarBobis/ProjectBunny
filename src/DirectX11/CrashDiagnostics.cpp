#include "CrashDiagnostics.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cwchar>

namespace {
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
wchar_t directory[MAX_PATH] = {};
HANDLE requested = nullptr;
HANDLE completed = nullptr;
LONG capturing = 0;
DWORD crashing_thread = 0;
EXCEPTION_RECORD record = {};
CONTEXT context = {};
EXCEPTION_POINTERS captured = { &record, &context };
EXCEPTION_POINTERS* exception = &captured;
LPTOP_LEVEL_EXCEPTION_FILTER previous = nullptr;

void WriteReport() noexcept
{
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t base[MAX_PATH - 8], path[MAX_PATH];
    if (_snwprintf_s(base, _TRUNCATE, L"%s\\dx11_crash_%04u%02u%02u_%02u%02u%02u_%03u_pid%lu",
        directory, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, GetCurrentProcessId()) < 0) return;
    swprintf_s(path, L"%s.txt", base);
    HANDLE report = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    // Publish the small report before entering DbgHelp, which can fail in a damaged process.
    char text[2048];
    HMODULE module = nullptr;
    wchar_t module_path[MAX_PATH] = {};
    const auto address = exception->ExceptionRecord->ExceptionAddress;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module);
    if (module) GetModuleFileNameW(module, module_path, MAX_PATH);
    char module_utf8[MAX_PATH * 4] = {};
    WideCharToMultiByte(CP_UTF8, 0, module_path, -1, module_utf8, sizeof(module_utf8), nullptr, nullptr);
    int count = sprintf_s(text,
        "DX11 unhandled exception\r\nBuild: %s %s\r\nPID: %lu TID: %lu\r\nCode: 0x%08lX Address: %p\r\nModule: %s\r\nModule base: %p RVA: 0x%llX\r\n",
        __DATE__, __TIME__, GetCurrentProcessId(), crashing_thread,
        exception->ExceptionRecord->ExceptionCode, address, module_utf8, module,
        static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(address) - reinterpret_cast<ULONG_PTR>(module)));
    DWORD written;
    if (report != INVALID_HANDLE_VALUE && count > 0) {
        WriteFile(report, text, count, &written, nullptr);
        FlushFileBuffers(report);
    }
    swprintf_s(path, L"%s.dmp", base);
    HANDLE dump = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD error = ERROR_SUCCESS;
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info = { crashing_thread, exception, FALSE };
        if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
            &info, nullptr, nullptr)) error = GetLastError();
        FlushFileBuffers(dump);
        CloseHandle(dump);
        if (error) DeleteFileW(path); // Do not present an incomplete dump as usable.
    } else error = GetLastError();
    if (report != INVALID_HANDLE_VALUE) {
        count = sprintf_s(text, "MiniDumpWriteDump error: 0x%08lX (0 = success)\r\n", error);
        WriteFile(report, text, count, &written, nullptr);
        CloseHandle(report);
    }
}

DWORD WINAPI Worker(void*) noexcept
{
    if (WaitForSingleObject(requested, INFINITE) == WAIT_OBJECT_0) {
        WriteReport();
        SetEvent(completed);
    }
    return 0;
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* info) noexcept
{
    if (info && info->ExceptionRecord && info->ContextRecord && InterlockedCompareExchange(&capturing, 1, 0) == 0) {
        record = *info->ExceptionRecord;
        context = *info->ContextRecord;
        crashing_thread = GetCurrentThreadId();
        SetEvent(requested);
        // Keep the exception context alive; never hang termination indefinitely.
        WaitForSingleObject(completed, 15000);
    }
    return previous ? previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

struct Settings { const wchar_t* ini; const wchar_t* log; };
BOOL CALLBACK Install(PINIT_ONCE, PVOID parameter, PVOID*) noexcept
{
    const auto settings = static_cast<const Settings*>(parameter);
    if (!GetPrivateProfileIntW(L"Logging", L"crash_dump", 1, settings->ini)) return TRUE;
    // Explicit legacy interactive debugging retains its existing handler and semantics.
    if (GetPrivateProfileIntW(L"Logging", L"crash", 0, settings->ini)) return TRUE;
    if (wcsncpy_s(directory, settings->log, _TRUNCATE)) return TRUE;
    auto slash = wcsrchr(directory, L'\\');
    if (!slash) return TRUE;
    *slash = 0;
    if (wcscat_s(directory, L"\\CrashDumps")) return TRUE;
    if (!CreateDirectoryW(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        if (!GetTempPathW(MAX_PATH, directory) || wcscat_s(directory, L"3Dmigoto-DX11-CrashDumps")) return TRUE;
        if (!CreateDirectoryW(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return TRUE;
    }
    requested = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!requested || !completed) {
        if (requested) CloseHandle(requested);
        if (completed) CloseHandle(completed);
        return TRUE;
    }
    // The worker and callback must remain valid even if an injector releases its reference.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Install), &pinned)) {
        CloseHandle(requested); CloseHandle(completed); return TRUE;
    }
    HANDLE worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!worker) { CloseHandle(requested); CloseHandle(completed); return TRUE; }
    CloseHandle(worker);
    previous = SetUnhandledExceptionFilter(Unhandled);
    return TRUE;
}
}

void InitializeCrashDiagnostics(const wchar_t* ini_path, const wchar_t* log_path) noexcept
{
    Settings settings = { ini_path, log_path };
    InitOnceExecuteOnce(&once, Install, &settings, nullptr);
}
