// DXGI/wait hooks, the DX12 runtime shell and UI actions.
// Split from DX12Resources.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12ResourceHookRuntime.h"

// Defined in ShaderTools/DX12NativeOverlay.cpp; the declaration also lives in
// DX12Resources.h, but this header must stay self-contained because it is
// included before that umbrella header in every TU.
void DX12NativeOverlayShutdown();

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
#endif
		DX12_LOG_TEXT(
			"\n*** 3DMigoto DX12 runtime initialized - build descriptor-churn+ia-reuse-v2 ***\n");

		std::wstring configPath = Bunny::MigotoConfigLocator::FindDefaultConfigPath(module);
		ConfigLoaded = Config.Load(configPath.c_str());
		if (ConfigLoaded) {
			DX12ModSetSafeMode(Config.Runtime().dx12SafeMode);
			gDeferredInitialModLoadDelayMs =
				static_cast<DWORD>(Config.Runtime().dx12ConfigInitializationDelayMs);
			DX12_LOG("DX12Config",
				"\"path\":\"%S\",\"safeMode\":%s,\"configInitializationDelayMs\":%lu",
				Config.Path().c_str(),
				Config.Runtime().dx12SafeMode ? "true" : "false",
				static_cast<unsigned long>(gDeferredInitialModLoadDelayMs));
			DX12ModScheduleInitialLoad(Config.Path().c_str());
			DX12OverlayStatusState::SetStatus(
				L"DX12 mod load deferred until startup reload");
			DX12_LOG("DX12ModRuntimeDeferredLoad",
				"\"status\":\"scheduled\",\"path\":\"%S\",\"delayMs\":%lu",
				Config.Path().c_str(),
				static_cast<unsigned long>(gDeferredInitialModLoadDelayMs));
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
		DXGIHooks::Shutdown();
		DX12HookRegistry::UnhookAll();
#if DX12_TIMING_BUILD
		DX12DiagLog::Close();
#endif
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
	HuntReset = 1 << 12,
	FrameAnalysis = 1 << 13,
	ShaderDump = 1 << 14,
	ModReload = 1 << 15,
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
		return true;
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
