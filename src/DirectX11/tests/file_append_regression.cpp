#include <Windows.h>
#include "util.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
class AppendFiles {
    std::vector<std::filesystem::path> owned;
public:
    std::filesystem::path binary, created, text;
    AppendFiles()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "locate append test executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool inside_build = false;
        for (const auto& component : directory) inside_build |= _wcsicmp(component.c_str(), L"build") == 0;
        check(inside_build, "append fixtures stay under build");
        const auto prefix = L"file_append_" + std::to_wstring(GetCurrentProcessId()) + L"_";
        binary = directory / (prefix + L"binary.tmp");
        created = directory / (prefix + L"created.tmp");
        text = directory / (prefix + L"text.tmp");
        try {
            for (const auto& path : {binary, created, text}) {
                HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                check(handle != INVALID_HANDLE_VALUE, "exclusively reserve append fixture");
                CloseHandle(handle);
                owned.push_back(path);
            }
        } catch (...) { cleanup(); throw; }
    }
    ~AppendFiles() { cleanup(); }
    void cleanup() noexcept { for (const auto& path : owned) DeleteFileW(path.c_str()); }
    static File open(const std::filesystem::path& path, const wchar_t* mode)
    {
        FILE* file = nullptr;
        check(wfopen_ensuring_access(&file, path.c_str(), mode) == 0 && file, "open real secure stdio stream");
        return File(file, &std::fclose);
    }
    static std::string read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        check(file.good(), "read exact append fixture bytes");
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};
void run_append_tests()
{
    AppendFiles files;
    const std::string original("A\n\r\x1a\0Z", 6);
    {
        auto file = AppendFiles::open(files.binary, L"wb");
        check(std::fwrite(original.data(), 1, original.size(), file.get()) == original.size(), "write binary control bytes");
    }
    check(AppendFiles::read(files.binary) == original, "binary write does not translate control bytes");
    {
        auto file = AppendFiles::open(files.binary, L"ab");
        check(_ftelli64(file.get()) == static_cast<__int64>(original.size()), "append starts at actual end");
        check(std::fseek(file.get(), 0, SEEK_SET) == 0 && std::fwrite("tail", 1, 4, file.get()) == 4,
            "append writes remain at end even after seeking");
    }
    const auto appended = original + "tail";
    FILE* exclusive = stdin;
    const auto collision = wfopen_exclusive_ensuring_access(&exclusive, files.binary.c_str(), L"wb");
    check(collision == EEXIST && !exclusive && AppendFiles::read(files.binary) == appended,
        "exclusive creation collision never truncates another existing file");
    check(AppendFiles::read(files.binary) == appended, "append preserves all original binary bytes");
    HANDLE locked = CreateFileW(files.binary.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "hold real conflicting Windows file share");
    FILE* denied = stdin;
    const auto error = wfopen_ensuring_access(&denied, files.binary.c_str(), L"ab");
    CloseHandle(locked);
    check(error != 0 && denied == nullptr && AppendFiles::read(files.binary) == appended,
        "exclusive append failure clears output without changing existing bytes");
    {
        auto file = AppendFiles::open(files.binary, L"ab");
        check(std::fputs("!", file.get()) >= 0, "append retries after conflicting handle closes");
    }
    check(AppendFiles::read(files.binary) == appended + "!", "append retry neither truncates nor duplicates prior bytes");
    check(DeleteFileW(files.created.c_str()) != 0, "remove only owned empty fixture to test append creation");
    {
        auto file = AppendFiles::open(files.created, L"ab");
        check(std::fputs("new", file.get()) >= 0, "append creates an absent file");
    }
    check(AppendFiles::read(files.created) == "new", "new append file has expected contents");
    check(DeleteFileW(files.created.c_str()) != 0, "remove only owned fixture for exclusive creation");
    check(wfopen_exclusive_ensuring_access(&exclusive, files.created.c_str(), L"wb") == 0 && exclusive,
        "exclusive creation creates a new secure stream");
    {
        File file(exclusive, &std::fclose);
        check(std::fputs("exclusive", file.get()) >= 0, "write exclusive stream");
    }
    check(AppendFiles::read(files.created) == "exclusive", "exclusive stream has expected bytes");
    for (const wchar_t* invalid : {L"ab", L"w+", L"aw", L"rw", L"wx"}) {
        exclusive = stdin;
        check(wfopen_exclusive_ensuring_access(&exclusive, files.created.c_str(), invalid) == EINVAL && !exclusive &&
            AppendFiles::read(files.created) == "exclusive", "invalid exclusive mode is rejected before changing existing content");
    }
    check(DeleteFileW(files.created.c_str()) != 0, "remove only owned fixture for invalid-mode creation test");
    check(wfopen_exclusive_ensuring_access(&exclusive, files.created.c_str(), L"w+") == EINVAL && !exclusive &&
        !std::filesystem::exists(files.created), "invalid exclusive mode cannot create a new file");
    for (const wchar_t* mode : {L"w", L"a"}) {
        auto file = AppendFiles::open(files.text, mode);
        check(std::fputs("line\n", file.get()) >= 0, "text write and append accept newline");
    }
    check(AppendFiles::read(files.text) == "line\r\nline\r\n", "text append retains CRT newline translation");
    {
        auto file = AppendFiles::open(files.binary, L"wb");
        check(std::fputs("replacement", file.get()) >= 0, "write mode still truncates");
    }
    check(AppendFiles::read(files.binary) == "replacement", "write behavior remains compatible");
}
}
void test_dx11_file_append_regression()
{
    try {
        run_append_tests();
        std::fputs("PASS: real secure exclusive append/write, binary/text and sharing-failure retry\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL file append: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
