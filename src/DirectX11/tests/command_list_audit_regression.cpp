// Compile the actual command implementation once; expose private count helpers
// only to this regression TU, without adding hooks to the production DLL.
#include "../CommandList.cpp"
#include "slot_range_test_support.h"

namespace {
using namespace dx11_slot_test;
void require_vertex_binding(BatchDevice& device,ID3D11Buffer* expected,UINT stride,UINT offset)
{
    ComPtr<ID3D11Buffer> observed; UINT actual_stride=0,actual_offset=0;
    device.context->IAGetVertexBuffers(0,1,&observed,&actual_stride,&actual_offset);
    require_batch(observed.Get()==expected && actual_stride==stride && actual_offset==offset,
        "F155: the real WARP runtime retains the tested vertex binding");
}
void require_index_binding(BatchDevice& device,ID3D11Buffer* expected,DXGI_FORMAT format,UINT offset)
{
    ComPtr<ID3D11Buffer> observed; DXGI_FORMAT actual_format=DXGI_FORMAT_UNKNOWN; UINT actual_offset=0;
    device.context->IAGetIndexBuffer(&observed,&actual_format,&actual_offset);
    require_batch(observed.Get()==expected && actual_format==format && actual_offset==offset,
        "F155: the real WARP runtime retains the tested index binding");
}
}

void test_dx11_automatic_draw_count_regression()
{
    BatchDevice device;
    auto vertices=buffer(device,64,D3D11_BIND_VERTEX_BUFFER);
    auto indices=buffer(device,64,D3D11_BIND_INDEX_BUFFER);
    ID3D11Buffer* bound=vertices.Get();
    const UINT strides[]={0,4,12,5,128,4,4};
    const UINT offsets[]={0,0,4,1,0,64,68};
    const UINT counts[]={0,16,5,12,0,0,0};
    for(size_t i=0;i<ARRAYSIZE(strides);++i) {
        device.context->IASetVertexBuffers(0,1,&bound,&strides[i],&offsets[i]);
        require_vertex_binding(device,bound,strides[i],offsets[i]);
        std::fprintf(stderr,"F155: infer count from native VB stride=%u offset=%u\n",strides[i],offsets[i]); std::fflush(stderr);
        require_batch(get_vertex_count_from_current_vb(device.context.Get())==counts[i],
            "F155: VB inference handles zero stride, partial elements, end and out-of-range offsets");
    }
    for(const auto format:{DXGI_FORMAT_R16_UINT,DXGI_FORMAT_R32_UINT}) {
        for(const UINT offset:{0u,4u,64u,68u}) {
            device.context->IASetIndexBuffer(indices.Get(),format,offset);
            require_index_binding(device,indices.Get(),format,offset);
            const UINT expected=offset>=64 ? 0 : (64-offset)/(format==DXGI_FORMAT_R16_UINT ? 2 : 4);
            require_batch(get_index_count_from_current_ib(device.context.Get())==expected,
                "F155: native 16/32-bit IB inference never subtracts an excessive offset");
        }
    }
    UINT stride=0,offset=0;
    device.context->IASetVertexBuffers(0,1,&bound,&stride,&offset);
    device.context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R16_UINT,68);
    DrawCallInfo caller; caller.InstanceCount=3; caller.FirstInstance=2;
    device.state.call_info=&caller;
    DrawCommand command; command.ini_section=L"CommandListAuditAutoCount";
    for(const auto type:{DrawCommandType::AUTO_VERTEX_COUNT,DrawCommandType::AUTO_INDEX_COUNT,DrawCommandType::AUTO_INDEX_INSTANCE_COUNT}) {
        command.type=type; command.run(&device.state);
    }
    require_vertex_binding(device,bound,0,0);
    require_index_binding(device,indices.Get(),DXGI_FORMAT_R16_UINT,68);
    device.state.call_info=nullptr;
    ID3D11Buffer* empty=nullptr;
    device.context->IASetVertexBuffers(0,1,&empty,&stride,&offset);
    device.context->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    require_batch(get_vertex_count_from_current_vb(device.context.Get())==0 && get_index_count_from_current_ib(device.context.Get())==0,
        "F155: missing native buffers retain the existing zero-count result");
    stride=4;
    device.context->IASetVertexBuffers(0,1,&bound,&stride,&offset);
    device.context->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);
    require_batch(get_vertex_count_from_current_vb(device.context.Get())==16 && get_index_count_from_current_ib(device.context.Get())==16,
        "F155: valid buffer rebinding restores normal automatic inference");
}
