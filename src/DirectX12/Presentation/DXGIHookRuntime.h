// DXGI hook runtime: swapchain presentation and resize flow.
// Split from DX12Presentation.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12WrapperNamespace.h"

class DXGIHookRuntime
{
public:
	inline static SRWLOCK PrimarySwapChainLock = SRWLOCK_INIT;
	inline static IUnknown *PrimarySwapChainIdentity = nullptr;
	// The native swap-chain pointer is stable across Present calls. This avoids
	// a QueryInterface(IUnknown) on the DX12 Present hot path; identity probing
	// remains available for aliases and newly-created swap chains.
	inline static std::atomic<IDXGISwapChain*> PrimarySwapChainObject{ nullptr };
	inline static volatile LONG SecondarySwapChainLogged = 0;

	static IUnknown *GetSwapChainIdentity(IDXGISwapChain *swapChain)
	{
		if (!swapChain)
			return nullptr;
		IUnknown *identity = nullptr;
		if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&identity))) || !identity)
			return reinterpret_cast<IUnknown*>(swapChain);
		identity->Release();
		return identity;
	}

	static bool IsPrimarySwapChain(const char *api, IDXGISwapChain *swapChain)
	{
		if (!swapChain)
			return false;
		if (PrimarySwapChainObject.load(std::memory_order_acquire) == swapChain)
			return true;
		IUnknown *identity = GetSwapChainIdentity(swapChain);
		if (!identity)
			return false;

		bool primary = false;
		bool selected = false;
		IUnknown *primaryIdentity = nullptr;
		AcquireSRWLockExclusive(&PrimarySwapChainLock);
		if (!PrimarySwapChainIdentity) {
			PrimarySwapChainIdentity = identity;
			PrimarySwapChainObject.store(swapChain, std::memory_order_release);
			selected = true;
		}
		primary = PrimarySwapChainIdentity == identity;
		primaryIdentity = PrimarySwapChainIdentity;
		ReleaseSRWLockExclusive(&PrimarySwapChainLock);

		if (selected) {
			InterlockedExchange(&SecondarySwapChainLogged, 0);
			DX12LogManager::FieldsFlush("DX12PrimarySwapChain",
				"\"event\":\"select\",\"api\":\"%s\",\"swapchain\":\"%p\",\"identity\":\"%p\"",
				api ? api : "", swapChain, identity);
		} else if (!primary &&
			InterlockedCompareExchange(&SecondarySwapChainLogged, 1, 0) == 0) {
			DX12LogManager::FieldsFlush("DX12PrimarySwapChain",
				"\"event\":\"ignore_secondary\",\"api\":\"%s\",\"swapchain\":\"%p\",\"identity\":\"%p\",\"primaryIdentity\":\"%p\"",
				api ? api : "", swapChain, identity, primaryIdentity);
		}
		return primary;
	}

	static void ForgetSwapChain(IDXGISwapChain *swapChain)
	{
		IUnknown *identity = GetSwapChainIdentity(swapChain);
		bool forgotten = false;
		AcquireSRWLockExclusive(&PrimarySwapChainLock);
		if (PrimarySwapChainIdentity == identity) {
			PrimarySwapChainObject.store(nullptr, std::memory_order_release);
			PrimarySwapChainIdentity = nullptr;
			forgotten = true;
		}
		ReleaseSRWLockExclusive(&PrimarySwapChainLock);
		if (forgotten)
			DX12LogManager::FieldsFlush("DX12PrimarySwapChain",
				"\"event\":\"release\",\"swapchain\":\"%p\",\"identity\":\"%p\"",
				swapChain, identity);
	}

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
			DX12LogManager::Fields("DX12PresentHeartbeat",
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

	static void ConsumeUiActionsForFrame(const char *api, IDXGISwapChain *swapChain)
	{
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		const bool hadActions = DX12UiActions::Consume();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "consume_ui_actions_poll", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		if (!hadActions)
			return;
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "consume_ui_actions_apply", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
	}

	static void RunPostPresentRuntime(
		const char *api,
		IDXGISwapChain *swapChain,
		HRESULT hr,
		bool allowUi)
	{
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModRunPresentMaintenance();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "present_maintenance", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12HuntOnPresent();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "hunt_on_present", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		if (allowUi) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			RunPostPresentUi(swapChain);
			DX12OuterTimingProbeUtils::LogPresentSlice(
				api, "post_present_ui", swapChain, hr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		}

		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "post_original_runtime_total", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
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
		if (kVerboseEnabled) {
			DX12LogManager::Fields("DX12PerFrameCounters",
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
					}
		// DX11 publishes feature changes from its Present-time frame actions.
		// Capture begin/end transitions happen above, so publish them here rather
		// than rebuilding global work flags at every queue submission.
		LARGE_INTEGER updateBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(updateBegin));
			}

	// DXGI Present hook for the primary game swapchain.
	//
	// Flow:
	//   1. Fast bypasses (invalid arg, internal replay, present-test, non-primary
	//      swapchains) forward straight to the real Present without touching the
	//      frame pipeline.
	//   2. skipAll mode still presents but skips mod/frame bookkeeping.
	//   3. Normal path: consume UI actions, reset per-frame counters, finalize
	//      pending captures, call the real Present, then run the per-frame mod
	//      work (frame epoch, capture state machine) and flush profiling logs.
	static HRESULT Present(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
	{
		// Track this Present call in the unified profiler (counter scope).
		DX12_PROFILE_FUNCTION(Present);

		// Nothing to present -> invalid call.
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;

		// Internal replay presents must not advance the frame pipeline: pause
		// the profile timer and forward untouched.
		if (DX12InternalReplayState::IsActive()) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}

		// DXGI_PRESENT_TEST is a query, not a real present; keep it out of the
		// frame bookkeeping and profiling counters.
		if (flags & DXGI_PRESENT_TEST) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		// Streamline frame generation and NVIDIA capture present through their
		// own swapchains. Those Presents contain no game dispatches and must not
		// advance the Mod frame epoch: DX12ModBeginFrame clears the active
		// PreSkin producer set, and present maintenance retires GPU resources.
		// DX11 naturally ties frame actions to its one wrapped game swapchain;
		// preserve that ownership explicitly for DX12.
		// Non-game swapchains (streamline frame gen, NVIDIA capture) contain no
		// game dispatches; forwarding them without frame work keeps the Mod
		// frame epoch and resource retirement tied to the real game swapchain.
		if (!IsPrimarySwapChain("Present", swapChain)) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}

		// Retire GPU copies that finished in previous frames before present.
		DX12ResourceDumpGpuCopy::DrainRetiredCopies();

		// Begin the per-frame timing timeline and consume queued UI actions.
		DX12OuterTimingProbeUtils::LogPresentEnter("Present", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		ConsumeUiActionsForFrame("Present", swapChain);

		// Log the first Present once per process (flush immediately so it is
		// visible even if the process hangs before the regular log flush).
		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll.load());

		// skipAll: present normally but skip all mod/frame work; still advance
		// the present counter, run the lightweight post-present runtime and
		// flush per-frame profiling so diagnostics stay consistent.
		if (DX12HotPathState::skipAll) {
			DX12PresentSkipLogUtils::Record("Present", "BeforeOriginalSkip", swapChain, S_OK);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			_profile_timer.Pause();
			HRESULT skipHr = swapChain->Present(syncInterval, flags);
			_profile_timer.Resume();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12PresentSkipLogUtils::Record("Present", "AfterOriginalSkip", swapChain, skipHr);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			RunPostPresentRuntime(
				"Present",
				swapChain,
				skipHr,
				DX12RuntimeState::GetPresentCount() > 2);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, skipHr);
			return skipHr;
		}


		// Reset per-frame counters (draws, dispatches, fast-forward hits, ...).
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		// Close any pending frame/shader captures and begin the profiling frame.
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

		// Call the real Present; time only the driver wait (profile timer is
		// paused around it so the counter measures our wrapper, not the stall).
		LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present(syncInterval, flags);
		_profile_timer.Resume();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "original_present", swapChain, hr,
			presentUs);

		// Present failed: finish profiling, flush logs and return the error.
		if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, hr);
			return hr;
		}

		// Successful present: publish the frame and run the normal post-present
		// runtime (hot-path state, resource maintenance, ...).
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		RunPostPresentRuntime("Present", swapChain, hr, true);

		// Advance the mod frame epoch (PreSkin producer set, per-frame mods).
				sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		// Complete a finished capture or begin the next one.
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		// End the profiling scope and flush all per-frame instrumentation
		// (counters, hot-path stages, frame timeline) in one pass.
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Finish();
		DX12Profiler::FlushFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		// Close the per-frame timing timeline and return the Present result.
		DX12OuterTimingProbeUtils::LogPresentLeave("Present", swapChain, hr);
		return hr;
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		DX12_PROFILE_FUNCTION(Present);
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive()) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		if (flags & DXGI_PRESENT_TEST) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		if (!IsPrimarySwapChain("Present1", swapChain)) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		DX12ResourceDumpGpuCopy::DrainRetiredCopies();

		DX12OuterTimingProbeUtils::LogPresentEnter("Present1", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		ConsumeUiActionsForFrame("Present1", swapChain);

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll.load());

		if (DX12HotPathState::skipAll) {
			DX12PresentSkipLogUtils::Record("Present1", "BeforeOriginalSkip", swapChain, S_OK);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			_profile_timer.Pause();
			HRESULT skipHr = swapChain->Present1(syncInterval, flags, presentParameters);
			_profile_timer.Resume();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12PresentSkipLogUtils::Record("Present1", "AfterOriginalSkip", swapChain, skipHr);
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			RunPostPresentRuntime(
				"Present1",
				swapChain,
				skipHr,
				DX12RuntimeState::GetPresentCount() > 2);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, skipHr);
			return skipHr;
		}


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
				LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		_profile_timer.Resume();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld", static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "original_present", swapChain, hr, presentUs);
						if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, hr);
			return hr;
		}
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		RunPostPresentRuntime("Present1", swapChain, hr, true);

				sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Finish();
		DX12Profiler::FlushFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
				DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, hr);
		return hr;
	}
};


