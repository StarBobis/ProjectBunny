#include "Logging.h"
#include "CommandList.h"
#include "IniHandler.h"
#include "globals.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void require_enum(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class EnumOptionFixture {
public:
    std::filesystem::path config;
    std::filesystem::path settings;
    HINSTANCE saved_module = migoto_handle;
    FILE* saved_log = LogFile;
    LogVerbosity saved_verbosity = gLogVerbosity;
    bool saved_debug = gLogDebug;
    FILE* capture = nullptr;
    bool owns_config = false;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    bool saved_warnings = G->gShowWarnings;

    EnumOptionFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        require_enum(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate enum test executable");
        const auto directory = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& component : directory)
            in_build |= _wcsicmp(component.c_str(), L"build") == 0;
        require_enum(in_build, "enum test executable must reside in build/");
        config = directory / L"d3dx.ini";
        settings = directory / (L"enum-regression_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        require_enum(!std::filesystem::exists(settings), "enum settings path must be unused");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        require_enum(file != INVALID_HANDLE_VALUE, "enum fixture never overwrites an existing configuration");
        CloseHandle(file);
        owns_config = true;
        migoto_handle = nullptr;
        capture = tmpfile();
        if (!capture) {
            DeleteFileW(config.c_str());
            owns_config = false;
            migoto_handle = saved_module;
            throw std::runtime_error("create diagnostic capture");
        }
        LogFile = capture;
    }

    ~EnumOptionFixture()
    {
        LogFile = saved_log;
        gLogVerbosity = saved_verbosity;
        gLogDebug = saved_debug;
        if (capture)
            fclose(capture);
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        G->gShowWarnings = saved_warnings;
        registered_command_lists.clear();
        G->mShaderOverrideMap.clear();
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        if (owns_config)
            DeleteFileW(config.c_str());
    }

    void write(bool invalid)
    {
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << "[Logging]\nlog_level=debug\nshow_warnings=" << (invalid ? 0 : 1) <<
            "\n[System]\nskip_early_includes_load=0\n[Hunting]\nhunting=0\n[Include]\nuser_config=" <<
            settings.filename().string() << "\n";
        output << "[Rendering]\ntrack_texture_updates=2\nrecursive_include=1+1\n";
        const char* values[] = {"overrule", "true", "false", "0", "1", "2", "1+1", "YES", "off"};
        for (unsigned i = 0; i < 9; ++i)
            output << "[ShaderOverrideEnumRegression" << i << "]\nhash=" << (1000 + i) <<
                "\nallow_duplicate_hash=" << values[i] << "\n";
        if (invalid) {
            const char* bad[] = {"5", "-1", "missing_enum", "1+"};
            for (unsigned i = 0; i < 4; ++i)
                output << "[ShaderOverrideEnumInvalid" << i << "]\nhash=" << (2000 + i) <<
                    "\nallow_duplicate_hash=" << bad[i] << "\n";
        }
        output.close();
        require_enum(output.good(), "write enum fixture");
    }

    std::string diagnostics()
    {
        fflush(capture);
        require_enum(fseek(capture, 0, SEEK_END) == 0, "seek diagnostic end");
        const long size = ftell(capture);
        require_enum(size >= 0 && fseek(capture, 0, SEEK_SET) == 0, "seek diagnostic beginning");
        std::string result(static_cast<size_t>(size), '\0');
        require_enum(fread(result.data(), 1, result.size(), capture) == result.size(), "read diagnostics");
        return result;
    }
};
}

void test_dx11_enum_option_regression()
{
    try {
        EnumOptionFixture fixture;
        fixture.write(false);
        LoadConfigFile();
        require_enum(G->track_texture_updates == 2 && G->recursive_include == 2,
            "integer rendering options keep numeric modes instead of boolean coercion");
        const int expected[] = {2, 1, 0, 0, 1, 2, 2, 1, 0};
        for (unsigned i = 0; i < 9; ++i) {
            // INI hash text is hexadecimal, even though these digits look decimal.
            const uint64_t hash = std::stoull(std::to_string(1000 + i), nullptr, 16);
            require_enum(G->mShaderOverrideMap.at(hash).allow_duplicate_hashes == expected[i],
                "real shader override accepts enum/boolean/integer/expression without coercing 2 to true");
        }
        LogInfo("enum-regression-capture-active\n");
        const auto diagnostics = fixture.diagnostics();
        require_enum(diagnostics.find("enum-regression-capture-active") != std::string::npos,
            "diagnostic capture is active under the selected verbosity");
        require_enum(diagnostics.find("Syntax Error") == std::string::npos &&
            diagnostics.find("Unrecognised Enum") == std::string::npos,
            "accepted enum alternatives do not emit failed-probe diagnostics with warnings enabled");
        fixture.write(true);
        LoadConfigFile();
        for (unsigned i = 0; i < 4; ++i) {
            const uint64_t hash = std::stoull(std::to_string(2000 + i), nullptr, 16);
            require_enum(G->mShaderOverrideMap.at(hash).allow_duplicate_hashes == 0,
                "unknown, malformed and out-of-range enum options preserve default");
        }
        std::fputs("PASS: real enum/boolean/integer options and diagnostic fallback\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL enum option: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
