#pragma once

#include <Windows.h>

#include "DX12HotPathState.hpp"
#include "DX12LogManager.hpp"
#include "DX12ResourceTrackerPrivate.hpp"
#include "DX12RuntimeState.hpp"

class DX12ResourceTrackerHookDiagnostics
{
public:
	static void LogResourceHookCall(const char *api, const void *object)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":\"%s\",\"present\":%ld,\"this\":\"%p\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), object);
	}

	static void LogResourceHookCallWhenTracking(const char *api, const void *object)
	{
		if (DX12HotPathState::skipAll &&
				!DX12ResourceTrackingPolicyUtils::NeedsFullResourceMetadata())
			return;
		LogResourceHookCall(api, object);
	}
};
