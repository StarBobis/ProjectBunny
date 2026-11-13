#include "Input.h"
#include "Override.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include "globals.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

extern std::vector<CommandList*> pending_post_command_lists;

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class ScopeDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    ScopeDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(runtime != nullptr, "load native scope-test D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve native device creator");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create scope-test WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query D3D11.1 scope-test interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~ScopeDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class ScopeFixture {
    bool owns_config = false;
    std::vector<std::filesystem::path> owned_files, owned_directories;

    void cleanup_files() noexcept
    {
        for (const auto& file : owned_files) DeleteFileW(file.c_str());
        for (auto directory = owned_directories.rbegin(); directory != owned_directories.rend(); ++directory)
            RemoveDirectoryW(directory->c_str());
        if (owns_config) { DeleteFileW(config.c_str()); DeleteFileW(settings.c_str()); }
    }

    void make_directory(const std::filesystem::path& path)
    {
        check(CreateDirectoryW(path.c_str(), nullptr) != 0, "create exclusively owned scope fixture directory");
        owned_directories.push_back(path);
    }

    void write_file(const std::filesystem::path& path, const std::string& contents)
    {
        check(!std::filesystem::exists(path), "scope fixture never replaces an existing file");
        owned_files.push_back(path);
        std::ofstream out(path, std::ios::binary);
        out << contents;
        out.close();
        check(out.good(), "write scope include fixture");
    }

public:
    std::filesystem::path directory, config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    bool saved_disabled = G->disable_input;
    bool saved_initialized = G->disable_input_initialized;
    InputDisableScope saved_scope = G->input_disable_scope;

    ScopeFixture()
    {
        try {
            wchar_t executable[MAX_PATH] = {};
            check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate scope regression executable");
            const auto base = std::filesystem::path(executable).parent_path();
            bool in_build = false;
            for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
            check(in_build, "scope fixtures must remain inside build/");
            config = base / L"d3dx.ini";
            settings = base / (L"input_scope_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
            check(!std::filesystem::exists(settings), "refuse existing scope settings fixture");
            HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            check(file != INVALID_HANDLE_VALUE, "refuse existing d3dx.ini");
            CloseHandle(file);
            owns_config = true;
            directory = base / (L"input_scope_" + std::to_wstring(GetCurrentProcessId()));
            make_directory(directory);
            make_directory(directory / L"Mods");
            make_directory(directory / L"Mods" / L"Sub");
            make_directory(directory / L"ModsOther");
            make_directory(directory / L"Outer");
            make_directory(directory / L"Outer" / L"Nested");
            write_file(directory / L"Mods" / L"mod.ini",
                "namespace = Completely Different\\Alias\n[KeyMod]\nkey=F14\n$input_scope_value=1\n");
            write_file(directory / L"Mods" / L"Sub" / L"cycle.ini",
                "namespace = ScopeCycleAlias\n[KeyCycle]\nkey=F15\nback=F16\ntype=cycle\n$input_scope_value=2,3\n");
            write_file(directory / L"Mods" / L"direct.ini",
                "namespace = ScopeDirectInside\n[KeyInside]\nkey=F10\n$input_scope_value=4\n");
            write_file(directory / L"ModsOther" / L"other.ini",
                "namespace = ScopeOther\n[KeyOther]\nkey=F11\n$input_scope_value=5\n");
            write_file(directory / L"direct.ini",
                "namespace = ScopeDirect\n[KeyDirect]\nkey=F12\n$input_scope_value=6\n");
            write_file(directory / L"Outer" / L"loader.ini",
                "namespace = Loader Renamed\n[Include]\ninclude_recursive=Nested\n");
            write_file(directory / L"Outer" / L"Nested" / L"deep.ini",
                "namespace = ScopeNestedAlias\n[KeyNested]\nkey=F17\n$input_scope_value=7\n");
            migoto_handle = nullptr;
        } catch (...) { cleanup_files(); throw; }
    }

    ~ScopeFixture()
    {
        ClearKeyBindings();
        pending_post_command_lists.clear();
        CurrentTransition.Stop();
        OverrideSave.Reset(nullptr);
        presetOverrides.clear();
        G->constants_command_list.clear();
        G->post_constants_command_list.clear();
        customResourcePools.clear();
        customResources.clear();
        persistent_variables.clear();
        command_list_globals.clear();
        migoto_handle = saved_module;
        G->user_config = saved_user_config;
        G->user_config_dirty = saved_dirty;
        G->disable_input = saved_disabled;
        G->disable_input_initialized = saved_initialized;
        G->input_disable_scope = saved_scope;
        cleanup_files();
    }

    void write_config(const char* initial, const char* scope, bool input_section = true)
    {
        const std::string prefix = directory.filename().string();
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\nlog_level=disabled\nshow_warnings=0\n[Hunting]\nhunting=0\n"
            "[System]\nskip_early_includes_load=0\n[Include]\nuser_config=" << settings.filename().string() << "\n"
            // The direct file is parsed first. A later recursive enumeration
            // must not overwrite its section's original provenance.
            "include=" << prefix << "\\Mods\\direct.ini\n"
            "include_recursive=" << prefix << "/mOdS/.\n"
            "include=" << prefix << "\\modsother\\other.ini\n"
            "include=" << prefix << "\\direct.ini\n"
            "include=" << prefix << "\\Outer\\loader.ini\n"
            "[Constants]\nglobal $input_scope_value=0\n"
            "[KeyRoot]\nkey=F13\n$input_scope_value=8\n";
        if (input_section) {
            out << "[Input]\ntoggle_input=no_modifiers F18\n";
            if (initial) out << "input=" << initial << "\n";
            if (scope) out << "input_disable_mode=" << scope << "\n";
        }
        out.close();
        check(out.good(), "write actual Input/include configuration");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    }

    void initial_load()
    {
        ClearKeyBindings();
        G->disable_input = false;
        G->disable_input_initialized = false;
        G->input_disable_scope = InputDisableScope::INVALID;
        LoadConfigFile();
    }
};

void source_and_default_tests(ScopeFixture& fixture, ScopeDevice& device)
{
    fixture.write_config(nullptr, nullptr, false);
    fixture.initial_load();
    check(!G->disable_input && G->disable_input_initialized && G->input_disable_scope == InputDisableScope::MODS,
        "absent Input section defaults to enabled/mods");
    check(CountRegisteredInputActions(InputDisableScope::NONE) == 0,
        "absent toggle does not create a privileged action");
    fixture.write_config(nullptr, nullptr);
    fixture.initial_load();
    check(!G->disable_input && G->input_disable_scope == InputDisableScope::MODS,
        "absent input and input_disable_mode keys retain upstream defaults");

    check(GetIniKeyBindingScope(L"KeyRoot") == InputDisableScope::ALL, "root INI key is a tool binding");
    check(GetIniKeyBindingScope(L"Key\\ScopeDirect\\Direct") == InputDisableScope::ALL,
        "direct include is not a mod binding");
    check(GetIniKeyBindingScope(L"Key\\ScopeOther\\Other") == InputDisableScope::ALL,
        "ModsOther direct include cannot match Mods directory by text prefix");
    check(GetIniKeyBindingScope(L"Key\\ScopeDirectInside\\Inside") == InputDisableScope::ALL,
        "direct-first include inside recursive directory retains actual first-source provenance");
    check(GetIniKeyBindingScope(L"Key\\Completely Different\\Alias\\Mod") == InputDisableScope::MODS,
        "recursive actual source survives unrelated explicit namespace alias");
    check(GetIniKeyBindingScope(L"Key\\ScopeCycleAlias\\Cycle") == InputDisableScope::MODS,
        "nested directory recursive cycle section is a mod");
    check(GetIniKeyBindingScope(L"Key\\ScopeNestedAlias\\Nested") == InputDisableScope::MODS,
        "nested include_recursive resolves relative to actual loader INI path despite namespace rename");
    check(CountRegisteredInputActions(InputDisableScope::MODS) == 4 &&
        CountRegisteredInputActions(InputDisableScope::ALL) == 4 && CountRegisteredInputActions(InputDisableScope::NONE) == 1,
        "actual parsed registry classifies mod Key and Back alike, four tool actions and one privileged toggle");

    auto source = (fixture.directory / L"Mods" / L"Sub" / L"cycle.ini").wstring();
    auto root = (fixture.directory / L"mOdS" / L".").wstring();
    std::replace(root.begin(), root.end(), L'\\', L'/');
    check(IsInputPathWithinDirectory(source, root), "Windows case/dot/slash normalization preserves membership");
    check(!IsInputPathWithinDirectory((fixture.directory / L"ModsOther" / L"other.ini").wstring(), root),
        "normalized directory comparison enforces a separator boundary");
    check(!IsInputPathWithinDirectory((fixture.directory / L"Mods" / L".." / L"ModsOther" / L"other.ini").wstring(), root),
        "parent traversal cannot pretend to remain inside a recursive root");
    check(!IsInputPathWithinDirectory(source, L""), "empty recursive root fails safely");

    ToggleInput(device.device, nullptr);
    check(G->disable_input, "actual toggle establishes runtime off after the corrected enabled default");
    fixture.write_config("on", "ALL");
    ReloadConfig(device.device);
    check(G->disable_input && G->input_disable_scope == InputDisableScope::ALL,
        "reload changes disable scope but keeps runtime off despite new initial on");
    ToggleInput(device.device, nullptr);
    check(!G->disable_input, "actual toggle callback enables bindings");
    fixture.write_config("off", "mods");
    ReloadConfig(device.device);
    check(!G->disable_input && G->input_disable_scope == InputDisableScope::MODS,
        "reload keeps runtime on despite new initial off");
    fixture.write_config("off", "invalid_scope");
    ReloadConfig(device.device);
    check(!G->disable_input && G->input_disable_scope == InputDisableScope::MODS,
        "invalid scope falls back without resetting initialized runtime input state");
    fixture.write_config("ON", "all");
    fixture.initial_load();
    check(!G->disable_input && G->input_disable_scope == InputDisableScope::ALL,
        "fresh start reads explicit on and case-insensitive scope");
    fixture.write_config("0", "mods");
    fixture.initial_load();
    check(G->disable_input, "fresh start still honors explicit numeric off");
}

struct ScopeCounts {
    unsigned down = 0;
    unsigned up = 0;
    unsigned polls = 0;
};

class ScopeButton : public InputButton {
    bool& state;
    ScopeCounts& counts;
public:
    ScopeButton(bool& state, ScopeCounts& counts) : state(state), counts(counts) {}
    bool CheckState() override { ++counts.polls; return state; }
};

class ScopedRegistryCleanup {
public:
    ~ScopedRegistryCleanup() { ClearKeyBindings(); }
};

void count_down(HackerDevice*, void* data) { ++static_cast<ScopeCounts*>(data)->down; }
void count_up(HackerDevice*, void* data) { ++static_cast<ScopeCounts*>(data)->up; }

void register_scoped_memory(bool& state, ScopeCounts& counts, InputDisableScope scope,
    InputCallback down = count_down, void* private_data = nullptr)
{
    auto listener = std::make_shared<InputCallbacks>(down, count_up, private_data ? private_data : &counts);
    auto button = std::make_unique<ScopeButton>(state, counts);
    auto action = std::make_unique<InputAction>(button.get(), listener);
    button.release();
    RegisterInputAction(std::move(action), scope);
}

void dispatch_tests(ScopeDevice& device)
{
    ClearKeyBindings();
    ScopeCounts mods, tools, privileged;
    bool mod_down = true, tool_down = true, privileged_down = true;
    ScopedRegistryCleanup cleanup;
    register_scoped_memory(mod_down, mods, GetIniKeyBindingScope(L"Key\\Completely Different\\Alias\\Mod"));
    register_scoped_memory(tool_down, tools, GetIniKeyBindingScope(L"KeyRoot"));
    register_scoped_memory(privileged_down, privileged, InputDisableScope::NONE);
    G->disable_input = true;
    G->input_disable_scope = InputDisableScope::MODS;
    check(DispatchRegisteredInputActions(device.device) && mods.polls == 0 && mods.down == 0 &&
        tools.down == 1 && privileged.down == 1, "mods mode gates mod before polling while tool/NONE actions run");
    G->input_disable_scope = InputDisableScope::ALL;
    tool_down = privileged_down = false;
    check(DispatchRegisteredInputActions(device.device) && mods.polls == 0 && tools.polls == 1 && tools.up == 0 &&
        privileged.up == 1, "all mode gates tool release too while NONE remains available");
    G->disable_input = false;
    check(DispatchRegisteredInputActions(device.device) && mods.down == 1 && tools.up == 1,
        "runtime on ignores scope and observes deferred actual backend transitions");
    ClearKeyBindings();

    mods = tools = privileged = {};
    mod_down = tool_down = privileged_down = true;
    register_scoped_memory(tool_down, tools, InputDisableScope::ALL);
    // Toggle deliberately follows a tool action; classification, not position,
    // grants privilege. The real callback updates the current global switch.
    auto button = std::make_unique<ScopeButton>(privileged_down, privileged);
    auto toggle = std::make_unique<InputAction>(button.get(), std::make_shared<InputCallbacks>(ToggleInput, nullptr, nullptr));
    button.release();
    RegisterInputAction(std::move(toggle), InputDisableScope::NONE);
    register_scoped_memory(mod_down, mods, InputDisableScope::MODS);
    G->disable_input = true;
    G->input_disable_scope = InputDisableScope::ALL;
    check(DispatchRegisteredInputActions(device.device) && !G->disable_input && tools.polls == 0 &&
        privileged.polls == 1 && mods.down == 1, "explicit NONE toggle can re-enable all mode even when registered out of order");
}
} // namespace

void test_dx11_input_scope_regression()
{
    try {
        ScopeFixture fixture;
        ScopeDevice device;
        source_and_default_tests(fixture, device);
        dispatch_tests(device);
        std::fputs("PASS: real Input/include provenance, scope/default/reload and classified registry (memory button sampling)\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: input scope regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
