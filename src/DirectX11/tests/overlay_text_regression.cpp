#include "slot_range_test_support.h"
#include "Overlay.h"
#include "OverlayText.h"
#include "profiling.h"
#include "ComScope.h"
#include <ScreenGrab.h>
#include <wincodec.h>
#include <DirectXColors.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <limits>

using namespace dx11_slot_test;
namespace {
std::wstring FormatNarrow(const char* format, ...)
{
    va_list args; va_start(args, format);
    auto result = FormatOverlayText(format, args);
    va_end(args); return result;
}
std::wstring FormatWide(const wchar_t* format, ...)
{
    va_list args; va_start(args, format);
    auto result = FormatOverlayText(format, args);
    va_end(args); return result;
}
class OverlayTestWindow {
public:
    HWND window = nullptr;
    OverlayTestWindow()
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ProjectBunnyOverlayRegression";
        require_batch(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "overlay: register hidden test window");
        window = CreateWindowW(wc.lpszClassName, L"Overlay regression", WS_OVERLAPPEDWINDOW,
            0, 0, 960, 540, nullptr, nullptr, wc.hInstance, nullptr);
        require_batch(window != nullptr, "overlay: create hidden swap-chain window");
    }
    ~OverlayTestWindow() { if (window) DestroyWindow(window); }
};
class OverlayGlobals {
    bool warnings = G->gShowWarnings;
    int hunting = G->hunting;
    unsigned frame = G->frame_no;
    Profiling::Mode mode = Profiling::mode;
public:
    OverlayGlobals()
    {
        G->gShowWarnings = true; G->hunting = HUNTING_MODE_DISABLED;
        Profiling::mode = Profiling::Mode::NONE; ++G->frame_no; ClearNotices();
    }
    ~OverlayGlobals()
    {
        ++G->frame_no; ClearNotices();
        G->gShowWarnings = warnings; G->hunting = hunting; G->frame_no = frame; Profiling::mode = mode;
    }
};

ComPtr<IDXGISwapChain> CreateTestChain(BatchDevice& device, HWND window, DXGI_FORMAT format)
{
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory> factory;
    require_batch(SUCCEEDED(device.native.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))), "overlay: actual DXGI factory");
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = 960; desc.BufferDesc.Height = 540; desc.BufferDesc.Format = format;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> chain;
    require_batch(SUCCEEDED(factory->CreateSwapChain(device.native.Get(), &desc, &chain)), "overlay: real swap chain");
    return chain;
}

std::vector<uint8_t> ReadPixels(BatchDevice& device, IDXGISwapChain* chain, unsigned& width, unsigned& height)
{
    ComPtr<ID3D11Texture2D> buffer, staging;
    require_batch(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))), "overlay: backbuffer readback");
    D3D11_TEXTURE2D_DESC desc; buffer->GetDesc(&desc);
    width = desc.Width; height = desc.Height;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require_batch(SUCCEEDED(device.native->CreateTexture2D(&desc, nullptr, &staging)), "overlay: readback staging");
    device.context->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE map = {};
    require_batch(SUCCEEDED(device.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "overlay: map rendered pixels");
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (unsigned y = 0; y < height; ++y)
        memcpy(pixels.data() + static_cast<size_t>(y) * width * 4, static_cast<uint8_t*>(map.pData) + y * map.RowPitch, width * 4);
    device.context->Unmap(staging.Get(), 0);
    return pixels;
}
void ClearBackbuffer(BatchDevice& device, IDXGISwapChain* chain)
{
    ComPtr<ID3D11Texture2D> buffer;
    ComPtr<ID3D11RenderTargetView> view;
    require_batch(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))) &&
        SUCCEEDED(device.native->CreateRenderTargetView(buffer.Get(), nullptr, &view)), "overlay: clear test target");
    const float clear[] = { 0.025f, 0.035f, 0.06f, 1 };
    device.context->ClearRenderTargetView(view.Get(), clear);
}
}

void test_dx11_overlay_text_regression()
{
    ULONG_PTR originalComToken = 0;
    const auto originalComStatus = CoGetContextToken(&originalComToken);
    require_batch(DecodeOverlayUtf8(u8"中文路径：Mods\\迦南夜吻\\十个按键ctrl.ini") ==
        L"中文路径：Mods\\迦南夜吻\\十个按键ctrl.ini", "overlay: UTF-8 Chinese decode");
    require_batch(DecodeOverlayUtf8("").empty(), "overlay: empty UTF-8");
    require_batch(DecodeOverlayUtf8(std::string("A\xffZ", 3)) == L"A\ufffdZ", "overlay: invalid UTF-8 replacement");
    require_batch(DecodeOverlayUtf8(u8"😀") == L"\U0001f600", "overlay: UTF-8 surrogate pair");
    require_batch(FormatNarrow("%s %S", u8"中文", L"路径") == L"中文 路径", "overlay: narrow printf UTF-8 and wide arguments");
    require_batch(FormatWide(L"%s %S", L"中文", u8"路径") == L"中文 路径", "overlay: wide printf UTF-8 arguments");
    require_batch(FormatNarrow("%s", std::string(4000, 'A').c_str()).size() == 1023, "overlay: bounded format truncation");

    bool rejectedNullDevice = false;
    try { OverlayTextFont invalid(nullptr, L"Segoe UI", 18); }
    catch (const std::invalid_argument&) { rejectedNullDevice = true; }
    require_batch(rejectedNullDevice, "overlay: missing device fails explicitly");
    bool rejectedSize = false;
    try { OverlayTextFont invalid(reinterpret_cast<ID3D11Device*>(1), L"Arial", std::numeric_limits<float>::quiet_NaN()); }
    catch (const std::invalid_argument&) { rejectedSize = true; }
    require_batch(rejectedSize, "overlay: nonfinite size rejected before device access or conversion");
    OverlayGlobals globals;
    BatchDevice device; // WARP, deliberately created WITHOUT BGRA_SUPPORT.
    OverlayTestWindow window;
    OverlayTextFont font(device.native.Get(), L"Consolas", 20);
    font.SetViewport(900, 500);
    const auto oneLine = DirectX::XMVectorGetY(font.MeasureString(L"中文混合 English 123"));
    const auto multiLine = DirectX::XMVectorGetY(font.MeasureString(L"中文混合\nEnglish 123"));
    require_batch(multiLine > oneLine, "overlay: actual multiline layout");
    const auto generated = font.Rasterizations();
    font.MeasureString(L"中文混合\nEnglish 123");
    require_batch(font.Rasterizations() == generated, "overlay: repeated text reuses raster/texture");
    font.SetViewport(80, 500);
    require_batch(DirectX::XMVectorGetY(font.MeasureString(L"中文混合 English 123 中文混合")) > oneLine,
        "overlay: wrapping after resize");
    for (unsigned i = 0; i < 90; ++i) font.MeasureString((L"不同文本 " + std::to_wstring(i)).c_str());
    require_batch(font.CacheEntries() <= 64 && font.CacheBytes() <= 16 * 1024 * 1024,
        "overlay: bounded cache evicts changing text");
    const auto beforeEvicted = font.Rasterizations();
    font.MeasureString(L"中文混合 English 123");
    require_batch(font.Rasterizations() == beforeEvicted + 1, "overlay: evicted text is recreated");
    font.MeasureString(std::wstring(20000, L'中').c_str());
    require_batch(font.CacheBytes() <= 16 * 1024 * 1024, "overlay: long text clips to bounded viewport");
    const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (unsigned i = 0; i < 100; ++i) font.MeasureString((L"释放验证 " + std::to_wstring(i)).c_str());
    require_batch(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdiBefore, "overlay: cache misses leak no GDI handles");
    std::thread worker([&font] { for (unsigned i = 0; i < 40; ++i) font.MeasureString(L"并发中文"); });
    for (unsigned i = 0; i < 40; ++i) { font.SetViewport(80 + i, 500); font.MeasureString(L"缓存失效"); }
    worker.join();
    ULONG_PTR afterComToken = 0;
    require_batch(CoGetContextToken(&afterComToken) == originalComStatus && afterComToken == originalComToken,
        "overlay: renderer neither initializes nor changes thread COM apartment");
    require_batch(!GetModuleHandleW(L"DWrite.dll") && !GetModuleHandleW(L"d2d1.dll"), "overlay: rasterization does not load DirectWrite/Direct2D");
    font.SetViewport(0, 0);
    font.MeasureString(L"边界");
    require_batch(font.CacheBytes() == 4, "overlay: zero-size viewport safely clamped");

    wchar_t executable[MAX_PATH] = {};
    require_batch(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "overlay: locate test artifact directory");
    const auto directory = std::filesystem::path(executable).parent_path();
    bool inBuild = false;
    for (const auto& part : directory) inBuild |= _wcsicmp(part.c_str(), L"build") == 0;
    require_batch(inBuild, "overlay: artifacts stay under build");
    const auto screenshot = directory / L"overlay-chinese.png";

    for (auto format : { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM }) {
        auto chain = CreateTestChain(device, window.window, format);
        Overlay overlay(device.device, device.wrapper.get(), chain.Get());
        ++G->frame_no; ClearNotices();
        LogOverlayW(LOG_DIRE, L"红色错误：中文路径 Mods\\迦南夜吻\\十个按键ctrl.ini\nHash parse error：hash5");
        LogOverlay(LOG_INFO, u8"绿色通知：UTF-8 中文显示正常，English 123，😀");
        LogOverlayW(LOG_WARNING_MONOSPACE, L"橙色警告：等宽统计 VS 5927c81f6515eb11");
        require_batch(GetOverlayNoticeMessages(LOG_DIRE).at(0).find(L"迦南夜吻") != std::wstring::npos,
            "overlay: production wide notice preserves Chinese");
        require_batch(GetOverlayNoticeMessages(LOG_INFO).at(0).find(L"中文显示正常") != std::wstring::npos,
            "overlay: production narrow notice preserves Chinese");
        ClearBackbuffer(device, chain.Get());

        // Exercise actual Overlay save/restore around game-owned GPU state.
        auto sentinel = texture(device, 0xff123456);
        ComPtr<ID3D11ShaderResourceView> srv;
        require_batch(SUCCEEDED(device.native->CreateShaderResourceView(sentinel.Get(), nullptr, &srv)), "overlay: sentinel SRV");
        ID3D11ShaderResourceView* raw = srv.Get();
        device.context->PSSetShaderResources(0, 1, &raw);
        D3D11_VIEWPORT viewport = { 11, 17, 321, 123, 0.1f, 0.9f };
        device.context->RSSetViewports(1, &viewport);
        device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        overlay.DrawOverlay();
        ComPtr<ID3D11ShaderResourceView> restored;
        device.context->PSGetShaderResources(0, 1, &restored);
        D3D11_VIEWPORT after = {}; UINT count = 1;
        device.context->RSGetViewports(&count, &after);
        D3D11_PRIMITIVE_TOPOLOGY topology;
        device.context->IAGetPrimitiveTopology(&topology);
        require_batch(restored.Get() == srv.Get() && count == 1 && memcmp(&after, &viewport, sizeof(viewport)) == 0 &&
            topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "overlay: SRV, viewport and topology restored");
        unsigned width, height;
        const auto pixels = ReadPixels(device, chain.Get(), width, height);
        size_t red = 0, green = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const auto r = pixels[i + (format == DXGI_FORMAT_R8G8B8A8_UNORM ? 0 : 2)];
            const auto g = pixels[i + 1];
            if (r > 80 && r > g * 2) ++red;
            if (g > 80 && g > r * 2) ++green;
        }
        require_batch(red > 100 && green > 100, "overlay: actual red and green glyph pixels rendered");
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            ComScope pngCom;
            require_batch(static_cast<bool>(pngCom), "overlay: PNG encoder COM scope, separate from renderer");
            ComPtr<ID3D11Texture2D> buffer;
            require_batch(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))) &&
                SUCCEEDED(DirectX::SaveWICTextureToFile(device.context.Get(), buffer.Get(), GUID_ContainerFormatPng, screenshot.c_str())),
                "overlay: save actual rendered Chinese screenshot");
        }
        const auto rasterCount = overlay.mFontNotifications->Rasterizations();
        auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 30; ++i) overlay.DrawOverlay();
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        require_batch(overlay.mFontNotifications->Rasterizations() == rasterCount, "overlay: stable notices do not rerasterize across frames");
        std::printf("Overlay WARP cached draw: 30 calls %.3f ms (CPU submission, not game FPS)\n", elapsed);

        require_batch(SUCCEEDED(chain->ResizeBuffers(1, 640, 360, format, 0)), "overlay: no retained backbuffer prevents resize");
        ClearBackbuffer(device, chain.Get()); overlay.DrawOverlay();
        require_batch(overlay.mFontNotifications->Rasterizations() > rasterCount, "overlay: viewport change invalidates raster cache");
        ++G->frame_no; ClearNotices();
        overlay.DrawOverlay();
    }
    // Separate renderer lifetime and device recreation.
    {
        BatchDevice replacement;
        OverlayTextFont replacementFont(replacement.native.Get(), L"Segoe UI", 18);
        replacementFont.MeasureString(L"新设备重建：中文");
    }
    std::printf("PASS: GDI Unicode Chinese, UTF-8, WARP RGBA/BGRA, real Overlay state/resize/cache; screenshot saved\n");
}
