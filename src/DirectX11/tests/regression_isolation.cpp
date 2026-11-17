#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
void check_isolation(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

std::filesystem::path executable_path()
{
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    check_isolation(length && length < MAX_PATH, "F202: resolve complete regression executable path");
    return std::filesystem::canonical(path);
}

class OwnedRegressionDirectory {
    std::filesystem::path base;
public:
    std::filesystem::path path;
    explicit OwnedRegressionDirectory(const std::filesystem::path& executable)
    {
        std::filesystem::path build;
        for (const auto& component : executable.parent_path()) {
            build /= component;
            if (!_wcsicmp(component.c_str(), L"build")) break;
        }
        check_isolation(!_wcsicmp(build.filename().c_str(), L"build"), "F202: fixture must reside under build");
        base = std::filesystem::canonical(build) / L"dx11-regression-fixtures";
        std::filesystem::create_directories(base);
        base = std::filesystem::canonical(base);
        check_isolation(base.parent_path() == std::filesystem::canonical(build),
            "F202: fixture root must resolve directly inside build");
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            auto candidate = base / (L"r-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (std::filesystem::create_directory(candidate)) { path = std::filesystem::canonical(candidate); break; }
        }
        check_isolation(!path.empty(), "F202: reserve an exclusive fixture directory");
    }
    void clean()
    {
        if (path.empty()) return;
        // Never recursively remove a computed path without checking its final
        // absolute parent and rejecting a replaced directory/reparse point.
        const auto resolved = std::filesystem::canonical(path);
        const auto attributes = GetFileAttributesW(path.c_str());
        check_isolation(resolved == path && resolved.parent_path() == base &&
            path.filename().wstring().find(L"r-") == 0 && attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "F202: refuse unsafe fixture cleanup target");
        std::filesystem::remove_all(path);
        path.clear();
    }
    ~OwnedRegressionDirectory()
    {
        try { clean(); }
        catch (const std::exception& error) { std::fprintf(stderr, "FAIL fixture cleanup: %s\n", error.what()); }
    }
};

class WindowsHandle {
public:
    HANDLE value;
    explicit WindowsHandle(HANDLE handle) : value(handle) {}
    ~WindowsHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;
};

std::wstring wide_argument(const char* text)
{
    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    check_isolation(length > 0, "F202: decode argument");
    std::wstring result(length, L'\0');
    check_isolation(MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], length) == length, "F202: decode complete argument");
    result.pop_back(); return result;
}

std::wstring quote_argument(const std::wstring& value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result.push_back(character); slashes = 0;
    }
    result.append(slashes * 2, L'\\'); result.push_back(L'"');
    return result;
}

DWORD run_fixture_child(const std::filesystem::path& executable, const std::filesystem::path& directory,
    int argc, char** argv)
{
    WindowsHandle job(CreateJobObjectW(nullptr, nullptr));
    check_isolation(job.value != nullptr, "F202: create owned child job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check_isolation(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
        "F202: configure child lifetime");
    std::wstring command = quote_argument(executable.wstring());
    for (int i = 1; i < argc; ++i) command += L" " + quote_argument(wide_argument(argv[i]));
    command += L" --fixture-isolated";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    check_isolation(CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process) != FALSE,
        "F202: launch isolated regression process");
    WindowsHandle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(child.value, EXIT_FAILURE); WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("F202: attach and resume owned child");
    }
    const auto waited = WaitForSingleObject(child.value, 60000);
    if (waited != WAIT_OBJECT_0) {
        TerminateJobObject(job.value, ERROR_TIMEOUT); WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("F202: isolated regression timeout or wait failure");
    }
    DWORD code = EXIT_FAILURE;
    check_isolation(GetExitCodeProcess(child.value, &code) != FALSE, "F202: collect child exit code");
    return code;
}
}

int run_dx11_regression_isolated(int argc, char** argv)
{
    try {
        const auto original = executable_path();
        OwnedRegressionDirectory fixture(original);
        for (const auto* name : {L"dx11_upstream_regression.exe", L"d3d11.dll", L"wic-regression.png"})
            std::filesystem::copy_file(original.parent_path() / name, fixture.path / name);
        const auto sanitizer = original.parent_path() / L"clang_rt.asan_dynamic-x86_64.dll";
        if (std::filesystem::exists(sanitizer)) std::filesystem::copy_file(sanitizer, fixture.path / sanitizer.filename());
        std::printf("Isolated regression directory: %s\n", fixture.path.u8string().c_str()); std::fflush(stdout);
        const auto code = run_fixture_child(fixture.path / original.filename(), fixture.path, argc, argv);
        fixture.clean();
        return static_cast<int>(code);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL isolated regression: %s\n", error.what()); return EXIT_FAILURE;
    }
}

int exit_dx11_regression_fixture()
{
    const auto config = executable_path().parent_path() / L"d3dx.ini";
    WindowsHandle file(CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    check_isolation(file.value != INVALID_HANDLE_VALUE, "F202: create abrupt-exit fixture without overwriting");
    const char text[] = "F202 owned abrupt-exit fixture"; DWORD count = 0;
    check_isolation(WriteFile(file.value, text, sizeof(text), &count, nullptr) && count == sizeof(text),
        "F202: write complete abrupt-exit fixture");
    CloseHandle(file.value); file.value = nullptr;
    std::fputs("F202: exit with a live owned d3dx.ini\n", stderr); std::fflush(stderr);
    std::exit(37);
}
