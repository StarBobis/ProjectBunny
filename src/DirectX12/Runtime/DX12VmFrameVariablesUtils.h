// Migoto command-VM frame variable helpers.
// Split from DX12Runtime.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12FrameAnalysisManifest.h"

#include "../../common/MigotoCommandVmUtils.hpp"

// Frame-boundary breadcrumb, defined in DX12CommandListExecutor.cpp. Declared
// here rather than pulled in through DX12ModRuntimeInternal.h: this header is
// included very early by the runtime headers, and the marker is a single
// relaxed store of a static literal, so a thin interface beats a heavy include.
void DX12MarkStage(const char *stage);

class DX12VmFrameVariablesUtils
{
public:
	class FrameSnapshot
	{
	public:
		HWND hwnd = nullptr;
		double windowWidth = 0.0;
		double windowHeight = 0.0;
		double resWidth = 0.0;
		double resHeight = 0.0;
		double rtWidth = 0.0;
		double rtHeight = 0.0;
		double cursorScreenX = 0.0;
		double cursorScreenY = 0.0;
		double cursorWindowX = 0.0;
		double cursorWindowY = 0.0;
		double cursorX = 0.0;
		double cursorY = 0.0;
		double cursorShowing = 0.0;
		double cursorHotspotX = 0.0;
		double cursorHotspotY = 0.0;
		double effectiveDpi = 96.0;
	};

	static void UpdateForSwapChain(IDXGISwapChain *swapChain)
	{
		// DX11 evaluates frame variables lazily at match time. DX12 pre-collects
		// them at Present; when no command list references them, skip the whole
		// snapshot (swap-chain desc queries, cursor poll, ~20 global writes) so
		// the frame boundary only pays one relaxed load.
		if (!MigotoCommandVmUtils::VariableStore::frameVariableReferenced.load(
				std::memory_order_relaxed))
			return;
		DX12MarkStage("frame_var_collect");
		FrameSnapshot frame;
		const bool collected = CollectFrameSnapshot(swapChain, &frame);
		DX12MarkStage("frame_var_apply");
		ApplyGlobals(frame);
		LogFrameVariables(frame, collected);
	}

	static bool CollectFrameSnapshot(IDXGISwapChain *swapChain, FrameSnapshot *frame)
	{
		if (!frame)
			return false;
		*frame = FrameSnapshot();
		if (!swapChain)
			return false;

		DXGI_SWAP_CHAIN_DESC desc = {};
		if (FAILED(swapChain->GetDesc(&desc)))
			return false;

		frame->hwnd = desc.OutputWindow;
		frame->resWidth = PositiveDimension(desc.BufferDesc.Width);
		frame->resHeight = PositiveDimension(desc.BufferDesc.Height);

		CollectSwapChain1Size(swapChain, frame);
		CollectWindowSize(frame);

		frame->rtWidth = frame->resWidth > 0.0 ? frame->resWidth : frame->windowWidth;
		frame->rtHeight = frame->resHeight > 0.0 ? frame->resHeight : frame->windowHeight;
		frame->windowWidth = ResolveWindowWidth(*frame);
		frame->windowHeight = ResolveWindowHeight(*frame);

		if (frame->resWidth <= 0.0)
			frame->resWidth = frame->windowWidth;
		if (frame->resHeight <= 0.0)
			frame->resHeight = frame->windowHeight;
		if (frame->rtWidth <= 0.0)
			frame->rtWidth = frame->resWidth;
		if (frame->rtHeight <= 0.0)
			frame->rtHeight = frame->resHeight;

		CollectCursor(frame);
		frame->effectiveDpi = ResolveEffectiveDpi(frame->hwnd);
		return true;
	}

	static void ApplyGlobals(const FrameSnapshot &frame)
	{
		// Keys are already normalized (lower-case literals); write only the
		// values that actually changed so a typical frame touches just the
		// cursor entries instead of re-inserting every key into the map.
		MigotoCommandVmUtils::SetGlobalIfChanged(L"window_width", frame.windowWidth);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"window_height", frame.windowHeight);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"res_width", frame.resWidth);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"res_height", frame.resHeight);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"rt_width", frame.rtWidth);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"rt_height", frame.rtHeight);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_screen_x", frame.cursorScreenX);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_screen_y", frame.cursorScreenY);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_window_x", frame.cursorWindowX);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_window_y", frame.cursorWindowY);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_x", frame.cursorX);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_y", frame.cursorY);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_showing", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_visible", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_hotspot_x", frame.cursorHotspotX);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"cursor_hotspot_y", frame.cursorHotspotY);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"effective_dpi", frame.effectiveDpi);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"hunting", 0.0);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"frame_analysis", 0.0);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"sli", 0.0);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"stereo_active", 0.0);
		MigotoCommandVmUtils::SetGlobalIfChanged(L"stereo_available", 0.0);
	}

	static void CollectSwapChain1Size(IDXGISwapChain *swapChain, FrameSnapshot *frame)
	{
		if (!swapChain || !frame)
			return;

		IDXGISwapChain1 *swapChain1 = nullptr;
		if (FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&swapChain1))) || !swapChain1)
			return;

		DXGI_SWAP_CHAIN_DESC1 desc1 = {};
		if (SUCCEEDED(swapChain1->GetDesc1(&desc1))) {
			if (desc1.Width > 0)
				frame->resWidth = PositiveDimension(desc1.Width);
			if (desc1.Height > 0)
				frame->resHeight = PositiveDimension(desc1.Height);
		}
		swapChain1->Release();
	}

	static void CollectWindowSize(FrameSnapshot *frame)
	{
		if (!frame || !frame->hwnd || !IsWindow(frame->hwnd))
			return;

		RECT client = {};
		if (!GetClientRect(frame->hwnd, &client))
			return;

		frame->windowWidth = PositiveSignedDimension(client.right - client.left);
		frame->windowHeight = PositiveSignedDimension(client.bottom - client.top);
	}

	static void CollectCursor(FrameSnapshot *frame)
	{
		if (!frame)
			return;

		CURSORINFO cursorInfo = {};
		cursorInfo.cbSize = sizeof(cursorInfo);
		POINT screenPoint = {};
		if (GetCursorInfo(&cursorInfo)) {
			screenPoint = cursorInfo.ptScreenPos;
			frame->cursorShowing = (cursorInfo.flags & CURSOR_SHOWING) ? 1.0 : 0.0;
			CollectCursorHotspot(cursorInfo.hCursor, frame);
		} else if (!GetCursorPos(&screenPoint)) {
			return;
		}

		frame->cursorScreenX = static_cast<double>(screenPoint.x);
		frame->cursorScreenY = static_cast<double>(screenPoint.y);

		POINT windowPoint = screenPoint;
		if (frame->hwnd && IsWindow(frame->hwnd) && ScreenToClient(frame->hwnd, &windowPoint)) {
			frame->cursorWindowX = static_cast<double>(windowPoint.x);
			frame->cursorWindowY = static_cast<double>(windowPoint.y);
		} else {
			frame->cursorWindowX = frame->cursorScreenX;
			frame->cursorWindowY = frame->cursorScreenY;
		}

		frame->cursorX = ResolveScaledCursor(
			frame->cursorWindowX, frame->cursorScreenX, frame->windowWidth);
		frame->cursorY = ResolveScaledCursor(
			frame->cursorWindowY, frame->cursorScreenY, frame->windowHeight);
	}

	static void CollectCursorHotspot(HCURSOR cursor, FrameSnapshot *frame)
	{
		if (!cursor || !frame)
			return;

		// The cursor handle and hotspot are stable for long periods. GetIconInfo
		// allocates two GDI bitmaps, so doing it on every Present is needlessly
		// expensive on the render thread.
		thread_local HCURSOR cachedCursor = nullptr;
		thread_local LONG cachedHotspotX = 0;
		thread_local LONG cachedHotspotY = 0;
		if (cursor == cachedCursor) {
			frame->cursorHotspotX = static_cast<double>(cachedHotspotX);
			frame->cursorHotspotY = static_cast<double>(cachedHotspotY);
			return;
		}

		ICONINFO iconInfo = {};
		if (!GetIconInfo(cursor, &iconInfo)) {
			cachedCursor = cursor;
			cachedHotspotX = 0;
			cachedHotspotY = 0;
			return;
		}
		cachedCursor = cursor;
		cachedHotspotX = static_cast<LONG>(iconInfo.xHotspot);
		cachedHotspotY = static_cast<LONG>(iconInfo.yHotspot);

		frame->cursorHotspotX = static_cast<double>(cachedHotspotX);
		frame->cursorHotspotY = static_cast<double>(cachedHotspotY);
		if (iconInfo.hbmMask)
			DeleteObject(iconInfo.hbmMask);
		if (iconInfo.hbmColor)
			DeleteObject(iconInfo.hbmColor);
	}

	static double ResolveWindowWidth(const FrameSnapshot &frame)
	{
		if (frame.windowWidth >= 640.0)
			return frame.windowWidth;
		if (frame.resWidth >= 640.0)
			return frame.resWidth;
		if (frame.rtWidth >= 640.0)
			return frame.rtWidth;
		return PositiveSignedDimension(GetSystemMetrics(SM_CXSCREEN));
	}

	static double ResolveWindowHeight(const FrameSnapshot &frame)
	{
		if (frame.windowHeight >= 480.0)
			return frame.windowHeight;
		if (frame.resHeight >= 480.0)
			return frame.resHeight;
		if (frame.rtHeight >= 480.0)
			return frame.rtHeight;
		return PositiveSignedDimension(GetSystemMetrics(SM_CYSCREEN));
	}

	static double ResolveScaledCursor(double windowCoord, double screenCoord, double dimension)
	{
		if (dimension <= 0.000001)
			return 0.0;

		double value = windowCoord / dimension;
		if (Abs(value) > 0.000001)
			return value;

		value = screenCoord / dimension;
		if (Abs(value) > 0.000001)
			return value;
		return 0.0;
	}

	static double ResolveEffectiveDpi(HWND hwnd)
	{
		typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
		static GetDpiForWindowFn getDpiForWindow = LoadGetDpiForWindow();

		UINT dpi = 0;
		if (getDpiForWindow && hwnd)
			dpi = getDpiForWindow(hwnd);
		if (dpi < 96)
			dpi = ResolveDeviceDpi(hwnd);
		return dpi >= 96 ? static_cast<double>(dpi) : 96.0;
	}

	static UINT(WINAPI *LoadGetDpiForWindow())(HWND)
	{
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
			user32 = LoadLibraryW(L"user32.dll");
		return user32 ?
			reinterpret_cast<UINT(WINAPI *)(HWND)>(GetProcAddress(user32, "GetDpiForWindow")) :
			nullptr;
	}

	static UINT ResolveDeviceDpi(HWND hwnd)
	{
		HWND dcWindow = hwnd;
		HDC dc = GetDC(dcWindow);
		if (!dc)
		{
			dcWindow = nullptr;
			dc = GetDC(nullptr);
		}
		if (!dc)
			return 96;

		const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
		ReleaseDC(dcWindow, dc);
		return dpi >= 96 ? static_cast<UINT>(dpi) : 96;
	}

	static double PositiveDimension(UINT value)
	{
		return value > 0 ? static_cast<double>(value) : 0.0;
	}

	static double PositiveSignedDimension(LONG value)
	{
		return value > 0 ? static_cast<double>(value) : 0.0;
	}

	static double Abs(double value)
	{
		return value < 0.0 ? -value : value;
	}

	static void LogFrameVariables(const FrameSnapshot &frame, bool collected)
	{
		// This is diagnostic output, not runtime behavior. Logging one formatted
		// record per Present can serialize the render thread on the log mutex and
		// turn a normal frame into a multi-millisecond CPU stall.
		if (!kVerboseEnabled)
			return;
		DX12LogManager::Fields("DX12VmFrameVariables",
			"\"status\":\"%s\",\"hwnd\":\"%p\",\"windowWidth\":%.0f,\"windowHeight\":%.0f,"
			"\"resWidth\":%.0f,\"resHeight\":%.0f,\"rtWidth\":%.0f,\"rtHeight\":%.0f,"
			"\"cursorScreenX\":%.0f,\"cursorScreenY\":%.0f,\"cursorWindowX\":%.0f,"
			"\"cursorWindowY\":%.0f,\"cursorX\":%.6f,\"cursorY\":%.6f,"
			"\"cursorShowing\":%.0f,\"effectiveDpi\":%.0f",
			collected ? "updated" : "fallback",
			frame.hwnd,
			frame.windowWidth,
			frame.windowHeight,
			frame.resWidth,
			frame.resHeight,
			frame.rtWidth,
			frame.rtHeight,
			frame.cursorScreenX,
			frame.cursorScreenY,
			frame.cursorWindowX,
			frame.cursorWindowY,
			frame.cursorX,
			frame.cursorY,
			frame.cursorShowing,
			frame.effectiveDpi);
	}
};


