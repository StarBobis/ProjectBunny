#pragma once

#include <Windows.h>
#include <dxgi1_2.h>

#include "DX12LogManager.hpp"
#include "MigotoCommandVmUtils.hpp"

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
		FrameSnapshot frame;
		const bool collected = CollectFrameSnapshot(swapChain, &frame);
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
		MigotoCommandVmUtils::SetGlobal(L"window_width", frame.windowWidth);
		MigotoCommandVmUtils::SetGlobal(L"window_height", frame.windowHeight);
		MigotoCommandVmUtils::SetGlobal(L"res_width", frame.resWidth);
		MigotoCommandVmUtils::SetGlobal(L"res_height", frame.resHeight);
		MigotoCommandVmUtils::SetGlobal(L"rt_width", frame.rtWidth);
		MigotoCommandVmUtils::SetGlobal(L"rt_height", frame.rtHeight);
		MigotoCommandVmUtils::SetGlobal(L"cursor_screen_x", frame.cursorScreenX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_screen_y", frame.cursorScreenY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_window_x", frame.cursorWindowX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_window_y", frame.cursorWindowY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_x", frame.cursorX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_y", frame.cursorY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_showing", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobal(L"cursor_visible", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobal(L"cursor_hotspot_x", frame.cursorHotspotX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_hotspot_y", frame.cursorHotspotY);
		MigotoCommandVmUtils::SetGlobal(L"effective_dpi", frame.effectiveDpi);
		MigotoCommandVmUtils::SetGlobal(L"hunting", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"frame_analysis", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"sli", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"stereo_active", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"stereo_available", 0.0);
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

		ICONINFO iconInfo = {};
		if (!GetIconInfo(cursor, &iconInfo))
			return;

		frame->cursorHotspotX = static_cast<double>(iconInfo.xHotspot);
		frame->cursorHotspotY = static_cast<double>(iconInfo.yHotspot);
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
		DX12LogManager::JsonFunc("DX12VmFrameVariables",
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
