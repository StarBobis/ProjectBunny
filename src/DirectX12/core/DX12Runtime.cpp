#include "DX12Runtime.h"

#include "DX12ImGuiOverlay.h"
#include "DX12ModRuntime.h"
#include "DX12Profiling.h"
#include "DX12State.h"
#include "DXGIHooks.h"
#include "MigotoConfig.h"

static bool gInitialized = false;
static Bunny::MigotoConfig gConfig;
static bool gConfigLoaded = false;
static DX12LoadRealD3D12Fn gLoadRealD3D12 = nullptr;

static DWORD WINAPI DX12WorkerThread(void*)
{
	DX12Log("DX12 worker thread started\n");
	if (gLoadRealD3D12)
		gLoadRealD3D12();

	for (int i = 0; i < 200; ++i) {
		DX12InstallDXGIHooks();
		Sleep(50);
	}

	return 0;
}

void BunnyDX12RuntimeInitialize(HINSTANCE module, DX12LoadRealD3D12Fn loadRealD3D12)
{
	if (gInitialized)
		return;
	gInitialized = true;
	gLoadRealD3D12 = loadRealD3D12;

	DX12SetModule(module);
	DX12OpenLogFile();
	DX12Profiling::Init();
	DX12Log("\n*** 3DMigoto DX12 runtime initialized - frame analysis logging ready ***\n");

	std::wstring configPath = Bunny::FindDefaultConfigPath(module);
	gConfigLoaded = gConfig.Load(configPath.c_str());
	if (gConfigLoaded) {
		DX12ModSetSafeMode(gConfig.Runtime().dx12SafeMode);
		DX12LogJsonFunc("DX12Config",
			"\"path\":\"%S\",\"safeMode\":%s,\"overlay\":%s,\"overlayStarted\":true",
			gConfig.Path().c_str(),
			gConfig.Runtime().dx12SafeMode ? "true" : "false",
			gConfig.Runtime().enableOverlay ? "true" : "false");
		std::wstring modStatus;
		bool modWarnings = false;
		bool modErrors = false;
		if (!DX12ModRuntimeLoad(gConfig.Path().c_str(), &modStatus, &modWarnings, &modErrors))
			DX12SetOverlayError(modStatus.c_str());
		else if (modErrors)
			DX12SetOverlayError(modStatus.c_str());
		else if (modWarnings)
			DX12SetOverlayWarning(modStatus.c_str());
	} else {
		DX12LogJsonFunc("DX12Config",
			"\"path\":\"%S\",\"status\":\"missing_or_invalid\",\"error\":\"%S\"",
			configPath.c_str(), gConfig.Error().c_str());
	}
	DX12HotPathUpdate();

	HANDLE thread = CreateThread(nullptr, 0, DX12WorkerThread, nullptr, 0, nullptr);
	if (thread)
		CloseHandle(thread);
	DX12FinalizeHooks();

	DX12EndReleaseStartupLogging();
}

void BunnyDX12RuntimeShutdown()
{
	DX12Log("*** 3DMigoto DX12 runtime shutting down ***\n");
	DX12ImGuiOverlayShutdown();
	DX12UnhookAll();
	DX12CloseLogFile();
	gLoadRealD3D12 = nullptr;
	gInitialized = false;
}
