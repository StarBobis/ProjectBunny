// DX12NativeOverlay: Win32 tool window, controls and UI thread.
// Split from DX12ShaderTools.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>



static constexpr wchar_t kOverlayWindowClass[] = L"ProjectBunnyDX12ToolWindow";
static constexpr UINT kOverlayClientWidth = 860;
static constexpr UINT kOverlayClientHeight = 620;
static constexpr int kOverlayGap = 8;
static constexpr UINT_PTR kUiRefreshTimer = 1;
static constexpr UINT kUiRefreshMs = 100;
static constexpr DWORD kOverlayStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
// The tool window must never take activation from the game. Several DX12
// titles aggressively throttle or alter their render path while unfocused.
static constexpr DWORD kOverlayExStyle =
	WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
static constexpr COLORREF kWindowBg = RGB(245, 247, 250);
static constexpr COLORREF kPanelBg = RGB(255, 255, 255);
static constexpr COLORREF kTextColor = RGB(26, 31, 39);

enum UiControlId
{
	ID_HUNT_TOGGLE = 1001,
	ID_HUNT_RESET,
	ID_HUNT_COPY,
	ID_CATEGORY_VS,
	ID_CATEGORY_PS,
	ID_CATEGORY_CS,
	ID_CATEGORY_IB,
	ID_CATEGORY_VB,
	ID_HASH_LIST,
	ID_FRAME_ANALYSIS,
	ID_SHADER_DUMP,
	ID_MOD_RELOAD,
	ID_STATUS_BAR,
};

static SRWLOCK gUiLock = SRWLOCK_INIT;
static SRWLOCK gThreadLock = SRWLOCK_INIT;
static HWND gGameWindow = nullptr;
static HWND gOverlayWindow = nullptr;
static HWND gStatusLabel = nullptr;
static HWND gHuntToggleButton = nullptr;
static HWND gHashList = nullptr;
static bool gNumpad0Down = false;
static HWND gCategoryButtons[5] = {};
static HWND gMainLabels[4] = {};
static HFONT gUiFont = nullptr;
static HFONT gTitleFont = nullptr;
static HBRUSH gWindowBrush = nullptr;
static HBRUSH gPanelBrush = nullptr;
static std::vector<DX12HuntListItem> gListSnapshot;
static HANDLE gUiThread = nullptr;
static DWORD gUiThreadId = 0;
static volatile LONG gThreadStarted = 0;
static volatile LONG gShutdownRequested = 0;
static volatile LONG gLastGameWindowRefreshPresent = -120;
static bool gClassRegistered = false;
static DX12HuntListCategory gCurrentCategory = DX12_HUNT_LIST_VS;
static bool gUpdatingList = false;

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
	DX12OverlayStatusState::SetWindow(hwnd);
}

static bool GetGameWindowRect(HWND hwnd, RECT *rect)
{
	if (!hwnd || !rect || !IsWindow(hwnd))
		return false;
	if (!GetWindowRect(hwnd, rect))
		return false;
	return rect->right > rect->left && rect->bottom > rect->top;
}

static bool IsGameWindowDisplayable(HWND hwnd, RECT *rect)
{
	if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
		return false;

	BOOL cloaked = FALSE;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
		return false;

	RECT client = {};
	if (!GetClientRect(hwnd, &client) || client.right <= client.left || client.bottom <= client.top)
		return false;

	return GetGameWindowRect(hwnd, rect);
}

static bool GetOverlayWindowSize(int *width, int *height)
{
	RECT rect = { 0, 0, static_cast<LONG>(kOverlayClientWidth), static_cast<LONG>(kOverlayClientHeight) };
	if (!AdjustWindowRectEx(&rect, kOverlayStyle, FALSE, kOverlayExStyle))
		return false;
	if (width)
		*width = rect.right - rect.left;
	if (height)
		*height = rect.bottom - rect.top;
	return true;
}

static HWND AddControl(HWND parent, const wchar_t *className, const wchar_t *text,
	DWORD style, int id, int x, int y, int width, int height)
{
	HWND control = CreateWindowExW(
		0, className, text, WS_CHILD | WS_VISIBLE | style,
		x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
		DX12RuntimeState::GetModule(), nullptr);
	if (control && gUiFont)
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
	return control;
}

static HWND AddButton(HWND parent, const wchar_t *text, int id, int x, int y, int width = 130)
{
	return AddControl(parent, L"BUTTON", text, BS_PUSHBUTTON, id, x, y, width, 28);
}

static HWND AddLabel(HWND parent, const wchar_t *text, int x, int y, int width, int height = 22)
{
	return AddControl(parent, L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, x, y, width, height);
}

static HWND AddSectionLabel(HWND parent, const wchar_t *text, int x, int y, int width)
{
	HWND label = AddLabel(parent, text, x, y, width, 24);
	if (label && gTitleFont)
		SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(gTitleFont), TRUE);
	return label;
}

static HWND AddListBox(HWND parent, int id, int x, int y, int width, int height)
{
	return AddControl(parent, L"LISTBOX", L"",
		LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_BORDER | WS_VSCROLL,
		id, x, y, width, height);
}

static int CategoryIndex(DX12HuntListCategory category)
{
	return static_cast<int>(category);
}

static void UpdateCategoryButtons()
{
	static const wchar_t *labels[] = { L"VS", L"PS", L"CS", L"IB", L"VB" };
	for (int i = 0; i < ARRAYSIZE(gCategoryButtons); ++i) {
		if (!gCategoryButtons[i])
			continue;
		wchar_t text[16];
		swprintf_s(text, L"%ls%ls", i == CategoryIndex(gCurrentCategory) ? L"> " : L"", labels[i]);
		SetWindowTextW(gCategoryButtons[i], text);
	}
}

static void RefreshHashListSnapshot()
{
	if (!gHashList)
		return;

	const size_t total = DX12HuntGetListItems(gCurrentCategory, nullptr, 0);
	gListSnapshot.assign(total, DX12HuntListItem());
	if (total)
		DX12HuntGetListItems(gCurrentCategory, gListSnapshot.data(), gListSnapshot.size());

	gUpdatingList = true;
	SendMessageW(gHashList, WM_SETREDRAW, FALSE, 0);
	SendMessageW(gHashList, LB_RESETCONTENT, 0, 0);
	int selectedIndex = -1;
	for (size_t i = 0; i < gListSnapshot.size(); ++i) {
		const int row = static_cast<int>(SendMessageW(
			gHashList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(gListSnapshot[i].label)));
		if (row >= 0 && gListSnapshot[i].selected)
			selectedIndex = row;
	}
	if (selectedIndex >= 0)
		SendMessageW(gHashList, LB_SETCURSEL, selectedIndex, 0);
	SendMessageW(gHashList, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(gHashList, nullptr, TRUE);
	gUpdatingList = false;
	UpdateCategoryButtons();
}

static void ClearHashListSelection()
{
	if (gHashList)
		SendMessageW(gHashList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
	for (DX12HuntListItem &item : gListSnapshot)
		item.selected = false;
}

static void UpdateButtonText()
{
	if (gHuntToggleButton)
		SetWindowTextW(gHuntToggleButton, DX12HuntIsEnabled() ? L"Disable Hunting" : L"Enable Hunting");
}

static void UpdateStatusText()
{
	if (!gStatusLabel)
		return;
	wchar_t status[1024] = {};
	DX12OverlayStatusState::GetStatus(status, ARRAYSIZE(status));
	if (!status[0])
		wcsncpy_s(status, L"3DMigoto DX12 hook alive", _TRUNCATE);
	SendMessageW(gStatusLabel, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(status));
	UpdateButtonText();
	UpdateCategoryButtons();
}

static void FollowGameWindow()
{
	HWND overlay = gOverlayWindow;
	if (!overlay || !IsWindowVisible(overlay))
		return;
	HWND game = GetSharedGameWindow();
	RECT rect = {};
	if (!IsGameWindowDisplayable(game, &rect)) {
		ShowWindow(overlay, SW_HIDE);
		return;
	}

	int width = 0;
	int height = 0;
	GetOverlayWindowSize(&width, &height);
	SetWindowPos(overlay, HWND_TOPMOST, rect.right + kOverlayGap, rect.top,
		width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void HandleOverlayHotkey(HWND hwnd)
{
	const bool down = (GetAsyncKeyState(VK_NUMPAD0) & 0x8000) != 0;
	if (down && !gNumpad0Down) {
		if (IsWindowVisible(hwnd)) {
			ShowWindow(hwnd, SW_HIDE);
			DX12_LOG("DX12NativeOverlay", "event=hidden,hotkey=NUMPAD0");
		} else {
			ShowWindow(hwnd, SW_SHOWNOACTIVATE);
			FollowGameWindow();
			DX12_LOG("DX12NativeOverlay", "event=shown,hotkey=NUMPAD0");
		}
	}
	gNumpad0Down = down;
}

static void RunCommand(int id)
{
	switch (id) {
	case ID_HUNT_TOGGLE: DX12UiActions::RequestHuntToggle(); RefreshHashListSnapshot(); break;
	case ID_HUNT_RESET: DX12UiActions::RequestHuntResetSelection(); ClearHashListSelection(); break;
	case ID_HUNT_COPY: DX12UiActions::RequestHuntCopySelectedHash(); break;
	case ID_CATEGORY_VS: gCurrentCategory = DX12_HUNT_LIST_VS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_PS: gCurrentCategory = DX12_HUNT_LIST_PS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_CS: gCurrentCategory = DX12_HUNT_LIST_CS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_IB: gCurrentCategory = DX12_HUNT_LIST_IB; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_VB: gCurrentCategory = DX12_HUNT_LIST_VB; RefreshHashListSnapshot(); break;
	case ID_FRAME_ANALYSIS: DX12UiActions::RequestFrameAnalysis(); break;
	case ID_SHADER_DUMP: DX12UiActions::RequestShaderDump(); break;
	case ID_MOD_RELOAD: DX12UiActions::RequestModReload(); break;
	default: break;
	}
}

static void CreateUiResources(HWND hwnd)
{
	if (!gUiFont) {
		gUiFont = CreateFontW(
			-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	}
	if (!gTitleFont) {
		gTitleFont = CreateFontW(
			-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	}
	if (!gWindowBrush)
		gWindowBrush = CreateSolidBrush(kWindowBg);
	if (!gPanelBrush)
		gPanelBrush = CreateSolidBrush(kPanelBg);
	SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(gWindowBrush));
}

static HWND AddStatusBar(HWND parent)
{
	HWND status = CreateWindowExW(
		0, STATUSCLASSNAMEW, L"",
		WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
		0, 0, 0, 0, parent,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS_BAR)),
		DX12RuntimeState::GetModule(), nullptr);
	if (status && gUiFont) {
		SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
		SendMessageW(status, SB_SETMINHEIGHT, 24, 0);
		SendMessageW(status, WM_SIZE, 0, 0);
	}
	return status;
}

static void LayoutStatusBar(HWND hwnd)
{
	if (!gStatusLabel)
		return;
	SendMessageW(gStatusLabel, WM_SIZE, 0, 0);
	RECT client = {};
	if (!GetClientRect(hwnd, &client))
		return;
	int parts[] = { client.right - 180, -1 };
	SendMessageW(gStatusLabel, SB_SETPARTS, ARRAYSIZE(parts), reinterpret_cast<LPARAM>(parts));
	SendMessageW(gStatusLabel, SB_SETTEXT, 1, reinterpret_cast<LPARAM>(
		DX12RuntimeState::IsHuntingMode() ? L"Mode: Hunting" : L"Mode: Mod"));
}

static void BuildControls(HWND hwnd)
{
	CreateUiResources(hwnd);

	gStatusLabel = AddStatusBar(hwnd);

	gMainLabels[0] = AddSectionLabel(hwnd, L"Mode", 16, 16, 160);
	gHuntToggleButton = AddButton(hwnd, L"Enable Hunting", ID_HUNT_TOGGLE, 16, 48, 160);

	gMainLabels[1] = AddSectionLabel(hwnd, L"Categories", 16, 100, 160);
	gCategoryButtons[0] = AddButton(hwnd, L"> VS", ID_CATEGORY_VS, 16, 132, 92);
	gCategoryButtons[1] = AddButton(hwnd, L"PS", ID_CATEGORY_PS, 116, 132, 92);
	gCategoryButtons[2] = AddButton(hwnd, L"CS", ID_CATEGORY_CS, 216, 132, 92);
	gCategoryButtons[3] = AddButton(hwnd, L"IB", ID_CATEGORY_IB, 316, 132, 92);
	gCategoryButtons[4] = AddButton(hwnd, L"VB", ID_CATEGORY_VB, 416, 132, 92);

	gMainLabels[2] = AddSectionLabel(hwnd, L"Hashes", 16, 184, 420);
	gHashList = AddListBox(hwnd, ID_HASH_LIST, 16, 216, 560, 308);

	gMainLabels[3] = AddSectionLabel(hwnd, L"Actions", 604, 184, 200);
	AddButton(hwnd, L"Frame Dump", ID_FRAME_ANALYSIS, 604, 216, 180);
	AddButton(hwnd, L"Shader Dump", ID_SHADER_DUMP, 604, 252, 180);
	AddButton(hwnd, L"Reload Mods", ID_MOD_RELOAD, 604, 288, 180);
	AddButton(hwnd, L"Copy Hash", ID_HUNT_COPY, 604, 344, 180);
	AddButton(hwnd, L"Num+ Reset Hunting", ID_HUNT_RESET, 604, 380, 180);

	UpdateStatusText();
	LayoutStatusBar(hwnd);
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
			HandleOverlayHotkey(hwnd);
			if (IsWindowVisible(hwnd)) {
				UpdateStatusText();
				LayoutStatusBar(hwnd);
			}
			FollowGameWindow();
		}
		return 0;
	case WM_SIZE:
		LayoutStatusBar(hwnd);
		return 0;
	case WM_COMMAND:
		if (LOWORD(wparam) == ID_HASH_LIST && HIWORD(wparam) == LBN_SELCHANGE && !gUpdatingList) {
			const int selected = static_cast<int>(SendMessageW(gHashList, LB_GETCURSEL, 0, 0));
			if (selected >= 0 && static_cast<size_t>(selected) < gListSnapshot.size())
				DX12HuntSelectListHash(gCurrentCategory, gListSnapshot[selected].hash);
		} else if (HIWORD(wparam) == BN_CLICKED) {
			RunCommand(LOWORD(wparam));
		}
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kUiRefreshTimer);
		SetSharedOverlayWindow(nullptr);
		PostQuitMessage(0);
		return 0;
	case WM_CTLCOLORSTATIC:
		SetTextColor(reinterpret_cast<HDC>(wparam), kTextColor);
		SetBkColor(reinterpret_cast<HDC>(wparam), kWindowBg);
		return reinterpret_cast<LRESULT>(gWindowBrush);
	case WM_CTLCOLORLISTBOX:
		SetTextColor(reinterpret_cast<HDC>(wparam), kTextColor);
		SetBkColor(reinterpret_cast<HDC>(wparam), kPanelBg);
		return reinterpret_cast<LRESULT>(gPanelBrush);
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
	wc.hInstance = DX12RuntimeState::GetModule();
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
	INITCOMMONCONTROLSEX commonControls = {};
	commonControls.dwSize = sizeof(commonControls);
	commonControls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
	InitCommonControlsEx(&commonControls);

	if (!RegisterOverlayWindowClass()) {
		InterlockedExchange(&gThreadStarted, 0);
		return 0;
	}

	int width = 0;
	int height = 0;
	GetOverlayWindowSize(&width, &height);
	HWND hwnd = CreateWindowExW(
		kOverlayExStyle,
		kOverlayWindowClass,
		L"Hunting Window",
		kOverlayStyle,
		CW_USEDEFAULT, CW_USEDEFAULT, width, height,
		nullptr, nullptr, DX12RuntimeState::GetModule(), nullptr);
	if (!hwnd) {
		InterlockedExchange(&gThreadStarted, 0);
		return 0;
	}

	SetSharedOverlayWindow(hwnd);
	DX12_LOG("DX12NativeOverlay",
		"event=thread_started,hwnd=%p", hwnd);
	ShowWindow(hwnd, SW_HIDE);

	MSG msg = {};
	while (InterlockedCompareExchange(&gShutdownRequested, 0, 0) == 0 &&
	       GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	if (IsWindow(hwnd))
		DestroyWindow(hwnd);
	DX12_LOG("DX12NativeOverlay", "event=thread_stopped");
	InterlockedExchange(&gThreadStarted, 0);
	return 0;
}

static void EnsureUiThread()
{
	AcquireSRWLockExclusive(&gThreadLock);
	if (InterlockedCompareExchange(&gThreadStarted, 1, 0) != 0) {
		ReleaseSRWLockExclusive(&gThreadLock);
		return;
	}
	if (gUiThread) {
		WaitForSingleObject(gUiThread, INFINITE);
		CloseHandle(gUiThread);
		gUiThread = nullptr;
		gUiThreadId = 0;
	}
	InterlockedExchange(&gShutdownRequested, 0);
	gUiThread = CreateThread(nullptr, 0, UiThreadProc, nullptr, 0, &gUiThreadId);
	if (!gUiThread) {
		gUiThreadId = 0;
		InterlockedExchange(&gThreadStarted, 0);
		DX12_LOG("DX12NativeOverlay", "event=thread_create_failed");
	}
	ReleaseSRWLockExclusive(&gThreadLock);
}

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	HWND gameWindow = GetSharedGameWindow();
	const bool refreshGameWindow =
		!gameWindow ||
		!IsWindow(gameWindow) ||
		present >= InterlockedCompareExchange(&gLastGameWindowRefreshPresent, 0, 0) + 120;
	if (refreshGameWindow) {
		DXGI_SWAP_CHAIN_DESC desc = {};
		if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow && IsWindow(desc.OutputWindow))
			SetSharedGameWindow(desc.OutputWindow);
		InterlockedExchange(&gLastGameWindowRefreshPresent, present);
	}
	if (InterlockedCompareExchange(&gThreadStarted, 0, 0) == 0)
		EnsureUiThread();
	return true;
}

void DX12NativeOverlayInvalidate(IDXGISwapChain*)
{
}

void DX12NativeOverlayShutdown()
{
	AcquireSRWLockExclusive(&gThreadLock);
	InterlockedExchange(&gShutdownRequested, 1);
	if (gUiThreadId)
		PostThreadMessageW(gUiThreadId, WM_QUIT, 0, 0);
	if (gUiThread) {
		WaitForSingleObject(gUiThread, INFINITE);
		CloseHandle(gUiThread);
		gUiThread = nullptr;
	}
	gUiThreadId = 0;
	ReleaseSRWLockExclusive(&gThreadLock);
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
