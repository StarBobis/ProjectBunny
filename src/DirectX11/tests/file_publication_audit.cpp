#include "slot_range_test_support.h"
#include "util.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

extern "C" decltype(&WriteFile) __imp_WriteFile;
extern "C" decltype(&FlushFileBuffers) __imp_FlushFileBuffers;
extern "C" decltype(&CloseHandle) __imp_CloseHandle;

namespace {
using dx11_slot_test::require_batch;
const auto native_write = __imp_WriteFile;
const auto native_flush = __imp_FlushFileBuffers;
const auto native_close = __imp_CloseHandle;
enum class FileFault { Write, ShortWrite, Flush, Close };
struct FileFaultState {
    FileFault kind;
    HANDLE file = nullptr;
    bool consumed = false;
};
thread_local FileFaultState* file_fault = nullptr;

BOOL WINAPI audit_write(HANDLE file, LPCVOID data, DWORD count, LPDWORD written, LPOVERLAPPED overlapped)
{
    auto* fault = file_fault;
    if (!fault) return native_write(file, data, count, written, overlapped);
    if (!fault->file) fault->file = file;
    if (file == fault->file && !fault->consumed) {
        if (fault->kind == FileFault::Write) {
            fault->consumed = true; if (written) *written = 0;
            SetLastError(ERROR_DISK_FULL); return FALSE;
        }
        if (fault->kind == FileFault::ShortWrite) {
            fault->consumed = true;
            return native_write(file, data, count ? count - 1 : 0, written, overlapped);
        }
    }
    return native_write(file, data, count, written, overlapped);
}
BOOL WINAPI audit_flush(HANDLE file)
{
    if (file_fault && file_fault->file == file && !file_fault->consumed && file_fault->kind == FileFault::Flush) {
        file_fault->consumed = true; SetLastError(ERROR_DISK_FULL); return FALSE;
    }
    return native_flush(file);
}
BOOL WINAPI audit_close(HANDLE file)
{
    if (file_fault && file_fault->file == file && !file_fault->consumed && file_fault->kind == FileFault::Close) {
        require_batch(native_close(file) != FALSE, "F003: injected close failure still releases the real owned handle");
        file_fault->consumed = true; SetLastError(ERROR_WRITE_FAULT); return FALSE;
    }
    return native_close(file);
}

template <typename Function>
class FileImportPatch {
    Function* slot;
    Function saved;
    void set(Function value)
    {
        DWORD protection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
            throw std::runtime_error("F003: change this regression host's import slot");
        *slot = value;
        DWORD ignored = 0;
        if (!VirtualProtect(slot, sizeof(*slot), protection, &ignored)) std::abort();
    }
public:
    FileImportPatch(Function* location, Function replacement) : slot(location), saved(*location) { set(replacement); }
    ~FileImportPatch() { try { set(saved); } catch (...) { std::abort(); } }
};
class FileFaultScope {
    FileImportPatch<decltype(&WriteFile)> write{&__imp_WriteFile, audit_write};
    FileImportPatch<decltype(&FlushFileBuffers)> flush{&__imp_FlushFileBuffers, audit_flush};
    FileImportPatch<decltype(&CloseHandle)> close{&__imp_CloseHandle, audit_close};
public:
    explicit FileFaultScope(FileFaultState& state) { require_batch(!file_fault, "F003: no nested file fault"); file_fault = &state; }
    ~FileFaultScope() { file_fault = nullptr; }
};

class PublicationFiles {
    std::filesystem::path base;
    FILE* saved_log = LogFile;
public:
    std::filesystem::path root;
    PublicationFiles()
    {
        wchar_t executable[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        require_batch(length && length < MAX_PATH, "F003: locate test host");
        base = std::filesystem::canonical(std::filesystem::path(executable).parent_path());
        bool build = false;
        for (const auto& part : base) build |= !_wcsicmp(part.c_str(), L"build");
        require_batch(build, "F003: file fixture belongs under build");
        root = base / L"file-publication-audit";
        require_batch(std::filesystem::create_directory(root), "F003: exclusive file fixture");
        LogFile = nullptr;
    }
    ~PublicationFiles()
    {
        LogFile = saved_log;
        std::error_code error;
        const auto resolved = std::filesystem::canonical(root, error);
        if (error || resolved != root || resolved.parent_path() != base) std::abort();
        std::filesystem::remove_all(root, error);
        if (error) std::abort();
    }
    void no_staging_files() const
    {
        for (const auto& file : std::filesystem::directory_iterator(root))
            require_batch(file.path().extension() != L".tmp", "F003: failed/successful publication leaves no staging files");
    }
};
std::string read_publication(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require_batch(input.good(), "F003: read actual published file");
    return std::string(std::istreambuf_iterator<char>(input), {});
}
HRESULT publish(const std::filesystem::path& path, std::string& data, bool overwrite)
{
    auto name = path.wstring();
    return CreateTextFile(&name[0], &data, overwrite);
}
}

void test_dx11_file_publication_regression()
{
    PublicationFiles files;
    std::string original = "keep original file";
    std::fputs("F003: a missing parent directory must not report successful export\n", stderr); std::fflush(stderr);
    require_batch(FAILED(publish(files.root / L"missing" / L"shader.txt", original, false)), "F003: open failure returns a failing HRESULT");
    const auto target = files.root / L"\u8f93\u51fa.txt";
    require_batch(SUCCEEDED(publish(target, original, false)) && read_publication(target) == original, "F003: publish exact original bytes");
    std::string replacement(2 * 1024 * 1024 + 19, 'a'); replacement[7] = '\0'; replacement.back() = '\n';
    require_batch(FAILED(publish(target, replacement, false)) && read_publication(target) == original,
        "F003: existing files produce a failing HRESULT and cannot be overwritten without permission");
    for (const auto kind : {FileFault::Write, FileFault::ShortWrite, FileFault::Flush, FileFault::Close}) {
        FileFaultState state{kind}; HRESULT result;
        {
            FileFaultScope fault(state);
            result = publish(target, replacement, true);
        }
        require_batch(state.consumed && FAILED(result) && read_publication(target) == original,
            "F003: observed write/short-write/flush/close failures preserve the old complete file");
        files.no_staging_files();
    }
    HANDLE reader = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require_batch(reader != INVALID_HANDLE_VALUE, "F003: lock destination against replacement");
    const auto blocked = publish(target, replacement, true);
    CloseHandle(reader);
    require_batch(FAILED(blocked) && read_publication(target) == original, "F003: commit failure preserves the old complete file");
    files.no_staging_files();
    require_batch(SUCCEEDED(publish(target, replacement, true)) && read_publication(target) == replacement,
        "F003: retry atomically replaces all bytes including embedded NUL and multiple chunks");
    std::string empty;
    require_batch(SUCCEEDED(publish(files.root / L"empty.txt", empty, false)) && read_publication(files.root / L"empty.txt").empty(),
        "F003: empty text publishes an empty file");

    constexpr size_t contenders = 8;
    std::array<HRESULT, contenders> results{};
    std::array<std::string, contenders> data;
    std::vector<std::thread> threads; threads.reserve(contenders);
    const auto race = files.root / L"race.txt";
    try {
        for (size_t i = 0; i < contenders; ++i) {
            data[i] = "owned contender " + std::to_string(i);
            threads.emplace_back([&, i] { try { results[i] = publish(race, data[i], false); } catch (...) { results[i] = E_FAIL; } });
        }
    } catch (...) { for (auto& thread : threads) thread.join(); throw; }
    for (auto& thread : threads) thread.join();
    unsigned winners = 0;
    for (size_t i = 0; i < contenders; ++i) {
        if (SUCCEEDED(results[i])) { ++winners; require_batch(read_publication(race) == data[i], "F003: final bytes belong to the successful owner"); }
    }
    require_batch(winners == 1, "F003: concurrent no-overwrite publication has exactly one successful owner");
    files.no_staging_files();
}
