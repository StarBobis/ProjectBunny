#include "DX12NativeOverlay.h"

#include <stdint.h>
#include <vector>

#include "DX12Profiling.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"
#include "DX12UiActions.h"

static constexpr wchar_t kOverlayWindowClass[] = L"ProjectBunnyDX12ToolWindow";
static constexpr UINT kOverlayWidth = 820;
static constexpr UINT kOverlayHeight = 620;
static constexpr int kOverlayGap = 8;
static constexpr UINT_PTR kUiRefreshTimer = 1;
static constexpr UINT kUiRefreshMs = 100;

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
	ID_PROFILING,
};

static SRWLOCK gUiLock = SRWLOCK_INIT;
static HWND gGameWindow = nullptr;
static HWND gOverlayWindow = nullptr;
static HWND gStatusLabel = nullptr;
static HWND gHuntToggleButton = nullptr;
static HWND gProfilingButton = nullptr;
static HWND gHashList = nullptr;
static HWND gCategoryButtons[5] = {};
static std::vector<DX12HuntListItem> gListSnapshot;
static HANDLE gUiThread = nullptr;
static DWORD gUiThreadId = 0;
static volatile LONG gThreadStarted = 0;
static volatile LONG gShutdownRequested = 0;
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
	UpdateCategoryButtons();
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
	case ID_HUNT_TOGGLE: DX12UiRequestHuntToggle(); RefreshHashListSnapshot(); break;
	case ID_HUNT_RESET: DX12UiRequestHuntResetSelection(); ClearHashListSelection(); break;
	case ID_HUNT_COPY: DX12UiRequestHuntCopySelectedHash(); break;
	case ID_CATEGORY_VS: gCurrentCategory = DX12_HUNT_LIST_VS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_PS: gCurrentCategory = DX12_HUNT_LIST_PS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_CS: gCurrentCategory = DX12_HUNT_LIST_CS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_IB: gCurrentCategory = DX12_HUNT_LIST_IB; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_VB: gCurrentCategory = DX12_HUNT_LIST_VB; RefreshHashListSnapshot(); break;
	case ID_FRAME_ANALYSIS: DX12UiRequestFrameAnalysis(); break;
	case ID_SHADER_DUMP: DX12UiRequestShaderDump(); break;
	case ID_MOD_RELOAD: DX12UiRequestModReload(); break;
	case ID_PROFILING: DX12UiRequestProfilingToggle(); break;
	default: break;
	}
}

static void BuildControls(HWND hwnd)
{
	gStatusLabel = AddLabel(hwnd, L"3DMigoto DX12 hook alive", 16, 14, 780, 40);

	gHuntToggleButton = AddButton(hwnd, L"Enable Hunting", ID_HUNT_TOGGLE, 16, 62, 150);

	AddLabel(hwnd, L"Category", 16, 112, 120);
	gCategoryButtons[0] = AddButton(hwnd, L"> VS", ID_CATEGORY_VS, 16, 140, 88);
	gCategoryButtons[1] = AddButton(hwnd, L"PS", ID_CATEGORY_PS, 16, 176, 88);
	gCategoryButtons[2] = AddButton(hwnd, L"CS", ID_CATEGORY_CS, 16, 212, 88);
	gCategoryButtons[3] = AddButton(hwnd, L"IB", ID_CATEGORY_IB, 16, 248, 88);
	gCategoryButtons[4] = AddButton(hwnd, L"VB", ID_CATEGORY_VB, 16, 284, 88);

	AddLabel(hwnd, L"Hashes", 124, 112, 420);
	gHashList = AddListBox(hwnd, ID_HASH_LIST, 124, 140, 500, 360);

	AddLabel(hwnd, L"Actions", 644, 112, 140);
	AddButton(hwnd, L"Clear Selection", ID_HUNT_RESET, 644, 140, 140);
	AddButton(hwnd, L"Copy Hash", ID_HUNT_COPY, 644, 176, 140);

	AddButton(hwnd, L"Frame Analysis", ID_FRAME_ANALYSIS, 16, 532);
	AddButton(hwnd, L"Shader Dump", ID_SHADER_DUMP, 156, 532);
	AddButton(hwnd, L"Reload Mods", ID_MOD_RELOAD, 296, 532);
	gProfilingButton = AddButton(hwnd, L"Enable Profiling", ID_PROFILING, 436, 532, 150);

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
