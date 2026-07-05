#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

#include "DX12BindingTracker.hpp"
#include "DX12CaptureLedger.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12HotPathState.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12NativeOverlay.hpp"
#include "DX12Profiling.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ShaderHunt.hpp"
#include "DX12UiActions.hpp"
#include "DXGIHookDiagnostics.hpp"
#include "hotpath/DX12HotPathTimingUtils.hpp"
#include "utils/DX12OuterTimingProbeUtils.hpp"

class DXGIHookRuntime
{
public:
	class DX12PresentSkipLogUtils
	{
	public:
		inline static volatile LONG64 SkipCalls = 0;
		inline static volatile LONG LastPresentLogged = -1;
		static constexpr LONG PresentInterval = 120;

		static void Record(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr)
		{
			InterlockedIncrement64(&SkipCalls);
			const LONG present = DX12RuntimeState::GetPresentCount();
			const LONG last = InterlockedCompareExchange(&LastPresentLogged, 0, 0);
			const bool shouldLog = last < 0 || present >= last + PresentInterval;
			if (!shouldLog)
				return;
			if (InterlockedCompareExchange(&LastPresentLogged, present, last) != last)
				return;
			DX12LogManager::JsonFunc("DX12PresentHeartbeat",
				"\"api\":\"%s\",\"stage\":\"%s\",\"mode\":\"aggregate_skip\",\"present\":%ld,\"threadId\":%lu,\"swapchain\":\"%p\",\"hr\":\"0x%lx\",\"calls\":%lld",
				api ? api : "",
				stage ? stage : "",
				present,
				GetCurrentThreadId(),
				swapChain,
				hr,
				static_cast<long long>(InterlockedCompareExchange64(&SkipCalls, 0, 0)));
		}
	};

	static void RunPostPresentUi(IDXGISwapChain *swapChain)
	{
		DX12NativeOverlayRender(swapChain);
	}

	static void ConsumeUiActionsForFrame()
	{
		if (!DX12UiActions::Consume())
			return;
		DX12HotPathState::Update();
		DX12CommandListRuntime::BumpTrackingGeneration();
	}

	static void BeginFrameAnalysisCapture(LONG present)
	{
		DX12BindingTracker::BeginFrame();
		DX12FrameAnalysis::BeginCapture();
		DX12CaptureLedger::BeginCapture(static_cast<UINT64>(present));
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12FrameAnalysis::LogJsonFunc("FrameCaptureBegin", "\"present\":%ld", present);
	}

	static void BeginShaderDumpCapture()
	{
		DX12BindingTracker::BeginFrame();
		DX12CaptureLedger::BeginCapture(static_cast<UINT64>(DX12RuntimeState::GetPresentCount()));
		DX12ShaderDumpBeginCapture();
		DX12CommandListRuntime::BumpTrackingGeneration();
	}

	static void LogAndResetPerFrameCounters()
	{
		const LONG draws = DX12RuntimeState::TakeFrameDraws();
		const LONG dispatches = DX12RuntimeState::TakeFrameDispatches();
		if (DX12LogManager::VerboseDiagnosticsEnabled()) {
			DX12LogManager::JsonFunc("DX12PerFrameCounters",
				"\"draws\":%ld,\"dispatches\":%ld", draws, dispatches);
		}
	}

	static void CompleteCaptureOrBeginNext(const char *api, IDXGISwapChain *swapChain, HRESULT hr, bool dumpFrame, bool dumpShaders)
	{
		if (dumpFrame) {
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureEnd",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
			DX12BindingTracker::DrainPendingEvents("FrameCaptureEnd");
			DX12CaptureLedger::EndCapture();
			DX12DumpFrameAnalysis();
			DX12CaptureLedger::Clear();
			DX12FrameAnalysis::End();
		} else if (dumpShaders) {
			DX12BindingTracker::DrainPendingEvents("ShaderDumpEnd");
			DX12CaptureLedger::EndCapture();
			DX12DumpCapturedFrameShaders();
			DX12CaptureLedger::Clear();
		} else if (DX12FrameAnalysis::IsCaptureRequested()) {
			BeginFrameAnalysisCapture(DX12RuntimeState::GetPresentCount());
		} else if (DX12ShaderDumpIsCaptureRequested()) {
			BeginShaderDumpCapture();
		} else if (DX12ShaderDumpIsCapturingFrame() || DX12ModNeedsPreSkinningUavProbe()) {
			DX12BindingTracker::BeginFrame();
			DXGIHookDiagnostics::LogPresentStage(api, "afterBindingBeginFrame", swapChain, hr);
		}
	}

	static HRESULT Present(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive())
			return swapChain->Present(syncInterval, flags);

		DX12OuterTimingProbeUtils::LogPresentEnter("Present", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		ConsumeUiActionsForFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "consume_ui_actions", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll) {
			DX12PresentSkipLogUtils::Record("Present", "BeforeOriginalSkip", swapChain, S_OK);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			HRESULT skipHr = swapChain->Present(syncInterval, flags);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12PresentSkipLogUtils::Record("Present", "AfterOriginalSkip", swapChain, skipHr);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DX12HuntOnPresent();
			if (DX12RuntimeState::GetPresentCount() > 2)
				RunPostPresentUi(swapChain);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, skipHr);
			return skipHr;
		}

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::Present", swapChain);
		DX12_PROFILE_SCOPE(Present);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "beforeOriginal", swapChain);
		LARGE_INTEGER tPresent0, tPresent1, freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = swapChain->Present(syncInterval, flags);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "original_present", swapChain, hr,
			static_cast<LONG64>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DXGIHookDiagnostics::LogPresentStage("Present", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		DX12CommandListRuntime::BumpTrackingGeneration();
		RunPostPresentUi(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "afterHotPathUpdate", swapChain, hr);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::EndFrame();
		DX12_HOTPATH_LOG_AND_RESET();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "end", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, hr);
		return hr;
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive())
			return swapChain->Present1(syncInterval, flags, presentParameters);

		DX12OuterTimingProbeUtils::LogPresentEnter("Present1", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		ConsumeUiActionsForFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "consume_ui_actions", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll) {
			DX12PresentSkipLogUtils::Record("Present1", "BeforeOriginalSkip", swapChain, S_OK);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			HRESULT skipHr = swapChain->Present1(syncInterval, flags, presentParameters);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12PresentSkipLogUtils::Record("Present1", "AfterOriginalSkip", swapChain, skipHr);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DX12HuntOnPresent();
			if (DX12RuntimeState::GetPresentCount() > 2)
				RunPostPresentUi(swapChain);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, skipHr);
			return skipHr;
		}

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain1::Present1", swapChain);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeOriginal", swapChain);
		LARGE_INTEGER tPresent0, tPresent1, freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		LONG64 presentUs = (tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart;
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld", static_cast<long long>(presentUs));
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "original_present", swapChain, hr, presentUs);
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		DX12CommandListRuntime::BumpTrackingGeneration();
		RunPostPresentUi(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterHotPathUpdate", swapChain, hr);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::EndFrame();
		DX12_HOTPATH_LOG_AND_RESET();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "end", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, hr);
		return hr;
	}
};
