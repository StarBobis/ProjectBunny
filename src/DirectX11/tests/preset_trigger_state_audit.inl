// F-195: PresetOverride's trigger state must be consumed as one unit before
// Activate/Deactivate run command lists. These cases use the production
// PresetOverride, command list parser and transition engine; only the test
// fixture (config file and WARP device) is scaffolding.

float preset_audit_value(const wchar_t* name)
{
    return command_list_globals.at(name).fval;
}

// Single-thread red case: the deactivation command list triggers the preset
// itself. Before F-195 the tail of Update() cleared that trigger, so the
// preset could never come back without a second external trigger.
void test_f195()
{
    IniParameterStateScope saved;
    AuditConfigScope config;
    BatchDevice device;

    config.load(
        "global $f195_phase=0\n"
        "[CommandListF195Self]\n"
        "post preset = PresetF195Self\n"
        "[PresetF195Self]\n"
        "unique_triggers_required = 1\n"
        "$f195_phase = 7\n"
        "run = CommandListF195Self\n", 0, true);

    auto& preset = presetOverrides.at(L"presetf195self");
    require_batch(preset.unique_triggers_required == 1,
        "F195: fixture parsed the real preset and its unique trigger count");
    require_batch(preset_audit_value(L"$f195_phase") == 0,
        "F195: fixture starts from the declared global value");

    // Frame 1: external trigger activates. Activation runs the target's pre
    // list, which is empty, so it cannot trigger itself.
    preset.Trigger(nullptr);
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_phase") == 7,
        "F195: frame 1 activation applies the preset value");

    // Frame 2: no new trigger releases the preset. The release list runs the
    // target's post list, which triggers this same preset.
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_phase") == 0,
        "F195: frame 2 without a trigger releases the preset");

    // Frame 3: the trigger emitted by the release command list must survive
    // the previous Update instead of being cleared by it.
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_phase") == 7,
        "F195: trigger emitted by the release command list survives to the next Update");
}

// Exclude has the same lost-window shape as Trigger: an exclusion emitted by
// the deactivation command list must apply to the next frame, not be cleared
// by the Update that is already acting.
void test_f195_exclude_lost_window()
{
    IniParameterStateScope saved;
    AuditConfigScope config;
    BatchDevice device;

    config.load(
        "global $f195_excluded_phase=0\n"
        "[CommandListF195ExcludeMark]\n"
        "post exclude_preset = PresetF195Exclude\n"
        "[PresetF195Exclude]\n"
        "unique_triggers_required = 1\n"
        "$f195_excluded_phase = 7\n"
        "run = CommandListF195ExcludeMark\n", 0, true);

    auto& preset = presetOverrides.at(L"presetf195exclude");
    require_batch(preset.unique_triggers_required == 1,
        "F195 exclude: fixture parsed the real preset");

    // Frame 1: activate.
    preset.Trigger(nullptr);
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_excluded_phase") == 7,
        "F195 exclude: frame 1 activation applies the preset value");

    // Frame 2: release; the release list emits exclude_preset for this preset.
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_excluded_phase") == 0,
        "F195 exclude: frame 2 releases the preset");

    // Frame 3: a fresh external trigger must still be excluded. Before the fix
    // the exclusion was cleared at the end of frame 2, so this activated.
    preset.Trigger(nullptr);
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_excluded_phase") == 0,
        "F195 exclude: exclusion from the release list is consumed on the next Update");

    // Frame 4: the exclusion was for one frame only; a new trigger activates.
    preset.Trigger(nullptr);
    CurrentTransition.UpdatePresets(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(preset_audit_value(L"$f195_excluded_phase") == 7,
        "F195 exclude: the next frame's trigger is not wedged by a stale exclusion");
}

// Concurrency guard for the container itself. The producer only calls
// Trigger/Exclude; the consumer only calls Update. Without the mutex the
// unordered_set insert can race with clear() (standard undefined behaviour;
// debug STL/ASan catches container corruption when it happens). The final
// deterministic sequence checks the object is still usable after the stress.
void test_f195_concurrent_guard()
{
    IniParameterStateScope saved;
    AuditConfigScope config;
    BatchDevice device;

    // Use a real parsed preset, not a default-constructed one: Override's
    // default constructor leaves transition/release_transition uninitialised,
    // and PresetOverride::ParseIniSection is what gives them their production
    // values (0 here).
    config.load(
        "global $f195_concurrent=0\n"
        "[PresetF195Concurrent]\n"
        "$f195_concurrent=7\n", 0, true);
    auto& preset = presetOverrides.at(L"presetf195concurrent");
    CommandListVariable& observed = command_list_globals.at(L"$f195_concurrent");
    require_batch(preset.mOverrideVars.count(&observed) == 1,
        "F195 concurrency: parsed preset overrides the observed global");

    constexpr size_t command_count = 4096;
    constexpr unsigned passes = 16;
    std::vector<PresetCommand> commands(command_count);
    for (auto& command : commands)
        command.preset = &preset;
    preset.unique_triggers_required = static_cast<unsigned>(command_count);

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (unsigned pass = 0; pass < passes; ++pass) {
            for (size_t i = 0; i < command_count; ++i) {
                preset.Trigger(&commands[i]);
                if ((i & 31) == 0)
                    preset.Exclude();
            }
        }
        stop.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        while (!stop.load(std::memory_order_acquire))
            preset.Update(device.device);
        preset.Update(device.device);
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    // Settle the stress state, then explicitly force the preset inactive
    // through the production Deactivate path. Two Update calls are enough to
    // consume the last trigger, but the explicit Deactivate keeps the final
    // availability probe independent of which side of the state machine the
    // consumer thread happened to stop on.
    preset.Update(device.device);
    preset.Update(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    preset.Deactivate(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    CurrentTransition.Stop();
    require_batch(observed.fval == 0,
        "F195 concurrency: drained preset restores the saved variable");
    require_batch(OverrideSave.vars.count(&observed) == 0,
        "F195 concurrency: drained preset releases its global save entry");

    preset.unique_triggers_required = 0;
    preset.Trigger(nullptr);
    preset.Update(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    require_batch(observed.fval == 7,
        "F195 concurrency: preset remains usable after concurrent Trigger/Exclude/Update");

    // Leave the global save/transition state clean for the next case.
    preset.Update(device.device);
    CurrentTransition.UpdateTransitions(device.device);
    presetOverrides.clear();
}
