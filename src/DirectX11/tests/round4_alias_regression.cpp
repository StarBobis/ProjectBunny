#include "slot_range_test_support.h"
#include "ExpressionFunctions.h"

namespace {
using namespace dx11_slot_test;
void replay_random(uint32_t desired)
{
    // SeedExpressionRandom deliberately preserves the process-wide call count.
    // Compensate that count so the next real random input is replayable without
    // mutating or resetting the production counter.
    SeedExpressionRandom(desired - 0x9e3779b9u * GetExpressionRandomSampleCount(), 23);
}
unsigned choose(BatchDevice& device)
{
    const std::wstring text=L"random(2)", ns;
    CommandListScope scope;
    scope.emplace_front();
    CommandListExpression expression;
    require_batch(expression.parse(&text,&ns,&scope),"round4 alias: parse selector oracle");
    const auto before=GetExpressionRandomSampleCount();
    const auto result=static_cast<unsigned>(expression.evaluate(&device.state)) % 2;
    require_batch(GetExpressionRandomSampleCount()==before+1,"round4 alias: oracle consumes one actual sample");
    return result;
}
}

void test_dx11_round4_alias_regression()
{
    using namespace dx11_slot_test;
    BatchDevice device;
    Registry registry;
    auto a=buffer(device,64,D3D11_BIND_VERTEX_BUFFER);
    auto b=buffer(device,128,D3D11_BIND_VERTEX_BUFFER);
    auto& source=registry.resource(L"resourceround4aliasa",device,a.Get(),DXGI_FORMAT_R32_FLOAT,4,64);
    auto& sentinel=registry.resource(L"resourceround4aliasb",device,b.Get(),DXGI_FORMAT_R32_FLOAT,4,128);
    source.original_format=DXGI_FORMAT_R32_FLOAT; source.original_dhash=0x123456789abcdef0ull;
    sentinel.original_format=DXGI_FORMAT_R16_UINT; sentinel.original_dhash=7;
    auto& destination=registry.pool(L"poolround4aliasdst",device,PoolIndexType::RING,2);
    destination.PropagateFlags(D3D11_BIND_VERTEX_BUFFER,static_cast<D3D11_RESOURCE_MISC_FLAG>(0));
    unsigned seed=1, selected=0;
    for (;seed<100;++seed) {
        replay_random(seed);
        selected=choose(device);
        if (selected != choose(device)) break;
    }
    require_batch(seed<100,"round4 alias: deterministic seed selects different first/second elements");
    bool all_correct=true;
    for (const auto* operation : {L"ref ",L"copy ",L"copy_desc ",L""}) {
        for (unsigned i=0;i<2;++i)
            scalar(device,destination.name+L"["+std::to_wstring(i)+L"]",L"ref resourceround4aliasb");
        CommandList commands;
        const std::wstring input=*operation ? std::wstring(operation)+source.name : L"null";
        append(commands,destination.name+L"[random(2)]",input);
        replay_random(seed);
        const auto before=GetExpressionRandomSampleCount();
        RunCommandList(device.device,device.wrapper.get(),&commands,nullptr,false);
        const auto samples=GetExpressionRandomSampleCount()-before;
        auto* chosen=destination.GetResource(static_cast<float>(selected),false,true,false);
        auto* other=destination.GetResource(static_cast<float>(1-selected),false,true,false);
        const bool null_operation=!*operation;
        bool correct=samples==1 && chosen && other && other->resource==b.Get() &&
            other->original_dhash==7 && !other->is_null;
        if (null_operation) correct &= chosen && chosen->is_null;
        else correct &= chosen && !chosen->is_null && chosen->resource!=b.Get() &&
            chosen->original_format==source.original_format && chosen->original_dhash==source.original_dhash;
        if (operation[0]==L'r') correct &= chosen && chosen->resource==a.Get();
        std::fprintf(stderr,"round4 alias assignment %ls: samples=%u selected=%u aligned=%d\n",
            operation,samples,selected,correct);
        all_correct &= correct;
    }
    auto& source_pool=registry.pool(L"poolround4aliassrc",device,PoolIndexType::RING,2);
    scalar(device,source_pool.name+L"[0]",L"ref resourceround4aliasa");
    scalar(device,source_pool.name+L"[1]",L"ref resourceround4aliasb");
    for (unsigned i=0;i<2;++i)
        scalar(device,destination.name+L"["+std::to_wstring(i)+L"]",L"ref resourceround4aliasb");
    replay_random(seed);
    const auto source_index=choose(device), destination_index=choose(device);
    CommandList both;
    append(both,destination.name+L"[random(2)]",L"ref "+source_pool.name+L"[random(2)]");
    replay_random(seed);
    const auto before=GetExpressionRandomSampleCount();
    RunCommandList(device.device,device.wrapper.get(),&both,nullptr,false);
    auto* actual=destination.GetResource(static_cast<float>(destination_index),false,true,false);
    const auto samples=GetExpressionRandomSampleCount()-before;
    all_correct &= samples==2 && actual && actual->resource==(source_index?b.Get():a.Get()) &&
        actual->original_dhash==(source_index?7:source.original_dhash);
    std::fprintf(stderr,"round4 alias dynamic source/destination: samples=%u\n",samples);
    // A failed selector must be consumed once and must not mutate a real slot.
    for (const auto* operation : {L"ref ",L"copy ",L"copy_desc ",L""}) {
        for (unsigned i=0;i<2;++i)
            scalar(device,destination.name+L"["+std::to_wstring(i)+L"]",L"ref resourceround4aliasb");
        CommandList missing;
        append(missing,destination.name+L"[random(2)/0]",
            *operation ? std::wstring(operation)+source.name : L"null");
        const auto start=GetExpressionRandomSampleCount();
        RunCommandList(device.device,device.wrapper.get(),&missing,nullptr,false);
        all_correct &= GetExpressionRandomSampleCount()==start+1;
        for (unsigned i=0;i<2;++i) {
            const auto* unchanged=destination.GetResource(static_cast<float>(i),false,true,false);
            all_correct &= unchanged && unchanged->resource==b.Get() && !unchanged->is_null &&
                unchanged->original_dhash==7;
        }
    }
    registry.resource(L"resourceround4aliasmissing",device);
    CommandList skipped;
    append(skipped,destination.name+L"[random(2)]",L"ref unless_null resourceround4aliasmissing");
    const auto skipped_before=GetExpressionRandomSampleCount();
    RunCommandList(device.device,device.wrapper.get(),&skipped,nullptr,false);
    all_correct &= GetExpressionRandomSampleCount()==skipped_before;
    for (unsigned i=0;i<2;++i) {
        const auto* unchanged=destination.GetResource(static_cast<float>(i),false,true,false);
        all_correct &= unchanged && unchanged->resource==b.Get() && !unchanged->is_null;
    }
    CommandList repeated;
    append(repeated,destination.name+L"[random(2)]",L"ref resourceround4aliasa");
    for (unsigned iteration=0;iteration<3;++iteration) {
        replay_random(seed+iteration);
        const unsigned index=choose(device);
        replay_random(seed+iteration);
        const auto start=GetExpressionRandomSampleCount();
        RunCommandList(device.device,device.wrapper.get(),&repeated,nullptr,false);
        const auto* result=destination.GetResource(static_cast<float>(index),false,true,false);
        all_correct &= GetExpressionRandomSampleCount()==start+1 && result &&
            result->resource==a.Get() && result->original_dhash==source.original_dhash;
    }
    require_batch(all_correct,"round4 alias: resolve assignment destination once and align cache/resource/reference metadata");
}
