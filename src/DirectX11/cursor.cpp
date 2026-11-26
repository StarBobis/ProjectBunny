#include "cursor.h"

#include <windows.h>
#include <atomic>
#include <vector>

#include "log.h"
#include "globals.h"
#include "User32Hooks.h"
#include <mutex>


static std::mutex mouse_install_mutex;
static bool hook_installed = false;


static std::atomic<HCURSOR> current_cursor{ nullptr };
static std::atomic<HCURSOR> invisible_cursor{ nullptr };

static std::atomic<decltype(&::DefWindowProcA)> trampoline_DefWindowProcA{::DefWindowProcA};
static std::atomic<decltype(&::DefWindowProcW)> trampoline_DefWindowProcW{::DefWindowProcW};

static std::atomic<decltype(&::SetCursor)> trampoline_SetCursor{::SetCursor};
static std::atomic<decltype(&::GetCursor)> trampoline_GetCursor{::GetCursor};
static std::atomic<decltype(&::GetCursorInfo)> trampoline_GetCursorInfo{::GetCursorInfo};
static std::atomic<decltype(&::SetCursorPos)> trampoline_SetCursorPos{::SetCursorPos};
static std::atomic<decltype(&::GetCursorPos)> trampoline_GetCursorPos{::GetCursorPos};
static std::atomic<decltype(&::ScreenToClient)> trampoline_ScreenToClient{::ScreenToClient};
static std::atomic<decltype(&::GetClientRect)> trampoline_GetClientRect{::GetClientRect};



// Create a fully transparent cursor using the system cursor dimensions.
static HCURSOR InvisibleCursor()
{
	static HCURSOR cursor = []() -> HCURSOR
	{
		const int width = GetSystemMetrics(SM_CXCURSOR);
		const int height = GetSystemMetrics(SM_CYCURSOR);

		if (width <= 0 || height <= 0)
			return nullptr;

		const size_t pitch = ((width + 31) / 32) * 4;
		const size_t size = pitch * height;

		std::vector<BYTE> andMask(size, 0xFF);
		std::vector<BYTE> xorMask(size, 0x00);

		return CreateCursor(GetModuleHandleW(nullptr), 0, 0, width, height, andMask.data(), xorMask.data());
	}();

	return cursor;
}



static bool GetUpscalingClientRect(RECT* lpRect)
{
	return G->SCREEN_UPSCALING > 0 && lpRect
		&& trampoline_GetClientRect && trampoline_GetClientRect.load()(G->hWnd, lpRect)
		&& lpRect->right && lpRect->bottom;
}

static LONG ScaleCoordinate(LONG value, LONG numerator, LONG denominator)
{
	return static_cast<LONG>(static_cast<long long>(value) * numerator / denominator);
}



// Track the cursor selected by the game, but keep the hardware cursor invisible.
static HCURSOR WINAPI Hooked_SetCursor(
	_In_opt_ HCURSOR hCursor)
{
	current_cursor.store(hCursor);

	if (G->hide_cursor)
	{
		HCURSOR invisible = invisible_cursor.load();

		if (invisible)
			hCursor = invisible;
	}

	return trampoline_SetCursor.load()(hCursor);
}

static HCURSOR WINAPI Hooked_GetCursor(void)
{
	if (G->hide_cursor)
		return current_cursor.load();
	return trampoline_GetCursor.load()();
}

// Preserve the game's logical cursor in GetCursorInfo while the hardware cursor
// is hidden. The original implementation did this whenever the cursor was showing.
static BOOL WINAPI HideCursor_GetCursorInfo(
	_Inout_ PCURSORINFO pci)
{
	if (!trampoline_GetCursorInfo)
		return FALSE;

	BOOL rc = trampoline_GetCursorInfo.load()(pci);

	if (rc && pci && G->hide_cursor && (pci->flags & CURSOR_SHOWING))
		pci->hCursor = current_cursor.load();

	return rc;
}

static BOOL WINAPI Hooked_GetCursorInfo(
	_Inout_ PCURSORINFO pci)
{
	BOOL rc = HideCursor_GetCursorInfo(pci);
	RECT client;

	if (rc && GetUpscalingClientRect(&client))
	{
		pci->ptScreenPos.x = ScaleCoordinate(pci->ptScreenPos.x, G->GAME_INTERNAL_WIDTH, client.right);
		pci->ptScreenPos.y = ScaleCoordinate(pci->ptScreenPos.y, G->GAME_INTERNAL_HEIGHT, client.bottom);
	}

	return rc;
}

BOOL WINAPI CursorUpscalingBypass_GetCursorInfo(
	_Inout_ PCURSORINFO pci)
{
	if (G->cursor_upscaling_bypass)
	{
		// Bypass coordinate upscaling, but preserve cursor-hiding behavior.
		return HideCursor_GetCursorInfo(pci);
	}
	return ::GetCursorInfo(pci);
}

static BOOL WINAPI Hooked_ScreenToClient(_In_ HWND hWnd, LPPOINT lpPoint)
{
	BOOL rc;
	RECT client;
	bool translate = lpPoint
		&& GetUpscalingClientRect(&client)
		&& G->GAME_INTERNAL_WIDTH && G->GAME_INTERNAL_HEIGHT;

	if (translate)
	{
		// Convert fake game coordinates back to the actual client coordinates.
		lpPoint->x = ScaleCoordinate(lpPoint->x, client.right, G->GAME_INTERNAL_WIDTH);
		lpPoint->y = ScaleCoordinate(lpPoint->y, client.bottom, G->GAME_INTERNAL_HEIGHT);
	}

	rc = trampoline_ScreenToClient.load()(hWnd, lpPoint);

	if (translate)
	{
		// Convert the rc back to the game's internal resolution.
		lpPoint->x = ScaleCoordinate(lpPoint->x, G->GAME_INTERNAL_WIDTH, client.right);
		lpPoint->y = ScaleCoordinate(lpPoint->y, G->GAME_INTERNAL_HEIGHT, client.bottom);
	}

	return rc;
}

BOOL WINAPI CursorUpscalingBypass_ScreenToClient(_In_ HWND hWnd, LPPOINT lpPoint)
{
	if (G->cursor_upscaling_bypass)
		return trampoline_ScreenToClient.load()(hWnd, lpPoint);
	return ::ScreenToClient(hWnd, lpPoint);
}

static BOOL WINAPI Hooked_GetClientRect(_In_ HWND hWnd, _Out_ LPRECT lpRect)
{
	BOOL rc = trampoline_GetClientRect.load()(hWnd, lpRect);

	if (G->upscaling_hooks_armed && rc && G->SCREEN_UPSCALING > 0 && lpRect)
	{
		lpRect->right = G->GAME_INTERNAL_WIDTH;
		lpRect->bottom = G->GAME_INTERNAL_HEIGHT;
	}

	return rc;
}

BOOL WINAPI CursorUpscalingBypass_GetClientRect(_In_ HWND hWnd, _Out_ LPRECT lpRect)
{
	if (G->cursor_upscaling_bypass)
		return trampoline_GetClientRect.load()(hWnd, lpRect);
	return ::GetClientRect(hWnd, lpRect);
}

static BOOL WINAPI Hooked_GetCursorPos(_Out_ LPPOINT lpPoint)
{
	BOOL res = trampoline_GetCursorPos.load()(lpPoint);
	RECT client;

	if (lpPoint && res && GetUpscalingClientRect(&client))
	{
		// Convert screen coordinates to the game's internal resolution.
		lpPoint->x = ScaleCoordinate(lpPoint->x, G->GAME_INTERNAL_WIDTH, client.right);
		lpPoint->y = ScaleCoordinate(lpPoint->y, G->GAME_INTERNAL_HEIGHT, client.bottom);
	}

	return res;
}

static BOOL WINAPI Hooked_SetCursorPos(_In_ int X, _In_ int Y)
{
	RECT client;

	if (GetUpscalingClientRect(&client) && G->GAME_INTERNAL_WIDTH && G->GAME_INTERNAL_HEIGHT)
	{
		// Convert the game's internal coordinates to screen coordinates.
		const int new_x = ScaleCoordinate(X, client.right, G->GAME_INTERNAL_WIDTH);
		const int new_y = ScaleCoordinate(Y, client.bottom, G->GAME_INTERNAL_HEIGHT);
		return trampoline_SetCursorPos.load()(new_x, new_y);
	}

	return trampoline_SetCursorPos.load()(X, Y);
}



// DefWindowProc can bypass our SetCursor hook when processing WM_SETCURSOR.
// Emulate its client-area handling so the cursor is tracked by Hooked_SetCursor.
//
// Parent windows are given the opportunity to handle the message first.
// For non-client areas, defer to the original DefWindowProc implementation.
template <typename Fn>
static LRESULT Hooked_DefWindowProc(
	_In_ HWND   hWnd,
	_In_ UINT   Msg,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam,
	Fn trampoline_DefWindowProc)
{
	if (Msg == WM_SETCURSOR) {
		// XXX: Should we use GetParent or GetAncestor? GetParent can
		// return an "owner" window, while GetAncestor only returns
		// parents... Not sure which the official DefWindowProc uses,
		// but I suspect the answer is GetAncestor, so go with that:
		HWND parent = GetAncestor(hWnd, GA_PARENT);

		if (parent) {
			// Pass the message to the parent window, just like the
			// real DefWindowProc does. This may call back in here
			// if the parent also doesn't handle this message, and
			// we stop processing if the parent handled it.
			LRESULT ret = SendMessage(parent, Msg, wParam, lParam);
			if (ret)
				return ret;
		}

		if (LOWORD(lParam) == HTCLIENT)
		{
			HCURSOR cursor = reinterpret_cast<HCURSOR>(GetClassLongPtr(hWnd, GCLP_HCURSOR));

			if (cursor)
				::SetCursor(cursor);

			// Allow child windows to set their own class cursor.
			return FALSE;
		}

		// Let the original implementation handle non-client areas and edge cases.
	}

	return trampoline_DefWindowProc(hWnd, Msg, wParam, lParam);
}

static LRESULT WINAPI Hooked_DefWindowProcA(_In_ HWND hWnd, _In_ UINT Msg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	return Hooked_DefWindowProc(hWnd, Msg, wParam, lParam, trampoline_DefWindowProcA.load());
}

static LRESULT WINAPI Hooked_DefWindowProcW(_In_ HWND hWnd, _In_ UINT Msg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	return Hooked_DefWindowProc(hWnd, Msg, wParam, lParam, trampoline_DefWindowProcW.load());
}



void InstallMouseHooks(bool hide)
{
	std::lock_guard lock(mouse_install_mutex);
	if (hook_installed)
		return;
	const HCURSOR invisible = InvisibleCursor();
	if (!invisible) {
		LogInfo("Failed to create invisible cursor\n");
		return;
	}
	invisible_cursor.store(invisible);
	current_cursor.store(::GetCursor());

	static User32Hook hooks[] = {
		BindUser32Hook<trampoline_SetCursor>("SetCursor", Hooked_SetCursor),
		BindUser32Hook<trampoline_GetCursor>("GetCursor", Hooked_GetCursor),
		BindUser32Hook<trampoline_GetCursorInfo>("GetCursorInfo", Hooked_GetCursorInfo),
		BindUser32Hook<trampoline_DefWindowProcA>("DefWindowProcA", Hooked_DefWindowProcA),
		BindUser32Hook<trampoline_DefWindowProcW>("DefWindowProcW", Hooked_DefWindowProcW),
		BindUser32Hook<trampoline_SetCursorPos>("SetCursorPos", Hooked_SetCursorPos),
		BindUser32Hook<trampoline_GetCursorPos>("GetCursorPos", Hooked_GetCursorPos),
		BindUser32Hook<trampoline_ScreenToClient>("ScreenToClient", Hooked_ScreenToClient),
		BindUser32Hook<trampoline_GetClientRect>("GetClientRect", Hooked_GetClientRect),
	};
	hook_installed = InstallUser32Hooks(hooks, std::size(hooks));
	if (hook_installed && hide)
		trampoline_SetCursor.load()(invisible);
	LogInfo("Cursor: hook installation %s (hide_cursor=%d)\n",
		hook_installed ? "successful" : "incomplete", hide ? 1 : 0);
}
