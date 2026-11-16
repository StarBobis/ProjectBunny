// F-271 fault host: loads the DXGI proxy and injects deterministic failures
// into its import table.  Only this test process is modified.
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

using LoadLibraryWFunction = HMODULE (WINAPI*)(LPCWSTR);
using GetProcAddressFunction = FARPROC (WINAPI*)(HMODULE, LPCSTR);
using CreateFactoryFunction = HRESULT (WINAPI*)(REFIID, void**);

enum class FaultMode { Control, LoadFailure, MissingExport, Race, SelfLoad };

FaultMode g_mode = FaultMode::Control;
LoadLibraryWFunction g_real_load_library = nullptr;
GetProcAddressFunction g_real_get_proc_address = nullptr;
HMODULE g_proxy_module = nullptr;
std::atomic<int> g_load_library_calls{0};
std::atomic<int> g_get_proc_address_calls{0};
HANDLE g_rendezvous_entered = nullptr;
HANDLE g_rendezvous_release = nullptr;
HANDLE g_second_thread_started = nullptr;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void require_hr(HRESULT actual, HRESULT expected, const char* message)
{
    if (actual == expected) return;
    char detail[256]{};
    std::snprintf(detail, sizeof(detail), "%s (expected %08lX, got %08lX)", message,
        static_cast<unsigned long>(expected), static_cast<unsigned long>(actual));
    throw std::runtime_error(detail);
}

std::string utf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring executable_path()
{
    wchar_t buffer[32768]{};
    check(GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer))) != 0,
        "locate test harness executable");
    return buffer;
}

std::wstring file_mtime_utc(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    check(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) != FALSE,
        "read harness file attributes");
    SYSTEMTIME time{};
    check(FileTimeToSystemTime(&data.ftLastWriteTime, &time) != FALSE, "convert harness mtime");
    wchar_t buffer[64]{};
    check(swprintf_s(buffer, std::size(buffer), L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds) > 0, "format harness mtime");
    return buffer;
}

void print_provenance(const std::filesystem::path& candidate, const std::wstring& dll_sha256)
{
    const std::wstring executable = executable_path();
    std::printf("HARNESS_EXE=%s\n", utf8(executable).c_str());
    std::printf("HARNESS_EXE_MTIME_UTC=%s\n", utf8(file_mtime_utc(executable)).c_str());
    std::printf("TEST_DLL=%s\n", utf8(candidate.wstring()).c_str());
    std::printf("TEST_DLL_SHA256=%s\n", utf8(dll_sha256).c_str());
    std::fflush(stdout);
}

HMODULE WINAPI ObserveLoadLibraryW(LPCWSTR name)
{
    g_load_library_calls.fetch_add(1);
    if (g_mode == FaultMode::LoadFailure) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    if (g_mode == FaultMode::SelfLoad) return g_proxy_module;
    return g_real_load_library(name);
}

FARPROC WINAPI ObserveGetProcAddress(HMODULE module, LPCSTR name)
{
    const int call = g_get_proc_address_calls.fetch_add(1) + 1;
    const bool named = name != nullptr && reinterpret_cast<ULONG_PTR>(name) > 0xFFFF;
    std::printf("GPA[%d] name=%s\n", call, named ? name : "<ordinal>");
    std::fflush(stdout);
    // The proxy's static CRT resolves FlsGetValue through this same IAT before
    // the initialization callback runs, so gate on the target export instead
    // of on the first call.
    if (g_mode == FaultMode::Race && named && std::strcmp(name, "CreateDXGIFactory") == 0) {
        SetEvent(g_rendezvous_entered);
        WaitForSingleObject(g_rendezvous_release, INFINITE);
    }
    if (g_mode == FaultMode::MissingExport && named &&
        std::strcmp(name, "CreateDXGIFactory1") == 0)
        return nullptr;
    return g_real_get_proc_address(module, name);
}

bool is_kernel_library(const char* name)
{
    return _stricmp(name, "KERNEL32.dll") == 0 ||
        _strnicmp(name, "api-ms-win-core-libraryloader", 29) == 0;
}

void** find_import_slot(HMODULE module, const char* function_name)
{
    auto* base = reinterpret_cast<unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    check(dos->e_magic == IMAGE_DOS_SIGNATURE, "candidate has a DOS header");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    check(nt->Signature == IMAGE_NT_SIGNATURE, "candidate has a PE header");
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    check(directory.VirtualAddress != 0, "candidate imports kernel32");
    auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        const char* library = reinterpret_cast<const char*>(base + descriptor->Name);
        if (!is_kernel_library(library)) continue;
        check(descriptor->OriginalFirstThunk != 0, "candidate has a named import table");
        auto* lookup = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* address = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; lookup->u1.AddressOfData != 0; ++lookup, ++address) {
            if (IMAGE_SNAP_BY_ORDINAL(lookup->u1.Ordinal)) continue;
            const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + lookup->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(imported->Name), function_name) == 0)
                return reinterpret_cast<void**>(&address->u1.Function);
        }
    }
    return nullptr;
}

class ImportPatch {
public:
    ImportPatch(void** slot, void* replacement) : slot_(slot), original_(*slot_)
    {
        DWORD protection = 0;
        check(VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &protection) != FALSE,
            "make import slot writable");
        protection_ = protection;
        *slot_ = replacement;
        DWORD ignored = 0;
        check(VirtualProtect(slot_, sizeof(*slot_), protection_, &ignored) != FALSE,
            "restore import slot protection");
    }
    ~ImportPatch()
    {
        DWORD ignored = 0;
        if (!VirtualProtect(slot_, sizeof(*slot_), PAGE_READWRITE, &ignored)) return;
        *slot_ = original_;
        VirtualProtect(slot_, sizeof(*slot_), protection_, &ignored);
    }
    ImportPatch(const ImportPatch&) = delete;
    ImportPatch& operator=(const ImportPatch&) = delete;
private:
    void** slot_;
    void* original_;
    DWORD protection_ = 0;
};

std::wstring system_directory()
{
    wchar_t directory[MAX_PATH]{};
    check(GetSystemDirectoryW(directory, MAX_PATH) != 0, "locate System32");
    return directory;
}

void preload_native_runtime()
{
    const std::wstring directory = system_directory() + L"\\";
    check(LoadLibraryExW((directory + L"dxgi.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) != nullptr,
        "preload system dxgi.dll");
    check(LoadLibraryExW((directory + L"d3d11.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) != nullptr,
        "preload system d3d11.dll");
}

HMODULE load_candidate(const std::filesystem::path& path)
{
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    check(module != nullptr, "load candidate dxgi.dll");
    wchar_t actual[32768]{};
    check(GetModuleFileNameW(module, actual, static_cast<DWORD>(std::size(actual))) != 0,
        "locate loaded candidate");
    check(std::filesystem::equivalent(path, std::filesystem::path(actual)),
        "loaded module is the requested candidate");
    return module;
}

CreateFactoryFunction factory_entry(HMODULE module)
{
    const auto function = reinterpret_cast<CreateFactoryFunction>(GetProcAddress(module, "CreateDXGIFactory1"));
    check(function != nullptr, "candidate exports CreateDXGIFactory1");
    return function;
}

void patch_imports(HMODULE module, FaultMode mode)
{
    if (mode == FaultMode::Control) return;

    void** load_slot = find_import_slot(module, "LoadLibraryW");
    check(load_slot != nullptr, "candidate imports LoadLibraryW from kernel32");
    g_real_load_library = reinterpret_cast<LoadLibraryWFunction>(*load_slot);

    void** get_proc_slot = find_import_slot(module, "GetProcAddress");
    check(get_proc_slot != nullptr, "candidate imports GetProcAddress from kernel32");
    g_real_get_proc_address = reinterpret_cast<GetProcAddressFunction>(*get_proc_slot);

    // Intentionally leaked for the lifetime of the test process: the proxy may
    // still be running on other threads when the harness exits.
    new ImportPatch(load_slot, reinterpret_cast<void*>(&ObserveLoadLibraryW));
    new ImportPatch(get_proc_slot, reinterpret_cast<void*>(&ObserveGetProcAddress));
}

void run_control(HMODULE module)
{
    ComPtr<IDXGIFactory4> factory;
    require_hr(factory_entry(module)(IID_PPV_ARGS(&factory)), S_OK, "control CreateDXGIFactory1");
    check(factory != nullptr, "control returned a factory");
    ComPtr<IDXGIAdapter1> adapter;
    check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) && adapter,
        "control enumerates the WARP adapter");
    DXGI_ADAPTER_DESC1 desc{};
    check(SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE),
        "control adapter is software");
    std::puts("PASS: T-0 normal forwarding");
}

void run_load_failure(HMODULE module)
{
    const auto create = factory_entry(module);
    for (int attempt = 1; attempt <= 2; ++attempt) {
        void* factory = reinterpret_cast<void*>(1);
        require_hr(create(__uuidof(IDXGIFactory1), &factory), HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND),
            "load-failure result");
        check(factory == nullptr, "load failure clears *ppFactory");
    }
    check(g_load_library_calls.load() == 1, "failed initialization is latched to one attempt");
    std::printf("PASS: T-1 load failure latched (hr=8007007E, LoadLibrary calls=%d)\n",
        g_load_library_calls.load());
}

void run_missing_export(HMODULE module)
{
    void* factory = reinterpret_cast<void*>(1);
    require_hr(factory_entry(module)(__uuidof(IDXGIFactory1), &factory),
        HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND), "missing-export result");
    check(factory == nullptr, "missing export clears *ppFactory");
    std::puts("PASS: T-1b missing CreateDXGIFactory1 (hr=8007007F)");
}

struct ThreadCall {
    CreateFactoryFunction create = nullptr;
    HANDLE started = nullptr;
    HRESULT hr = E_FAIL;
    void* factory = nullptr;
};

DWORD WINAPI CallFactoryThread(LPVOID parameter)
{
    auto* call = static_cast<ThreadCall*>(parameter);
    if (call->started) SetEvent(call->started);
    call->hr = call->create(__uuidof(IDXGIFactory1), &call->factory);
    return 0;
}

void run_race(HMODULE module)
{
    check(g_rendezvous_entered != nullptr && g_rendezvous_release != nullptr,
        "race rendezvous events exist");
    const auto create = factory_entry(module);

    ThreadCall first{create, nullptr};
    HANDLE first_thread = CreateThread(nullptr, 0, CallFactoryThread, &first, 0, nullptr);
    check(first_thread != nullptr, "start first factory thread");
    check(WaitForSingleObject(g_rendezvous_entered, 10000) == WAIT_OBJECT_0,
        "first thread reached the GetProcAddress rendezvous");

    ThreadCall second{create, g_second_thread_started};
    HANDLE second_thread = CreateThread(nullptr, 0, CallFactoryThread, &second, 0, nullptr);
    check(second_thread != nullptr, "start second factory thread");
    check(WaitForSingleObject(g_second_thread_started, 10000) == WAIT_OBJECT_0,
        "second thread entered the export");
    // The first thread is pinned inside GetProcAddress by the IAT hook, so the
    // second thread must block in the initialization gate, not return early.
    Sleep(200);
    check(SetEvent(g_rendezvous_release) != FALSE, "release the first GetProcAddress call");

    check(WaitForSingleObject(first_thread, 10000) == WAIT_OBJECT_0, "first factory thread exits");
    check(WaitForSingleObject(second_thread, 10000) == WAIT_OBJECT_0, "second factory thread exits");
    CloseHandle(first_thread);
    CloseHandle(second_thread);

    require_hr(first.hr, S_OK, "first racing call");
    require_hr(second.hr, S_OK, "second racing call");
    check(first.factory != nullptr && second.factory != nullptr, "both racing calls returned a factory");
    std::puts("PASS: T-2 first-initialization race");
}

void run_self_load(HMODULE module)
{
    void* factory = reinterpret_cast<void*>(1);
    require_hr(factory_entry(module)(__uuidof(IDXGIFactory1), &factory),
        HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), "self-load refusal");
    check(factory == nullptr, "self-load clears *ppFactory");
    std::puts("PASS: T-3 self-load refused (hr=80070032)");
}

FaultMode parse_mode(const std::wstring& name)
{
    if (name == L"control") return FaultMode::Control;
    if (name == L"loadfail") return FaultMode::LoadFailure;
    if (name == L"missing-export") return FaultMode::MissingExport;
    if (name == L"race") return FaultMode::Race;
    if (name == L"self") return FaultMode::SelfLoad;
    throw std::runtime_error("unknown fault mode");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        check(argc == 3 || argc == 4,
            "usage: dxgi_proxy_fault_host <absolute dxgi.dll> <control|loadfail|missing-export|race|self> [dll-sha256]");
        const std::filesystem::path candidate(argv[1]);
        check(candidate.is_absolute(), "candidate path must be absolute");
        g_mode = parse_mode(argv[2]);
        print_provenance(candidate, argc == 4 ? std::wstring(argv[3]) : std::wstring(L"<not-provided>"));

        if (g_mode == FaultMode::Race) {
            g_rendezvous_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            g_rendezvous_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            g_second_thread_started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            check(g_rendezvous_entered != nullptr && g_rendezvous_release != nullptr &&
                g_second_thread_started != nullptr, "create race synchronization events");
        }

        preload_native_runtime();
        HMODULE module = load_candidate(candidate);
        g_proxy_module = module;
        patch_imports(module, g_mode);

        switch (g_mode) {
        case FaultMode::Control: run_control(module); break;
        case FaultMode::LoadFailure: run_load_failure(module); break;
        case FaultMode::MissingExport: run_missing_export(module); break;
        case FaultMode::Race: run_race(module); break;
        case FaultMode::SelfLoad: run_self_load(module); break;
        }
        std::fflush(stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL dxgi-fault: %s (Win32=%lu)\n", error.what(), GetLastError());
        return 1;
    }
}
