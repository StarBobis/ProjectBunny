#include "slot_range_test_support.h"
#include "cache_policy_test_scope.h"
#include "ResourceHash.h"
namespace {
using namespace dx11_slot_test;
class PointerPatch {
    void** slot; void* original;
public:
    PointerPatch(void** slot,void* replacement):slot(slot),original(*slot) {
        DWORD previous=0,ignored=0;
        require_batch(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&previous)!=FALSE,"round5: writable vtable");
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),replacement);
        if (!VirtualProtect(slot,sizeof(void*),previous,&ignored)) {
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),original);
            if (!VirtualProtect(slot,sizeof(void*),previous,&ignored)) std::terminate();
            throw std::runtime_error("round5: restore vtable protection");
        }
    }
    ~PointerPatch() {
        DWORD previous=0,ignored=0;
        if (!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&previous)) std::terminate();
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),original);
        if (!VirtualProtect(slot,sizeof(void*),previous,&ignored)) std::terminate();
    }
    PointerPatch(const PointerPatch&)=delete;
    PointerPatch& operator=(const PointerPatch&)=delete;
};
using CreateBufferFunction=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer**);
CreateBufferFunction original_create=nullptr;
ID3D11Device* selected_device=nullptr;
bool armed=false;
unsigned failures=0;
HRESULT STDMETHODCALLTYPE fail_once(ID3D11Device* device,const D3D11_BUFFER_DESC* desc,
    const D3D11_SUBRESOURCE_DATA* initial,ID3D11Buffer** output) {
    if (device==selected_device && armed && desc && desc->ByteWidth==128 && !initial) {
        armed=false; ++failures; if (output) *output=nullptr;
        return E_OUTOFMEMORY;
    }
    return original_create(device,desc,initial,output);
}
class FailedCreate {
    std::unique_ptr<PointerPatch> patch;
public:
    explicit FailedCreate(ID3D11Device* device) {
        require_batch(!original_create,"round5: exclusive creation hook");
        auto** table=*reinterpret_cast<void***>(device);
        original_create=reinterpret_cast<CreateBufferFunction>(table[3]); selected_device=device;
        try { patch=std::make_unique<PointerPatch>(&table[3],reinterpret_cast<void*>(&fail_once)); }
        catch (...) { original_create=nullptr; selected_device=nullptr; throw; }
    }
    ~FailedCreate() { armed=false; patch.reset(); original_create=nullptr; selected_device=nullptr; }
    void Arm() { require_batch(!armed,"round5: previous injection consumed"); armed=true; }
};
ComPtr<ID3D11Buffer> tracked(BatchDevice& device,UINT size,UINT seed) {
    std::vector<UINT> bytes(size/sizeof(UINT),seed);
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=size;
    desc.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=bytes.data();
    ComPtr<ID3D11Buffer> result;
    require_batch(SUCCEEDED(device.device->CreateBuffer(&desc,&initial,&result)),"round5: valid tracked buffer");
    return result;
}
}
void test_dx11_round5_assignment_regression()
{
    using namespace dx11_slot_test;
    DataCacheTestScope policy;
    BatchDevice device;
    Registry registry;
    auto old_buffer=tracked(device,64,91),new_buffer=tracked(device,128,42);
    require_batch(GetRegionHash(device.wrapper.get(),old_buffer.Get(),0,64)!=0 &&
        GetRegionHash(device.wrapper.get(),new_buffer.Get(),0,128)!=0,
        "round5: populate real snapshots through production GPU readback");
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format=DXGI_FORMAT_R32_UINT; desc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; desc.Buffer.NumElements=16;
    ComPtr<ID3D11ShaderResourceView> old_view;
    require_batch(SUCCEEDED(device.native->CreateShaderResourceView(old_buffer.Get(),&desc,&old_view)),"round5: real old view");
    auto& source=registry.resource(L"resourceround5source",device,new_buffer.Get(),DXGI_FORMAT_R32_FLOAT,4,128);
    source.source_stride=8; source.original_format=DXGI_FORMAT_R32_FLOAT; source.original_dhash=12345;
    bool all_preserved=true,all_retried=true;
    for (bool copy_desc : {false,true}) {
        const std::wstring name=copy_desc?L"resourceround5desc":L"resourceround5copy";
        auto& destination=registry.resource(name,device,old_buffer.Get(),DXGI_FORMAT_R16_UINT,2,64);
        destination.bind_flags=static_cast<D3D11_BIND_FLAG>(D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_SHADER_RESOURCE);
        destination.view=old_view.Get(); old_view->AddRef();
        destination.source_stride=7; destination.original_format=DXGI_FORMAT_R16_UINT; destination.original_dhash=67890;
        destination.SetHandleInfo(old_buffer.Get(),0,64);
        auto* snapshot=destination.GetHandleInfo();
        require_batch(snapshot && snapshot->GetCachedData(),"round5: actual captured CPU snapshot");
        std::vector<uint8_t> captured(snapshot->GetCachedData(),snapshot->GetCachedData()+64);
        destination.is_null=true;
        const auto cache_size=destination.resource_pool.cache.size();
        const auto before=failures;
        {
            FailedCreate injection(device.native.Get()); injection.Arm();
            scalar(device,name,(copy_desc?L"copy_desc ":L"copy ")+source.name);
        }
        require_batch(failures==before+1,"round5: valid descriptor creation received exactly one E_OUTOFMEMORY");
        const bool preserved=destination.is_null && destination.resource==old_buffer.Get() && destination.view==old_view.Get() &&
            destination.stride==2 && destination.source_stride==7 && destination.offset==0 && destination.buf_size==64 &&
            destination.format==DXGI_FORMAT_R16_UINT && destination.original_format==DXGI_FORMAT_R16_UINT &&
            destination.original_dhash==67890 && destination.GetHandleInfo()==snapshot &&
            std::memcmp(snapshot->GetCachedData(),captured.data(),64)==0 && destination.resource_pool.cache.size()==cache_size;
        std::fprintf(stderr,"round5 valid OOM %ls: state_preserved=%d\n",copy_desc?L"copy_desc":L"copy",preserved);
        all_preserved &= preserved;
        scalar(device,name,(copy_desc?L"copy_desc ":L"copy ")+source.name);
        D3D11_BUFFER_DESC allocation{};
        if (destination.resource) static_cast<ID3D11Buffer*>(destination.resource)->GetDesc(&allocation);
        const bool retried=!destination.is_null && destination.resource!=old_buffer.Get() && allocation.ByteWidth==128 &&
            destination.original_dhash==12345 && destination.source_stride==8;
        std::fprintf(stderr,"round5 valid OOM %ls: same_descriptor_retry=%d width=%u\n",copy_desc?L"copy_desc":L"copy",retried,allocation.ByteWidth);
        all_retried &= retried;
    }
    require_batch(all_preserved && all_retried,"round5: failed creation preserves all destination state and retries valid descriptor");
}
