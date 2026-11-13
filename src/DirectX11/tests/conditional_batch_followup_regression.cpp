#include "slot_range_test_support.h"
#include "ExpressionFunctions.h"
#include "util.h"
#include <d3dcompiler.h>
#include <optional>
#include <mutex>

extern std::vector<std::shared_ptr<CommandList>> dynamically_allocated_command_lists;

namespace {
using namespace dx11_slot_test;

class FlowProgram {
    size_t registered = registered_command_lists.size();
    size_t allocated = dynamically_allocated_command_lists.size();
    CommandListScope scope;
public:
    CommandList pre, post;
    FlowProgram()
    {
        scope.emplace_front();
        pre.scope = post.scope = &scope;
        pre.post = false; post.post = true;
    }
    ~FlowProgram() { release_parser_roots(); }
    void flow(const std::wstring& text)
    {
        const std::wstring ns;
        std::wstring parsed = text;
        // Normal INI loading lowercases command lines before this internal
        // parser. Preserve that production preprocessing in the fixture.
        std::transform(parsed.begin(), parsed.end(), parsed.begin(), ::towlower);
        const bool okay = ParseCommandListFlowControl(L"CommandListConditionalFollowup", &parsed, &pre, &post, &ns);
        if (!okay) std::fprintf(stderr, "conditional followup: rejected flow line: %ls\n", text.c_str());
        require_batch(okay, "conditional followup: parse real if/elif/else/endif");
    }
    void copy(const std::wstring& destination, std::wstring source, bool after = false)
    {
        const std::wstring ns;
        require_batch(ParseCommandListResourceCopyTargetDirective(L"CommandListConditionalFollowup", destination.c_str(),
            &source, after ? &post : &pre, &ns), "conditional followup: parse real branch copy");
    }
    void assign(const std::wstring& destination, std::wstring source)
    {
        const std::wstring ns, raw = destination + L" = " + source;
        require_batch(ParseCommandListVariableAssignment(L"CommandListConditionalFollowup", destination.c_str(), &source,
            &raw, &pre, &pre, &post, &ns), "conditional followup: parse real local/global assignment");
    }
    void finish()
    {
        require_batch(std::distance(scope.begin(), scope.end()) == 1,
            "conditional followup: all parsed branch scopes close");
        pre.scope = post.scope = nullptr;
        scope.clear(); // Runtime operands must own/use stable variables, not this parsing scope.
    }
    void release_parser_roots()
    {
        registered_command_lists.resize(registered);
        dynamically_allocated_command_lists.resize(allocated);
    }
    void merge()
    {
        merge_shader_resource_batches(&pre);
        merge_shader_resource_batches(&post);
    }
};

ShaderResourceBatchStats run_phase(FlowProgram& program, BatchDevice& device, bool post)
{
    const bool original = device.state.post;
    device.state.post = post;
    try {
        auto result = execute(post ? program.post : program.pre, device);
        device.state.post = original;
        return result;
    } catch (...) { device.state.post = original; throw; }
}

void test_nonzero_duplicates_and_phases(BatchDevice& device, Registry& registry,
    ID3D11Resource* a, ID3D11Resource* b, ID3D11ShaderResourceView* sentinel)
{
    auto& choice = registry.variable(L"$conditional_follow_choice", 0);
    auto& skip = registry.variable(L"$conditional_follow_skip", 0);
    for (wchar_t stage : std::wstring(L"vhdgpc")) {
        const auto slot = std::wstring(1, stage) + L"s-t";
        for (bool post : {false, true}) {
            FlowProgram program;
            program.flow(L"if $conditional_follow_choice == 1");
            program.copy(slot + L"3", L"ref resourceconditional_follow_a", post);
            program.flow(L"elif $conditional_follow_choice == 2");
            program.copy(slot + L"3", L"ref resourceconditional_follow_b", post);
            program.flow(L"else");
            program.copy(slot + L"3", L"null", post);
            program.flow(L"endif");
            program.copy(slot + L"7", L"ref resourceconditional_follow_b", post);
            program.flow(L"if $conditional_follow_skip");
            program.copy(slot + L"3", L"ref unless_null resourceconditional_follow_empty", post);
            program.flow(L"endif");
            program.finish();
            program.merge();
            program.merge();
            const auto& active = post ? program.post : program.pre;
            require_batch(active.commands.size() == 1 && dynamic_cast<ShaderResourceBindBatch*>(active.commands[0].get()),
                "conditional followup: nonzero duplicate/gapped operations become one real bind batch");
            for (int selected : {1, 2, 0}) {
                choice.fval = static_cast<float>(selected);
                skip.fval = selected ? 1.0f : 0.0f;
                device.context->ClearState();
                for (UINT i = 3; i <= 7; ++i) set_slot(device, stage, i, sentinel);
                const auto stats = run_phase(program, device, post);
                const auto expected = selected == 1 ? a : selected == 2 ? b : nullptr;
                require_batch(bound_resource(device, stage, 3).Get() == expected && bound_resource(device, stage, 7).Get() == b,
                    "conditional followup: branch selection and later duplicate null-skip preserve ordered writes at slots3/7");
                for (UINT i = 4; i < 7; ++i)
                    require_batch(get_slot(device, stage, i).Get() == sentinel,
                        "conditional followup: gap slots preserve original native view identity");
                require_batch(stats.get_calls == 1 && stats.set_calls == 1,
                    "conditional followup: gapped conditional bind uses lazy seed once and publishes once");
                // The same If is present in both phase roots. Running its empty
                // half must not execute the active half again after a draw.
                set_slot(device, stage, 3, sentinel);
                const auto inactive = run_phase(program, device, !post);
                require_batch(get_slot(device, stage, 3).Get() == sentinel && inactive.set_calls == 0,
                    "conditional followup: shared if in opposite phase does not repeat active assignments");
            }
        }
    }
}

void test_conditional_fetch_duplicates(BatchDevice& device, Registry& registry, ID3D11Resource* a,
    ID3D11Resource* b, ID3D11ShaderResourceView* a_view, ID3D11ShaderResourceView* b_view)
{
    auto& choice = registry.variable(L"$conditional_fetch_choice", 0);
    auto& left = registry.resource(L"resourceconditional_fetch_left", device, b);
    auto& right = registry.resource(L"resourceconditional_fetch_right", device, b);
    auto& tail = registry.resource(L"resourceconditional_fetch_tail", device);
    FlowProgram program;
    program.flow(L"if $conditional_fetch_choice == 1");
    program.copy(left.name, L"ref ps-t7");
    program.flow(L"elif $conditional_fetch_choice == 2");
    program.copy(right.name, L"ref ps-t7");
    program.flow(L"endif");
    program.copy(tail.name, L"ref ps-t3");
    program.flow(L"if $conditional_fetch_choice == 1");
    program.copy(left.name, L"ref unless_null ps-t7");
    program.flow(L"endif");
    program.finish(); program.merge(); program.merge();
    require_batch(program.pre.commands.size() == 1 && dynamic_cast<ShaderResourceFetchBatch*>(program.pre.commands[0].get()),
        "conditional followup: ordered duplicate source slots use a real fetch batch");
    for (int selected : {1, 2, 0}) {
        assign(device, &left, b); assign(device, &right, b);
        choice.fval = static_cast<float>(selected);
        set_slot(device, L'p', 7, a_view); set_slot(device, L'p', 3, b_view);
        const auto stats = run_phase(program, device, false);
        require_batch(left.resource == (selected == 1 ? a : b) && right.resource == (selected == 2 ? a : b) && tail.resource == b,
            "conditional followup: fetch selects branch-specific destination without default-slot-zero indexing");
        require_batch(stats.get_calls == 1 && stats.set_calls == 0 && get_slot(device, L'p', 7).Get() == a_view,
            "conditional followup: fetch reads native span once and never changes source binding");
    }
    choice.fval = 1;
    set_slot(device, L'p', 7, nullptr);
    run_phase(program, device, false);
    require_batch(left.is_null, "conditional followup: ordinary null fetch clears destination before duplicate unless_null skip");
}

void test_random_and_source_lifetime(BatchDevice& device, ID3D11Resource* a, ID3D11Resource* b)
{
    FlowProgram random;
    random.flow(L"if random(1) < 0");
    random.copy(L"ps-t3", L"ref resourceconditional_follow_b");
    random.flow(L"elif random(1) >= 0");
    random.copy(L"ps-t3", L"ref resourceconditional_follow_a");
    random.flow(L"else");
    random.copy(L"ps-t3", L"null");
    random.flow(L"endif");
    random.copy(L"ps-t4", L"ref resourceconditional_follow_b");
    auto original = std::dynamic_pointer_cast<IfCommand>(random.pre.commands[0]);
    require_batch(original != nullptr, "conditional followup: flow parser creates real If before folding");
    const auto before = GetExpressionRandomSampleCount();
    original->optimise(device.device);
    original->noop(false, false, false);
    random.finish(); random.merge(); random.merge();
    require_batch(GetExpressionRandomSampleCount() == before,
        "conditional followup: expression optimization/static probes and repeat merging do not sample random");
    for (unsigned iteration = 0; iteration < 3; ++iteration) {
        const auto samples = GetExpressionRandomSampleCount();
        const auto stats = run_phase(random, device, false);
        require_batch(GetExpressionRandomSampleCount() - samples == 2 && stats.get_calls == 0 && stats.set_calls == 1,
            "conditional followup: each encountered random if/elif condition samples exactly once per runtime execution");
        require_batch(bound_resource(device, L'p', 3).Get() == a && bound_resource(device, L'p', 4).Get() == b,
            "conditional followup: random chain dispatches the expected real branch");
    }

    FlowProgram lifetime;
    lifetime.assign(L"local $conditional_follow_local", L"1");
    lifetime.flow(L"if $conditional_follow_local");
    lifetime.copy(L"ps-t3", L"ref resourceconditional_follow_a");
    lifetime.flow(L"else");
    lifetime.copy(L"ps-t3", L"ref resourceconditional_follow_b");
    lifetime.flow(L"endif");
    lifetime.copy(L"ps-t4", L"ref resourceconditional_follow_b");
    std::weak_ptr<CommandListCommand> original_if = lifetime.pre.commands[1];
    lifetime.finish(); lifetime.merge();
    lifetime.post.commands.clear();
    lifetime.release_parser_roots();
    require_batch(!original_if.expired(), "conditional followup: folded source_if owns conditions after parser roots release");
    run_phase(lifetime, device, false);
    require_batch(bound_resource(device, L'p', 3).Get() == a,
        "conditional followup: local variable remains valid after lexical scope and parser roots are gone");
    lifetime.pre.commands.clear();
    require_batch(original_if.expired(), "conditional followup: clearing actual batch releases source_if ownership");
}

void test_pipeline_barriers_and_unbatched_identity(BatchDevice& device, Registry& registry,
    ID3D11Resource* a, ID3D11Resource* b)
{
    auto& condition = registry.variable(L"$conditional_barrier_condition", 1);
    (void)condition;
    for (const auto* expression : {L"ps-t3->Width == 4", L"abs(2 * ps-t3->Width - 8) == 0", L"1 / ps-t3 > 0"}) {
        device.context->ClearState();
        FlowProgram program;
        program.copy(L"ps-t3", L"ref resourceconditional_follow_a");
        program.flow(std::wstring(L"if ") + expression);
        program.copy(L"ps-t4", L"ref resourceconditional_follow_a");
        program.flow(L"else");
        program.copy(L"ps-t4", L"ref resourceconditional_follow_b");
        program.flow(L"endif");
        program.copy(L"ps-t5", L"ref resourceconditional_follow_b");
        auto original_if = program.pre.commands[1];
        program.finish(); program.merge(); program.merge();
        require_batch(program.pre.commands.size() == 3 && program.pre.commands[1] == original_if,
            "conditional followup: metadata/function/resource-filter condition remains its original pipeline barrier");
        run_phase(program, device, false);
        require_batch(bound_resource(device, L'p', 3).Get() == a && bound_resource(device, L'p', 4).Get() == a &&
            bound_resource(device, L'p', 5).Get() == b,
            "conditional followup: pipeline condition sees the preceding actual SRV assignment");
    }
    for (bool stage_break : {false, true}) {
        FlowProgram program;
        program.flow(L"if $conditional_barrier_condition");
        program.copy(L"ps-t3", L"ref resourceconditional_follow_a");
        program.flow(L"endif");
        if (!stage_break) program.assign(L"$conditional_barrier_condition", L"1");
        program.flow(L"if $conditional_barrier_condition");
        program.copy(stage_break ? L"vs-t7" : L"ps-t7", L"ref resourceconditional_follow_b");
        program.flow(L"endif");
        const auto first = program.pre.commands.front(), last = program.pre.commands.back();
        program.finish(); program.merge(); program.merge();
        require_batch(program.pre.commands.front() == first && program.pre.commands.back() == last,
            "conditional followup: stage/unrelated-command singleton groups restore original If after repeat optimizer");
        run_phase(program, device, false);
        require_batch(bound_resource(device, L'p', 3).Get() == a &&
            bound_resource(device, stage_break ? L'v' : L'p', 7).Get() == b,
            "conditional followup: restored unbatched If commands retain runtime behavior");
    }
}

class ShaderConditionScope {
    BatchDevice& device;
    UINT64 hash;
    UINT hunting = G->hunting;
    ShaderHashType old_hash = G->shader_hash_type;
    std::wstring shader_path = G->SHADER_PATH, cache_path = G->SHADER_CACHE_PATH;
    std::optional<ShaderBindings> saved;
public:
    ComPtr<ID3D11PixelShader> shader;
    ShaderConditionScope(BatchDevice& device, UINT64 hash) : device(device), hash(hash)
    {
        require_batch(!G->mShaderOverrideMap.count(hash), "conditional followup: unique actual shader filter fixture");
        {
            std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
            auto it = G->mShaderBindingsCache.find(hash);
            if (it != G->mShaderBindingsCache.end()) saved = it->second;
        }
        G->hunting = 0;
        G->shader_hash_type = ShaderHashType::FNV;
        G->SHADER_PATH[0] = G->SHADER_CACHE_PATH[0] = 0; // No shader files are read or written.
    }
    ~ShaderConditionScope()
    {
        device.wrapper->PSSetShader(nullptr, nullptr, 0);
        if (shader) CleanupShaderMaps(shader.Get());
        shader.Reset();
        G->mShaderOverrideMap.erase(hash);
        {
            std::lock_guard<std::mutex> guard(G->mShaderBindingsMutex);
            if (saved) G->mShaderBindingsCache[hash] = *saved;
            else G->mShaderBindingsCache.erase(hash);
        }
        G->hunting = hunting;
        G->shader_hash_type = old_hash;
        wcscpy_s(G->SHADER_PATH, shader_path.c_str());
        wcscpy_s(G->SHADER_CACHE_PATH, cache_path.c_str());
    }
};

void test_shader_condition_can_batch(BatchDevice& device, ID3D11Resource* a, ID3D11Resource* b)
{
    const char hlsl[] = "float4 main():SV_Target{return float4(0.21875,0.3125,0.46875,1);}";
    ComPtr<ID3DBlob> bytecode, errors;
    require_batch(SUCCEEDED(D3DCompile(hlsl, sizeof(hlsl) - 1, "ConditionalBatchShader", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &bytecode, &errors)), "conditional followup: compile actual condition shader");
    const auto hash = fnv_64_buf(bytecode->GetBufferPointer(), bytecode->GetBufferSize());
    ShaderConditionScope scope(device, hash);
    require_batch(SUCCEEDED(device.device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr,
        &scope.shader)), "conditional followup: create and register actual pixel shader");
    G->mShaderOverrideMap[hash].filter_index = 23;
    device.wrapper->PSSetShader(scope.shader.Get(), nullptr, 0);
    ComPtr<ID3D11PixelShader> actual;
    device.context->PSGetShader(&actual, nullptr, nullptr);
    require_batch(actual.Get() == scope.shader.Get(), "conditional followup: real shader is bound before filter evaluation");
    FlowProgram program;
    program.flow(L"if ps == 23");
    program.copy(L"ps-t3", L"ref resourceconditional_follow_a");
    program.flow(L"else");
    program.copy(L"ps-t3", L"ref resourceconditional_follow_b");
    program.flow(L"endif");
    program.copy(L"ps-t4", L"ref resourceconditional_follow_b");
    program.finish(); program.merge(); program.merge();
    require_batch(program.pre.commands.size() == 1 && dynamic_cast<ShaderResourceBindBatch*>(program.pre.commands[0].get()),
        "conditional followup: legitimate SHADER condition remains eligible as upstream TEXTURE-only barrier requires");
    const auto stats = run_phase(program, device, false);
    require_batch(bound_resource(device, L'p', 3).Get() == a && bound_resource(device, L'p', 4).Get() == b &&
        stats.get_calls == 0 && stats.set_calls == 1,
        "conditional followup: actual shader filter controls branch with one Set and no unnecessary Get");
}

class ConditionalLogFailure final : public std::runtime_error {
public:
    ConditionalLogFailure() : std::runtime_error("intentional conditional batch virtual logging failure") {}
};

class ConditionalThrowContext final : public HackerContext {
    unsigned calls = 0, throw_on = 0;
public:
    ConditionalThrowContext(ID3D11Device1* device, ID3D11DeviceContext1* context) : HackerContext(device, context) {}
    void Arm(unsigned target) { calls = 0; throw_on = target; }
    unsigned Calls() const { return calls; }
    void FrameAnalysisLog(char*, ...) override
    {
        if (++calls == throw_on) {
            throw_on = 0;
            throw ConditionalLogFailure();
        }
    }
};

class ConditionalExceptionDevice : public BatchDevice {
public:
    ConditionalThrowContext* throwing;
    ConditionalExceptionDevice()
    {
        auto context = std::make_unique<ConditionalThrowContext>(native.Get(), this->context.Get());
        context->SetHackerDevice(device);
        throwing = context.get();
        wrapper = std::move(context);
        device->SetHackerContext(wrapper.get());
        state.mHackerContext = wrapper.get();
    }
};

class ConditionalAnalysisScope {
    bool previous = G->analyse_frame;
public:
    ConditionalAnalysisScope() { G->analyse_frame = true; }
    ~ConditionalAnalysisScope() { G->analyse_frame = previous; }
};

ULONG reference_count(IUnknown* object)
{
    object->AddRef();
    return object->Release();
}

void test_conditional_exception_prefix()
{
    ConditionalExceptionDevice device;
    Registry registry;
    auto a = texture(device, 0x12345678u), b = texture(device, 0x23456789u), old = texture(device, 0x34567890u);
    auto old_view = srv(device, old.Get());
    registry.resource(L"resourceconditional_exception_a", device, a.Get());
    registry.resource(L"resourceconditional_exception_b", device, b.Get());
    registry.variable(L"$conditional_exception_true", 1);
    registry.variable(L"$conditional_exception_false", 0);
    const auto a_refs = reference_count(a.Get()), b_refs = reference_count(b.Get()), old_refs = reference_count(old_view.Get());
    ConditionalAnalysisScope analysis;
    FlowProgram program;
    program.flow(L"if $conditional_exception_true");
    program.copy(L"ps-t3", L"ref resourceconditional_exception_a");
    program.flow(L"endif");
    program.flow(L"if $conditional_exception_false");
    program.copy(L"ps-t4", L"ref resourceconditional_exception_b");
    program.flow(L"endif");
    program.flow(L"if $conditional_exception_true");
    program.copy(L"ps-t5", L"ref resourceconditional_exception_b");
    program.flow(L"endif");
    program.finish(); program.merge();
    auto batch = std::dynamic_pointer_cast<ShaderResourceBindBatch>(program.pre.commands.front());
    require_batch(batch && batch->operations.size() == 3, "conditional followup: exception uses real parsed folded operations");
    // Warm the first successful branch's cache, then count its real log calls
    // plus the skipped branch. No text is matched and no copy logic replaced.
    for (unsigned pass = 0; pass < 2; ++pass) {
        device.throwing->Arm(0);
        for (unsigned i = 0; i < 2; ++i) {
            DeferredBinding sink;
            batch->operations[i]->RunWithBinding(&device.state, &sink);
        }
    }
    const unsigned prefix_logs = device.throwing->Calls();
    require_batch(prefix_logs > 0, "conditional followup: actual virtual logging boundary was exercised");
    for (UINT slot = 3; slot <= 5; ++slot) set_slot(device, L'p', slot, old_view.Get());
    device.throwing->Arm(prefix_logs + 1); // Third operation's first real log.
    bool caught = false;
    try { run_phase(program, device, false); }
    catch (const ConditionalLogFailure&) { caught = true; }
    require_batch(caught && bound_resource(device, L'p', 3).Get() == a.Get() &&
        get_slot(device, L'p', 4).Get() == old_view.Get() && get_slot(device, L'p', 5).Get() == old_view.Get(),
        "conditional followup: exception flushes successful prefix and preserves skipped/unexecuted native views");
    const auto recovery = run_phase(program, device, false);
    require_batch(bound_resource(device, L'p', 5).Get() == b.Get() && recovery.get_calls == 1 && recovery.set_calls == 1,
        "conditional followup: same real batch recovers after exception with its skipped view intact");
    device.context->ClearState();
    batch.reset();
    program.pre.commands.clear(); program.post.commands.clear(); program.release_parser_roots();
    require_batch(reference_count(a.Get()) == a_refs && reference_count(b.Get()) == b_refs && reference_count(old_view.Get()) == old_refs,
        "conditional followup: successful/exceptional invocation views and branch caches release actual COM references");
}
} // namespace

void test_dx11_conditional_batch_followup_regression()
{
    {
        BatchDevice device;
        Registry registry;
        auto a = texture(device, 0x11223344u, 4), b = texture(device, 0x55667788u, 9), sentinel = texture(device, 0x99aabbccu, 2);
        auto a_view = srv(device, a.Get()), b_view = srv(device, b.Get()), old_view = srv(device, sentinel.Get());
        registry.resource(L"resourceconditional_follow_a", device, a.Get());
        registry.resource(L"resourceconditional_follow_b", device, b.Get());
        registry.resource(L"resourceconditional_follow_empty", device);
        test_nonzero_duplicates_and_phases(device, registry, a.Get(), b.Get(), old_view.Get());
        test_conditional_fetch_duplicates(device, registry, a.Get(), b.Get(), a_view.Get(), b_view.Get());
        test_random_and_source_lifetime(device, a.Get(), b.Get());
        test_pipeline_barriers_and_unbatched_identity(device, registry, a.Get(), b.Get());
        test_shader_condition_can_batch(device, a.Get(), b.Get());
        device.context->ClearState();
    }
    test_conditional_exception_prefix();
}
