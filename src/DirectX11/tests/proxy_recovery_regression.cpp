#include "CommandList.h"
#include "HackerContext.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void recovery_check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL proxy recovery: %s\n", message); std::exit(EXIT_FAILURE); }
}

// The real CPU dispatcher uses this virtual log sink; no GPU dependency is
// substituted. Observe whether copy_desc executes past an old failed ref cache.
class ProxyRecoveryLogContext : public HackerContext {
public:
    unsigned metadata_copies = 0;
    ProxyRecoveryLogContext() : HackerContext(nullptr, nullptr) {}
    void FrameAnalysisLog(char* format, ...) override
    {
        if (std::strstr(format, "copying pool metadata")) ++metadata_copies;
    }
};

static CustomResourcePool& recovery_pool(const std::wstring& name)
{
    recovery_check(!customResourcePools.count(name) && !customResources.count(name + L"_template"),
        "unique recovery pool fixture");
    auto& pool = customResourcePools[name];
    pool.name = name;
    pool.resource_template = &customResources[name + L"_template"];
    pool.resource_template->pool = &pool;
    pool.variable_template = std::make_unique<CommandListVariable>(name + L"_template", 0, VariableFlags::NONE);
    pool.lazy_initialization = true;
    pool.index_type = PoolIndexType::RING;
    pool.Initialize(1);
    return pool;
}

void test_dx11_proxy_recovery_regression()
{
    ProxyRecoveryLogContext context;
    CommandListState state; state.mHackerContext = &context;
    const std::wstring ns;
    const wchar_t* an = L"commandlistproxyrecoverya";
    const wchar_t* bn = L"commandlistproxyrecoveryb";
    recovery_check(!explicitCommandListSections.count(an) && !explicitCommandListSections.count(bn),
        "unique recovery callback fixtures");
    auto& a = explicitCommandListSections[an];
    auto& b = explicitCommandListSections[bn];
    CommandList callbacks;
    std::wstring source = L"ref commandlistproxyrecoveryb";
    recovery_check(ParseCommandListGeneralCommands(L"ProxyRecovery", an, &source, &callbacks,
        &callbacks, &callbacks, &ns), "parse real callback recovery command");
    auto* callback = dynamic_cast<CopyCommandListCommand*>(callbacks.commands.front().get());
    recovery_check(callback != nullptr, "real callback operation type");
    b.post_command_list.SetSourceCommandList(&a.post_command_list);
    callback->run(&state);
    callback->run(&state);
    recovery_check(a.command_list.ResolveCommandList() == &a.command_list &&
        a.post_command_list.ResolveCommandList() == &a.post_command_list,
        "repeated post-only cycle rejection never commits pre half");
    b.post_command_list.SetSourceCommandList(nullptr);
    callback->run(&state);
    recovery_check(a.command_list.ResolveCommandList() == &b.command_list &&
        a.post_command_list.ResolveCommandList() == &b.post_command_list,
        "post-root-only change recovers same callback command");
    a.command_list.clear(); a.post_command_list.clear();
    b.command_list.SetSourceCommandList(&a.command_list);
    callback->run(&state); callback->run(&state);
    b.command_list.clear();
    callback->run(&state);
    recovery_check(a.command_list.ResolveCommandList() == &b.command_list,
        "clearing cyclic source recovers cached callback failure");
    callback->src = nullptr;
    callback->run(&state);
    recovery_check(a.command_list.ResolveCommandList() == &a.command_list &&
        a.post_command_list.ResolveCommandList() == &a.post_command_list,
        "null source removes callback without sticky failed state");

    const std::wstring pa = L"poolproxyrecoverya", pb = L"poolproxyrecoveryb";
    auto& first = recovery_pool(pa);
    auto& second = recovery_pool(pb);
    CommandList copies;
    std::wstring rhs = L"ref " + pb;
    recovery_check(ParseCommandListResourceCopyTargetDirective(L"ProxyRecovery", pa.c_str(), &rhs,
        &copies, &ns), "parse real pool recovery command");
    auto* pool_copy = dynamic_cast<PoolCopyOperation*>(copies.commands.front().get());
    recovery_check(pool_copy != nullptr, "real pool copy operation type");
    second.SetSourcePool(&first);
    pool_copy->run(&state); pool_copy->run(&state);
    recovery_check(first.ResolvePool() == &first, "repeated pool cycle rejection preserves destination");
    // The same production operation's public mode can change: an old failed
    // reference root must never prevent a metadata copy from running.
    pool_copy->options = ResourceCopyOptions::COPY_DESC;
    pool_copy->run(&state);
    recovery_check(context.metadata_copies == 1, "copy_desc executes despite cached same failed root");
    pool_copy->options = ResourceCopyOptions::REFERENCE;
    second.SetSourcePool(nullptr);
    pool_copy->run(&state);
    recovery_check(first.ResolvePool() == &second, "source root change recovers same pool copy command");
    first.SetSourcePool(nullptr);
    second.SetSourcePool(&first);
    pool_copy->run(&state); pool_copy->run(&state);
    second.ResetPool(false);
    pool_copy->run(&state);
    recovery_check(first.ResolvePool() == &second, "source pool reset clears cycle and permits retry");
    callbacks.clear(); copies.clear();
    explicitCommandListSections.erase(an); explicitCommandListSections.erase(bn);
    customResourcePools.erase(pa); customResourcePools.erase(pb);
    customResources.erase(pa + L"_template"); customResources.erase(pb + L"_template");
    std::fputs("PASS: callback/pool failed-root recovery and metadata-copy bypass\n", stderr);
}
