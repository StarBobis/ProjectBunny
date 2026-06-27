#include "DX12NativeOverlay.h"

#include <stdint.h>

#include "DX12Profiling.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"
#include "DX12UiActions.h"

static constexpr wchar_t kOverlayWindowClass[] = L"ProjectBunnyDX12ToolWindow";
static constexpr UINT kOverlayWidth = 720;
static constexpr UINT kOverlayHeight = 560;
static constexpr int kOverlayGap = 8;
static constexpr UINT_PTR kUiRefreshTimer = 1;
static constexpr UINT kUiRefreshMs = 100;

enum UiControlId
{
	ID_HUNT_TOGGLE = 1001,
	ID_HUNT_RESET,
	ID_HUNT_COPY,
	ID_VS_PREV,
	ID_VS_NEXT,
	ID_PS_PREV,
	ID_PS_NEXT,
	ID_CS_PREV,
	ID_CS_NEXT,
	ID_IB_PREV,
	ID_IB_NEXT,
	ID_VB_PREV,
	ID_VB_NEXT,
	ID_FRAME_ANALYSIS,
	ID_SHADER_DUMP,
	ID_MOD_RELOAD,
	ID_PROFILING,
};

static SRWLOCK gUiLock = SRWLOCK_INIT;
static HWND gGameWindow = nullptr;
static HWND gOverlayWindow = nullptr;
static HWND gStatusLabel = nullptr;
static HWND gHuntToggleButton = nullptr;
static HWND gProfilingButton = nullptr;
static HANDLE gUiThread = nullptr;
static DWORD gUiThreadId = 0;
static volatile LONG gThreadStarted = 0;
static volatile LONG gShutdownRequested = 0;
static bool gClassRegistered = false;

static void SetSharedGameWindow(HWND hwnd)
{
	AcquireSRWLockExclusive(&gUiLock);
	gGameWindow = hwnd;
	ReleaseSRWLockExclusive(&gUiLock);
}

static HWND GetSharedGameWindow()
{
	AcquireSRWLockShared(&gUiLock);
	HWND hwnd = gGameWindow;
	ReleaseSRWLockShared(&gUiLock);
	return hwnd;
}

static void SetSharedOverlayWindow(HWND hwnd)
{
	AcquireSRWLockExclusive(&gUiLock);
	gOverlayWindow = hwnd;
	ReleaseSRWLockExclusive(&gUiLock);
	DX12SetOverlayWindow(hwnd);
}

static bool GetGameWindowRect(HWND hwnd, RECT *rect)
{
	if (!hwnd || !rect || !IsWindow(hwnd))
		return false;
	if (!GetWindowRect(hwnd, rect))
		return false;
	return rect->right > rect->left && rect->bottom > rect->top;
}

static HWND AddControl(HWND parent, const wchar_t *className, const wchar_t *text,
	DWORD style, int id, int x, int y, int width, int height)
{
	return CreateWindowExW(
		0, className, text, WS_CHILD | WS_VISIBLE | style,
		x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
		DX12GetModule(), nullptr);
}

static HWND AddButton(HWND parent, const wchar_t *text, int id, int x, int y, int width = 130)
{
	return AddControl(parent, L"BUTTON", text, BS_PUSHBUTTON, id, x, y, width, 28);
}

static HWND AddLabel(HWND parent, const wchar_t *text, int x, int y, int width, int height = 22)
{
	return AddControl(parent, L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, x, y, width, height);
}

static void UpdateButtonText()
{
	if (gHuntToggleButton)
		SetWindowTextW(gHuntToggleButton, DX12HuntIsEnabled() ? L"Disable Hunting" : L"Enable Hunting");
	if (gProfilingButton)
		SetWindowTextW(gProfilingButton, DX12Profiling::IsActive() ? L"Disable Profiling" : L"Enable Profiling");
}

static void UpdateStatusText()
{
	if (!gStatusLabel)
		return;
	wchar_t status[1024] = {};
	DX12GetOverlayStatus(status, ARRAYSIZE(status));
	if (!status[0])
		wcsncpy_s(status, L"3DMigoto DX12 hook alive", _TRUNCATE);
	SetWindowTextW(gStatusLabel, status);
	UpdateButtonText();
}

static void FollowGameWindow()
{
	HWND overlay = gOverlayWindow;
	HWND game = GetSharedGameWindow();
	RECT rect = {};
	if (!overlay || !game || !GetGameWindowRect(game, &rect) || !IsWindowVisible(game) || IsIconic(game)) {
		if (overlay)
			ShowWindow(overlay, SW_HIDE);
		return;
	}

	SetWindowPos(overlay, HWND_TOPMOST, rect.right + kOverlayGap, rect.top,
		kOverlayWidth, kOverlayHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void RunCommand(int id)
{
	switch (id) {
	case ID_HUNT_TOGGLE: DX12UiRequestHuntToggle(); break;
	case ID_HUNT_RESET: DX12UiRequestHuntResetSelection(); break;
	case ID_HUNT_COPY: DX12UiRequestHuntCopySelectedHash(); break;
	case ID_VS_PREV: DX12UiRequestHuntPreviousVS(); break;
	case ID_VS_NEXT: DX12UiRequestHuntNextVS(); break;
	case ID_PS_PREV: DX12UiRequestHuntPreviousPS(); break;
	case ID_PS_NEXT: DX12UiRequestHuntNextPS(); break;
	case ID_CS_PREV: DX12UiRequestHuntPreviousCS(); break;
	case ID_CS_NEXT: DX12UiRequestHuntNextCS(); break;
	case ID_IB_PREV: DX12UiRequestHuntPreviousIB(); break;
	case ID_IB_NEXT: DX12UiRequestHuntNextIB(); break;
	case ID_VB_PREV: DX12UiRequestHuntPreviousVB(); break;
	case ID_VB_NEXT: DX12UiRequestHuntNextVB(); break;
	case ID_FRAME_ANALYSIS: DX12UiRequestFrameAnalysis(); break;
	case ID_SHADER_DUMP: DX12UiRequestShaderDump(); break;
	case ID_MOD_RELOAD: DX12UiRequestModReload(); break;
	case ID_PROFILING: DX12UiRequestProfilingToggle(); break;
	default: break;
	}
}

static void BuildControls(HWND hwnd)
{
	gStatusLabel = AddLabel(hwnd, L"3DMigoto DX12 hook alive", 16, 16, 660, 44);

	gHuntToggleButton = AddButton(hwnd, L"Enable Hunting", ID_HUNT_TOGGLE, 16, 76);
	AddButton(hwnd, L"Reset Selection", ID_HUNT_RESET, 156, 76);
	AddButton(hwnd, L"Copy Hash", ID_HUNT_COPY, 296, 76);

	AddLabel(hwnd, L"Shader", 16, 126, 200);
	AddButton(hwnd, L"VS Previous", ID_VS_PREV, 16, 154);
	AddButton(hwnd, L"VS Next", ID_VS_NEXT, 156, 154);
	AddButton(hwnd, L"PS Previous", ID_PS_PREV, 16, 190);
	AddButton(hwnd, L"PS Next", ID_PS_NEXT, 156, 190);
	AddButton(hwnd, L"CS Previous", ID_CS_PREV, 16, 226);
	AddButton(hwnd, L"CS Next", ID_CS_NEXT, 156, 226);

	AddLabel(hwnd, L"Input Assembler", 360, 126, 200);
	AddButton(hwnd, L"IB Previous", ID_IB_PREV, 360, 154);
	AddButton(hwnd, L"IB Next", ID_IB_NEXT, 500, 154);
	AddButton(hwnd, L"VB Previous", ID_VB_PREV, 360, 190);
	AddButton(hwnd, L"VB Next", ID_VB_NEXT, 500, 190);

	AddButton(hwnd, L"Frame Analysis", ID_FRAME_ANALYSIS, 16, 296);
	AddButton(hwnd, L"Shader Dump", ID_SHADER_DUMP, 156, 296);
	AddButton(hwnd, L"Reload Mods", ID_MOD_RELOAD, 296, 296);
	gProfilingButton = AddButton(hwnd, L"Enable Profiling", ID_PROFILING, 436, 296, 150);

	UpdateStatusText();
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_CREATE:
		BuildControls(hwnd);
		SetTimer(hwnd, kUiRefreshTimer, kUiRefreshMs, nullptr);
		return 0;
	case WM_TIMER:
		if (wparam == kUiRefreshTimer) {
			UpdateStatusText();
			FollowGameWindow();
		}
		return 0;
	case WM_COMMAND:
		if (HIWORD(wparam) == BN_CLICKED)
			RunCommand(LOWORD(wparam));
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kUiRefreshTimer);
		SetSharedOverlayWindow(nullptr);
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wparam, lparam);
	}
}

static bool RegisterOverlayWindowClass()
{
	if (gClassRegistered)
		return true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = DX12GetModule();
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wc.lpszClassName = kOverlayWindowClass;
	if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;
	gClassRegistered = true;
	return true;
}

static DWORD WINAPI UiThreadProc(void*)
{
	if (!RegisterOverlayWindowClass()) {
		InterlockedExchange(&gThreadStarted, 0);
		return 0;
	}

	HWND hwnd = CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		kOverlayWindowClass,
		L"3DMigoto DX12",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, kOverlayWidth, kOverlayHeight,
		nullptr, nullptr, DX12GetModule(), nullptr);
	if (!hwnd) {
		InterlockedExchange(&gThreadStarted, 0);
		return 0;
	}

	SetSharedOverlayWindow(hwnd);
	DX12LogJsonFunc("DX12NativeOverlay",
		"\"event\":\"thread_started\",\"hwnd\":\"%p\"", hwnd);
	FollowGameWindow();

	MSG msg = {};
	while (InterlockedCompareExchange(&gShutdownRequested, 0, 0) == 0 &&
	       GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	if (IsWindow(hwnd))
		DestroyWindow(hwnd);
	DX12LogJsonFunc("DX12NativeOverlay", "\"event\":\"thread_stopped\"");
	InterlockedExchange(&gThreadStarted, 0);
	return 0;
}

static void EnsureUiThread()
{
	if (InterlockedCompareExchange(&gThreadStarted, 1, 0) != 0)
		return;
	InterlockedExchange(&gShutdownRequested, 0);
	gUiThread = CreateThread(nullptr, 0, UiThreadProc, nullptr, 0, &gUiThreadId);
	if (!gUiThread) {
		gUiThreadId = 0;
		InterlockedExchange(&gThreadStarted, 0);
		DX12LogJsonFunc("DX12NativeOverlay", "\"event\":\"thread_create_failed\"");
	}
}

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;
	DXGI_SWAP_CHAIN_DESC desc = {};
	if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow && IsWindow(desc.OutputWindow))
		SetSharedGameWindow(desc.OutputWindow);
	EnsureUiThread();
	return true;
}

void DX12NativeOverlayInvalidate(IDXGISwapChain*)
{
}

void DX12NativeOverlayShutdown()
{
	InterlockedExchange(&gShutdownRequested, 1);
	if (gUiThreadId)
		PostThreadMessageW(gUiThreadId, WM_QUIT, 0, 0);
	if (gUiThread) {
		WaitForSingleObject(gUiThread, 1000);
		CloseHandle(gUiThread);
		gUiThread = nullptr;
	}
	gUiThreadId = 0;
	SetSharedGameWindow(nullptr);
	SetSharedOverlayWindow(nullptr);
}

bool DX12NativeOverlayIsOverlayWindow(HWND hwnd)
{
	AcquireSRWLockShared(&gUiLock);
	const bool result = hwnd && hwnd == gOverlayWindow;
	ReleaseSRWLockShared(&gUiLock);
	return result;
}
