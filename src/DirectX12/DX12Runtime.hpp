#pragma once

#include <Windows.h>

#include "DX12NativeOverlay.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12Profiling.hpp"
#include "DX12HotPathState.hpp"
#include "DX12HookRegistry.hpp"
#include "DX12LogManager.hpp"
#include "DX12OverlayStatusState.hpp"
#include "DX12RuntimeState.hpp"
#include "DXGIHooks.hpp"
#include "MigotoConfig.hpp"

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
		DX12LogManager::Text("DX12 worker thread started\n");
		if (LoadRealD3D12)
			LoadRealD3D12();

		for (int i = 0; i < 200; ++i) {
			DXGIHooks::Install();
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
		DX12LogManager::Open();
		DX12Profiling::Init();
		DX12LogManager::Text("\n*** 3DMigoto DX12 runtime initialized - frame analysis logging ready ***\n");

		std::wstring configPath = Bunny::MigotoConfigUtils::FindDefaultConfigPath(module);
		ConfigLoaded = Config.Load(configPath.c_str());
		if (ConfigLoaded) {
			DX12ModSetSafeMode(Config.Runtime().dx12SafeMode);
			DX12LogManager::JsonFunc("DX12Config",
				"\"path\":\"%S\",\"safeMode\":%s,\"overlay\":%s,\"overlayStarted\":true",
				Config.Path().c_str(),
				Config.Runtime().dx12SafeMode ? "true" : "false",
				Config.Runtime().enableOverlay ? "true" : "false");
			std::wstring modStatus;
			bool modWarnings = false;
			bool modErrors = false;
			if (!DX12ModRuntimeLoad(Config.Path().c_str(), &modStatus, &modWarnings, &modErrors))
				DX12OverlayStatusState::SetError(modStatus.c_str());
			else if (modErrors)
				DX12OverlayStatusState::SetError(modStatus.c_str());
			else if (modWarnings)
				DX12OverlayStatusState::SetWarning(modStatus.c_str());
		} else {
			DX12LogManager::JsonFunc("DX12Config",
				"\"path\":\"%S\",\"status\":\"missing_or_invalid\",\"error\":\"%S\"",
				configPath.c_str(), Config.Error().c_str());
		}
		DX12HotPathState::Update();

		// CreateThread requires a DWORD WINAPI thread entry. Keep the entry
		// as a class method so runtime startup state stays in one module.
		HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		DX12HookRegistry::FinalizeHooks();

		DX12LogManager::EndReleaseStartupLogging();
	}

	static void Shutdown()
	{
		DX12LogManager::Text("*** 3DMigoto DX12 runtime shutting down ***\n");
		DX12NativeOverlayShutdown();
		DX12HookRegistry::UnhookAll();
		DX12LogManager::Close();
		LoadRealD3D12 = nullptr;
		Initialized = false;
	}
};
