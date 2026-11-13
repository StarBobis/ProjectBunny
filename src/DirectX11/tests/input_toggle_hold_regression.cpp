#include "Input.h"
#include "Override.h"
#include "IniHandler.h"
#include "HackerContext.h"
#include "globals.h"

#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

// The actual queue is drained by UpdateTransitions. Clear it during fixture
// cleanup before parsed command lists/global variables can be destroyed.
extern std::vector<CommandList*> pending_post_command_lists;

namespace {
using Microsoft::WRL::ComPtr;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct InputCounts {
    unsigned down = 0;
    unsigned up = 0;
};

void count_down(HackerDevice*, void* data) { ++static_cast<InputCounts*>(data)->down; }
void count_up(HackerDevice*, void* data) { ++static_cast<InputCounts*>(data)->up; }

// Only hardware sampling is replaced. Actual InputAction state machines,
// registration, disabled gate, callbacks and override machinery run unchanged.
class MemoryInputButton : public InputButton {
    bool& state;
    unsigned& polls;
    unsigned& destroyed;
public:
    MemoryInputButton(bool& state, unsigned& polls, unsigned& destroyed) :
        state(state), polls(polls), destroyed(destroyed) {}
    ~MemoryInputButton() override { ++destroyed; }
    bool CheckState() override { ++polls; return state; }
};

enum class MemoryActionKind { Normal, Repeat, Delayed };

class RegistryCleanup {
public:
    ~RegistryCleanup() { ClearKeyBindings(); }
};

void register_memory(bool& state, unsigned& polls, unsigned& destroyed,
    std::shared_ptr<InputListener> listener, bool bypass = false,
    MemoryActionKind kind = MemoryActionKind::Normal)
{
    auto button = std::make_unique<MemoryInputButton>(state, polls, destroyed);
    std::unique_ptr<InputAction> action;
    if (kind == MemoryActionKind::Repeat)
        action = std::make_unique<RepeatingInputAction>(button.get(), listener, 1001); // zero-ms interval
    else if (kind == MemoryActionKind::Delayed)
        action = std::make_unique<DelayedInputAction>(button.get(), listener, 0, 0);
    else
        action = std::make_unique<InputAction>(button.get(), listener);
    button.release();
    RegisterInputAction(std::move(action), bypass ? InputDisableScope::NONE : InputDisableScope::ALL);
}

class InputDevice {
public:
    HMODULE runtime = nullptr;
    ComPtr<ID3D11Device1> native;
    ComPtr<ID3D11DeviceContext1> context;
    HackerDevice* device = nullptr;
    std::unique_ptr<HackerContext> wrapper;

    InputDevice()
    {
        runtime = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(runtime != nullptr, "load native input-test D3D11 runtime");
        auto create = reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(runtime, "D3D11CreateDevice"));
        check(create != nullptr, "resolve native device creation");
        ComPtr<ID3D11Device> base;
        ComPtr<ID3D11DeviceContext> immediate;
        check(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &base, nullptr, &immediate)), "create real input-test WARP device");
        check(SUCCEEDED(base.As(&native)) && SUCCEEDED(immediate.As(&context)), "query D3D11.1 input-test interfaces");
        device = new HackerDevice(native.Get(), context.Get());
        native->AddRef();
        wrapper = std::make_unique<HackerContext>(native.Get(), context.Get());
        wrapper->SetHackerDevice(device);
        device->SetHackerContext(wrapper.get());
        device->SetHackerSwapChain(nullptr);
    }

    ~InputDevice()
    {
        wrapper->ClearState();
        wrapper.reset();
        context.Reset();
        native.Reset();
        device->Release();
        FreeLibrary(runtime);
    }
};

class InputFixture {
public:
    std::filesystem::path config, settings;
    HINSTANCE saved_module = migoto_handle;
    std::wstring saved_user_config = G->user_config;
    int saved_dirty = G->user_config_dirty;
    InputDisableScope saved_scope = G->input_disable_scope;
    bool saved_disabled = G->disable_input;
    bool saved_initialized = G->disable_input_initialized;
    bool owns_config = false;

    InputFixture()
    {
        wchar_t executable[MAX_PATH] = {};
        check(GetModuleFileNameW(nullptr, executable, MAX_PATH) > 0, "locate input-test executable");
        const auto base = std::filesystem::path(executable).parent_path();
        bool in_build = false;
        for (const auto& part : base) in_build |= _wcsicmp(part.c_str(), L"build") == 0;
        check(in_build, "input fixture must be inside build/");
        config = base / L"d3dx.ini";
        settings = base / (L"input_toggle_regression_" + std::to_wstring(GetCurrentProcessId()) + L".ini");
        check(!std::filesystem::exists(settings), "refuse existing input settings fixture");
        HANDLE file = CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(file != INVALID_HANDLE_VALUE, "refuse existing d3dx.ini in input fixture");
        CloseHandle(file);
        owns_config = true;
        migoto_handle = nullptr;
        G->disable_input = false;
        G->disable_input_initialized = false;
        ClearKeyBindings();
    }

    ~InputFixture()
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
        G->input_disable_scope = saved_scope;
        G->disable_input = saved_disabled;
        G->disable_input_initialized = saved_initialized;
        if (owns_config) { DeleteFileW(config.c_str()); DeleteFileW(settings.c_str()); }
    }

    void write(const char* toggle, bool disabled = true)
    {
        std::ofstream out(config, std::ios::binary | std::ios::trunc);
        out << "[Logging]\ncalls=0\nshow_warnings=0\n"
            "[Hunting]\nhunting=0\n[System]\nskip_early_includes_load=0\n[Input]\ninput_disable_mode=all\ninput=" << (disabled ? "off" : "on") << "\n";
        if (toggle) out << "toggle_input=" << toggle << "\n";
        out << "[Include]\nuser_config=" << settings.filename().string() << "\n"
            "[Constants]\nglobal $input_hold_value=5\nglobal $input_hold_hits=0\n"
            "global $input_hold_posts=0\nglobal locked $input_hold_locked=7\n"
            "[CommandListInputHoldCommands]\n$input_hold_hits=$input_hold_hits+1\n"
            "post $input_hold_posts=$input_hold_posts+1\n"
            "[KeyInputHold]\nkey=F18\ntype=hold\n$input_hold_value=9\nrun=CommandListInputHoldCommands\n"
            "[KeyInputToggle]\nkey=F19\ntype=toggle\n$input_hold_value=10\n"
            "[KeyInputLocked]\nkey=F20\ntype=hold\n$input_hold_locked=99\n$input_hold_value=12\n"
            "[KeyInputCycle]\nkey=F21\ntype=cycle\n$input_hold_value=3,4\n"
            "[PresetInputHold]\n$input_hold_value=11\n";
        out.close();
        check(out.good(), "write production input/hold configuration");
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, config.c_str());
    }
};

void registry_tests(InputDevice& device)
{
    ClearKeyBindings();
    unsigned polls = 0, destroyed = 0;
    RegistryCleanup cleanup; // Destroy registered buttons before their counters.
    bool state = true;
    InputCounts counts;
    auto listener = std::make_shared<InputCallbacks>(count_down, count_up, &counts);
    check(!RegisterKeyBinding(L"invalid", L"not_a_valid_keyboard_binding", listener, 0, 0, 0, InputDisableScope::NONE),
        "invalid toggle binding cannot register a privileged action");
    register_memory(state, polls, destroyed, listener);
    G->disable_input = true;
    check(!DispatchRegisteredInputActions(device.device) && counts.down == 0 && polls == 0,
        "disabled gate rejects the first ordinary action before sampling/dispatch");
    G->disable_input = false;
    check(DispatchRegisteredInputActions(device.device) && counts.down == 1 && polls == 1,
        "enabled ordinary action executes");
    ClearKeyBindings();
    check(destroyed == 1, "registry releases the actual polymorphic button backend");

    counts = {};
    polls = destroyed = 0;
    bool first = true, toggle = true, last = true;
    register_memory(first, polls, destroyed, listener);
    auto toggler = std::make_shared<InputCallbacks>(ToggleInput, nullptr, nullptr);
    register_memory(toggle, polls, destroyed, toggler, true); // Deliberately not first.
    register_memory(last, polls, destroyed, listener);
    G->disable_input = true;
    check(DispatchRegisteredInputActions(device.device) && !G->disable_input && counts.down == 1 && polls == 2,
        "explicit toggle remains usable out of order and enables following ordinary actions");
    check(DispatchRegisteredInputActions(device.device) && counts.down == 2,
        "previously skipped ordinary action resumes on the next dispatch");
    toggle = false;
    DispatchRegisteredInputActions(device.device);
    toggle = true;
    DispatchRegisteredInputActions(device.device);
    check(G->disable_input, "privileged action can disable input again");
    const unsigned disabled_polls = polls;
    first = last = false;
    DispatchRegisteredInputActions(device.device);
    check(polls == disabled_polls + 1 && counts.up == 0, "disabled ordinary up events are deferred, toggle still polls");
    toggle = false;
    DispatchRegisteredInputActions(device.device);
    toggle = true;
    DispatchRegisteredInputActions(device.device);
    DispatchRegisteredInputActions(device.device);
    check(!G->disable_input && counts.up == 2, "re-enable observes releases through actual action state machines");
    ClearKeyBindings();
    check(destroyed == 3, "all classified actions release their input backends");

    for (auto kind : {MemoryActionKind::Repeat, MemoryActionKind::Delayed}) {
        counts = {};
        polls = destroyed = 0;
        state = true;
        register_memory(state, polls, destroyed, listener, false, kind);
        G->disable_input = true;
        check(!DispatchRegisteredInputActions(device.device) && polls == 0, "gate precedes repeat/delay state mutation");
        G->disable_input = false;
        check(DispatchRegisteredInputActions(device.device) && counts.down == 1, "repeat/delay action resumes when enabled");
        G->disable_input = true;
        const unsigned previous = polls;
        DispatchRegisteredInputActions(device.device);
        check(polls == previous, "repeat/delay actions remain blocked on later frames");
        ClearKeyBindings();
        check(destroyed == 1, "repeat/delay backend lifetime");
    }
    G->disable_input = false;
}

void config_tests(InputFixture& fixture, InputDevice& device)
{
    fixture.write(nullptr);
    LoadConfigFile();
    check(G->disable_input && G->disable_input_initialized, "initial Input/input loaded exactly once");
    check(!RegisterIniKeyBinding(L"Input", L"toggle_input", ToggleInput, nullptr, 0, nullptr, InputDisableScope::NONE),
        "missing toggle option does not register an action");
    ClearKeyBindings();
    ToggleInput(device.device, nullptr);
    check(!G->disable_input, "actual toggle callback updates runtime state");
    fixture.write("not_a_valid_keyboard_binding");
    ReloadConfig(device.device);
    check(!G->disable_input, "reload preserves runtime enable despite initial input=off");
    check(!RegisterIniKeyBinding(L"Input", L"toggle_input", ToggleInput, nullptr, 0, nullptr, InputDisableScope::NONE),
        "invalid configured toggle reports failure");
    fixture.write("no_modifiers F22", false);
    ReloadConfig(device.device);
    check(!G->disable_input, "valid toggle reload retains runtime state");
    check(RegisterIniKeyBinding(L"Input", L"toggle_input", ToggleInput, nullptr, 0, nullptr, InputDisableScope::NONE),
        "actual config-backed toggle registration succeeds");
    ToggleInput(device.device, nullptr);
    fixture.write("no_modifiers F22", false);
    ReloadConfig(device.device);
    check(G->disable_input, "reload preserves runtime disable despite initial input=on");
    ClearKeyBindings();
}

void hold_tests(InputDevice& device)
{
    G->disable_input = false;
    auto& value = command_list_globals.at(L"$input_hold_value");
    auto& hits = command_list_globals.at(L"$input_hold_hits");
    auto& posts = command_list_globals.at(L"$input_hold_posts");
    auto& locked = command_list_globals.at(L"$input_hold_locked");
    value.fval = 5;
    hits.fval = posts.fval = 0;
    auto hold = std::make_shared<KeyOverride>(KeyOverrideType::HOLD);
    hold->ParseIniSection(L"KeyInputHold");
    unsigned polls = 0, destroyed = 0;
    RegistryCleanup cleanup;
    bool first = false, second = false;
    register_memory(first, polls, destroyed, hold);
    register_memory(second, polls, destroyed, hold);
    first = true;
    check(DispatchRegisteredInputActions(device.device), "first actual HOLD Down");
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 9 && hits.fval == 1 && posts.fval == 0 && hold->mSavedVars.at(&value) == 5,
        "first HOLD Down saves original and runs pre list only");
    second = true;
    DispatchRegisteredInputActions(device.device);
    check(hits.fval == 2 && hold->mSavedVars.at(&value) == 5, "repeated HOLD Down runs activation without replacing original save");
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 9 && posts.fval == 1 && OverrideSave.vars.size() == 1,
        "repeated HOLD retains one global save and runs queued post list");
    first = false;
    DispatchRegisteredInputActions(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 5 && posts.fval == 2 && OverrideSave.vars.empty(), "either HOLD Up restores original once even while other key remains down");
    second = false;
    DispatchRegisteredInputActions(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 5 && posts.fval == 2 && OverrideSave.vars.empty(), "later HOLD Up does not restore/run post a second time");
    ClearKeyBindings();
    check(destroyed == 2, "shared HOLD listener actions release both backends");

    polls = destroyed = 0;
    hits.fval = posts.fval = 0;
    first = false;
    register_memory(first, polls, destroyed, hold);
    first = true;
    check(DispatchRegisteredInputActions(device.device), "HOLD activates before input disable");
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 9 && hits.fval == 1 && posts.fval == 0 &&
        hold->mSavedVars.at(&value) == 5 && OverrideSave.vars.size() == 1 && pending_post_command_lists.empty(),
        "HOLD saves original once without queued release before disable");
    ToggleInput(device.device, nullptr);
    check(G->disable_input, "actual input toggle disables an active HOLD");
    first = false; // Hardware release occurs while the ordinary action is gated.
    const unsigned polls_before_release = polls;
    check(!DispatchRegisteredInputActions(device.device), "disabled HOLD release is not dispatched");
    CurrentTransition.UpdateTransitions(device.device);
    check(polls == polls_before_release && value.fval == 9 && hits.fval == 1 && posts.fval == 0 &&
        hold->mSavedVars.at(&value) == 5 && OverrideSave.vars.size() == 1 && pending_post_command_lists.empty(),
        "disabled gate preserves active HOLD/save without polling Up or running pre/post");
    ToggleInput(device.device, nullptr);
    check(!G->disable_input && DispatchRegisteredInputActions(device.device), "re-enabled dispatcher observes the deferred HOLD Up");
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 5 && hits.fval == 1 && posts.fval == 1 &&
        hold->mSavedVars.at(&value) == 5 && OverrideSave.vars.empty() && pending_post_command_lists.empty(),
        "deferred HOLD Up restores original and runs release post exactly once");
    check(!DispatchRegisteredInputActions(device.device), "unchanged released HOLD has no second Up");
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 5 && hits.fval == 1 && posts.fval == 1 && OverrideSave.vars.empty() &&
        pending_post_command_lists.empty(), "later frames cannot repeat deferred restoration or post commands");
    ClearKeyBindings();
    check(destroyed == 1, "deferred-release HOLD backend cleanup");

    KeyOverride toggle(KeyOverrideType::TOGGLE);
    toggle.ParseIniSection(L"KeyInputToggle");
    toggle.DownEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 10, "ordinary TOGGLE activates");
    toggle.UpEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 10, "ordinary TOGGLE ignores Up");
    toggle.DownEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 5 && OverrideSave.vars.empty(), "ordinary TOGGLE second Down restores");

    KeyOverride lock_hold(KeyOverrideType::HOLD);
    lock_hold.ParseIniSection(L"KeyInputLocked");
    check(lock_hold.mOverrideVars.count(&locked) == 0, "parsed HOLD rejects locked target");
    lock_hold.DownEvent(device.device);
    lock_hold.DownEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(locked.fval == 7 && value.fval == 12, "repeated HOLD preserves LOCKED value while normal target updates");
    lock_hold.UpEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(locked.fval == 7 && value.fval == 5, "LOCKED value preserved during HOLD restoration");

    PresetOverride preset;
    preset.ParseIniSection(L"PresetInputHold");
    preset.Trigger(nullptr);
    preset.Update(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 11, "ordinary preset activation retained");
    preset.Update(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 5 && OverrideSave.vars.empty(), "ordinary preset restoration retained");

    KeyOverrideCycle cycle;
    cycle.ParseIniSection(L"KeyInputCycle");
    cycle.DownEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 3, "ordinary cycle first value retained");
    cycle.DownEvent(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    check(value.fval == 4 && pending_post_command_lists.empty(), "ordinary cycle second value/post drain retained");
}
} // namespace

void test_dx11_input_toggle_hold_regression()
{
    try {
        InputFixture fixture;
        InputDevice device;
        config_tests(fixture, device);
        registry_tests(device);
        hold_tests(device);
        std::fputs("PASS: real input registry/gate, config reload and HOLD/TOGGLE/preset/cycle behavior (memory button sampling)\n", stderr);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: input-toggle/HOLD regression: %s\n", error.what());
        std::exit(EXIT_FAILURE);
    }
}
