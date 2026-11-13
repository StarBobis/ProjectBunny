#include "CommandList.h"
#include "HackerContext.h"
#include "globals.h"
#include "profiling.h"
#include "lock.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class ProfileFixture {
public:
    const Profiling::Mode mode = Profiling::mode.load();
    const unsigned frame = G->frame_no;
    const INT64 interval = Profiling::interval;
    const bool analysis = G->analyse_frame;
    ProfileFixture() { G->analyse_frame = false; Profiling::clear(); }
    ~ProfileFixture() { Profiling::clear(); Profiling::mode = mode; G->frame_no = frame; Profiling::interval = interval; G->analyse_frame = analysis; }
};
enum class ProfileAction { None, Summary, TopCommands, Disabled, Clear };
class MeasuredCommand : public CommandListCommand {
public:
    std::atomic<unsigned> calls{0};
    ProfileAction action = ProfileAction::None;
    void run(CommandListState*) override
    {
        ++calls;
        switch (action) {
        case ProfileAction::Summary: Profiling::mode = Profiling::Mode::SUMMARY; break;
        case ProfileAction::TopCommands: Profiling::mode = Profiling::Mode::TOP_COMMANDS; break;
        case ProfileAction::Disabled: Profiling::mode = Profiling::Mode::NONE; break;
        case ProfileAction::Clear: Profiling::clear(); break;
        default: break;
        }
    }
};
void run(CommandList& list, bool post = false)
{
    HackerContext context(nullptr, nullptr);
    CommandListState state; state.mHackerContext = &context; state.post = post;
    RunLinkedCommandList runner(&list); runner.run(&state);
}
void test_registration_and_modes()
{
    CommandList list;
    auto command = std::make_shared<MeasuredCommand>(); list.commands.push_back(command);
    check(list.executions == 0 && command->pre_executions == 0 && command->post_executions == 0,
        "new profile objects initialize counters before first use");
    Profiling::mode = Profiling::Mode::SUMMARY;
    run(list); run(list);
    check(command_lists_profiling.size() == 1 && list.executions == 2,
        "one registration per current collection still counts every execution");
    auto old = Profiling::GetCollectionToken();
    clear_command_list_profiling();
    check(command_lists_profiling.empty() && !Profiling::IsCurrentCollection(old), "explicit reload clear retires registration generation");
    run(list);
    check(command_lists_profiling.size() == 1 && list.executions == 1, "old object registers and resets counters in new window");
    // Ownership identities have no finite epoch counter to wrap into an old
    // stamp, even when the object's last participation was many windows ago.
    for (int i = 0; i < 512; ++i) clear_command_list_profiling();
    check(!Profiling::IsCurrentCollection(old), "retired ownership identity cannot collide after repeated clears");
    run(list); check(list.executions == 1, "registration after many windows uses a new identity");
    Profiling::clear(); Profiling::mode = Profiling::Mode::TOP_COMMAND_LISTS;
    run(list); check(list.executions == 1 && command_lists_profiling.size() == 1, "TOP_COMMAND_LISTS registration mode");
    Profiling::clear(); Profiling::mode = Profiling::Mode::TOP_COMMANDS;
    run(list); run(list, true);
    check(command_lists_cmd_profiling.size() == 1 && command->pre_executions == 1 && command->post_executions == 1,
        "TOP_COMMANDS shares registration while tracking both phases separately");
    Profiling::clear(); Profiling::mode = Profiling::Mode::NONE;
    command->action = ProfileAction::Summary;
    run(list); check(command_lists_profiling.empty(), "NONE-to-SUMMARY mid-run never ends an unstarted list sample");
    Profiling::clear(); Profiling::mode = Profiling::Mode::SUMMARY;
    command->action = ProfileAction::Disabled;
    run(list); check(list.executions == 1, "SUMMARY-to-NONE completes the entry-mode sample safely");
    Profiling::clear(); Profiling::mode = Profiling::Mode::NONE;
    command->action = ProfileAction::TopCommands;
    run(list); check(command_lists_cmd_profiling.empty(), "NONE-to-TOP_COMMANDS cannot end an unstarted command sample");
    Profiling::clear(); Profiling::mode = Profiling::Mode::TOP_COMMANDS;
    command->action = ProfileAction::Disabled;
    run(list); check(command->pre_executions == 1, "TOP_COMMANDS-to-NONE uses start-time mode snapshot");
    Profiling::clear(); Profiling::mode = Profiling::Mode::SUMMARY;
    command->action = ProfileAction::Clear;
    run(list); check(command_lists_profiling.empty(), "in-flight sample from retired window cannot republish old registration");
    Profiling::clear();
}
void test_nested_and_concurrent()
{
    CommandList outer, inner;
    auto leaf = std::make_shared<MeasuredCommand>(); inner.commands.push_back(leaf);
    auto link = std::make_shared<RunLinkedCommandList>(&inner); outer.commands.push_back(link);
    Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear(); run(outer);
    check(outer.executions == 1 && inner.executions == 1 && command_lists_profiling.size() == 2 &&
        outer.time_spent_inclusive.QuadPart >= outer.time_spent_exclusive.QuadPart && outer.time_spent_exclusive.QuadPart >= 0,
        "nested production command traversal preserves inclusive/exclusive timing");
    CommandList shared; auto command = std::make_shared<MeasuredCommand>(); shared.commands.push_back(command);
    Profiling::clear();
    std::thread a([&] { for (int i = 0; i < 100; ++i) run(shared); });
    std::thread b([&] { for (int i = 0; i < 100; ++i) run(shared); });
    a.join(); b.join();
    check(command_lists_profiling.size() == 1 && shared.executions == 200 && command->calls == 200,
        "concurrent contexts synchronize registration and aggregate every list execution");
    Profiling::clear(); Profiling::mode = Profiling::Mode::TOP_COMMANDS;
    std::thread pre([&] { for (int i = 0; i < 100; ++i) run(shared); });
    std::thread post([&] { for (int i = 0; i < 100; ++i) run(shared, true); });
    pre.join(); post.join();
    check(command_lists_cmd_profiling.size() == 1 && command->pre_executions == 100 && command->post_executions == 100,
        "concurrent pre/post samples use per-run state and one shared registration");
    Profiling::clear();
}
void test_samples_and_summary()
{
    Profiling::mode = Profiling::Mode::SUMMARY; Profiling::clear();
    Profiling::Overhead local;
    {
        Profiling::Sample sample(local);
        Profiling::mode = Profiling::Mode::NONE;
        sample.Finish(true);
    }
    check(local.count == 1 && local.hits == 1 && local.cpu.QuadPart >= 0, "entry-mode sample remains paired after mode switch");
    Profiling::mode = Profiling::Mode::SUMMARY; local.clear();
    {
        Profiling::Sample sample(local);
        Profiling::clear(); sample.Finish(true);
    }
    check(local.count == 0 && local.hits == 0 && local.cpu.QuadPart == 0, "clear discards old-window CPU/count/hit updates");
    {
        Profiling::Sample sample(Profiling::texture_override_fuzzy_match_overhead); sample.Finish(true);
        Profiling::Sample lookup(Profiling::texture_override_candidates_lookup_overhead); lookup.Finish(false);
    }
    G->frame_no += 1; Profiling::interval = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Profiling::update_txt();
    const auto& text = Profiling::text;
    const auto rebuild = text.find(L"TO candidates rebuild");
    const auto lookup = text.find(L"TO candidates lookup");
    check(rebuild != std::wstring::npos && lookup != std::wstring::npos &&
        text.find(L"(1/1 hits/frame)", rebuild) < text.find(L'\n', rebuild) &&
        text.find(L"(0/1 hits/frame)", lookup) < text.find(L'\n', lookup) &&
        text.find(L"Texture update hits:") != std::wstring::npos && text.back() == L'\n',
        "summary contains complete separate rebuild/lookup rows and accurate hit/count semantics");
    check(Profiling::texture_override_fuzzy_match_overhead.count == 0 &&
        Profiling::texture_override_candidates_lookup_overhead.count == 0 &&
        Profiling::texture_override_fuzzy_match_overhead.cpu.QuadPart == 0 && Profiling::texture_override_fuzzy_match_overhead.hits == 0 &&
        Profiling::texture_override_candidates_lookup_overhead.cpu.QuadPart == 0 && Profiling::texture_override_candidates_lookup_overhead.hits == 0 &&
        Profiling::hash_tracking_overhead.hits == 0 && Profiling::hash_tracking_overhead.count == 0,
        "summary interval clear resets all new count/time/hit fields");
}
}
void test_dx11_profiling_generation_regression()
{
    try {
        ProfileFixture fixture; test_registration_and_modes(); test_nested_and_concurrent(); test_samples_and_summary();
        std::fputs("PASS: production profiling generation identity, concurrent registration, mode/window samples and complete summary rows\n", stderr);
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL profiling generation: %s\n", error.what()); std::exit(EXIT_FAILURE); }
}
