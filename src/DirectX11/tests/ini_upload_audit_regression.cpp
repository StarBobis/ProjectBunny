#include "slot_range_test_support.h"
#include "Override.h"
#include "Profiling.h"

extern "C" size_t dx11_audit_map_slot(void);
extern "C" size_t dx11_audit_unmap_slot(void);
extern std::vector<CommandList*> pending_post_command_lists;

namespace {
using namespace dx11_slot_test;
using NativeMap=HRESULT (STDMETHODCALLTYPE *)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
using NativeUnmap=void (STDMETHODCALLTYPE *)(ID3D11DeviceContext1*,ID3D11Resource*,UINT);
class IniUploadMapScope;
IniUploadMapScope* active_ini_map=nullptr;
HRESULT STDMETHODCALLTYPE audit_ini_map(ID3D11DeviceContext1*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
void STDMETHODCALLTYPE audit_ini_unmap(ID3D11DeviceContext1*,ID3D11Resource*,UINT);

class IniUploadMapScope {
    void** table;
    void* saved_map;
    void* saved_unmap;
    static void set(void** slot,void* value)
    {
        DWORD old=0;
        require_batch(VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old)!=FALSE,"F194: existing native context method slot");
        *slot=value; DWORD ignored=0;
        if(!VirtualProtect(slot,sizeof(*slot),old,&ignored)) std::abort();
    }
public:
    ID3D11DeviceContext1* context;
    ComPtr<ID3D11Resource> resource;
    HRESULT fail=S_OK;
    unsigned maps=0,unmaps=0;
    D3D11_MAP last_type=static_cast<D3D11_MAP>(0);
    UINT last_flags=UINT_MAX;
    NativeMap native_map;
    NativeUnmap native_unmap;
    IniUploadMapScope(ID3D11DeviceContext1* ctx,ID3D11Resource* target):context(ctx),resource(target)
    {
        require_batch(!active_ini_map,"F194: only one scoped native hook in this process");
        table=*reinterpret_cast<void***>(ctx);
        saved_map=table[dx11_audit_map_slot()]; saved_unmap=table[dx11_audit_unmap_slot()];
        native_map=reinterpret_cast<NativeMap>(saved_map); native_unmap=reinterpret_cast<NativeUnmap>(saved_unmap);
        active_ini_map=this;
        try {
            set(table+dx11_audit_map_slot(),reinterpret_cast<void*>(audit_ini_map));
            set(table+dx11_audit_unmap_slot(),reinterpret_cast<void*>(audit_ini_unmap));
        } catch(...) {
            set(table+dx11_audit_map_slot(),saved_map); active_ini_map=nullptr; throw;
        }
    }
    ~IniUploadMapScope()
    {
        try {set(table+dx11_audit_unmap_slot(),saved_unmap); set(table+dx11_audit_map_slot(),saved_map);}
        catch(...) {std::abort();}
        active_ini_map=nullptr;
    }
    void target(ID3D11Resource* value) {resource=value; maps=unmaps=0;}
};

HRESULT STDMETHODCALLTYPE audit_ini_map(ID3D11DeviceContext1* context,ID3D11Resource* resource,UINT subresource,
    D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* mapped)
{
    auto& scope=*active_ini_map;
    if(context==scope.context && resource==scope.resource.Get()) {
        ++scope.maps; scope.last_type=type; scope.last_flags=flags;
        if(FAILED(scope.fail)) {*mapped={}; return scope.fail;}
    }
    return scope.native_map(context,resource,subresource,type,flags,mapped);
}
void STDMETHODCALLTYPE audit_ini_unmap(ID3D11DeviceContext1* context,ID3D11Resource* resource,UINT subresource)
{
    auto& scope=*active_ini_map;
    if(context==scope.context && resource==scope.resource.Get()) ++scope.unmaps;
    scope.native_unmap(context,resource,subresource);
}

class IniUploadStateScope {
    std::vector<DirectX::XMFLOAT4> parameters=std::move(G->iniParams);
    int reserved=G->iniParamsReserved;
    unsigned updates=Profiling::iniparams_updates;
    int dirty=G->user_config_dirty;
    std::vector<CommandList*> posts=std::move(pending_post_command_lists);
    OverrideTransition transitions=std::move(CurrentTransition);
public:
    IniUploadStateScope() {G->iniParams.assign(2,DirectX::XMFLOAT4(0,0,0,0)); G->iniParamsReserved=2; CurrentTransition=OverrideTransition{};}
    ~IniUploadStateScope()
    {
        pending_post_command_lists=std::move(posts); G->iniParams=std::move(parameters); G->iniParamsReserved=reserved;
        Profiling::iniparams_updates=updates; G->user_config_dirty=dirty;
        CurrentTransition=std::move(transitions);
    }
};

std::vector<DirectX::XMFLOAT4> read_ini_upload_texture(BatchDevice& device)
{
    D3D11_TEXTURE1D_DESC desc{}; device.device->mIniTexture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture1D> staging;
    require_batch(SUCCEEDED(device.native->CreateTexture1D(&desc,nullptr,&staging)),"F194: real readback texture");
    device.context->CopyResource(staging.Get(),device.device->mIniTexture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"F194: read actual GPU parameters");
    std::vector<DirectX::XMFLOAT4> values(desc.Width);
    std::memcpy(values.data(),mapped.pData,values.size()*sizeof(values[0]));
    device.context->Unmap(staging.Get(),0);
    return values;
}

class ObserveIniPost : public CommandListCommand {
    unsigned& runs;
    float& value;
public:
    ObserveIniPost(unsigned& count,float& observed):runs(count),value(observed) {}
    void run(CommandListState*) override {++runs; value=G->iniParams[0].x;}
};
void schedule_ini_upload(OverrideTransition& transition,HackerDevice* device,float target,OverrideVars* vars=nullptr)
{
    OverrideParams params; params.emplace(OverrideParam(0,&DirectX::XMFLOAT4::x),target);
    OverrideVars empty;
    transition.ScheduleTransition(device,&params,vars?vars:&empty,0,TransitionType::LINEAR);
}
}

void test_dx11_ini_upload_failure_regression()
{
    IniUploadStateScope state;
    {
    BatchDevice device;
    require_batch(SUCCEEDED(device.device->CreateIniParamResources()) && device.device->mIniTexture,"F194: actual dynamic IniParams allocation");
    OverrideTransition transition;
    CommandListVariable variable(L"$ini_upload_audit",2,VariableFlags::PERSIST);
    OverrideVars variables; variables[&variable]=9;
    unsigned posts=0; float post_value=0;
    CommandList post; post.commands.push_back(std::make_shared<ObserveIniPost>(posts,post_value));
    pending_post_command_lists.push_back(&post);
    IniUploadMapScope mapping(device.context.Get(),device.device->mIniTexture);
    const unsigned before=Profiling::iniparams_updates;
    schedule_ini_upload(transition,device.device,7,&variables);
    mapping.fail=E_FAIL;
    std::fputs("F194: failed real Map must not expose its invalid output or lose the completed CPU update\n",stderr); std::fflush(stderr);
    transition.UpdateTransitions(device.device);
    require_batch(mapping.maps==1 && !mapping.unmaps && mapping.last_type==D3D11_MAP_WRITE_DISCARD && !mapping.last_flags,
        "F194: failure reaches the actual Map call and never Unmaps it");
    require_batch(transition.params.empty() && transition.vars.empty() && G->iniParams[0].x==7 && variable.fval==9 &&
        posts==1 && post_value==7 && pending_post_command_lists.empty(),"F194: CPU transitions and post actions complete once despite failed upload");
    require_batch(Profiling::iniparams_updates==before && read_ini_upload_texture(device)[0].x==0,
        "F194: failed upload does not claim success or alter existing GPU values");
    mapping.fail=DXGI_ERROR_DEVICE_REMOVED;
    transition.UpdateTransitions(device.device);
    require_batch(mapping.maps==2 && !mapping.unmaps && variable.fval==9 && posts==1 && G->iniParams[0].x==7 && Profiling::iniparams_updates==before,
        "F194: repeated failed retry neither replays effects nor uploads FLT_MAX");
    G->iniParams[0].x=8;
    mapping.fail=S_OK; transition.UpdateTransitions(device.device);
    require_batch(mapping.maps==3 && mapping.unmaps==1 && Profiling::iniparams_updates==before+1 && read_ini_upload_texture(device)[0].x==8,
        "F194: an empty transition queue still retries the latest CPU array successfully");
    transition.UpdateTransitions(device.device);
    require_batch(mapping.maps==3 && posts==1,"F194: successful retry clears dirty state and does not repeat post commands");
    schedule_ini_upload(transition,device.device,13);
    mapping.fail=E_FAIL; transition.UpdateTransitions(device.device); transition.Stop();
    G->iniParamsReserved=1; G->iniParams.resize(1);
    require_batch(SUCCEEDED(device.device->CreateIniParamResources()),"F194: actual resource recreation for a new layout");
    mapping.target(device.device->mIniTexture); mapping.fail=S_OK;
    const unsigned after_stop=Profiling::iniparams_updates;
    transition.UpdateTransitions(device.device);
    require_batch(!mapping.maps && Profiling::iniparams_updates==after_stop,"F194: Stop clears pending upload from the old layout");
    schedule_ini_upload(transition,device.device,19); transition.UpdateTransitions(device.device);
    require_batch(mapping.maps==1 && mapping.unmaps==1 && read_ini_upload_texture(device)[0].x==19,
        "F194: a fresh transition works after Stop and resource recreation");
    }
    {
        BatchDevice device; OverrideTransition transition;
        require_batch(!device.device->mIniTexture,"F194: genuine not-yet-created parameter resource");
        const unsigned before=Profiling::iniparams_updates;
        schedule_ini_upload(transition,device.device,31); transition.UpdateTransitions(device.device);
        require_batch(G->iniParams[0].x==31 && transition.params.empty() && Profiling::iniparams_updates==before,
            "F194: missing resource completes CPU work without reporting an upload");
        require_batch(SUCCEEDED(device.device->CreateIniParamResources()),"F194: real resource becomes available");
        IniUploadMapScope mapping(device.context.Get(),device.device->mIniTexture);
        G->iniParams[0].x=32; transition.UpdateTransitions(device.device);
        require_batch(mapping.maps==1 && mapping.unmaps==1 && Profiling::iniparams_updates==before+1 && read_ini_upload_texture(device)[0].x==32,
            "F194: pending state survives resource absence and uploads the newest CPU data");
        transition.UpdateTransitions(device.device);
        require_batch(mapping.maps==1,"F194: resource-absence retry clears after one successful upload");
    }
}
