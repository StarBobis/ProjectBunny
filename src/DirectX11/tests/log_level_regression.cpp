#include "Logging.h"
#include "IniHandler.h"
#include "globals.h"
#include "HackerDevice.h"
#include "HackerContext.h"
#include "D3D11Wrapper.h"
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string contents(FILE* file)
{
    std::fflush(file);
    check(std::fseek(file, 0, SEEK_SET) == 0, "rewind real log stream");
    std::string result; char buffer[1024]; size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) result.append(buffer, count);
    check(std::fseek(file, 0, SEEK_END) == 0, "restore append position");
    return result;
}
bool contains_log_text(const std::string& bytes, const std::string& text)
{
    if (bytes.find(text) != std::string::npos) return true;
    // UCRT wide stdio can write UTF-16LE while narrow calls write bytes on
    // this real binary tmpfile. Check the complete marker in either encoding.
    std::string wide;
    for (char character : text) { wide.push_back(character); wide.push_back('\0'); }
    return bytes.find(wide) != std::string::npos;
}
void va_narrow(const char* fmt, ...)
{
    va_list args; va_start(args, fmt); vLogWarning(fmt, args); va_end(args);
}
void va_wide(const wchar_t* fmt, ...)
{
    va_list args; va_start(args, fmt); vLogWarningW(fmt, args); va_end(args);
}
class LoggingFixture {
public:
    FILE* previous_log = LogFile;
    LogVerbosity previous_level = gLogVerbosity;
    bool previous_debug = gLogDebug, previous_warnings = G->gShowWarnings;
    HINSTANCE previous_module = migoto_handle;
    std::wstring previous_user = G->user_config;
    int previous_dirty = G->user_config_dirty;
    bool previous_initialized = G->gConfigInitialized;
    bool previous_auto = G->clear_unknown_settings, previous_clear = G->clear_saved_persist_vars;
    std::filesystem::path config, settings;
    bool owns_config = false;
    unsigned write_count = 0;
    std::string syntax_marker;
    LoggingFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate logging fixture executable");
        auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : directory) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "logging fixture belongs inside build");
        config = directory / L"d3dx.ini";
        settings = directory / (L"log_level_settings_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing logging settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing logging config fixture");
        CloseHandle(file); owns_config = true;
        LogFile = nullptr; migoto_handle = nullptr; G->gShowWarnings = false;
    }
    ~LoggingFixture()
    {
        if (LogFile && LogFile != previous_log) std::fclose(LogFile);
        LogFile = previous_log; gLogVerbosity = previous_level; gLogDebug = previous_debug;
        G->gShowWarnings = previous_warnings; migoto_handle = previous_module;
        G->constants_command_list.clear(); G->post_constants_command_list.clear();
        explicitCommandListSections.clear(); customResourcePools.clear(); customResources.clear();
        persistent_variables.clear(); command_list_globals.clear();
        G->user_config = previous_user; G->user_config_dirty = previous_dirty;
        G->gConfigInitialized = previous_initialized;
        G->clear_unknown_settings = previous_auto; G->clear_saved_persist_vars = previous_clear;
        if (owns_config) { DeleteFileW(config.c_str()); DeleteFileW(settings.c_str()); }
    }
    void capture()
    {
        if (LogFile) std::fclose(LogFile);
        LogFile = std::tmpfile(); check(LogFile != nullptr, "open real log capture");
    }
    void write(const char* options, const char* show = "0", bool syntax_warning = false, bool resource_warning = true)
    {
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
        std::ofstream file(config, std::ios::binary | std::ios::trunc);
        file << "[Logging]\nshow_warnings=" << show << "\n" << options <<
            "\n[System]\nskip_early_includes_load=0\n[Hunting]\nhunting=0\n[Include]\nuser_config=" << settings.filename().string() <<
            "\n[ResourceLogLevelWarning]\ncolor_space=" << (resource_warning ? "invalid_log_level_fixture" : "linear") << "\n";
        syntax_marker = "LOG_SYNTAX_WARNING_WITHOUT_EQUALS_" + std::to_string(++write_count);
        if (syntax_warning) file << syntax_marker << "\n";
        file.close(); check(file.good(), "write owned logging config");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    }
};
class LoggingDevice {
public:
    HMODULE runtime = nullptr;
    ID3D11Device1* native = nullptr;
    ID3D11DeviceContext1* context = nullptr;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;
    LoggingDevice()
    {
        wchar_t system[MAX_PATH] = {};
        check(GetSystemDirectoryW(system, MAX_PATH) > 0, "locate WARP runtime");
        runtime = LoadLibraryW((std::filesystem::path(system) / L"d3d11.dll").c_str());
        check(runtime != nullptr, "load logging WARP runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve logging WARP entry");
        ID3D11Device* base = nullptr; ID3D11DeviceContext* immediate = nullptr;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &base, nullptr, &immediate)), "create actual logging reload WARP device");
        const HRESULT d = base->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&native));
        const HRESULT c = immediate->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&context));
        immediate->Release(); base->Release(); check(SUCCEEDED(d) && SUCCEEDED(c), "query logging DX11 interfaces");
        device = new HackerDevice(native, context);
        wrapper = std::make_unique<HackerContext>(native, context);
        wrapper->SetHackerDevice(device); device->SetHackerContext(wrapper.get());
    }
    ~LoggingDevice()
    {
        wrapper.reset(); if (context) context->Release();
        if (device) device->Release(); else if (native) native->Release();
        if (runtime) FreeLibrary(runtime);
    }
};
void verify_level(LogVerbosity expected)
{
    check(gLogVerbosity == expected && gLogDebug == (expected == LogVerbosity::DEBUG), "actual loaded level and debug mirror");
    LogWarning("LEVEL_WARNING %d\n", 11);
    LogInfo("LEVEL_INFO %d\n", 22);
    LogDebug("LEVEL_DEBUG %d\n", 33);
    LogOverlay(LOG_WARNING, "LEVEL_OVERLAY %d %s\n", 44, "hidden");
    auto output = contents(LogFile);
    check(contains_log_text(output, "D3D11 DLL starting init") == (expected >= LogVerbosity::WARNING) &&
        contains_log_text(output, "Game path:") == (expected >= LogVerbosity::WARNING) &&
        contains_log_text(output, "3DMigoto path:") == (expected >= LogVerbosity::WARNING),
        "startup banner and paths are visible at warning level");
    if ((contains_log_text(output, "invalid_log_level_fixture")) != (expected >= LogVerbosity::WARNING))
        std::fprintf(stderr, "Log diagnostic level=%d size=%zu capture=%.2048s\n", static_cast<int>(expected), output.size(), output.c_str());
    check((contains_log_text(output, "invalid_log_level_fixture")) == (expected >= LogVerbosity::WARNING),
        "actual INI warning reaches file with show_warnings disabled");
    check((contains_log_text(output, "LEVEL_WARNING 11")) == (expected >= LogVerbosity::WARNING), "warning threshold actual bytes");
    check((contains_log_text(output, "LEVEL_INFO 22")) == (expected >= LogVerbosity::INFO), "info threshold actual bytes");
    check((contains_log_text(output, "LEVEL_DEBUG 33")) == (expected >= LogVerbosity::DEBUG), "debug threshold actual bytes");
    check((contains_log_text(output, "LEVEL_OVERLAY 44 hidden")) == (expected >= LogVerbosity::WARNING), "hidden overlay warning still writes actual formatted bytes");
}
void run_logging_regression()
{
    LoggingFixture fixture;
    fixture.capture(); fixture.write("log_level=warning\ncalls=1\ndebug=1", "false", true);
    G->gShowWarnings = true;
    const auto initial_notices = GetOverlayNoticeMessages(LOG_WARNING).size();
    LoadConfigFile(); verify_level(LogVerbosity::WARNING);
    check(!G->gShowWarnings && GetOverlayNoticeMessages(LOG_WARNING).size() == initial_notices &&
        contains_log_text(contents(LogFile), "LOG_SYNTAX_WARNING_WITHOUT_EQUALS"),
        "first load reads new false before syntax warning while preserving warning file output");
    LoggingDevice device;
    const char* options[] = {"log_level=DeBuG", "log_level=info", "log_level=disabled\ncalls=1\ndebug=1",
        "log_level=invalid\ncalls=1", "log_level=invalid\ncalls=1\ndebug=1", "log_level=invalid\ncalls=0\ndebug=1",
        "log_level=\ncalls=1\ndebug=1", "calls=1\ndebug=1", "calls=1\ndebug=0",
        "calls=0\ndebug=1", ""};
    const LogVerbosity levels[] = {LogVerbosity::DEBUG, LogVerbosity::INFO, LogVerbosity::DISABLED,
        LogVerbosity::INFO, LogVerbosity::DEBUG, LogVerbosity::DISABLED, LogVerbosity::DEBUG,
        LogVerbosity::DEBUG, LogVerbosity::INFO, LogVerbosity::DISABLED, LogVerbosity::DISABLED};
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        fixture.capture(); fixture.write(options[i]); ReloadConfig(device.device); verify_level(levels[i]);
    }
    // Enabled configurations are clean to avoid parser warning tones. Hidden
    // configurations retain real syntax warnings to exercise the early policy.
    for (const char* show : {"true", "(1 - 1)", "(2 > 1)", "off"}) {
        const bool expected = std::string(show) == "true" || std::string(show) == "(2 > 1)";
        fixture.capture(); fixture.write("log_level=warning", show, !expected, !expected);
        G->gShowWarnings = !expected;
        ReloadConfig(device.device);
        check(G->gShowWarnings == expected, "main reload honors same-source bool/expression warning policy");
        const auto before = GetOverlayNoticeMessages(LOG_WARNING).size();
        LogOverlayW(LOG_WARNING, L"LOG_POLICY_MESSAGE %ls\n", expected ? L"visible" : L"hidden");
        const auto after = GetOverlayNoticeMessages(LOG_WARNING);
        check(after.size() == before + (expected ? 1 : 0), "actual overlay warning follows both warning-policy directions");
        check(contains_log_text(contents(LogFile), expected ? "LOG_POLICY_MESSAGE visible" : "LOG_POLICY_MESSAGE hidden"),
            "warning file output remains independent of queue policy");
        if (expected) {
            check(after.back().find(L"LOG_POLICY_MESSAGE visible") != std::wstring::npos,
                "actual queued warning preserves formatted text");
        } else {
            const std::wstring syntax(fixture.syntax_marker.begin(), fixture.syntax_marker.end());
            bool queued = false;
            for (const auto& message : after) queued |= message.find(syntax) != std::wstring::npos;
            check(!queued && contains_log_text(contents(LogFile), fixture.syntax_marker),
                "main parser hidden syntax warning reaches file without entering queue");
        }
    }
    fixture.capture(); fixture.write("log_level=warning", "false", true);
    G->gShowWarnings = true;
    ReloadConfig(device.device);
    const auto hidden = GetOverlayNoticeMessages(LOG_WARNING);
    bool hidden_syntax = false;
    const std::wstring current_syntax(fixture.syntax_marker.begin(), fixture.syntax_marker.end());
    for (const auto& message : hidden)
        hidden_syntax |= message.find(current_syntax) != std::wstring::npos;
    check(!G->gShowWarnings && !hidden_syntax &&
        contains_log_text(contents(LogFile), "LOG_SYNTAX_WARNING_WITHOUT_EQUALS"),
        "reload replaces stale true before syntax warning without suppressing file output");
    fixture.capture(); gLogVerbosity = LogVerbosity::WARNING;
    va_narrow("VA_NARROW %d %s\n", 71, "ok");
    check(contains_log_text(contents(LogFile), "VA_NARROW 71 ok"), "narrow va_list logging");
    fixture.capture();
    va_wide(L"VA_WIDE %d %ls\n", 72, L"ok");
    LogOverlayW(LOG_WARNING, L"OVERLAY_WIDE %d %ls\n", 73, L"hidden");
    auto output = contents(LogFile);
    check(contains_log_text(output, "VA_WIDE 72 ok") && contains_log_text(output, "OVERLAY_WIDE 73 hidden"),
        "wide va_list and hidden overlay preserve actual format arguments");
    // Exercise the real detach hook body without loading/unloading a DLL or
    // entering the OS loader lock. Safe-point persistence is tested separately.
    const int saved_dirty = G->user_config_dirty;
    G->user_config_dirty = 1;
    DestroyDLL();
    LogWarning("AFTER_DETACH_HOOK\n");
    check(G->user_config_dirty == 1 && contains_log_text(contents(LogFile), "AFTER_DETACH_HOOK"),
        "detach hook neither saves dirty settings nor closes the active stream");
    G->user_config_dirty = saved_dirty;
    std::fclose(LogFile); LogFile = nullptr; gLogVerbosity = LogVerbosity::DEBUG; gLogDebug = true;
    LogWarning("null"); LogInfo("null"); LogDebug("null");
    LogWarningW(L"null"); LogInfoW(L"null"); LogDebugW(L"null");
    va_narrow("null %d", 1); va_wide(L"null %d", 2);
    LogOverlay(LOG_WARNING, "null %d", 3); LogOverlayW(LOG_WARNING, L"null %d", 4);
    fixture.write("log_level=debug");
    const auto missing_parent = fixture.config.parent_path() / L"missing_log_level_directory" / L"log.txt";
    check(!std::filesystem::exists(missing_parent.parent_path()), "missing log parent must be absent");
    InitializeLogging(fixture.config.c_str(), missing_parent.c_str());
    check(!LogFile && gLogVerbosity == LogVerbosity::DISABLED && !gLogDebug, "failed file open disables all gates safely");
}
}
void test_dx11_log_level_regression()
{
    try { run_logging_regression(); std::fputs("PASS: log levels, real FILE/va_list, hidden overlay and actual INI load/reload\n", stderr); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL log levels: %s\n", error.what()); std::exit(EXIT_FAILURE); }
}
