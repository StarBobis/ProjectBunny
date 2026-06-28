#include "DX12UiActions.h"

#include <Windows.h>

#include "DX12FrameAnalysis.hpp"
#include "DX12Input.h"
#include "DX12Profiling.h"
#include "DX12ShaderDump.h"
#include "DX12ShaderHunt.h"
#include "DX12State.h"

enum DX12UiActionBits : LONG
{
	DX12_UI_ACTION_HUNT_TOGGLE = 1 << 0,
	DX12_UI_ACTION_HUNT_PREV_VS = 1 << 1,
	DX12_UI_ACTION_HUNT_NEXT_VS = 1 << 2,
	DX12_UI_ACTION_HUNT_PREV_PS = 1 << 3,
	DX12_UI_ACTION_HUNT_NEXT_PS = 1 << 4,
	DX12_UI_ACTION_HUNT_PREV_CS = 1 << 5,
	DX12_UI_ACTION_HUNT_NEXT_CS = 1 << 6,
	DX12_UI_ACTION_HUNT_PREV_IB = 1 << 7,
	DX12_UI_ACTION_HUNT_NEXT_IB = 1 << 8,
	DX12_UI_ACTION_HUNT_PREV_VB = 1 << 9,
	DX12_UI_ACTION_HUNT_NEXT_VB = 1 << 10,
	DX12_UI_ACTION_HUNT_COPY = 1 << 11,
	DX12_UI_ACTION_HUNT_RESET = 1 << 12,
	DX12_UI_ACTION_FRAME_ANALYSIS = 1 << 13,
	DX12_UI_ACTION_SHADER_DUMP = 1 << 14,
	DX12_UI_ACTION_MOD_RELOAD = 1 << 15,
	DX12_UI_ACTION_PROFILING = 1 << 16,
};

static volatile LONG gPendingUiActions = 0;

static void RequestAction(LONG action)
{
	InterlockedOr(&gPendingUiActions, action);
}

void DX12UiRequestHuntToggle() { RequestAction(DX12_UI_ACTION_HUNT_TOGGLE); }
void DX12UiRequestHuntPreviousVS() { RequestAction(DX12_UI_ACTION_HUNT_PREV_VS); }
void DX12UiRequestHuntNextVS() { RequestAction(DX12_UI_ACTION_HUNT_NEXT_VS); }
void DX12UiRequestHuntPreviousPS() { RequestAction(DX12_UI_ACTION_HUNT_PREV_PS); }
void DX12UiRequestHuntNextPS() { RequestAction(DX12_UI_ACTION_HUNT_NEXT_PS); }
void DX12UiRequestHuntPreviousCS() { RequestAction(DX12_UI_ACTION_HUNT_PREV_CS); }
void DX12UiRequestHuntNextCS() { RequestAction(DX12_UI_ACTION_HUNT_NEXT_CS); }
void DX12UiRequestHuntPreviousIB() { RequestAction(DX12_UI_ACTION_HUNT_PREV_IB); }
void DX12UiRequestHuntNextIB() { RequestAction(DX12_UI_ACTION_HUNT_NEXT_IB); }
void DX12UiRequestHuntPreviousVB() { RequestAction(DX12_UI_ACTION_HUNT_PREV_VB); }
void DX12UiRequestHuntNextVB() { RequestAction(DX12_UI_ACTION_HUNT_NEXT_VB); }
void DX12UiRequestHuntCopySelectedHash() { RequestAction(DX12_UI_ACTION_HUNT_COPY); }
void DX12UiRequestHuntResetSelection() { RequestAction(DX12_UI_ACTION_HUNT_RESET); }
void DX12UiRequestFrameAnalysis() { RequestAction(DX12_UI_ACTION_FRAME_ANALYSIS); }
void DX12UiRequestShaderDump() { RequestAction(DX12_UI_ACTION_SHADER_DUMP); }
void DX12UiRequestModReload() { RequestAction(DX12_UI_ACTION_MOD_RELOAD); }
void DX12UiRequestProfilingToggle() { RequestAction(DX12_UI_ACTION_PROFILING); }

static void RunFrameAnalysisRequest()
{
	DX12LogJsonFunc("DX12UiAction", "\"action\":\"FrameAnalysisRequest\"");
	if (!DX12IsHuntingMode()) {
		DX12LogJsonFunc("FrameAnalysisBeginFailed", "\"reason\":\"requires_hunting_mode\"");
		DX12SetOverlayWarning(L"Enable Hunting before Frame Dump");
		return;
	}
	if (DX12FrameAnalysis::Begin()) {
		DX12FrameAnalysis::LogJsonFunc("FrameAnalysisArmed", nullptr);
		DX12FrameAnalysis::RequestCapture();
		DX12SetOverlayStatus(L"Frame analysis armed");
	} else {
		DX12LogJsonFunc("FrameAnalysisBeginFailed", "\"reason\":\"create_directory_failed\"");
		DX12SetOverlayError(L"Frame analysis failed: cannot create directory");
	}
}

bool DX12UiConsumeActions()
{
	const LONG actions = InterlockedExchange(&gPendingUiActions, 0);
	if (!actions)
		return false;

	const bool huntToggleRequested = (actions & DX12_UI_ACTION_HUNT_TOGGLE) != 0;

	if (huntToggleRequested)
		DX12HuntToggle();
	if (!huntToggleRequested && DX12HuntIsEnabled()) {
		if (actions & DX12_UI_ACTION_HUNT_PREV_VS)
			DX12HuntPreviousVS();
		if (actions & DX12_UI_ACTION_HUNT_NEXT_VS)
			DX12HuntNextVS();
		if (actions & DX12_UI_ACTION_HUNT_PREV_PS)
			DX12HuntPreviousPS();
		if (actions & DX12_UI_ACTION_HUNT_NEXT_PS)
			DX12HuntNextPS();
		if (actions & DX12_UI_ACTION_HUNT_PREV_CS)
			DX12HuntPreviousCS();
		if (actions & DX12_UI_ACTION_HUNT_NEXT_CS)
			DX12HuntNextCS();
		if (actions & DX12_UI_ACTION_HUNT_PREV_IB)
			DX12HuntPreviousIB();
		if (actions & DX12_UI_ACTION_HUNT_NEXT_IB)
			DX12HuntNextIB();
		if (actions & DX12_UI_ACTION_HUNT_PREV_VB)
			DX12HuntPreviousVB();
		if (actions & DX12_UI_ACTION_HUNT_NEXT_VB)
			DX12HuntNextVB();
		if (actions & DX12_UI_ACTION_HUNT_COPY)
			DX12HuntCopySelectedHash();
		if (actions & DX12_UI_ACTION_HUNT_RESET)
			DX12HuntResetSelection();
	}
	if (actions & DX12_UI_ACTION_FRAME_ANALYSIS)
		RunFrameAnalysisRequest();
	if (actions & DX12_UI_ACTION_SHADER_DUMP)
		DX12RequestShaderDump();
	if (actions & DX12_UI_ACTION_MOD_RELOAD)
		DX12RequestModReload();
	if (actions & DX12_UI_ACTION_PROFILING)
		DX12Profiling::Toggle();
	return true;
}
