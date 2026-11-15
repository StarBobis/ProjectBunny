#include "slot_range_test_support.h"
#include "HackerDXGI.h"
#include "Hunting.h"
#include <ctime>

void with_dx11_audit_hunting_configuration(void (*scenario)());

namespace {
using namespace dx11_slot_test;
class HuntingWindow {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::wstring name = L"F179-hidden-" + std::to_wstring(GetCurrentProcessId());
public:
    HWND handle = nullptr;
    HuntingWindow()
    {
        WNDCLASSW window{}; window.lpfnWndProc=DefWindowProcW; window.hInstance=instance; window.lpszClassName=name.c_str();
        require_batch(RegisterClassW(&window)!=0,"F179: register hidden fixture window");
        handle=CreateWindowExW(0,name.c_str(),L"hunting audit",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,instance,nullptr);
        require_batch(handle!=nullptr,"F179: create actual hidden window");
    }
    ~HuntingWindow(){if(handle)DestroyWindow(handle);UnregisterClassW(name.c_str(),instance);}
};
class HuntingStateScope {
    decltype(G->mVisitedIndexBuffers) old_ib=G->mVisitedIndexBuffers;
    decltype(G->mVisitedVertexBuffers) old_vb=G->mVisitedVertexBuffers;
    decltype(G->mVisitedIndexBuffersLastSeenFrame) old_ib_frames=G->mVisitedIndexBuffersLastSeenFrame;
    decltype(G->mVisitedVertexBuffersLastSeenFrame) old_vb_frames=G->mVisitedVertexBuffersLastSeenFrame;
    uint32_t old_selected_ib=G->mSelectedIndexBuffer, old_selected_vb=G->mSelectedVertexBuffer;
    int old_ib_pos=G->mSelectedIndexBufferPos, old_vb_pos=G->mSelectedVertexBufferPos;
    int old_hunting=G->hunting, old_slot=G->gSelectedVertexBufferSlotId, old_lifetime=G->overlay_buffer_hash_lifetime;
    bool old_reset=G->gResetSelectedVertexBufferSlotId, old_suppress=G->suppress_overlay, old_foreground=G->check_foreground_window;
    std::wstring old_title=G->additionalForegroundWindowTitle;
    time_t old_hunt_time=G->huntTime;
public:
    HuntingStateScope()
    {
        G->hunting=0; G->gSelectedVertexBufferSlotId=-1; G->gResetSelectedVertexBufferSlotId=false;
        G->overlay_buffer_hash_lifetime=1000; G->check_foreground_window=true; G->additionalForegroundWindowTitle.clear();
        clear_and_prev();
    }
    void clear_and_prev()
    {
        G->mVisitedIndexBuffers.clear(); G->mVisitedVertexBuffers.clear();
        G->mVisitedIndexBuffersLastSeenFrame.clear(); G->mVisitedVertexBuffersLastSeenFrame.clear();
        // Exactly the pending-wrap state produced by PrevIndex/VertexBuffer.
        G->mSelectedIndexBuffer=G->mSelectedVertexBuffer=UINT32_MAX;
        G->mSelectedIndexBufferPos=G->mSelectedVertexBufferPos=INT_MAX;
    }
    ~HuntingStateScope()
    {
        G->mVisitedIndexBuffers=std::move(old_ib); G->mVisitedVertexBuffers=std::move(old_vb);
        G->mVisitedIndexBuffersLastSeenFrame=std::move(old_ib_frames); G->mVisitedVertexBuffersLastSeenFrame=std::move(old_vb_frames);
        G->mSelectedIndexBuffer=old_selected_ib;G->mSelectedVertexBuffer=old_selected_vb;
        G->mSelectedIndexBufferPos=old_ib_pos;G->mSelectedVertexBufferPos=old_vb_pos;
        G->hunting=old_hunting;G->gSelectedVertexBufferSlotId=old_slot;G->gResetSelectedVertexBufferSlotId=old_reset;
        G->overlay_buffer_hash_lifetime=old_lifetime;G->suppress_overlay=old_suppress;
        G->check_foreground_window=old_foreground;G->additionalForegroundWindowTitle=old_title;G->huntTime=old_hunt_time;
    }
};
}

static void exercise_empty_hunting_present()
{
    using namespace dx11_slot_test;
    HuntingStateScope state; BatchDevice device; HuntingWindow window;
    ComPtr<IDXGIDevice> dxgi_device; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
    require_batch(SUCCEEDED(device.native.As(&dxgi_device)) && SUCCEEDED(dxgi_device->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))),"F179: actual software-device factory");
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain1> native;
    require_batch(SUCCEEDED(factory->CreateSwapChainForHwnd(device.native.Get(),window.handle,&desc,nullptr,nullptr,&native)),"F179: real hidden swapchain");
    ComPtr<HackerSwapChain> chain;
    chain.Attach(new HackerSwapChain(native.Detach(),device.device,device.wrapper.get()));
    require_batch(chain->mOverlay!=nullptr,"F179: production font/resource dependencies are actually connected");
    G->hunting=HUNTING_MODE_ENABLED;
    state.clear_and_prev();
    require_batch(G->mVisitedIndexBuffers.empty() && G->mVisitedVertexBuffers.empty(),"F179: empty-set precondition is real");
    auto present=[&](){
        G->huntTime=std::time(nullptr);
        // Keep UI rendering from adding its own buffers to the controlled hunt
        // set; the actual hunting/selection Present path remains enabled.
        G->suppress_overlay=true;
        const unsigned frame=G->frame_no;
        require_batch(SUCCEEDED(chain->Present(0,0)),"F179: real wrapped Present");
        require_batch(G->frame_no==frame+1u,"F179: empty-set handling must not skip normal frame actions");
    };
    std::fputs("F179: Present with empty IB/VB sets and pending previous selection\n",stderr);std::fflush(stderr);
    present();
    require_batch(G->mSelectedIndexBuffer==UINT32_MAX && G->mSelectedVertexBuffer==UINT32_MAX &&
        G->mSelectedIndexBufferPos==INT_MAX && G->mSelectedVertexBufferPos==INT_MAX,
        "F179: empty sets preserve unselected pending-wrap semantics");
    RegisterVisitedIndexBuffer(0x100);RegisterVisitedIndexBuffer(0x200);
    RegisterVisitedVertexBuffer(0x300,0);RegisterVisitedVertexBuffer(0x400,0);
    std::fprintf(stderr,"F179 pre: IB=%zu VB=%zu pos=%d/%d slot=%d reset=%d\n",G->mVisitedIndexBuffers.size(),G->mVisitedVertexBuffers.size(),G->mSelectedIndexBufferPos,G->mSelectedVertexBufferPos,G->gSelectedVertexBufferSlotId,G->gResetSelectedVertexBufferSlotId);
    present();
    std::fprintf(stderr,"F179 post: IB=%zu VB=%zu selected=%08x/%08x pos=%d/%d hunt=%d\n",G->mVisitedIndexBuffers.size(),G->mVisitedVertexBuffers.size(),G->mSelectedIndexBuffer,G->mSelectedVertexBuffer,G->mSelectedIndexBufferPos,G->mSelectedVertexBufferPos,G->hunting);
    require_batch(G->mSelectedIndexBuffer==0x200 && G->mSelectedIndexBufferPos==1 &&
        G->mSelectedVertexBuffer==0x400 && G->mSelectedVertexBufferPos==1,"F179: nonempty sets still resolve previous/wrap to their last item");
    state.clear_and_prev();present();
    require_batch(G->mSelectedIndexBuffer==UINT32_MAX && G->mSelectedVertexBuffer==UINT32_MAX,
        "F179: clear/previous/Present sequence does not revive stale buffer hashes");
}

void test_dx11_empty_hunting_present_regression()
{
    with_dx11_audit_hunting_configuration(&exercise_empty_hunting_present);
}
