#include "slot_range_test_support.h"
#include "HackerDXGI.h"
#include "HookedDXGI.h"
#include "Hunting.h"
#include <ctime>
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

namespace {
class OverlayAbsenceScope {
    HackerSwapChain* owner;
    Overlay* saved;
public:
    explicit OverlayAbsenceScope(HackerSwapChain* chain) : owner(chain), saved(chain->mOverlay) { owner->mOverlay = nullptr; }
    ~OverlayAbsenceScope() { owner->mOverlay = saved; }
};

void exercise_overlay_lifecycle()
{
    using namespace dx11_slot_test;
    HuntingStateScope globals; BatchDevice device;
    Draw3DMigotoOverlayCommand command(L"CommandListOverlayInitializationAudit");
    G->suppress_overlay = false;
    command.run(&device.state);
    require_batch(!G->suppress_overlay, "F005: absent swapchain is a no-op without suppressing a future overlay");

    HuntingWindow window;
    ComPtr<IDXGIDevice> dxgi_device; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
    require_batch(SUCCEEDED(device.native.As(&dxgi_device)) && SUCCEEDED(dxgi_device->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))), "F005: native factory for overlay lifecycle");
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = desc.Height = 64; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    {
        ComPtr<IDXGISwapChain1> native;
        require_batch(SUCCEEDED(factory->CreateSwapChainForHwnd(device.native.Get(), window.handle, &desc, nullptr, nullptr, &native)),
            "F005: actual hidden swapchain");
        ComPtr<HackerSwapChain> chain;
        chain.Attach(new HackerSwapChain(native.Detach(), device.device, device.wrapper.get()));
        require_batch(chain->mOverlay && device.device->GetHackerSwapChain() == chain.Get(), "F005: real overlay fonts and device/swapchain wiring");
        {
            OverlayAbsenceScope absent(chain.Get());
            command.run(&device.state);
            require_batch(!G->suppress_overlay, "F005: absent optional overlay preserves no-op behavior");
        }
        G->hunting = HUNTING_MODE_ENABLED;
        command.run(&device.state);
        require_batch(G->suppress_overlay, "F005: available overlay still executes and suppresses duplicate automatic drawing");
    }
    require_batch(device.device->GetHackerSwapChain() == nullptr, "F005: normal swapchain release clears its device link");
    G->suppress_overlay = false;
    command.run(&device.state);
    require_batch(!G->suppress_overlay, "F005: command is safe again after normal swapchain release");
}
}

void test_dx11_overlay_lifecycle_regression()
{
    with_dx11_audit_hunting_configuration(&exercise_overlay_lifecycle);
}

namespace {
using namespace dx11_slot_test;
enum F113Scenario : unsigned {
    F113_NORMAL = 1u << 0,
    F113_MODE1 = 1u << 1,
    F113_MODE0 = 1u << 2,
    F113_INVALID_MODE = 1u << 3,
    F113_CREATE_FAILURE = 1u << 4,
    F113_ALL = F113_NORMAL | F113_MODE1 | F113_MODE0 | F113_INVALID_MODE | F113_CREATE_FAILURE,
};

void f113_trace(const char* scenario, const char* phase)
{
    std::printf("F113 case %s: %s\n", scenario, phase);
    std::fflush(stdout);
}

const char* g_f113_current_case = "<outside-case>";

class F113CaseScope {
    const char* label_;
public:
    explicit F113CaseScope(const char* scenario) : label_(scenario)
    {
        g_f113_current_case = label_;
        f113_trace(label_, "begin");
    }
    ~F113CaseScope()
    {
        f113_trace(label_, "done");
        g_f113_current_case = "<outside-case>";
    }
    const char* label() const { return label_; }
    void phase(const char* phase) const { f113_trace(label_, phase); }
};

void f113_log_chain_state(const char* scenario, IDXGISwapChain* wrapper)
{
    IDXGISwapChain1* orig = nullptr;
    ULONG refs = 0;
    if (wrapper) {
        auto* hacker = static_cast<HackerSwapChain*>(wrapper);
        orig = hacker->GetOrigSwapChain1();
        if (orig) {
            orig->AddRef();
            refs = orig->Release();
        }
    }
    std::printf("F113 state %s: wrapper=%p orig=%p refs=%lu\n", scenario, wrapper, orig, refs);
    std::fflush(stdout);
}

struct F113MapEntry {
    unsigned long long rva;
    char name[192];
};
struct F113MapTable {
    HMODULE module = nullptr;
    std::vector<F113MapEntry> entries;
};
F113MapTable g_f113_maps[2];
size_t g_f113_map_count = 0;
HANDLE g_f113_av_log = INVALID_HANDLE_VALUE;

void f113_load_map(HMODULE module, const wchar_t* map_name)
{
    if (!module || g_f113_map_count >= _countof(g_f113_maps)) return;
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, _countof(exe))) return;
    std::wstring path(exe);
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    path.resize(slash + 1);
    path += map_name;
    std::ifstream file{std::filesystem::path(path)};
    if (!file) {
        std::printf("F113 map missing: %ls\n", map_name);
        std::fflush(stdout);
        return;
    }

    unsigned long long preferred = 0;
    bool symbols = false;
    std::vector<F113MapEntry> entries;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("Preferred load address is") != std::string::npos) {
            const auto pos = line.find_last_of(' ');
            if (pos != std::string::npos) {
                try { preferred = std::stoull(line.substr(pos + 1), nullptr, 16); } catch (...) {}
            }
            continue;
        }
        if (line.find("Publics by Value") != std::string::npos) {
            symbols = true;
            continue;
        }
        if (!symbols) continue;
        std::istringstream input(line);
        std::string address, name, va_text;
        if (!(input >> address >> name >> va_text)) continue;
        if (address.find(':') == std::string::npos) continue;
        unsigned long long va = 0;
        try { va = std::stoull(va_text, nullptr, 16); } catch (...) { continue; }
        if (va < preferred) continue;
        F113MapEntry entry{};
        entry.rva = va - preferred;
        strncpy_s(entry.name, name.c_str(), _TRUNCATE);
        entries.push_back(entry);
    }
    if (entries.empty()) return;
    std::sort(entries.begin(), entries.end(), [](const F113MapEntry& a, const F113MapEntry& b) {
        return a.rva < b.rva;
    });
    auto& table = g_f113_maps[g_f113_map_count++];
    table.module = module;
    table.entries = std::move(entries);
    std::printf("F113 map loaded: %ls (%llu symbols)\n", map_name,
        static_cast<unsigned long long>(table.entries.size()));
    std::fflush(stdout);
}

void f113_reset_maps()
{
    g_f113_map_count = 0;
    for (auto& table : g_f113_maps) {
        table.module = nullptr;
        table.entries.clear();
    }
}

void f113_write_all(HANDLE handle, const char* data, size_t length)
{
    if (handle == INVALID_HANDLE_VALUE) return;
    while (length) {
        DWORD written = 0;
        if (!WriteFile(handle, data, static_cast<DWORD>(length), &written, nullptr) || written == 0) return;
        data += written;
        length -= written;
    }
}

void f113_append_text(char** cursor, const char* text)
{
    while (*text) *(*cursor)++ = *text++;
}

void f113_append_hex(char** cursor, unsigned long long value, int width = 0)
{
    char digits[32];
    int count = 0;
    do {
        const int nibble = static_cast<int>(value & 0xF);
        digits[count++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
        value >>= 4;
    } while (value);
    while (count < width) digits[count++] = '0';
    while (count) *(*cursor)++ = digits[--count];
}

// First-chance handler.  No CRT formatting, no allocation: only Win32 calls
// and a pre-opened log handle.  Returns CONTINUE_SEARCH so the AV case stays
// red (the process still crashes with 0xC0000005).
LONG CALLBACK f113_vectored_exception_handler(EXCEPTION_POINTERS* info)
{
    if (!info || !info->ExceptionRecord ||
        info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    const auto* record = info->ExceptionRecord;
    const auto code = record->ExceptionCode;
    const auto address = record->ExceptionAddress;
    HMODULE module = nullptr;
    wchar_t module_path[MAX_PATH]{};
    unsigned long long rva = 0;
    const char* kind = "unknown";

    if (address && GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)) {
        GetModuleFileNameW(module, module_path, _countof(module_path));
        rva = reinterpret_cast<unsigned long long>(address) -
            reinterpret_cast<unsigned long long>(module);
        const wchar_t* base = module_path;
        for (const wchar_t* p = module_path; *p; ++p)
            if (*p == L'\\' || *p == L'/') base = p + 1;
        if (!_wcsicmp(base, L"d3d10warp.dll")) kind = "WARP";
        else if (!_wcsicmp(base, L"d3d11.dll")) kind = "d3d11-runtime";
        else if (!_wcsicmp(base, L"dxgi.dll")) kind = "dxgi-runtime";
        else kind = "test-exe-or-other";
    }

    const char* function = "<no-map>";
    if (module) {
        for (size_t table_index = 0; table_index < g_f113_map_count; ++table_index) {
            const auto& table = g_f113_maps[table_index];
            if (table.module != module || table.entries.empty()) continue;
            const auto* entries = table.entries.data();
            size_t lo = 0;
            size_t hi = table.entries.size();
            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;
                if (entries[mid].rva <= rva) lo = mid + 1;
                else hi = mid;
            }
            if (lo) function = entries[lo - 1].name;
            break;
        }
    }

    char line[2048];
    char* cursor = line;
    f113_append_text(&cursor, "F113 AV code=0x");
    f113_append_hex(&cursor, code, 8);
    f113_append_text(&cursor, " address=0x");
    f113_append_hex(&cursor, reinterpret_cast<unsigned long long>(address), 16);
    f113_append_text(&cursor, " case=");
    f113_append_text(&cursor, g_f113_current_case);
    if (record->NumberParameters >= 2) {
        const ULONG_PTR operation = record->ExceptionInformation[0];
        const ULONG_PTR access_address = record->ExceptionInformation[1];
        f113_append_text(&cursor, " op=");
        f113_append_text(&cursor, operation == 0 ? "read" : operation == 1 ? "write" :
            operation == 8 ? "execute" : "other");
        f113_append_text(&cursor, " at=0x");
        f113_append_hex(&cursor, access_address, 16);
    }
    f113_append_text(&cursor, "\r\n");
    f113_write_all(g_f113_av_log, line, static_cast<size_t>(cursor - line));

    cursor = line;
    f113_append_text(&cursor, "F113 AV module=");
    const int module_length = module_path[0] ? WideCharToMultiByte(CP_UTF8, 0, module_path, -1,
        cursor, static_cast<int>(sizeof(line) - static_cast<size_t>(cursor - line)), nullptr, nullptr) : 0;
    if (module_length > 0) cursor += module_length - 1;
    else f113_append_text(&cursor, "<unknown>");
    f113_append_text(&cursor, " base=0x");
    f113_append_hex(&cursor, reinterpret_cast<unsigned long long>(module), 16);
    f113_append_text(&cursor, " rva=0x");
    f113_append_hex(&cursor, rva, 16);
    f113_append_text(&cursor, " kind=");
    f113_append_text(&cursor, kind);
    f113_append_text(&cursor, "\r\n");
    f113_write_all(g_f113_av_log, line, static_cast<size_t>(cursor - line));

    cursor = line;
    f113_append_text(&cursor, "F113 AV function=");
    f113_append_text(&cursor, function);
    f113_append_text(&cursor, "\r\n");
    f113_write_all(g_f113_av_log, line, static_cast<size_t>(cursor - line));
    return EXCEPTION_CONTINUE_SEARCH;
}

std::wstring f113_av_log_path()
{
    wchar_t configured[MAX_PATH]{};
    const DWORD configured_length = GetEnvironmentVariableW(L"F113_AV_LOG", configured, _countof(configured));
    if (configured_length > 0 && configured_length < static_cast<DWORD>(_countof(configured)))
        return configured;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, _countof(exe));
    std::wstring directory(exe);
    const auto file_slash = directory.find_last_of(L"\\/");
    if (file_slash != std::wstring::npos) directory.resize(file_slash);

    std::wstring root = directory;
    for (;;) {
        const auto pos = root.find_last_of(L"\\/");
        if (pos == std::wstring::npos) break;
        const std::wstring component = root.substr(pos + 1);
        if (!_wcsicmp(component.c_str(), L"build")) {
            root.resize(pos);
            break;
        }
        root.resize(pos);
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64]{};
    swprintf_s(name, _countof(name), L"F-113-av-%04u%02u%02u-%02u%02u%02u.log",
        static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
        static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
        static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond));
    if (!root.empty()) {
        const std::wstring review = root + L"\\_review";
        CreateDirectoryW(review.c_str(), nullptr);
        return review + L"\\" + name;
    }
    return directory + L"\\" + name;
}

class F113CrashLogScope {
    PVOID handler_ = nullptr;
public:
    F113CrashLogScope()
    {
        const std::wstring path = f113_av_log_path();
        std::printf("F113 AV log: %ls\n", path.c_str());
        std::fflush(stdout);
        g_f113_av_log = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_f113_av_log == INVALID_HANDLE_VALUE)
            std::printf("F113 AV log: open failed (%lu)\n", GetLastError());
        handler_ = AddVectoredExceptionHandler(1, f113_vectored_exception_handler);
    }
    ~F113CrashLogScope()
    {
        if (handler_) RemoveVectoredExceptionHandler(handler_);
        if (g_f113_av_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_f113_av_log);
            g_f113_av_log = INVALID_HANDLE_VALUE;
        }
    }
};
class F113ConfigScope {
    int upscaling = G->SCREEN_UPSCALING;
    int upscale_mode = G->UPSCALE_MODE;
    int width = G->SCREEN_WIDTH;
    int height = G->SCREEN_HEIGHT;
    int fullscreen = G->SCREEN_FULLSCREEN;
public:
    ~F113ConfigScope()
    {
        G->SCREEN_UPSCALING = upscaling;
        G->UPSCALE_MODE = upscale_mode;
        G->SCREEN_WIDTH = width;
        G->SCREEN_HEIGHT = height;
        G->SCREEN_FULLSCREEN = fullscreen;
    }
};

DXGI_SWAP_CHAIN_DESC f113_swap_chain_desc(HWND window, UINT width, UINT height, UINT sample_count = 1)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.SampleDesc.Count = sample_count;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    return desc;
}

ComPtr<IDXGIFactory2> f113_factory(BatchDevice& device)
{
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    require_batch(SUCCEEDED(device.native.As(&dxgi_device)) &&
        SUCCEEDED(dxgi_device->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) && factory != nullptr,
        "F113: actual WARP swap-chain factory");
    return factory;
}

ComPtr<IDXGISwapChain> f113_real_chain(BatchDevice& device, IDXGIFactory2* factory, HWND window,
    UINT width, UINT height)
{
    DXGI_SWAP_CHAIN_DESC desc = f113_swap_chain_desc(window, width, height);
    ComPtr<IDXGISwapChain> chain;
    require_batch(SUCCEEDED(factory->CreateSwapChain(device.native.Get(), &desc, &chain)) &&
        chain != nullptr, "F113: create real WARP swap chain");
    return chain;
}

ComPtr<ID3D11Texture2D> f113_get_buffer(IDXGISwapChain* chain, const char* scenario, const char* message)
{
    require_batch(chain != nullptr, "F113: GetBuffer on a null swap-chain wrapper");
    ID3D11Texture2D* buffer = nullptr;
    const HRESULT hr = chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&buffer));
    std::printf("F113 state %s: GetBuffer hr=%08x buffer=%p\n", scenario, static_cast<unsigned>(hr), buffer);
    std::fflush(stdout);
    require_batch(SUCCEEDED(hr) && buffer != nullptr, message);
    ComPtr<ID3D11Texture2D> result;
    result.Attach(buffer);
    return result;
}

void f113_require_solid_pixels(BatchDevice& device, ID3D11Texture2D* texture, UINT width, UINT height,
    const char* scenario)
{
    require_batch(texture != nullptr, "F113: readback target is null");
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    require_batch(desc.Width == width && desc.Height == height,
        "F113: buffer dimensions stay at the game desc");
    // A staging texture cannot be multisampled.  The target must be the
    // single-sample game/back-buffer texture; MSAA targets need
    // ResolveSubresource into a single-sample staging texture instead.
    require_batch(desc.SampleDesc.Count == 1,
        "F113: readback target must be single-sample (MSAA needs ResolveSubresource)");

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;
    require_batch(stagingDesc.Width == desc.Width && stagingDesc.Height == desc.Height &&
        stagingDesc.Format == desc.Format && stagingDesc.MipLevels == desc.MipLevels &&
        stagingDesc.ArraySize == desc.ArraySize,
        "F113: staging desc keeps the target width/height/format/mip/array");
    ComPtr<ID3D11Texture2D> staging;
    const HRESULT stagingHr = device.native->CreateTexture2D(&stagingDesc, nullptr, &staging);
    std::printf("F113 state %s: staging hr=%08x staging=%p target=%p samples=%u\n", scenario,
        static_cast<unsigned>(stagingHr), staging.Get(), texture, desc.SampleDesc.Count);
    std::fflush(stdout);
    if (FAILED(stagingHr)) {
        std::fprintf(stderr,
            "F113 staging create failed hr=%08x target=%ux%u fmt=%u mip=%u array=%u samples=%u\n",
            static_cast<unsigned>(stagingHr), desc.Width, desc.Height,
            static_cast<unsigned>(desc.Format), desc.MipLevels, desc.ArraySize, desc.SampleDesc.Count);
    }
    require_batch(SUCCEEDED(stagingHr) && staging != nullptr,
        "F113: create actual staging readback");
    device.context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)) &&
        mapped.pData != nullptr, "F113: map the actual readback");
    require_batch(mapped.RowPitch >= width * sizeof(UINT), "F113: readback row pitch covers the row");
    bool equal = true;
    unsigned long long hash = 1469598103934665603ull;
    for (UINT y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const UINT*>(
            static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * y);
        for (UINT x = 0; x < width; ++x) {
            equal &= row[x] == 0xFF0000FFu;
            for (unsigned byteIndex = 0; byteIndex < sizeof(UINT); ++byteIndex) {
                hash ^= (row[x] >> (byteIndex * 8)) & 0xFFu;
                hash *= 1099511628211ull;
            }
        }
    }
    device.context->Unmap(staging.Get(), 0);
    std::printf("F113 readback %ux%u hash=%016llx\n", width, height, hash);
    std::fflush(stdout);
    require_batch(equal, "F113: same fixture produces the same solid-red pixels on every row");
}

void f113_clear_buffer(BatchDevice& device, ID3D11Texture2D* texture, UINT width, UINT height,
    const char* scenario)
{
    require_batch(texture != nullptr, "F113: clear target is null");
    ID3D11RenderTargetView* view = nullptr;
    require_batch(SUCCEEDED(device.native->CreateRenderTargetView(texture, nullptr, &view)) && view,
        "F113: create an RTV on the returned buffer");
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    device.context->ClearRenderTargetView(view, red);
    f113_require_solid_pixels(device, texture, width, height, scenario);
    view->Release();
}

void exercise_f113_swap_chain(unsigned scenarios)
{
    F113CrashLogScope av_log;
    F113ConfigScope config;
    BatchDevice device;
    HuntingWindow window;
    ComPtr<IDXGIFactory2> factory = f113_factory(device);

    f113_reset_maps();
    f113_load_map(GetModuleHandleW(nullptr), L"dx11_upstream_regression.map");
    f113_load_map(GetModuleHandleW(L"d3d11.dll"), L"d3d11.map");

    // 1. Normal path: SCREEN_UPSCALING == 0.  This is the branch that must
    //    keep using the untouched plain HackerSwapChain code.
    if (scenarios & F113_NORMAL)
    {
        F113CaseScope trace("1-normal");
        G->SCREEN_UPSCALING = 0;
        G->UPSCALE_MODE = 0;
        G->SCREEN_WIDTH = G->SCREEN_HEIGHT = -1;

        DXGI_SWAP_CHAIN_DESC desc = f113_swap_chain_desc(window.handle, 64, 64);
        ComPtr<IDXGISwapChain> chain = f113_real_chain(device, factory.Get(), window.handle, 64, 64);
        trace.phase("real chain created");
        IDXGISwapChain* wrapped = chain.Detach();
        wrap_swap_chain(device.device, &wrapped, &desc, &desc);
        trace.phase("wrap_swap_chain returned");
        require_batch(wrapped != nullptr, "F113: normal path returns a wrapper");
        f113_log_chain_state(trace.label(), wrapped);

        ComPtr<IDXGISwapChain> wrapper;
        wrapper.Attach(wrapped);
        trace.phase("GetBuffer begin");
        ComPtr<ID3D11Texture2D> buffer = f113_get_buffer(wrapper.Get(), trace.label(),
            "F113: normal GetBuffer still succeeds");
        trace.phase("GetBuffer done");
        trace.phase("readback begin");
        f113_clear_buffer(device, buffer.Get(), 64, 64, trace.label());
        trace.phase("readback done");

        DXGI_SWAP_CHAIN_DESC actual{};
        require_batch(SUCCEEDED(wrapper->GetDesc(&actual)) &&
            actual.BufferDesc.Width == 64 && actual.BufferDesc.Height == 64,
            "F113: normal GetDesc is unchanged");
        trace.phase("GetDesc done");
        wrapper.Reset();
        trace.phase("wrapper released");
    }

    // 2. Upscaling success path: the fake resources are created exactly as
    //    before, then the wrapper is constructed from them.
    if (scenarios & F113_MODE1)
    {
        F113CaseScope trace("2-mode1");
        G->SCREEN_UPSCALING = 1;
        G->UPSCALE_MODE = 1;
        G->SCREEN_WIDTH = G->SCREEN_HEIGHT = -1;

        DXGI_SWAP_CHAIN_DESC gameDesc = f113_swap_chain_desc(window.handle, 64, 64);
        DXGI_SWAP_CHAIN_DESC outputDesc = f113_swap_chain_desc(window.handle, 128, 128);
        ComPtr<IDXGISwapChain> chain = f113_real_chain(device, factory.Get(), window.handle, 128, 128);
        trace.phase("real chain created");
        IDXGISwapChain* wrapped = chain.Detach();
        wrap_swap_chain(device.device, &wrapped, &outputDesc, &gameDesc);
        trace.phase("wrap_swap_chain returned");
        require_batch(wrapped != nullptr, "F113: upscaling success returns a wrapper");
        f113_log_chain_state(trace.label(), wrapped);

        trace.phase("GetBuffer begin");
        ComPtr<ID3D11Texture2D> buffer = f113_get_buffer(wrapped, trace.label(),
            "F113: successful upscaling still returns the fake back buffer");
        trace.phase("GetBuffer done");
        trace.phase("readback begin");
        f113_clear_buffer(device, buffer.Get(), 64, 64, trace.label());
        trace.phase("readback done");

        DXGI_SWAP_CHAIN_DESC actual{};
        require_batch(SUCCEEDED(wrapped->GetDesc(&actual)) &&
            actual.BufferDesc.Width == 64 && actual.BufferDesc.Height == 64,
            "F113: successful upscaling keeps the fake-chain GetDesc");
        require_batch(G->SCREEN_UPSCALING == 1,
            "F113: a successful fake-resource creation must not disable upscaling");
        trace.phase("GetDesc done");
        trace.phase("wrapper kept alive (F-006)");

        // Deliberately not Release()d: HackerSwapChain::Release() does
        // `delete this` through the base type, and F-006 (non-virtual
        // destructor) is out of scope for this change.  The audit item runs
        // in its own process, so the leak is bounded to this fixture.
    }

    // 2b. Upscaling success in fake-texture mode (UPSCALE_MODE 0).  This
    //     used to be created by the same constructor and must still return
    //     the same fake texture.
    if (scenarios & F113_MODE0)
    {
        F113CaseScope trace("3-mode0");
        G->SCREEN_UPSCALING = 1;
        G->UPSCALE_MODE = 0;
        G->SCREEN_WIDTH = G->SCREEN_HEIGHT = -1;

        DXGI_SWAP_CHAIN_DESC desc = f113_swap_chain_desc(window.handle, 64, 64);
        ComPtr<IDXGISwapChain> chain = f113_real_chain(device, factory.Get(), window.handle, 64, 64);
        trace.phase("real chain created");
        IDXGISwapChain* wrapped = chain.Detach();
        wrap_swap_chain(device.device, &wrapped, &desc, &desc);
        trace.phase("wrap_swap_chain returned");
        require_batch(wrapped != nullptr, "F113: fake-texture success returns a wrapper");
        f113_log_chain_state(trace.label(), wrapped);

        trace.phase("GetBuffer begin");
        ComPtr<ID3D11Texture2D> buffer = f113_get_buffer(wrapped, trace.label(),
            "F113: fake-texture mode still returns the fake back buffer");
        trace.phase("GetBuffer done");
        trace.phase("readback begin");
        f113_clear_buffer(device, buffer.Get(), 64, 64, trace.label());
        trace.phase("readback done");

        DXGI_SWAP_CHAIN_DESC actual{};
        require_batch(SUCCEEDED(wrapped->GetDesc(&actual)) &&
            actual.BufferDesc.Width == 64 && actual.BufferDesc.Height == 64,
            "F113: fake-texture mode keeps the fake-texture GetDesc");
        require_batch(G->SCREEN_UPSCALING == 1,
            "F113: a successful fake-texture creation must not disable upscaling");
        trace.phase("GetDesc done");
        trace.phase("wrapper kept alive (F-006)");
        // Same F-006 caveat as the fake-swap-chain success block above.
    }

    // 3. Invalid UPSCALE_MODE: deterministic red before the fix (the object
    //    was delivered with both fake members null -> GetBuffer ->
    //    DoubleBeepExit -> 0xC0000135).
    if (scenarios & F113_INVALID_MODE)
    {
        F113CaseScope trace("4-mode99");
        G->SCREEN_UPSCALING = 1;
        G->UPSCALE_MODE = 99;
        G->SCREEN_WIDTH = G->SCREEN_HEIGHT = -1;

        DXGI_SWAP_CHAIN_DESC desc = f113_swap_chain_desc(window.handle, 64, 64);
        ComPtr<IDXGISwapChain> chain = f113_real_chain(device, factory.Get(), window.handle, 64, 64);
        trace.phase("real chain created");
        IDXGISwapChain* wrapped = chain.Detach();
        wrap_swap_chain(device.device, &wrapped, &desc, &desc);
        trace.phase("wrap_swap_chain returned");
        require_batch(wrapped != nullptr, "F113: invalid mode still returns a wrapper object");

        ComPtr<IDXGISwapChain> wrapper;
        wrapper.Attach(wrapped);
        f113_log_chain_state(trace.label(), wrapper.Get());
        trace.phase("GetBuffer begin");
        // On the old code this GetBuffer() hits both-null fake members and
        // DoubleBeepExit()s with 0xC0000135 before the SCREEN_UPSCALING check.
        ComPtr<ID3D11Texture2D> buffer = f113_get_buffer(wrapper.Get(), trace.label(),
            "F113: fallback GetBuffer succeeds after an invalid mode");
        trace.phase("GetBuffer done");
        trace.phase("readback begin");
        f113_clear_buffer(device, buffer.Get(), 64, 64, trace.label());
        trace.phase("readback done");

        DXGI_SWAP_CHAIN_DESC actual{};
        require_batch(SUCCEEDED(wrapper->GetDesc(&actual)) &&
            actual.BufferDesc.Width == 64 && actual.BufferDesc.Height == 64,
            "F113: invalid-mode fallback reports the game dimensions");
        require_batch(G->SCREEN_UPSCALING == 0,
            "F113: invalid upscaling mode falls back to the plain chain");
        trace.phase("GetDesc done");
        wrapper.Reset();
        trace.phase("wrapper released");
    }

    // 4. Injected CreateSwapChain failure: DXGI_FORMAT_UNKNOWN is rejected by
    //    CreateSwapChain, while the fallback ResizeBuffers(..., UNKNOWN, 0)
    //    preserves the real chain's format and only changes width/height.
    //    Before the fix this dereferenced the uninitialised swapChain at
    //    HackerDXGI.cpp:1053.
    if (scenarios & F113_CREATE_FAILURE)
    {
        F113CaseScope trace("5-fallback-create-failure");
        G->SCREEN_UPSCALING = 1;
        G->UPSCALE_MODE = 1;
        G->SCREEN_WIDTH = G->SCREEN_HEIGHT = -1;

        DXGI_SWAP_CHAIN_DESC gameDesc = f113_swap_chain_desc(window.handle, 64, 64);
        gameDesc.BufferDesc.Format = DXGI_FORMAT_UNKNOWN; // CreateSwapChain rejects this
        DXGI_SWAP_CHAIN_DESC outputDesc = f113_swap_chain_desc(window.handle, 128, 128);
        ComPtr<IDXGISwapChain> chain = f113_real_chain(device, factory.Get(), window.handle, 128, 128);
        trace.phase("real chain created");
        IDXGISwapChain* wrapped = chain.Detach();
        wrap_swap_chain(device.device, &wrapped, &outputDesc, &gameDesc);
        trace.phase("wrap_swap_chain returned");
        require_batch(wrapped != nullptr, "F113: failed fake creation still returns a wrapper object");

        ComPtr<IDXGISwapChain> wrapper;
        wrapper.Attach(wrapped);
        f113_log_chain_state(trace.label(), wrapper.Get());
        trace.phase("GetBuffer begin");
        ComPtr<ID3D11Texture2D> buffer = f113_get_buffer(wrapper.Get(), trace.label(),
            "F113: fallback GetBuffer succeeds after CreateSwapChain failure");
        trace.phase("GetBuffer done");
        trace.phase("readback begin");
        f113_clear_buffer(device, buffer.Get(), 64, 64, trace.label());
        trace.phase("readback done");

        DXGI_SWAP_CHAIN_DESC actual{};
        require_batch(SUCCEEDED(wrapper->GetDesc(&actual)) &&
            actual.BufferDesc.Width == 64 && actual.BufferDesc.Height == 64,
            "F113: fallback resize restores the game dimensions");
        require_batch(G->SCREEN_UPSCALING == 0,
            "F113: CreateSwapChain failure falls back to the plain chain without an AV");
        trace.phase("GetDesc done");
        wrapper.Reset();
        trace.phase("wrapper released");
    }
}
}

void test_dx11_f113_swap_chain_fallback_regression()
{
    exercise_f113_swap_chain(F113_ALL);
}

// Separate process entry points so a pre-fix build can show each red exit
// code independently (the first red crash would otherwise stop the test).
void test_dx11_f113_rc_regression()
{
    exercise_f113_swap_chain(F113_INVALID_MODE);
}

void test_dx11_f113_ra_regression()
{
    exercise_f113_swap_chain(F113_CREATE_FAILURE);
}

namespace {
// F-006: HackerSwapChain::Release() does `delete this` through the base type.
// The base must have a virtual destructor so the upscaling wrapper's destructor
// runs; otherwise the fake back buffer reference survives the final Release.
// Measured on a real WARP device and a real hidden swap chain: the fake
// texture's reference count must drop by exactly one across that Release.
void exercise_swap_chain_destructor_protocol()
{
    using namespace dx11_slot_test;
    F113ConfigScope config;
    BatchDevice device;
    HuntingWindow window;
    ComPtr<IDXGIFactory2> factory = f113_factory(device);

    G->SCREEN_UPSCALING = 1;
    G->UPSCALE_MODE = 0;
    G->SCREEN_WIDTH = G->SCREEN_HEIGHT = -1;

    DXGI_SWAP_CHAIN_DESC desc = f113_swap_chain_desc(window.handle, 64, 64);
    ComPtr<IDXGISwapChain> real = f113_real_chain(device, factory.Get(), window.handle, 64, 64);
    IDXGISwapChain* wrapped = real.Detach();
    wrap_swap_chain(device.device, &wrapped, &desc, &desc);
    require_batch(wrapped != nullptr && G->SCREEN_UPSCALING == 1,
        "F-006: real fake-texture upscaling wrapper created");

    // Call through the IDXGISwapChain1 prefix: a shifted or overwritten COM
    // vtable slot would dispatch to the wrong method here.
    IDXGISwapChain1* iface = static_cast<IDXGISwapChain1*>(wrapped);
    DXGI_SWAP_CHAIN_DESC1 actual1{};
    require_batch(SUCCEEDED(iface->GetDesc1(&actual1)) &&
        actual1.Width == 64 && actual1.Height == 64,
        "F-006: IDXGISwapChain1 prefix layout unchanged");

    ComPtr<ID3D11Texture2D> fake;
    require_batch(SUCCEEDED(wrapped->GetBuffer(0, IID_PPV_ARGS(&fake))) && fake,
        "F-006: fake back buffer exists");

    const ULONG before = fake->AddRef();
    fake->Release();
    require_batch(wrapped->Release() == 0, "F-006: final Release reaches zero");
    const ULONG after = fake->AddRef();
    fake->Release();
    std::fprintf(stderr, "F-006 fake back buffer refcounts: before=%lu after=%lu delta=%ld\n",
        before, after, static_cast<long>(after) - static_cast<long>(before));
    std::fflush(stderr);
    require_batch(after + 1 == before,
        "F-006: derived destructor must release the fake back buffer");
    require_batch(device.device->GetHackerSwapChain() == nullptr,
        "F-006: base Release cleanup preserved");

    // A plain HackerSwapChain must stay a plain object even while the
    // hot-reloadable config says upscaling: branching delete on
    // G->SCREEN_UPSCALING would treat it as the derived type.
    G->SCREEN_UPSCALING = 1;
    ComPtr<IDXGISwapChain> plainReal = f113_real_chain(device, factory.Get(), window.handle, 64, 64);
    ComPtr<IDXGISwapChain1> plainOne;
    require_batch(SUCCEEDED(plainReal.As(&plainOne)), "F-006: plain chain offers IDXGISwapChain1");
    plainReal.Reset();
    HackerSwapChain* plain = new HackerSwapChain(plainOne.Detach(), device.device, device.wrapper.get());
    require_batch(plain->Release() == 0,
        "F-006: plain wrapper released while config says upscaling");
}
}

void test_dx11_swapchain_destructor_protocol_regression()
{
    exercise_swap_chain_destructor_protocol();
}
