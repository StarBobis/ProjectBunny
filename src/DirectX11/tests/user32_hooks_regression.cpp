#include "slot_range_test_support.h"
#include "../User32Hooks.h"
#include "../cursor.h"
#include "../HackerDXGI.h"

namespace {
std::atomic<decltype(&::GetDoubleClickTime)> original_click_time{::GetDoubleClickTime};
std::atomic<decltype(&::GetSystemMetrics)> original_metrics{::GetSystemMetrics};
UINT WINAPI click_time() { return original_click_time.load()() + 1; }
int WINAPI metrics(int index) { return original_metrics.load()(index); }
}

void test_dx11_user32_hooks()
{
    using dx11_slot_test::require_batch;
    require_batch(!InstallUser32Hooks(nullptr, 0), "user32: empty batch is rejected");
    OpenUser32HooksLog(GetModuleHandleW(nullptr));
    const UINT before = ::GetDoubleClickTime();
    User32Hook bindings[] = {
        BindUser32Hook<original_click_time>("GetDoubleClickTime", click_time),
        BindUser32Hook<original_metrics>("NoSuchProjectBunnyUser32Export", metrics)
    };
    require_batch(!InstallUser32Hooks(bindings, 2) && ::GetDoubleClickTime() == before,
        "user32: failed group leaves the first API native");
    bindings[1].name = "GetSystemMetrics";
    require_batch(InstallUser32Hooks(bindings, 2) && ::GetDoubleClickTime() == before + 1 &&
        InstallUser32Hooks(bindings, 2) && original_click_time.load()() == before,
        "user32: retry and duplicate installation preserve original trampoline");

    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 80, nullptr, nullptr, nullptr, nullptr);
    require_batch(window != nullptr, "user32: create private hidden window");
    G->hWnd = window;
    G->SCREEN_FULLSCREEN = 2;
    G->SCREEN_UPSCALING = 0;
    InstallSetWindowPosHook();
    InstallSetWindowPosHook();
    RECT rect{};
    require_batch(SetWindowPos(window, nullptr, 0, 0, 12, 14, SWP_NOZORDER | SWP_NOACTIVATE) &&
        GetClientRect(window, &rect) && rect.right == 100, "user32: full_screen=2 preserves window size");
    G->hide_cursor = false;
    InstallMouseHooks(false);
    InstallMouseHooks(false);
    G->SCREEN_UPSCALING = 1;
    G->upscaling_hooks_armed = true;
    G->GAME_INTERNAL_WIDTH = 200;
    G->GAME_INTERNAL_HEIGHT = 160;
    G->cursor_upscaling_bypass = true;
    require_batch(GetClientRect(window, &rect) && rect.right == 200 && rect.bottom == 160 &&
        CursorUpscalingBypass_GetClientRect(window, &rect) && rect.right == 100 && rect.bottom == 80,
        "user32: public client size is scaled, bypass uses the native size");
    RemoveUser32Hooks();
    require_batch(::GetDoubleClickTime() == before && GetClientRect(window, &rect) && rect.right == 100,
        "user32: unloading restores the native entry points");
    DestroyWindow(window);
}
