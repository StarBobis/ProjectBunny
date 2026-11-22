#include "CrashDiagnostics.h"
#include <Windows.h>
#include <cwchar>

LONG WINAPI Previous(EXCEPTION_POINTERS*)
{
    HANDLE file = CreateFileW(L"previous-filter-called", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return EXCEPTION_EXECUTE_HANDLER;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(Previous);
    InitializeCrashDiagnostics(argv[1], argv[2]);
    InitializeCrashDiagnostics(argv[1], argv[2]); // reload is idempotent
    if (!wcscmp(argv[3], L"normal")) return 0;
    if (!wcscmp(argv[3], L"handled")) {
        __try { RaiseException(0xE0123456, 0, 0, nullptr); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    *reinterpret_cast<volatile int*>(static_cast<ULONG_PTR>(1)) = 42;
    return 3;
}
