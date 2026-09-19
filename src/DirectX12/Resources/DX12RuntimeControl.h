// DXGI/wait hooks, the DX12 runtime shell and UI actions.
// Split from DX12Resources.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12ResourceHookRuntime.h"
#include "../Core/DX12Diagnostics.h"
#include "../Core/Diag/DX12CrashReport.h"

// Defined in ShaderTools/DX12NativeOverlay.cpp; the declaration also lives in
// DX12Resources.h, but this header must stay self-contained because it is
// included before that umbrella header in every TU.
void DX12NativeOverlayShutdown();
// Also ShaderTools/DX12NativeOverlay.cpp. Arms the one-shot permission that
// lets the skipAll fast path create the overlay's GPU objects, so the HUD
// hotkey works even when no mod is doing per-draw work.
void DX12NativeOverlayRequestInit();

class DXGIHooks
{
public:
	static void Install();
	static void Shutdown();
};

class DX12WaitHooks
{
public:
	static void Install();
	static void Shutdown();
};

// Keeps dbghelp symbol resolution offline. Some engines symbolize a native
// stack trace at moments like a DLSS frame-generation toggle; when the
// search path carries a srv*/symsrv* symbol-server token, symsrv+wininet
// blocks the calling thread on a network fetch (observed: render thread
// frozen inside wininet, game dead). These hooks strip network tokens from
// every search path handed to dbghelp; local symbol resolution is untouched.
class DX12DbgHelpHooks
{
public:
	static void Install();
	static void Shutdown();

	// Path-level guard: every search path handed to dbghelp is sanitized, and
	// a session created before this proxy loaded is rewritten.
	static void InstallDbgHelpHooks(HMODULE dbghelp);

	// Hard backstop: symsrv's retrieval exports are stubbed to fail fast, so
	// no symbol-server path - however and whenever it was configured - can
	// reach the network. Covers sessions created before this proxy loaded or
	// on handles we cannot see.
	static void InstallSymSrvHooks(HMODULE symsrv);

	// Walks the loaded-module list and applies both layers to every dbghelp
	// and symsrv copy found. LoadLibrary interception alone was not enough:
	// the engine's own dbghelp lives in the Windows Kits debuggers directory
	// and pulled its sibling symsrv in without passing through the hooked
	// kernel32 exports, so the stubs were never applied and the render thread
	// parked in wininet anyway. Also called from the freeze watchdog while
	// the game makes no progress - the only window in which a late load can
	// still be caught.
	static void Rescan();
};


class DX12Runtime
{
public:
	using LoadRealD3D12Fn = HMODULE (*)();

	inline static bool Initialized = false;
	inline static Bunny::MigotoConfig Config;
	inline static bool ConfigLoaded = false;
	inline static LoadRealD3D12Fn LoadRealD3D12 = nullptr;

	static DWORD WINAPI WorkerThread(void*)
	{
		DX12_LOG_TEXT("DX12 worker thread started\n");
		if (LoadRealD3D12)
			LoadRealD3D12();

		for (int i = 0; i < 200; ++i) {
			DXGIHooks::Install();
#if DX12_TIMING_BUILD
			// Wait-object instrumentation hooks kernel32 exports process-wide;
			// it exists to attribute stall stacks in TIMING builds and must not
			// ship in normal Release (DX11 has no equivalent global wait hook).
			// Safe to install since Nektra now suspends all threads while
			// patching (DX12HookRegistry ctor), eliminating the torn-patch race
			// that previously corrupted waits (permanent 100ms timeout polling,
			// GPU pegged, flicker).
			DX12WaitHooks::Install();
#endif
			// dbghelp's offline guard ships in every build: a single LoadLibrary
			// hook installed here fires whenever the game (or a plugin) loads
			// dbghelp, no matter how late.
			DX12DbgHelpHooks::Install();
			Sleep(50);
		}

		return 0;
	}

	static void Initialize(HINSTANCE module, LoadRealD3D12Fn loadRealD3D12)
	{
		if (Initialized)
			return;
		Initialized = true;
		LoadRealD3D12 = loadRealD3D12;

		DX12RuntimeState::SetModule(module);
#if DX12_TIMING_BUILD
		DX12DiagLog::Open();
		// Install the crash report filter before any D3D12 object exists, so a
		// driver/engine fault on the next startup is diagnosed from inside the
		// process (report + breadcrumbs + minidump in the log directory), and
		// tell it how to park the log writer first: an appended report would
		// otherwise be overwritten by the writer's next buffered flush.
		DX12CrashReportInstall();
		DX12CrashReportSetLogQuiesce(&DX12DiagLog::QuiesceForCrash);
#endif
		DX12_LOG_TEXT(
			"\n*** 3DMigoto DX12 runtime initialized - build descriptor-churn+ia-reuse-v2 ***\n");

		std::wstring configPath = Bunny::MigotoConfigLocator::FindDefaultConfigPath(module);
		ConfigLoaded = Config.Load(configPath.c_str());
		if (ConfigLoaded) {
			DX12_LOG("DX12Config",
				"\"path\":\"%S\"",
				Config.Path().c_str());
			DX12ModScheduleInitialLoad(Config.Path().c_str());
			DX12OverlayStatusState::SetStatus(
				L"DX12 mod load deferred until startup reload");
			DX12_LOG("DX12ModRuntimeDeferredLoad",
				"\"status\":\"scheduled\",\"path\":\"%S\"",
				Config.Path().c_str());
		} else {
			DX12_LOG("DX12Config",
				"\"path\":\"%S\",\"status\":\"missing_or_invalid\",\"error\":\"%S\"",
				configPath.c_str(), Config.Error().c_str());
		}
		DX12HotPathState::Update();

		// Start persistent resource/descriptor tracking queue (wrapper mode)
		DX12ResourceTrackerHooks::Install(nullptr);

		// CreateThread requires a DWORD WINAPI thread entry. Keep the entry
		// as a class method so runtime startup state stays in one module.
		HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		DX12HookRegistry::FinalizeHooks();
	}

	static void Shutdown()
	{
		DX12_LOG_TEXT("*** 3DMigoto DX12 runtime shutting down ***\n");
		DX12AsyncReadbackShutdown();
		DX12NativeOverlayShutdown();
#if DX12_TIMING_BUILD
		DX12WaitHooks::Shutdown();
#endif
		DX12DbgHelpHooks::Shutdown();
		DXGIHooks::Shutdown();
		DX12HookRegistry::UnhookAll();
		// Unconditional on the build mode: a non-TIMING build opens no log, but
		// it still starts the aggregation pump the first time the profiling HUD
		// is switched on, and that thread has to be joined here. Close() is a
		// no-op when neither the file nor the thread exists.
		DX12DiagLog::Close();
		LoadRealD3D12 = nullptr;
		Initialized = false;
	}
};

enum class DX12UiActionBits : LONG
{
	HuntToggle = 1 << 0,
	HuntPreviousVS = 1 << 1,
	HuntNextVS = 1 << 2,
	HuntPreviousPS = 1 << 3,
	HuntNextPS = 1 << 4,
	HuntPreviousCS = 1 << 5,
	HuntNextCS = 1 << 6,
	HuntPreviousIB = 1 << 7,
	HuntNextIB = 1 << 8,
	HuntPreviousVB = 1 << 9,
	HuntNextVB = 1 << 10,
	// Profiling HUD. Deliberately outside the hunting gate below: the HUD is
	// useful with hunting off, and switching it on must not enable the heavy
	// tracking that hunting implies.
	ProfHudCycle = 1 << 11,
	HuntReset = 1 << 12,
	FrameAnalysis = 1 << 13,
	ShaderDump = 1 << 14,
	ModReload = 1 << 15,
	ProfHudFreeze = 1 << 16,
	HuntMarkVS = 1 << 17,
	HuntMarkPS = 1 << 18,
	HuntMarkCS = 1 << 19,
	HuntMarkIB = 1 << 20,
	HuntMarkVB = 1 << 21,
};

class DX12UiActions
{
public:
	inline static volatile LONG PendingActions = 0;

	static void Request(DX12UiActionBits action)
	{
		InterlockedOr(&PendingActions, static_cast<LONG>(action));
	}

	// DX11 [Hunting] key bindings (Dependencies/d3dx.ini parity), polled on
	// the render thread at the Present boundary. Numpad 0 toggles hunting -
	// and with it the in-game green text - as one indivisible state, so there
	// is no way to leave hunting burning tracking resources with the text
	// hidden. Numpad 1-9 / * - navigate and copy PS/VS/IB/VB, Decimal+1/2/3
	// covers CS, Numpad+ clears the selection, F8 dumps a frame, F9 dumps
	// shaders, F10 reloads mods. Navigation keys auto-repeat at the DX11
	// repeat_rate of 6/s; every binding requires all modifiers released.
	static void PollKeyboard()
	{
		static constexpr ULONGLONG kKeyRepeatMs = 166;

		struct KeyBinding
		{
			int vk;
			DX12UiActionBits action;
			bool repeatable;
			bool requiresDecimal;
			bool down = false;
			ULONGLONG lastFireTick = 0;
		};
		static KeyBinding bindings[] = {
			{ VK_NUMPAD0,  DX12UiActionBits::HuntToggle,     false, false },
			{ VK_NUMPAD1,  DX12UiActionBits::HuntPreviousPS, true,  false },
			{ VK_NUMPAD2,  DX12UiActionBits::HuntNextPS,     true,  false },
			{ VK_NUMPAD3,  DX12UiActionBits::HuntMarkPS,     false, false },
			{ VK_NUMPAD4,  DX12UiActionBits::HuntPreviousVS, true,  false },
			{ VK_NUMPAD5,  DX12UiActionBits::HuntNextVS,     true,  false },
			{ VK_NUMPAD6,  DX12UiActionBits::HuntMarkVS,     false, false },
			{ VK_NUMPAD7,  DX12UiActionBits::HuntPreviousIB, true,  false },
			{ VK_NUMPAD8,  DX12UiActionBits::HuntNextIB,     true,  false },
			{ VK_NUMPAD9,  DX12UiActionBits::HuntMarkIB,     false, false },
			{ VK_DIVIDE,   DX12UiActionBits::HuntPreviousVB, true,  false },
			{ VK_MULTIPLY, DX12UiActionBits::HuntNextVB,     true,  false },
			{ VK_SUBTRACT, DX12UiActionBits::HuntMarkVB,     false, false },
			{ VK_NUMPAD1,  DX12UiActionBits::HuntPreviousCS, true,  true },
			{ VK_NUMPAD2,  DX12UiActionBits::HuntNextCS,     true,  true },
			{ VK_NUMPAD3,  DX12UiActionBits::HuntMarkCS,     false, true },
			{ VK_ADD,      DX12UiActionBits::HuntReset,      false, false },
			{ VK_F8,       DX12UiActionBits::FrameAnalysis,  false, false },
			{ VK_F9,       DX12UiActionBits::ShaderDump,     false, false },
			{ VK_F10,      DX12UiActionBits::ModReload,      false, false },
			// Profiling HUD: F11 cycles Off -> Compact -> Summary -> Top ->
			// Sections -> Counters, F12 holds the report the screen is showing
			// so the numbers stop moving while they are read.
			{ VK_F11,      DX12UiActionBits::ProfHudCycle,   false, false },
			{ VK_F12,      DX12UiActionBits::ProfHudFreeze,  false, false },
		};

		const bool decimalDown = (GetAsyncKeyState(VK_DECIMAL) & 0x8000) != 0;
		const bool modifierDown =
			(GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
			(GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
			(GetAsyncKeyState(VK_MENU) & 0x8000) ||
			(GetAsyncKeyState(VK_LWIN) & 0x8000) ||
			(GetAsyncKeyState(VK_RWIN) & 0x8000);
		const ULONGLONG now = GetTickCount64();
		for (KeyBinding &binding : bindings) {
			const bool active = !modifierDown &&
				((GetAsyncKeyState(binding.vk) & 0x8000) != 0) &&
				(decimalDown == binding.requiresDecimal);
			bool fire = false;
			if (active && !binding.down)
				fire = true;
			else if (active && binding.repeatable &&
				now >= binding.lastFireTick + kKeyRepeatMs)
				fire = true;
			if (fire) {
				binding.lastFireTick = now;
				Request(binding.action);
			}
			binding.down = active;
		}
	}

	static bool Consume()
	{
		// Fast path: UI actions are rare (hotkeys / overlay requests only).
		// The previous code exchanged the shared flag on every present; a
		// plain volatile read keeps the common no-action frame lock-free.
		const LONG pending = PendingActions;
		if (!pending)
			return false;
		const LONG actions = InterlockedExchange(&PendingActions, 0);
		if (!actions)
			return false;

		const bool huntToggleRequested = HasAction(actions, DX12UiActionBits::HuntToggle);

		if (huntToggleRequested)
			DX12HuntToggle();
		if (!huntToggleRequested && DX12HuntIsEnabled()) {
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVS))
				DX12HuntPreviousVS();
			if (HasAction(actions, DX12UiActionBits::HuntNextVS))
				DX12HuntNextVS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousPS))
				DX12HuntPreviousPS();
			if (HasAction(actions, DX12UiActionBits::HuntNextPS))
				DX12HuntNextPS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousCS))
				DX12HuntPreviousCS();
			if (HasAction(actions, DX12UiActionBits::HuntNextCS))
				DX12HuntNextCS();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousIB))
				DX12HuntPreviousIB();
			if (HasAction(actions, DX12UiActionBits::HuntNextIB))
				DX12HuntNextIB();
			if (HasAction(actions, DX12UiActionBits::HuntPreviousVB))
				DX12HuntPreviousVB();
			if (HasAction(actions, DX12UiActionBits::HuntNextVB))
				DX12HuntNextVB();
			if (HasAction(actions, DX12UiActionBits::HuntMarkVS))
				DX12HuntCopySelectedHash(HuntCopyTarget::VS);
			if (HasAction(actions, DX12UiActionBits::HuntMarkPS))
				DX12HuntCopySelectedHash(HuntCopyTarget::PS);
			if (HasAction(actions, DX12UiActionBits::HuntMarkCS))
				DX12HuntCopySelectedHash(HuntCopyTarget::CS);
			if (HasAction(actions, DX12UiActionBits::HuntMarkIB))
				DX12HuntCopySelectedHash(HuntCopyTarget::IB);
			if (HasAction(actions, DX12UiActionBits::HuntMarkVB))
				DX12HuntCopySelectedHash(HuntCopyTarget::VB);
			if (HasAction(actions, DX12UiActionBits::HuntReset))
				DX12HuntResetSelection();
		}
		if (HasAction(actions, DX12UiActionBits::FrameAnalysis))
			RunFrameAnalysisRequest();
		if (HasAction(actions, DX12UiActionBits::ShaderDump))
			DX12RequestShaderDump();
		if (HasAction(actions, DX12UiActionBits::ModReload))
			DX12Input::RequestModReload();
		// Outside the hunting gate on purpose: the profiling HUD works with
		// hunting off, and it never touches the hot-path gates.
		if (HasAction(actions, DX12UiActionBits::ProfHudCycle))
			HandleProfilingHudCycle();
		if (HasAction(actions, DX12UiActionBits::ProfHudFreeze))
			HandleProfilingHudFreeze();
		return true;
	}

	static void HandleProfilingHudCycle()
	{
		DX12ProfilingCycleMode();
		// Switching the HUD on has to be able to bring the overlay up even on
		// the skipAll fast path, which is the only path a session with no
		// per-draw mod work ever takes.
		if (DX12ProfilingHud::IsEnabled())
			DX12NativeOverlayRequestInit();
		DX12OverlayStatusState::SetTemporary(
			DX12ProfilingHudStatus(), 1500);
	}

	static void HandleProfilingHudFreeze()
	{
		if (!DX12ProfilingHud::IsEnabled())
			return;
		DX12ProfilingHud::ToggleFreeze();
		DX12OverlayStatusState::SetTemporary(
			DX12ProfilingHudStatus(), 1500);
	}

	// One transient notice line describing the HUD state, so the mode switch is
	// visible even while the report itself is frozen.
	static const wchar_t *DX12ProfilingHudStatus()
	{
		static wchar_t text[96];
		swprintf_s(text, L"Profiling HUD: %S%s",
			DX12ProfilingHud::ModeName(DX12ProfilingHud::GetMode()),
			DX12ProfilingHud::IsFrozen() ? L" (frozen)" : L"");
		return text;
	}

	static bool HasAction(LONG actions, DX12UiActionBits action)
	{
		return (actions & static_cast<LONG>(action)) != 0;
	}

	static void RunFrameAnalysisRequest()
	{
		DX12_LOG("DX12UiAction", "\"action\":\"FrameAnalysisRequest\"");
		if (!DX12RuntimeState::IsHuntingMode()) {
			DX12_LOG("FrameAnalysisBeginFailed", "\"reason\":\"requires_hunting_mode\"");
			DX12OverlayStatusState::SetWarning(L"Enable Hunting before Frame Dump");
			return;
		}
		if (DX12FrameAnalysis::Begin()) {
			DX12FrameAnalysis::LogJsonFunc("FrameAnalysisArmed", nullptr);
			DX12FrameAnalysis::RequestCapture();
			DX12OverlayStatusState::SetStatus(L"Frame analysis armed");
		} else {
			DX12_LOG("FrameAnalysisBeginFailed", "\"reason\":\"create_directory_failed\"");
			DX12OverlayStatusState::SetError(L"Frame analysis failed: cannot create directory");
		}
	}
};
