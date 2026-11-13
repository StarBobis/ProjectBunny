#include "slot_range_test_support.h"
#include "ResourceHash.h"
namespace {
using namespace dx11_slot_test;
class PointerPatch {
    void** slot;
    void* original;
public:
    PointerPatch(void** slot, void* replacement) : slot(slot), original(*slot)
    {
        DWORD previous = 0;
        require_batch(VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous) != FALSE,
            "Store cache: make observer pointer writable");
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), replacement);
        DWORD ignored = 0;
        if (!VirtualProtect(slot, sizeof(void*), previous, &ignored)) {
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), original);
            if (!VirtualProtect(slot, sizeof(void*), previous, &ignored)) std::terminate();
            throw std::runtime_error("Store cache: restore observer page protection");
        }
    }
    ~PointerPatch()
    {
        DWORD previous = 0, ignored = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) std::terminate();
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), original);
        if (!VirtualProtect(slot, sizeof(void*), previous, &ignored)) std::terminate();
    }
    PointerPatch(const PointerPatch&) = delete;
    PointerPatch& operator=(const PointerPatch&) = delete;
};


using MapFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
MapFunction original_map = nullptr;
ID3D11DeviceContext* selected_context = nullptr;
bool armed = false;
unsigned failures = 0;
HRESULT STDMETHODCALLTYPE fail_once(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
 D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
{
 if (context == selected_context && type == D3D11_MAP_READ && armed) {
  armed = false; ++failures;
  return E_FAIL; // Only the HRESULT is injected. No successful mapped data is fabricated.
 }
 return original_map(context,resource,subresource,type,flags,mapped);
}
class FailedReadMap {
 std::unique_ptr<PointerPatch> patch;
public:
 explicit FailedReadMap(ID3D11DeviceContext* context) {
  require_batch(!original_map,"round3: exclusive Map hook");
  auto** table=*reinterpret_cast<void***>(context);
  original_map=reinterpret_cast<MapFunction>(table[14]); selected_context=context;
  try { patch=std::make_unique<PointerPatch>(&table[14],reinterpret_cast<void*>(&fail_once)); }
  catch (...) { original_map=nullptr; selected_context=nullptr; throw; }
 }
 ~FailedReadMap() { armed=false; patch.reset(); original_map=nullptr; selected_context=nullptr; }
 void Arm() { require_batch(!armed,"round3: prior failure must be consumed"); armed=true; }
};
class DirtyState {
 int saved=G->user_config_dirty;
public: ~DirtyState() { G->user_config_dirty=saved; }
};
void store(BatchDevice& device) {
 CommandList commands; CommandListScope scope; scope.emplace_front(); commands.scope=&scope;
 std::wstring args=L"$round3_map_output, resourceround3_map, 0"; const std::wstring ns;
 const bool parsed=ParseCommandListGeneralCommands(L"Round3Map",L"store",&args,&commands,&commands,nullptr,&ns);
 commands.scope=nullptr; require_batch(parsed,"round3: parse actual Store"); execute(commands,device);
}
}
void test_dx11_round3_oracle_regression()
{
 using namespace dx11_slot_test;
 DirtyState restore; BatchDevice device; Registry registry;
 const float input=17;
 D3D11_BUFFER_DESC bd{}; bd.ByteWidth=16; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
 float data[4]={input,2,3,4}; D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=data;
 ComPtr<ID3D11Buffer> source;
 require_batch(SUCCEEDED(device.native->CreateBuffer(&bd,&initial,&source)),"round3: real native uncached Store buffer");
 registry.resource(L"resourceround3_map",device,source.Get(),DXGI_FORMAT_R32_FLOAT,4,16);
 auto& output=registry.variable(L"$round3_map_output",91); output.flags|=VariableFlags::PERSIST;
 {
  FailedReadMap hook(device.context.Get()); const unsigned before=failures;
  G->user_config_dirty=0; hook.Arm(); store(device);
  require_batch(!armed && failures==before+1 && output.fval==0 && (G->user_config_dirty&1),"round3: actual Store READ Map failure publishes zero and persistence dirty");
  G->user_config_dirty=0; store(device);
  require_batch(output.fval==input && (G->user_config_dirty&1),"round3: actual Store retry reads real GPU bytes");
 }
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=1;td.MipLevels=td.ArraySize=1;
 td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 const UINT black=0; initial.pSysMem=&black;initial.SysMemPitch=4;
 ComPtr<ID3D11Texture2D> texture;
 require_batch(SUCCEEDED(device.device->CreateTexture2D(&td,&initial,&texture)),"round3: real tracked dHash texture");
 {
  FailedReadMap hook(device.context.Get()); const unsigned before=failures; uint64_t hash=123;
  hook.Arm();
  require_batch(!GetDhash(device.wrapper.get(),texture.Get(),256,false,hash) && !armed && failures==before+1 && hash==0,"round3: actual dHash readback failure returns unavailable without stale hash");
  { CriticalSectionGuard(&G->mCriticalSection);
    require_batch(!G->mResources.at(texture.Get()).dhash_valid,"round3: failed dHash Map never publishes valid cache"); }
  require_batch(GetDhash(device.wrapper.get(),texture.Get(),256,false,hash) && hash==0,"round3: dHash retries real GPU black hash after failure");
  { CriticalSectionGuard(&G->mCriticalSection);
    require_batch(G->mResources.at(texture.Get()).dhash_valid,"round3: successful zero hash becomes valid cache"); }
 }
 device.context->ClearState();
 std::fputs("PASS: injected native READ Map HRESULT failures preserve Store and dHash retry contracts\n",stderr);
}
