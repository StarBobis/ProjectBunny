#include "DX12Overlay.h"

#include "DX12State.h"

static volatile LONG gOverlayStarting = 0;
static HWND gOverlayTargetWindow = nullptr;
static RECT gOverlayTargetRect = {};

static int MeasureOverlayTextBlock(HDC dc, const wchar_t *text, int *width, int *lines)
{
	if (width)
		*width = 0;
	if (lines)
		*lines = 0;
	if (!text)
		return 0;

	int lineHeight = 0;
	TEXTMETRICW metrics = {};
	if (GetTextMetricsW(dc, &metrics))
		lineHeight = metrics.tmHeight + 3;
	if (!lineHeight)
		lineHeight = 18;

	int maxWidth = 0;
	int lineCount = 0;
	const wchar_t *lineStart = text;
	for (const wchar_t *p = text; ; ++p) {
		if (*p == L'\n' || *p == L'\0') {
			int len = static_cast<int>(p - lineStart);
			SIZE size = {};
			GetTextExtentPoint32W(dc, lineStart, len, &size);
			if (size.cx > maxWidth)
				maxWidth = size.cx;
			lineCount++;
			if (*p == L'\0')
				break;
			lineStart = p + 1;
		}
	}

	if (width)
		*width = maxWidth;
	if (lines)
		*lines = lineCount;
	return lineHeight;
}

static int DrawOverlayTextBlock(HDC dc, const wchar_t *text, int x, int y)
{
	int maxWidth = 0;
	int lines = 0;
	int lineHeight = MeasureOverlayTextBlock(dc, text, &maxWidth, &lines);
	if (!lineHeight)
		return y;

	RECT background = {
		x - 4,
		y - 4,
		x + maxWidth + 8,
		y + lines * lineHeight + 4
	};
	FillRect(dc, &background, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

	int drawY = y;
	const wchar_t *lineStart = text;
	for (const wchar_t *p = text; ; ++p) {
		if (*p == L'\n' || *p == L'\0') {
			int len = static_cast<int>(p - lineStart);
			TextOutW(dc, x, drawY, lineStart, len);
			drawY += lineHeight;
			if (*p == L'\0')
				break;
			lineStart = p + 1;
		}
	}
	return background.bottom;
}

static int DrawOverlayTextBlockCentered(HDC dc, const wchar_t *text, int clientWidth, int y)
{
	int maxWidth = 0;
	int lines = 0;
	int lineHeight = MeasureOverlayTextBlock(dc, text, &maxWidth, &lines);
	if (!lineHeight)
		return y;

	int x = (clientWidth - maxWidth) / 2;
	if (x < 8)
		x = 8;
	return DrawOverlayTextBlock(dc, text, x, y);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	case WM_TIMER:
		DX12ClearExpiredOverlayStatus();
		InvalidateRect(hwnd, nullptr, TRUE);
		return 0;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		RECT client;
		GetClientRect(hwnd, &client);
		HBRUSH transparentBrush = CreateSolidBrush(RGB(1, 1, 1));
		FillRect(dc, &client, transparentBrush);
		DeleteObject(transparentBrush);

		HFONT font = CreateFontW(
			24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
		HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;

		wchar_t text[1024];
		DX12GetOverlayStatus(text, ARRAYSIZE(text));

		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, DX12GetOverlayStatusColor());
		DrawOverlayTextBlockCentered(dc, text, client.right - client.left, 16);

		if (oldFont)
			SelectObject(dc, oldFont);
		if (font)
			DeleteObject(font);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		DX12SetOverlayWindow(nullptr);
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wparam, lparam);
	}
}

DWORD WINAPI DX12OverlayThread(void*)
{
	const wchar_t className[] = L"3DMigotoDX12LightweightOverlay";

	WNDCLASSW wc = {};
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = DX12GetModule();
	wc.lpszClassName = className;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	RegisterClassW(&wc);

	int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int y = GetSystemMetrics(SM_YVIRTUALSCREEN);

	HWND hwnd = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		className,
		L"3DMigoto DX12 Lightweight Overlay",
		WS_POPUP,
		x, y, 1, 1,
		nullptr, nullptr, DX12GetModule(), nullptr);

	if (!hwnd) {
		DX12LogJsonFunc("OverlayWindowCreate",
			"\"status\":\"failed\",\"error\":%lu", GetLastError());
		InterlockedExchange(&gOverlayStarting, 0);
		return 0;
	}

	DX12SetOverlayWindow(hwnd);
	SetLayeredWindowAttributes(hwnd, RGB(1, 1, 1), 220, LWA_COLORKEY | LWA_ALPHA);
	ShowWindow(hwnd, SW_HIDE);
	SetWindowPos(hwnd, HWND_TOPMOST, x, y, 1, 1,
		SWP_NOACTIVATE | SWP_HIDEWINDOW);
	SetTimer(hwnd, 1, 250, nullptr);

	DX12LogJsonFunc("OverlayWindowCreate",
		"\"status\":\"ok\",\"hwnd\":\"%p\"", hwnd);
	InterlockedExchange(&gOverlayStarting, 0);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return 0;
}

void DX12EnsureOverlayWindow()
{
	HWND hwnd = DX12GetOverlayWindow();
	if (hwnd && IsWindow(hwnd))
		return;

	if (InterlockedCompareExchange(&gOverlayStarting, 1, 0) != 0)
		return;

	HANDLE thread = CreateThread(nullptr, 0, DX12OverlayThread, nullptr, 0, nullptr);
	if (thread) {
		CloseHandle(thread);
		return;
	}

	InterlockedExchange(&gOverlayStarting, 0);
	DX12LogJsonFunc("OverlayWindowCreate",
		"\"status\":\"failed\",\"error\":%lu,\"reason\":\"create_thread\"", GetLastError());
}

void DX12ShowOverlayWindowFallback()
{
	DX12EnsureOverlayWindow();
	HWND overlay = DX12GetOverlayWindow();
	if (!overlay || !IsWindow(overlay))
		return;

	int x = GetSystemMetrics(SM_XVIRTUALSCREEN) + 20;
	int y = GetSystemMetrics(SM_YVIRTUALSCREEN) + 20;
	SetWindowPos(overlay, HWND_TOPMOST, x, y, 900, 180,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
	InvalidateRect(overlay, nullptr, FALSE);
}

bool DX12UpdateOverlayWindowForSwapChain(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;

	HWND overlay = DX12GetOverlayWindow();
	if (!overlay || !IsWindow(overlay))
		return false;

	DXGI_SWAP_CHAIN_DESC desc = {};
	if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindow(desc.OutputWindow))
		return false;

	RECT client = {};
	if (!GetClientRect(desc.OutputWindow, &client))
		return false;

	POINT topLeft = { client.left, client.top };
	POINT bottomRight = { client.right, client.bottom };
	if (!ClientToScreen(desc.OutputWindow, &topLeft) ||
	    !ClientToScreen(desc.OutputWindow, &bottomRight))
		return false;

	RECT target = {
		topLeft.x,
		topLeft.y,
		bottomRight.x,
		bottomRight.y
	};
	int width = target.right - target.left;
	int height = target.bottom - target.top;
	if (width <= 0 || height <= 0)
		return false;

	if (gOverlayTargetWindow != desc.OutputWindow ||
	    !EqualRect(&gOverlayTargetRect, &target)) {
		gOverlayTargetWindow = desc.OutputWindow;
		gOverlayTargetRect = target;
		DX12LogJsonFunc("OverlayWindowTarget",
			"\"hwnd\":\"%p\",\"target\":\"%p\",\"x\":%ld,\"y\":%ld,\"width\":%d,\"height\":%d",
			overlay, desc.OutputWindow, target.left, target.top, width, height);
	}

	SetWindowPos(overlay, HWND_TOPMOST, target.left, target.top, width, height,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
	InvalidateRect(overlay, nullptr, FALSE);
	return true;
}

void DX12DrawSwapChainText(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return;

	DXGI_SWAP_CHAIN_DESC desc = {};
	if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindow(desc.OutputWindow))
		return;

	HDC dc = GetDC(desc.OutputWindow);
	if (!dc)
		return;

	int oldBkMode = SetBkMode(dc, TRANSPARENT);
	COLORREF oldTextColor = SetTextColor(dc, DX12GetOverlayStatusColor());
	HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
	wchar_t text[512];
	DX12GetOverlayStatus(text, ARRAYSIZE(text));
	DrawOverlayTextBlock(dc, text, 20, 20);

	if (oldFont)
		SelectObject(dc, oldFont);
	SetTextColor(dc, oldTextColor);
	SetBkMode(dc, oldBkMode);
	ReleaseDC(desc.OutputWindow, dc);
}

void DX12CloseOverlayWindow()
{
	HWND hwnd = DX12GetOverlayWindow();
	if (hwnd)
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
}
