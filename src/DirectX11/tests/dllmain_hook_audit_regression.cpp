// Compile the real loader implementation once in the regression host, without
// adding production exports or running the hook-installing DllMain from a test.
#include "../DLLMainHook.cpp"
#include "slot_range_test_support.h"
#include <filesystem>
#include <fstream>
#include <winioctl.h>

extern "C" decltype(&CreateFileW) __imp_CreateFileW;
extern "C" decltype(&GetFileSize) __imp_GetFileSize;
extern "C" decltype(&GetFileSizeEx) __imp_GetFileSizeEx;
extern "C" decltype(&ReadFile) __imp_ReadFile;
extern "C" decltype(&CloseHandle) __imp_CloseHandle;
extern "C" decltype(&HeapAlloc) __imp_HeapAlloc;
extern "C" decltype(&HeapFree) __imp_HeapFree;

namespace {
using namespace dx11_slot_test;
const auto loader_create_native = __imp_CreateFileW;
const auto loader_size_native = __imp_GetFileSize;
const auto loader_size_ex_native = __imp_GetFileSizeEx;
const auto loader_read_native = __imp_ReadFile;
const auto loader_close_native = __imp_CloseHandle;
const auto loader_allocate_native = __imp_HeapAlloc;
const auto loader_free_native = __imp_HeapFree;

enum class LoaderReadFault { None, Open, Size, Read, ShortRead, Allocate, DwordBoundary };
struct LoaderReadCalls {
    unsigned opened = 0, size32 = 0, size64 = 0, read = 0, alloc_calls = 0, freed = 0, closed = 0;
    SIZE_T allocation_size = 0;
    DWORD read_size = 0;
};
LoaderReadCalls loader_calls;
LoaderReadFault loader_fault = LoaderReadFault::None;
std::wstring loader_expected_path, loader_fixture_path;
HANDLE loader_input = INVALID_HANDLE_VALUE;
void* loader_buffer = nullptr;
thread_local bool loader_allocation_armed = false;

HANDLE WINAPI loader_create(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD attributes, HANDLE template_file)
{
    if (!path || _wcsicmp(path, loader_expected_path.c_str()))
        return loader_create_native(path, access, share, security, disposition, attributes, template_file);
    ++loader_calls.opened;
    if (loader_fault == LoaderReadFault::Open) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    loader_input = loader_create_native(loader_fixture_path.c_str(), access, share, security, disposition, attributes, template_file);
    return loader_input;
}
DWORD WINAPI loader_size(HANDLE file, LPDWORD high)
{
    if (file != loader_input || file == INVALID_HANDLE_VALUE) return loader_size_native(file, high);
    ++loader_calls.size32; loader_allocation_armed = true;
    if (loader_fault == LoaderReadFault::Size) { SetLastError(ERROR_READ_FAULT); return INVALID_FILE_SIZE; }
    if (loader_fault == LoaderReadFault::DwordBoundary) return MAXDWORD;
    return loader_size_native(file, high);
}
BOOL WINAPI loader_size_ex(HANDLE file, PLARGE_INTEGER size)
{
    if (file != loader_input || file == INVALID_HANDLE_VALUE) return loader_size_ex_native(file, size);
    ++loader_calls.size64; loader_allocation_armed = true;
    if (loader_fault == LoaderReadFault::Size) { SetLastError(ERROR_READ_FAULT); return FALSE; }
    if (loader_fault == LoaderReadFault::DwordBoundary) { size->QuadPart = MAXDWORD; return TRUE; }
    return loader_size_ex_native(file, size);
}
BOOL WINAPI loader_read(HANDLE file, LPVOID data, DWORD size, LPDWORD read, LPOVERLAPPED overlapped)
{
    if (file != loader_input || file == INVALID_HANDLE_VALUE) return loader_read_native(file, data, size, read, overlapped);
    ++loader_calls.read; loader_calls.read_size = size;
    if (loader_fault == LoaderReadFault::Read || loader_fault == LoaderReadFault::Size ||
        loader_fault == LoaderReadFault::DwordBoundary) {
        *read = 0; SetLastError(ERROR_READ_FAULT); return FALSE;
    }
    if (loader_fault == LoaderReadFault::ShortRead && size) --size;
    return loader_read_native(file, data, size, read, overlapped);
}
LPVOID WINAPI loader_allocate(HANDLE heap, DWORD flags, SIZE_T size)
{
    if (!loader_allocation_armed || heap != GetProcessHeap()) return loader_allocate_native(heap, flags, size);
    loader_allocation_armed = false; ++loader_calls.alloc_calls; loader_calls.allocation_size = size;
    if (loader_fault == LoaderReadFault::Allocate || loader_fault == LoaderReadFault::DwordBoundary) return nullptr;
    loader_buffer = loader_allocate_native(heap, flags, size);
    return loader_buffer;
}
BOOL WINAPI loader_free(HANDLE heap, DWORD flags, LPVOID data)
{
    const bool owned = data && data == loader_buffer;
    const BOOL result = loader_free_native(heap, flags, data);
    if (owned && result) { ++loader_calls.freed; loader_buffer = nullptr; }
    return result;
}
BOOL WINAPI loader_close(HANDLE file)
{
    const bool owned = file == loader_input && file != INVALID_HANDLE_VALUE;
    const BOOL result = loader_close_native(file);
    if (owned) {
        loader_allocation_armed = false;
        if (result) { ++loader_calls.closed; loader_input = INVALID_HANDLE_VALUE; }
    }
    return result;
}

template<class Function> class LoaderImportScope {
    Function& slot;
    Function saved;
    void set(Function function)
    {
        DWORD protection = 0;
        require_batch(VirtualProtect(&slot, sizeof(slot), PAGE_READWRITE, &protection) != FALSE,
            "F163: protect this host's actual file/heap import slot");
        slot = function; DWORD ignored = 0;
        if (!VirtualProtect(&slot, sizeof(slot), protection, &ignored)) std::abort();
    }
public:
    LoaderImportScope(Function& target, Function function) : slot(target), saved(target) { set(function); }
    ~LoaderImportScope() { try { set(saved); } catch (...) { std::abort(); } }
};

class LoaderChildHandle {
public:
    HANDLE value;
    explicit LoaderChildHandle(HANDLE handle) : value(handle) {}
    ~LoaderChildHandle() { if (value) loader_close_native(value); }
};

class LoaderInputFixture {
    LoaderImportScope<decltype(&CreateFileW)> create{__imp_CreateFileW, loader_create};
    LoaderImportScope<decltype(&GetFileSize)> size32{__imp_GetFileSize, loader_size};
    LoaderImportScope<decltype(&GetFileSizeEx)> size64{__imp_GetFileSizeEx, loader_size_ex};
    LoaderImportScope<decltype(&ReadFile)> read{__imp_ReadFile, loader_read};
    LoaderImportScope<decltype(&CloseHandle)> close{__imp_CloseHandle, loader_close};
    LoaderImportScope<decltype(&HeapAlloc)> allocate{__imp_HeapAlloc, loader_allocate};
    LoaderImportScope<decltype(&HeapFree)> free{__imp_HeapFree, loader_free};
public:
    std::filesystem::path directory, path, proxy, profile_directory, profile_host;
    HMODULE owner = GetModuleHandleW(L"kernel32.dll");
    std::string executable_name;
    LoaderInputFixture()
    {
        wchar_t executable[MAX_PATH]{}, module[MAX_PATH]{};
        require_batch(owner && GetModuleFileNameW(nullptr, executable, MAX_PATH) &&
            GetModuleFileNameW(owner, module, MAX_PATH), "F163: real executable/module path identities");
        executable_name = std::filesystem::path(executable).filename().u8string();
        // Stay inside the regression process's owned directory. CMake supplies
        // the real component paths; no separate simulator build is required.
        const auto build = std::filesystem::canonical(executable).parent_path();
        directory = build / (L"F163-loader-" + std::to_wstring(GetCurrentProcessId()));
        require_batch(directory.is_absolute() && std::filesystem::create_directory(directory) &&
            std::filesystem::canonical(directory).parent_path() == build, "F163: exclusive absolute fixture path stays in this worktree's build");
        path = directory / L"d3dx.ini";
        proxy = directory / L"F163-proxy.dll";
        profile_directory = directory / L"profile-host";
        profile_host = profile_directory / L"rundll32.exe";
        loader_expected_path = (std::filesystem::path(module).parent_path() / L"d3dx.ini").wstring();
        loader_fixture_path = path.wstring();
    }
    ~LoaderInputFixture()
    {
        loader_allocation_armed = false;
        if (loader_input != INVALID_HANDLE_VALUE) { loader_close_native(loader_input); loader_input = INVALID_HANDLE_VALUE; }
        // A failed old-code assertion may leave a pointer whose CRT free took a
        // different route. Never risk a second free; the failing child exits.
        loader_buffer = nullptr;
        DeleteFileW(profile_host.c_str()); RemoveDirectoryW(profile_directory.c_str());
        DeleteFileW(path.c_str()); DeleteFileW(proxy.c_str()); RemoveDirectoryW(directory.c_str());
        loader_expected_path.clear(); loader_fixture_path.clear();
    }
    void write(const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), text.size()); output.close();
        require_batch(output.good(), "F163: write complete actual INI fixture");
    }
    void sparse_overflow(const std::string& loader_name)
    {
        wchar_t volume[MAX_PATH]{}, filesystem[MAX_PATH]{};
        require_batch(GetVolumePathNameW(directory.c_str(), volume, MAX_PATH) &&
            GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, filesystem, MAX_PATH) &&
            !_wcsicmp(filesystem, L"NTFS"), "F163: sparse overflow fixture requires real NTFS");
        HANDLE file = loader_create_native(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require_batch(file != INVALID_HANDLE_VALUE, "F163: create exclusive real sparse file");
        DWORD returned = 0, written = 0;
        const bool sparse = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != FALSE;
        std::string prefix = "[Loader]\nloader=" + loader_name + "\n"; prefix.resize(256, '\n');
        LARGE_INTEGER end{}; end.QuadPart = (1LL << 32) + 256;
        const bool completed = sparse && WriteFile(file, prefix.data(), static_cast<DWORD>(prefix.size()), &written, nullptr) &&
            written == prefix.size() && SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
        FILE_STANDARD_INFO standard{};
        const bool measured = GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) != FALSE;
        loader_close_native(file);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        require_batch(completed && measured && standard.EndOfFile.QuadPart == end.QuadPart &&
            standard.AllocationSize.QuadPart > 0 && standard.AllocationSize.QuadPart < (1LL << 20) &&
            attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_SPARSE_FILE),
            "F163: >4GiB logical file is sparse with less than 1MiB actual allocated storage");
        std::fprintf(stderr, "F163 sparse path=%s logical=%lld allocated=%lld\n", path.u8string().c_str(),
            standard.EndOfFile.QuadPart, standard.AllocationSize.QuadPart);
    }
    void component_case(const std::filesystem::path& host, const std::wstring& scenario)
    {
        LoaderChildHandle job(CreateJobObjectW(nullptr, nullptr));
        require_batch(job.value != nullptr, "F163: create owned DLL-loading child job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require_batch(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
            "F163: configure owned child cleanup");
        std::wstring command = L"\"" + host.wstring() + L"\" \"" + proxy.wstring() + L"\" \"" + scenario + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};
        require_batch(CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process) != FALSE,
            "F163: launch actual-DLL loader-lock validation in an isolated child");
        LoaderChildHandle child(process.hProcess), thread(process.hThread);
        if (!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == DWORD(-1)) {
            TerminateProcess(child.value, EXIT_FAILURE); WaitForSingleObject(child.value, 5000);
            throw std::runtime_error("F163: attach and resume the DLL-loading child");
        }
        const auto waited = WaitForSingleObject(child.value, 10000);
        if (waited != WAIT_OBJECT_0) {
            TerminateJobObject(job.value, ERROR_TIMEOUT); WaitForSingleObject(child.value, 5000);
        }
        DWORD code = EXIT_FAILURE;
        const BOOL collected = GetExitCodeProcess(child.value, &code);
        std::fprintf(stderr, "F163 actual-DLL mode=%ls exit=%lu wait=%lu\n", scenario.c_str(), code, waited);
        require_batch(waited == WAIT_OBJECT_0 && collected && code == 0,
            "F163: actual production DLL preserves loader file and early-allow decisions inside real DllMain");
    }
    void components()
    {
        const auto host = std::filesystem::u8path(DX11_LOADER_TEST_HOST);
        const auto candidate = std::filesystem::u8path(DX11_LOADER_TEST_DLL);
        require_batch(CopyFileW(candidate.c_str(), proxy.c_str(), TRUE) != FALSE,
            "F163: copy the actual built production DLL into the owned outside-directory fixture");
        component_case(host, L"loader-size-rejection");
        require_batch(std::filesystem::create_directory(profile_directory) && CopyFileW(host.c_str(), profile_host.c_str(), TRUE),
            "F163: copy the SDK-only host under the real profile-helper basename");
        // This executable is our SDK fixture, not Windows rundll32. It only
        // loads the DLL; it never resolves or invokes the driver-profile export.
        component_case(profile_host, L"loader-profile-allow,Install3DMigotoDriverProfile ");
        write("[Loader]\nloader=rundll32.exe\n");
        component_case(profile_host, L"loader-profile-deny");
    }
    void run(LoaderReadFault fault, bool expected, unsigned reads, unsigned allocation_calls, unsigned frees)
    {
        loader_fault = fault; loader_calls = {}; loader_allocation_armed = false;
        bool result = false, threw = false;
        try { result = verify_intended_target(owner); } catch (...) { threw = true; }
        std::fprintf(stderr, "F163 fault=%d result=%d threw=%d open=%u size32=%u size64=%u read=%u allocCalls=%u bytes=%llu freed=%u close=%u\n",
            static_cast<int>(fault), result, threw, loader_calls.opened, loader_calls.size32, loader_calls.size64,
            loader_calls.read, loader_calls.alloc_calls, static_cast<unsigned long long>(loader_calls.allocation_size), loader_calls.freed, loader_calls.closed);
        require_batch(!threw && result == expected, "F163: production loader result/failure must not escape as an allocation exception");
        require_batch(loader_calls.opened == 1 && loader_calls.closed == (fault == LoaderReadFault::Open ? 0u : 1u) &&
            loader_calls.read == reads && loader_calls.alloc_calls == allocation_calls && loader_calls.freed == frees &&
            loader_input == INVALID_HANDLE_VALUE && !loader_buffer,
            "F163: actual Win32 file/heap calls obey the rejection boundary and release every owned handle/buffer");
        require_batch(fault == LoaderReadFault::Open || loader_calls.size64 == 1,
            "F163: full-width file size is queried on the actual opened handle");
    }
};
}

void test_dx11_loader_file_bounds_regression()
{
    LoaderInputFixture fixture;
    fixture.sparse_overflow("dx11_sim_host.exe");
    fixture.components();
    require_batch(DeleteFileW(fixture.path.c_str()) != FALSE, "F163: remove the owned component INI before the second sparse fixture");
    fixture.sparse_overflow(fixture.executable_name);
    fixture.run(LoaderReadFault::None, false, 0, 0, 0);
    DeleteFileW(fixture.path.c_str());
    const std::string loader = "[Loader]\nloader=" + fixture.executable_name + "\n";
    fixture.write(loader);
    fixture.run(LoaderReadFault::None, true, 1, 1, 1);
    require_batch(loader_calls.allocation_size == loader.size() + 1 && loader_calls.read_size == loader.size(),
        "F163: read complete input bytes while reserving exactly one terminator");
    fixture.write("[Loader]\nloader=" + fixture.executable_name);
    fixture.run(LoaderReadFault::None, true, 1, 1, 1);
    fixture.write("[Loader]\nloader=unrelated-F163.exe\ntarget=" + fixture.executable_name + "\n");
    fixture.run(LoaderReadFault::None, true, 1, 1, 1);
    fixture.write("[Loader]\ntarget=" + fixture.executable_name + "\n");
    fixture.run(LoaderReadFault::None, true, 1, 1, 1);
    fixture.write("[Loader]\ntarget=unrelated-F163.exe\n");
    fixture.run(LoaderReadFault::None, false, 1, 1, 1);
    fixture.write("[Other]\ntarget=" + fixture.executable_name + "\n");
    fixture.run(LoaderReadFault::None, false, 1, 1, 1);
    fixture.write(""); fixture.run(LoaderReadFault::None, false, 1, 1, 1);
    require_batch(loader_calls.allocation_size == 1, "F163: empty input retains an in-bounds terminator buffer");
    fixture.write(loader);
    fixture.run(LoaderReadFault::Open, false, 0, 0, 0);
    fixture.run(LoaderReadFault::Size, false, 0, 0, 0);
    fixture.run(LoaderReadFault::Allocate, false, 0, 1, 0);
    fixture.run(LoaderReadFault::Read, false, 1, 1, 1);
    fixture.run(LoaderReadFault::ShortRead, false, 1, 1, 1);
    fixture.run(LoaderReadFault::DwordBoundary, false, 0, sizeof(SIZE_T) > sizeof(DWORD) ? 1u : 0u, 0);
    if (sizeof(SIZE_T) > sizeof(DWORD)) require_batch(loader_calls.allocation_size == (1ULL << 32),
        "F163: DWORD maximum plus terminator is computed in pointer-width arithmetic");
    loader_calls = {}; loader_allocation_armed = false;
    require_batch(verify_intended_target(GetModuleHandleW(nullptr)) && !loader_calls.opened,
        "F163: unchanged same-directory early allow does not depend on INI input faults");
    loader_fault = LoaderReadFault::None;
}
