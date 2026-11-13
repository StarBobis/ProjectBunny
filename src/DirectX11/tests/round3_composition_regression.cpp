#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include "ExpressionFunctions.h"

void test_dx11_round3_composition_regression()
{
    using namespace dx11_slot_test;
    DataCacheTestScope policy;
    BatchDevice device;
    Registry registry;
    float values[16] = {1,2,3};
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth=sizeof(values); desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=values;
    ComPtr<ID3D11Buffer> source;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc,&initial,&source)), "round3 composition: tracked VB");
    require_batch(GetRegionHash(device.wrapper.get(),source.Get(),0,sizeof(values))!=0,"round3 composition: warm actual source cache");
    ID3D11Buffer* bound=source.Get(); const UINT stride=4,offset=0;
    device.context->IASetVertexBuffers(0,1,&bound,&stride,&offset);
    auto& pool=registry.pool(L"poolround3composition",device,PoolIndexType::RING,1);
    scalar(device,L"poolround3composition[0]",L"ref vb0");
    auto* element=pool.GetResource(0,false,false,false);
    require_batch(element && element->GetHandleInfo() && element->GetHandleInfo()->GetCachedData(),"round3 composition: captured real cache");
    CommandListScope scope; scope.emplace_front(); const std::wstring ns;
    bool evaluated_once = true;
    element->buf_size = 0; // Exercise descriptor fallback rather than cached size.
    using Getter = float (ResourceCopyTarget::*)(CommandListState*);
    const Getter getters[] = {&ResourceCopyTarget::GetResourceRegionHash,
        &ResourceCopyTarget::GetResourceSpatialHash, &ResourceCopyTarget::GetResourceStride,
        &ResourceCopyTarget::GetResourceFormat, &ResourceCopyTarget::GetResourceSize,
        &ResourceCopyTarget::GetResourceWidth, &ResourceCopyTarget::GetResourceHeight,
        &ResourceCopyTarget::GetResourceArray, &ResourceCopyTarget::GetResourceMips};
    const wchar_t* suffixes[] = {L"->hashregion(0,12)", L"->spatialhash(0,1,2,1)",
        L"->stride", L"->format", L"->size", L"->width", L"->height", L"->array", L"->mips"};
    const float expected_buffer[] = {0, 0, 4, -1, 64, -4, -4, -4, -4};
    for(unsigned member = 0; member < 9; ++member) {
        ResourceCopyTarget target, baseline, missing;
        const std::wstring text=std::wstring(L"poolround3composition[random(1)]")+suffixes[member];
        const std::wstring fixed=std::wstring(L"poolround3composition[0]")+suffixes[member];
        const std::wstring absent=std::wstring(L"poolround3composition[random(1)/0]")+suffixes[member];
        require_batch(target.ParseTarget(text.c_str(),true,&ns,&scope) &&
            baseline.ParseTarget(fixed.c_str(),true,&ns,&scope) &&
            missing.ParseTarget(absent.c_str(),true,&ns,&scope),"round3 composition: parse dynamic/fixed/missing pool property");
        const float expected = (baseline.*getters[member])(&device.state);
        if(member >= 2)
            require_batch(expected == expected_buffer[member],"round3 composition: actual buffer metadata and nontexture failures");
        ResourceCopyTarget shared = target;
        for(unsigned repeat=0;repeat<3;++repeat) {
            ResourceCopyTarget& invocation = repeat==1 ? shared : target;
            uint32_t before=GetExpressionRandomSampleCount();
            const float result=(invocation.*getters[member])(&device.state);
            uint32_t samples=GetExpressionRandomSampleCount()-before;
            std::fprintf(stderr,"round3 dynamic member %u repeat %u: samples=%u result=%g\n",member,repeat,samples,result);
            evaluated_once &= samples==1;
            require_batch(result==expected,"round3 composition: dynamic member agrees with stable selected resource");
            before=GetExpressionRandomSampleCount();
            const float unavailable=(missing.*getters[member])(&device.state);
            samples=GetExpressionRandomSampleCount()-before;
            evaluated_once &= samples==1;
            require_batch(unavailable==-2,"round3 composition: resolved missing resource stays missing without fallback reselection");
        }
    }
    auto image=texture(device,0xff345678,4);
    registry.resource(L"resourceround3image",device,image.Get(),DXGI_FORMAT_R8G8B8A8_UNORM);
    registry.pool(L"poolround3image",device,PoolIndexType::RING,1);
    scalar(device,L"poolround3image[0]",L"ref resourceround3image");
    const float expected_image[] = {4,3,1,1};
    for(unsigned axis=0;axis<4;++axis) {
        ResourceCopyTarget target;
        const std::wstring text=std::wstring(L"poolround3image[random(1)]")+suffixes[axis+5];
        require_batch(target.ParseTarget(text.c_str(),true,&ns,&scope),"round3 composition: parse real texture metadata");
        for(unsigned repeat=0;repeat<2;++repeat) {
            const uint32_t before=GetExpressionRandomSampleCount();
            require_batch((target.*getters[axis+5])(&device.state)==expected_image[axis],"round3 composition: native texture extent/array/mips");
            evaluated_once &= GetExpressionRandomSampleCount()==before+1;
        }
    }
    device.context->ClearState();
    require_batch(evaluated_once,"round3 composition: each property resolves its dynamic resource exactly once");
}
