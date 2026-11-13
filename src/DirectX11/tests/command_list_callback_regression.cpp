#include "CommandList.h"
#include "HackerContext.h"
#include "globals.h"
#include <cstdio>
#include <cstdlib>

static void callback_check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL callback: %s\n", message); std::exit(EXIT_FAILURE); }
}

class CallbackCounterCommand : public CommandListCommand {
    unsigned& count;
public:
    explicit CallbackCounterCommand(unsigned& target) : count(target) {}
    void run(CommandListState*) override { ++count; }
};

static bool parse_callback(CommandList& commands, const wchar_t* dst, const wchar_t* source)
{
    const std::wstring ns;
    std::wstring value = source;
    return ParseCommandListGeneralCommands(L"CallbackRegression", dst, &value,
        &commands, &commands, &commands, &ns);
}

void test_dx11_command_list_callback_regression()
{
    // These commands use the real CPU command dispatcher; no GPU operation or
    // simulated resource dependencies are involved in the callback graph.
    HackerContext context(nullptr, nullptr);
    CommandListState state;
    state.mHackerContext = &context;
    const wchar_t* an = L"commandlistcallbackregressiona";
    const wchar_t* bn = L"commandlistcallbackregressionb";
    const wchar_t* cn = L"commandlistcallbackregressionc";
    callback_check(!explicitCommandListSections.count(an) && !explicitCommandListSections.count(bn) &&
        !explicitCommandListSections.count(cn), "unique callback fixture names");
    auto& a = explicitCommandListSections[an];
    auto& b = explicitCommandListSections[bn];
    auto& c = explicitCommandListSections[cn];
    unsigned a_pre = 0, a_post = 0, b_pre = 0, b_post = 0;
    a.command_list.commands.push_back(std::make_shared<CallbackCounterCommand>(a_pre));
    a.post_command_list.commands.push_back(std::make_shared<CallbackCounterCommand>(a_post));
    b.command_list.commands.push_back(std::make_shared<CallbackCounterCommand>(b_pre));
    b.post_command_list.commands.push_back(std::make_shared<CallbackCounterCommand>(b_post));
    CommandList copy;
    callback_check(parse_callback(copy, an, L"ref commandlistcallbackregressionb") && copy.commands.size() == 1,
        "parse callback copy");
    copy.commands[0]->run(&state);
    RunExplicitCommandList run;
    run.command_list_section = &a;
    run.run_pre_and_post_together = true;
    state.post = true;
    run.run(&state);
    callback_check(b_pre == 1 && b_post == 1 && a_pre == 0 && a_post == 0 && state.post,
        "callback forwards pre/post and restores caller phase");
    callback_check(!a.command_list.noop() && !run.noop(false, false, false), "runtime callback survives noop checks");
    c.command_list.clear();
    c.post_command_list.clear();
    CommandList flatten;
    callback_check(parse_callback(flatten, cn, L"ref commandlistcallbackregressiona"), "parse proxy of proxy");
    callback_check(c.command_list.commands.empty() && c.post_command_list.commands.empty() &&
        !c.command_list.noop() && !c.post_command_list.noop(),
        "parsed but unpopulated callback retains empty pre/post lists");
    RunExplicitCommandList empty_explicit;
    empty_explicit.command_list_section = &c;
    empty_explicit.run_pre_and_post_together = true;
    callback_check(!empty_explicit.noop(false, false, false),
        "explicit empty runtime callback is not removed by noop optimization");
    empty_explicit.run_pre_and_post_together = false;
    callback_check(!empty_explicit.noop(false, false, false) && !empty_explicit.noop(true, false, false),
        "individual empty pre/post callback phases remain reachable");
    CommandList linked_container;
    const std::wstring link_label = L"empty callback regression link";
    auto empty_link = LinkCommandLists(&linked_container, &c.command_list, &link_label);
    callback_check(!empty_link->noop(false, false, false),
        "linked empty runtime callback remains reachable before assignment executes");
    flatten.commands[0]->run(&state);
    const unsigned before_link = b_pre;
    empty_link->run(&state);
    callback_check(b_pre == before_link + 1,
        "retained empty callback link executes populated source after runtime assignment");
    linked_container.clear();
    CommandList reset;
    callback_check(parse_callback(reset, an, L"null"), "parse callback reset");
    reset.commands[0]->run(&state);
    callback_check(a.command_list.ResolveCommandList() == &a.command_list &&
        c.command_list.ResolveCommandList() == &b.command_list, "null restores original; flatten preserves captured root");
    run.run(&state);
    callback_check(a_pre == 1 && a_post == 1, "reset executes original content");
    callback_check(!b.command_list.SetSourceCommandList(&c.command_list), "flattened two-node cycle rejected");
    // A post-only cycle must not commit the valid pre half. Reusing the same
    // command after fixing the graph must succeed rather than staying failed.
    b.post_command_list.SetSourceCommandList(&a.post_command_list);
    copy.commands[0]->run(&state);
    callback_check(a.command_list.ResolveCommandList() == &a.command_list &&
        a.post_command_list.ResolveCommandList() == &a.post_command_list, "pre/post update is atomic on rejection");
    b.post_command_list.SetSourceCommandList(nullptr);
    copy.commands[0]->run(&state);
    callback_check(a.command_list.ResolveCommandList() == &b.command_list &&
        a.post_command_list.ResolveCommandList() == &b.post_command_list, "callback retries after cycle removed");
    for (const wchar_t* invalid : {L"nulljunk", L"copy commandlistcallbackregressionb", L"ref", L"resourcefoo",
        L"ref commandlistmissingcallback", L"ref commandlistcallbackregressionb garbage", L""}) {
        CommandList rejected;
        callback_check(!parse_callback(rejected, an, invalid) && rejected.commands.empty(), "invalid callback does not queue");
    }
    a.command_list.clear();
    callback_check(a.command_list.ResolveCommandList() == &a.command_list && a.command_list.noop() &&
        !a.command_list.runtime_populated, "clear removes proxy and runtime flags for reload");
    // Linked lists use recursive=false for indentation, so a separate depth
    // guard must still bound accidental callback/linked execution cycles.
    CommandList recursive;
    unsigned visits = 0;
    recursive.commands.push_back(std::make_shared<CallbackCounterCommand>(visits));
    recursive.commands.push_back(std::make_shared<RunLinkedCommandList>(&recursive));
    RunLinkedCommandList linked(&recursive);
    linked.run(&state);
    callback_check(visits == MAX_COMMAND_LIST_RECURSION && state.command_list_depth == 0,
        "all dispatch paths honor 256-depth guard and unwind state");
    recursive.clear();
    explicitCommandListSections.erase(an);
    explicitCommandListSections.erase(bn);
    explicitCommandListSections.erase(cn);
    std::fputs("PASS: real callback parser, forwarding, atomic cycle rejection and depth limit\n", stderr);
}
