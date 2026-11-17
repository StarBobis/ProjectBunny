// F-003b: the shared VM persist writer must propagate stdio failures without
// clearing the dirty flag. This runs the real MigotoCommandVmUtils::
// SavePersistedVariables; only the stdio entry points used by the writer are
// intercepted. Write/flush faults redirect the stream onto a real read-only
// descriptor; the close fault injects EOF after releasing the real stream.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Pre-include every dependency of the header under test, so the
// fault-injection macros below can only rewrite the calls inside
// SavePersistedVariables and cannot corrupt a standard library declaration.
#include <math.h>
#include <share.h>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include "MigotoIniCommandListUtils.hpp"

namespace {

enum class SaveFault {
    None,
    Header,
    Variable,
    Flush,
    Close
};

SaveFault g_fault = SaveFault::None;
bool g_faultConsumed = false;

const auto real_fputs = &fputs;
const auto real_vfwprintf_s = &vfwprintf_s;
const auto real_fflush = &fflush;
const auto real_fclose = &fclose;

void break_stream(FILE *file)
{
    const int readOnly = _open("NUL", _O_RDONLY | _O_BINARY);
    if (readOnly < 0 || _dup2(readOnly, _fileno(file)) != 0)
        std::abort();
    _close(readOnly);
}

int audit_fputs(const char *text, FILE *file)
{
    if (!g_faultConsumed &&
        (g_fault == SaveFault::Header || g_fault == SaveFault::Variable)) {
        // The reported write must reach the descriptor immediately instead of
        // sitting in the stdio buffer until the later flush/close check.
        if (setvbuf(file, nullptr, _IONBF, 0) != 0)
            std::abort();
        if (g_fault == SaveFault::Header) {
            g_faultConsumed = true;
            break_stream(file);
        }
    }
    return real_fputs(text, file);
}

int audit_fwprintf_s(FILE *file, const wchar_t *format, ...)
{
    if (!g_faultConsumed && g_fault == SaveFault::Variable) {
        g_faultConsumed = true;
        break_stream(file);
    }
    va_list args;
    va_start(args, format);
    const int result = real_vfwprintf_s(file, format, args);
    va_end(args);
    return result;
}

int audit_fflush(FILE *file)
{
    if (!g_faultConsumed && g_fault == SaveFault::Flush) {
        g_faultConsumed = true;
        break_stream(file);
    }
    return real_fflush(file);
}

int audit_fclose(FILE *file)
{
    if (!g_faultConsumed && g_fault == SaveFault::Close) {
        // A successful fflush (which the writer performs first) leaves nothing
        // for fclose to flush, and closing the broken descriptor would still
        // succeed. Release the real stream, then report the close failure the
        // writer has to honor. This is an injected return, not an OS failure.
        g_faultConsumed = true;
        real_fclose(file);
        return EOF;
    }
    return real_fclose(file);
}

} // namespace

// No other DX11 test translation unit includes this header, so the rewritten
// inline definition below is this executable's only definition of the writer.
#define fputs audit_fputs
#define fwprintf_s audit_fwprintf_s
#define fflush audit_fflush
#define fclose audit_fclose
#include "MigotoCommandVmUtils.hpp"
#undef fputs
#undef fwprintf_s
#undef fflush
#undef fclose

namespace {

using PersistedVariable = MigotoCommandVmUtils::PersistedVariable;
using VariableStore = MigotoCommandVmUtils::VariableStore;

// One recorded call of the production failure sink.
struct FailureLog {
    int count = 0;
    std::string stage;
    std::wstring path;
    int error = 0;
};

FailureLog g_failure;

void capture_failure(const char *stage, const wchar_t *path, int error)
{
    ++g_failure.count;
    g_failure.stage = stage ? stage : "";
    g_failure.path = path ? path : L"";
    g_failure.error = error;
}

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void install_store(std::initializer_list<std::pair<const wchar_t *, double>> values)
{
    std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
    VariableStore::globals.clear();
    VariableStore::persistedVariables.clear();
    for (const auto &value : values) {
        PersistedVariable declaration;
        declaration.name = value.first;
        declaration.key = value.first;
        VariableStore::persistedVariables.push_back(declaration);
        VariableStore::globals[value.first] = value.second;
    }
    VariableStore::persistDirty.store(1, std::memory_order_release);
}

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "F003b: read the written persist file");
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

// The text-mode writer translates every '\n' to CRLF, and %.9g renders these
// three values identically in the C numeric locale used by the test.
const char *const kExpectedSuccess =
    "; AUTOMATICALLY GENERATED FILE - DO NOT EDIT\r\n"
    ";\r\n"
    "; 3DMigoto will overwrite this file whenever any persistent settings are\r\n"
    "; altered by hot key or command list. Tag global variables with the \"persist\"\r\n"
    "; keyword to save them in this file. Use the post keyword in the [Constants]\r\n"
    "; command list if you need to do any intialisation after this file is loaded.\r\n"
    ";\r\n"
    "[Constants]\r\n"
    "$alpha = 1\r\n"
    "$beta = -2.5\r\n"
    "$gamma = 0.125\r\n";

class NumericLocaleScope
{
    std::string saved;
public:
    NumericLocaleScope()
    {
        const char *current = std::setlocale(LC_NUMERIC, nullptr);
        saved = current ? current : "C";
        if (!std::setlocale(LC_NUMERIC, "C"))
            throw std::runtime_error("F003b: establish C numeric formatting");
    }
    ~NumericLocaleScope()
    {
        std::setlocale(LC_NUMERIC, saved.c_str());
    }
};

class SinkScope
{
    MigotoCommandVmUtils::PersistFailureSink saved;
public:
    SinkScope() : saved(MigotoCommandVmUtils::persistFailureSink)
    {
        MigotoCommandVmUtils::SetPersistFailureSink(&capture_failure);
    }
    ~SinkScope()
    {
        MigotoCommandVmUtils::SetPersistFailureSink(saved);
    }
};

class StoreScope
{
public:
    ~StoreScope()
    {
        std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
        VariableStore::globals.clear();
        VariableStore::persistedVariables.clear();
        VariableStore::persistDirty.store(0, std::memory_order_release);
    }
};

class PersistAuditFixture
{
public:
    std::filesystem::path root;
    std::filesystem::path targetPath;
    MigotoCommandVmUtils::PersistTarget persistTarget;

    PersistAuditFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        require(length != 0 && length < MAX_PATH, "F003b: locate the test host");
        const auto base = std::filesystem::path(executable).parent_path();
        bool buildDirectory = false;
        for (const auto &part : base)
            buildDirectory |= _wcsicmp(part.c_str(), L"build") == 0;
        require(buildDirectory, "F003b: persist fixtures must stay under build");
        root = base / (L"persist-write-audit-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()));
        require(CreateDirectoryW(root.c_str(), nullptr) != 0,
            "F003b: create an exclusive fixture directory");
        targetPath = root / L"d3dx_user.ini";
        persistTarget = MigotoCommandVmUtils::MakePersistTarget(targetPath.wstring());
    }

    ~PersistAuditFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

void expect_success(PersistAuditFixture &fixture)
{
    g_fault = SaveFault::None;
    g_faultConsumed = false;
    install_store({{L"$alpha", 1.0}, {L"$beta", -2.5}, {L"$gamma", 0.125}});
    std::error_code ignored;
    std::filesystem::remove(fixture.targetPath, ignored);
    g_failure = FailureLog{};
    require(MigotoCommandVmUtils::SavePersistedVariables(fixture.persistTarget),
        "F003b: legal save must succeed");
    require(!MigotoCommandVmUtils::PersistDirty(),
        "F003b: successful save clears persistDirty");
    require(g_failure.count == 0,
        "F003b: successful save must not log a failure");
    require(read_file(fixture.targetPath) == kExpectedSuccess,
        "F003b: legal success path bytes changed");
}

void expect_fopen_failure(PersistAuditFixture &fixture)
{
    g_fault = SaveFault::None;
    g_faultConsumed = false;
    install_store({{L"$alpha", 1.0}});
    g_failure = FailureLog{};
    const auto missing = MigotoCommandVmUtils::MakePersistTarget(
        (fixture.root / L"missing" / L"d3dx_user.ini").wstring());
    require(!MigotoCommandVmUtils::SavePersistedVariables(missing),
        "F003b: missing parent directory must return false");
    require(MigotoCommandVmUtils::PersistDirty(),
        "F003b: fopen failure must leave persistDirty armed");
    require(g_failure.count == 1 && g_failure.stage == "fopen",
        "F003b: fopen failure must log one fopen stage");
}

void expect_fault(PersistAuditFixture &fixture, SaveFault fault, const char *stage)
{
    install_store({{L"$alpha", 1.0}, {L"$beta", -2.5}});
    std::error_code ignored;
    std::filesystem::remove(fixture.targetPath, ignored);
    g_fault = fault;
    g_faultConsumed = false;
    g_failure = FailureLog{};
    const bool saved = MigotoCommandVmUtils::SavePersistedVariables(fixture.persistTarget);
    g_fault = SaveFault::None;
    require(!saved, "F003b: injected stdio failure must return false");
    require(MigotoCommandVmUtils::PersistDirty(),
        "F003b: injected stdio failure must leave persistDirty armed");
    require(g_failure.count == 1,
        "F003b: injected stdio failure must log exactly one line");
    require(g_failure.stage == stage,
        std::string("F003b: expected failure stage ") + stage +
            ", got " + g_failure.stage);
    require(g_failure.path == fixture.targetPath.wstring(),
        "F003b: failure log must name the persist file");
}

void expect_retry_after_failure(PersistAuditFixture &fixture)
{
    install_store({{L"$alpha", 1.0}, {L"$beta", -2.5}, {L"$gamma", 0.125}});
    std::error_code ignored;
    std::filesystem::remove(fixture.targetPath, ignored);
    g_fault = SaveFault::Header;
    g_faultConsumed = false;
    g_failure = FailureLog{};
    require(!MigotoCommandVmUtils::SavePersistedVariables(fixture.persistTarget),
        "F003b: first attempt of the retry test must fail");
    g_fault = SaveFault::None;
    g_faultConsumed = false;
    g_failure = FailureLog{};
    require(MigotoCommandVmUtils::SavePersistedVariables(fixture.persistTarget),
        "F003b: retry after a failed save must succeed");
    require(!MigotoCommandVmUtils::PersistDirty(),
        "F003b: successful retry clears persistDirty");
    require(read_file(fixture.targetPath) == kExpectedSuccess,
        "F003b: successful retry writes the unchanged legal bytes");
}

} // namespace

void test_persist_write_failure_regression()
{
    try {
        NumericLocaleScope locale;
        SinkScope sink;
        PersistAuditFixture fixture;
        StoreScope store;

        expect_success(fixture);
        expect_fopen_failure(fixture);
        expect_fault(fixture, SaveFault::Header, "fputs");
        expect_fault(fixture, SaveFault::Variable, "fwprintf_s");
        expect_fault(fixture, SaveFault::Flush, "fflush");
        expect_fault(fixture, SaveFault::Close, "fclose");
        expect_retry_after_failure(fixture);

        std::fputs("PASS: F-003b persist write failures keep dirty, report the stage and retry\n",
            stderr);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: F-003b persist write failure: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
