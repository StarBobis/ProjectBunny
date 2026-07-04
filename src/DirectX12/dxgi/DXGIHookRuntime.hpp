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
#include "DXGIHookRegistry.hpp"

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

	static HRESULT PresentSkipPath(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags, volatile LONG *firstPresentLog)
	{
		DXGIHookSignatures::Present original = DXGIHookRegistry::GetPresentOriginal(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		DX12PresentSkipLogUtils::Record("Present", "EnterSkip", swapChain, S_OK);
		if (InterlockedCompareExchange(firstPresentLog, 2, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"before_original_skip\",\"swapchain\":\"%p\"",
				swapChain);
		DX12PresentSkipLogUtils::Record("Present", "BeforeOriginalSkip", swapChain, S_OK);
		HRESULT hr = original(swapChain, syncInterval, flags);
		DX12PresentSkipLogUtils::Record("Present", "AfterOriginalSkip", swapChain, hr);
		if (InterlockedCompareExchange(firstPresentLog, 3, 2) == 2)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"after_original_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		if (DX12RuntimeState::GetPresentCount() > 2)
			RunPostPresentUi(swapChain);
		DX12PresentSkipLogUtils::Record("Present", "AfterUiSkip", swapChain, hr);
		if (InterlockedCompareExchange(firstPresentLog, 4, 3) == 3)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"before_hotpath_update_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12PresentSkipLogUtils::Record("Present", "BeforeHotPathUpdateSkip", swapChain, hr);
		DX12HotPathState::Update();
		DX12PresentSkipLogUtils::Record("Present", "ReturnSkip", swapChain, hr);
		if (InterlockedCompareExchange(firstPresentLog, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"return_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		return hr;
	}

	static HRESULT Present1SkipPath(
		IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters, volatile LONG *firstPresentLog)
	{
		DXGIHookSignatures::Present1 original = DXGIHookRegistry::GetPresent1Original(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		DX12PresentSkipLogUtils::Record("Present1", "EnterSkip", swapChain, S_OK);
		if (InterlockedCompareExchange(firstPresentLog, 2, 1) == 1)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"before_original_skip\",\"swapchain\":\"%p\"",
				swapChain);
		DX12PresentSkipLogUtils::Record("Present1", "BeforeOriginalSkip", swapChain, S_OK);
		HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
		DX12PresentSkipLogUtils::Record("Present1", "AfterOriginalSkip", swapChain, hr);
		if (InterlockedCompareExchange(firstPresentLog, 3, 2) == 2)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"after_original_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		if (DX12RuntimeState::GetPresentCount() > 2)
			RunPostPresentUi(swapChain);
		DX12PresentSkipLogUtils::Record("Present1", "AfterUiSkip", swapChain, hr);
		if (InterlockedCompareExchange(firstPresentLog, 4, 3) == 3)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"before_hotpath_update_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		DX12PresentSkipLogUtils::Record("Present1", "BeforeHotPathUpdateSkip", swapChain, hr);
		DX12HotPathState::Update();
		DX12PresentSkipLogUtils::Record("Present1", "ReturnSkip", swapChain, hr);
		if (InterlockedCompareExchange(firstPresentLog, 5, 4) == 4)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"return_skip\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
				swapChain, hr);
		return hr;
	}

	static HRESULT Present(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
	{
		if (DX12InternalReplayState::IsActive()) {
			DXGIHookSignatures::Present original = DXGIHookRegistry::GetPresentOriginal(swapChain);
			return original ? original(swapChain, syncInterval, flags) : DXGI_ERROR_INVALID_CALL;
		}

		ConsumeUiActionsForFrame();

		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll)
			return PresentSkipPath(swapChain, syncInterval, flags, &firstPresentLog);

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::Present", swapChain);
		DX12_PROFILE_SCOPE(Present);

		LogAndResetPerFrameCounters();

		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DXGIHookSignatures::Present original = DXGIHookRegistry::GetPresentOriginal(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		DXGIHookDiagnostics::LogPresentStage("Present", "beforeOriginal", swapChain);
		LARGE_INTEGER tPresent0, tPresent1, freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = original(swapChain, syncInterval, flags);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>((tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart));
		DXGIHookDiagnostics::LogPresentStage("Present", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present", swapChain, hr);
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		DX12CommandListRuntime::BumpTrackingGeneration();
		RunPostPresentUi(swapChain);

		DXGIHookDiagnostics::LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
		DX12ModBeginFrame();

		DX12HotPathState::Update();
		DXGIHookDiagnostics::LogPresentStage("Present", "afterHotPathUpdate", swapChain, hr);

		CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12Profiling::EndFrame();
		DXGIHookDiagnostics::LogPresentStage("Present", "end", swapChain, hr);
		return hr;
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		if (DX12InternalReplayState::IsActive()) {
			DXGIHookSignatures::Present1 original = DXGIHookRegistry::GetPresent1Original(swapChain);
			return original ? original(swapChain, syncInterval, flags, presentParameters) : DXGI_ERROR_INVALID_CALL;
		}

		ConsumeUiActionsForFrame();

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::JsonFuncFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll);

		if (DX12HotPathState::skipAll)
			return Present1SkipPath(swapChain, syncInterval, flags, presentParameters, &firstPresent1Log);

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain1::Present1", swapChain);

		LogAndResetPerFrameCounters();

		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DXGIHookSignatures::Present1 original = DXGIHookRegistry::GetPresent1Original(swapChain);
		if (!original)
			return DXGI_ERROR_INVALID_CALL;
		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeOriginal", swapChain);
		LARGE_INTEGER tPresent0, tPresent1, freq;
		QueryPerformanceCounter(&tPresent0);
		HRESULT hr = original(swapChain, syncInterval, flags, presentParameters);
		QueryPerformanceCounter(&tPresent1);
		QueryPerformanceFrequency(&freq);
		LONG64 presentUs = (tPresent1.QuadPart - tPresent0.QuadPart) * 1000000 / freq.QuadPart;
		DX12LogManager::JsonFunc("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld", static_cast<long long>(presentUs));
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
		DX12RuntimeState::IncrementPresentCount();
		DX12HuntOnPresent();
		DX12CommandListRuntime::BumpTrackingGeneration();
		RunPostPresentUi(swapChain);

		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
		DX12ModBeginFrame();

		DX12HotPathState::Update();
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterHotPathUpdate", swapChain, hr);

		CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12Profiling::EndFrame();
		DXGIHookDiagnostics::LogPresentStage("Present1", "end", swapChain, hr);
		return hr;
	}
};
