// ============================================================
// DX12Presentation.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include "DX12Resources.h"


namespace DX12Wrapper
{
	bool WrapD3D12DeviceResult(REFIID riid, void **device);
	bool WrapD3D12CommandAllocatorResult(REFIID riid, void **allocator);
	bool WrapD3D12FenceResult(REFIID riid, void **fence);
	bool WrapD3D12DescriptorHeapResult(REFIID riid, void **heap);
	bool WrapD3D12ResourceResult(REFIID riid, void **resource);
	bool WrapD3D12HeapResult(REFIID riid, void **heap);
	bool WrapD3D12PipelineStateResult(REFIID riid, void **pipelineState);
	bool WrapD3D12RootSignatureResult(REFIID riid, void **rootSignature);
	bool WrapD3D12QueryHeapResult(REFIID riid, void **queryHeap);
	bool WrapD3D12CommandSignatureResult(REFIID riid, void **commandSignature);
	bool WrapD3D12LifetimeTrackerResult(REFIID riid, void **tracker);
	bool WrapD3D12ProtectedResourceSessionResult(REFIID riid, void **session);
	bool WrapD3D12MetaCommandResult(REFIID riid, void **metaCommand);
	bool WrapD3D12StateObjectResult(REFIID riid, void **stateObject);
	bool WrapD3D12PipelineLibraryResult(REFIID riid, void **library);
	bool WrapD3D12ShaderCacheSessionResult(REFIID riid, void **session);
	bool WrapD3D12DebugInterfaceResult(REFIID riid, void **object);
	bool WrapD3D12AuxiliaryResult(REFIID riid, void **object);
	bool WrapD3D12DeviceFactoryResult(REFIID riid, void **object);
	bool WrapDXGIFactoryResult(REFIID riid, void **factory);
	bool WrapDXGISwapChainResult(REFIID riid, void **swapChain);
	IUnknown *Unwrap(IUnknown *object);
	void Shutdown();
}



class DXGIHookDiagnostics
{
public:
	static void LogHookCall(const char *api, const void *object)
	{
		DX12OriginalFunctionRegistry::LogHookCall(api, object);
	}

	static void LogPresentStage(
		const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr = S_OK)
	{
		if (!kVerboseEnabled)
			return;
		DX12LogManager::DebugFieldsFlush("DX12PresentStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"hr\":\"0x%lx\"",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), swapChain, hr);
	}

	static void LogPresentDeviceRemovedReason(const char *api, IDXGISwapChain *swapChain, HRESULT presentHr)
	{
		if (SUCCEEDED(presentHr) || !swapChain)
			return;

		ID3D12Device *device = nullptr;
		HRESULT queryHr = swapChain->GetDevice(IID_PPV_ARGS(&device));
		if (FAILED(queryHr) || !device) {
			DX12LogManager::DebugFieldsFlush("DX12DeviceRemoved",
				"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"queryHr\":\"0x%lx\"",
				api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, queryHr);
			return;
		}

		const HRESULT reason = device->GetDeviceRemovedReason();
		DX12LogManager::DebugFieldsFlush("DX12DeviceRemoved",
			"\"api\":\"%s\",\"present\":%ld,\"swapchain\":\"%p\",\"presentHr\":\"0x%lx\",\"reason\":\"0x%lx\"",
			api ? api : "", DX12RuntimeState::GetPresentCount(), swapChain, presentHr, reason);
		device->Release();
	}
};



class DXGIHookRuntime
{
public:
	inline static SRWLOCK PrimarySwapChainLock = SRWLOCK_INIT;
	inline static IUnknown *PrimarySwapChainIdentity = nullptr;
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
		IUnknown *identity = GetSwapChainIdentity(swapChain);
		if (!identity)
			return false;

		bool primary = false;
		bool selected = false;
		IUnknown *primaryIdentity = nullptr;
		AcquireSRWLockExclusive(&PrimarySwapChainLock);
		if (!PrimarySwapChainIdentity) {
			PrimarySwapChainIdentity = identity;
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
			DXGIHookDiagnostics::LogPresentStage(api, "afterBindingBeginFrame", swapChain, hr);
		}
		// DX11 publishes feature changes from its Present-time frame actions.
		// Capture begin/end transitions happen above, so publish them here rather
		// than rebuilding global work flags at every queue submission.
		LARGE_INTEGER updateBegin = DX12OuterTimingProbeUtils::Now();
		DX12HotPathState::Update();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			api, "hotpath_update", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(updateBegin));
		DXGIHookDiagnostics::LogPresentStage(api, "afterHotPathUpdate", swapChain, hr);
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
			DX12HangWatchdog::NotifyPresentCompleted();
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

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::Present", swapChain);

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
		DXGIHookDiagnostics::LogPresentStage("Present", "beforeOriginal", swapChain);

		// Call the real Present; time only the driver wait (profile timer is
		// paused around it so the counter measures our wrapper, not the stall).
		LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present(syncInterval, flags);
		_profile_timer.Resume();
		DX12HangWatchdog::NotifyPresentCompleted();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present", "original_present", swapChain, hr,
			presentUs);
		DXGIHookDiagnostics::LogPresentStage("Present", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present", swapChain, hr);

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
		DXGIHookDiagnostics::LogPresentStage("Present", "beforeModBeginFrame", swapChain, hr);
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
		DXGIHookDiagnostics::LogPresentStage("Present", "end", swapChain, hr);

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
			DX12HangWatchdog::NotifyPresentCompleted();
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
		LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		_profile_timer.Resume();
		DX12HangWatchdog::NotifyPresentCompleted();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld", static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1", "original_present", swapChain, hr, presentUs);
		DXGIHookDiagnostics::LogPresentStage("Present1", "afterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
		if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, hr);
			return hr;
		}
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		RunPostPresentRuntime("Present1", swapChain, hr, true);

		DXGIHookDiagnostics::LogPresentStage("Present1", "beforeModBeginFrame", swapChain, hr);
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
		DXGIHookDiagnostics::LogPresentStage("Present1", "end", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present1", swapChain, hr);
		return hr;
	}
};




class DX12WrapperRuntimeUtils
{
public:
	static IUnknown *UnwrapD3D12Child(IUnknown *object)
	{
		return DX12Wrapper::Unwrap(object);
	}

	static ID3D12CommandAllocator *UnwrapCommandAllocator(ID3D12CommandAllocator *allocator)
	{
		return reinterpret_cast<ID3D12CommandAllocator*>(UnwrapD3D12Child(allocator));
	}

	static ID3D12CommandSignature *UnwrapCommandSignature(ID3D12CommandSignature *signature)
	{
		return reinterpret_cast<ID3D12CommandSignature*>(UnwrapD3D12Child(signature));
	}

	static ID3D12CommandList *UnwrapCommandList(ID3D12CommandList *commandList)
	{
		return reinterpret_cast<ID3D12CommandList*>(UnwrapD3D12Child(commandList));
	}

	static ID3D12DescriptorHeap *UnwrapDescriptorHeap(ID3D12DescriptorHeap *heap)
	{
		return reinterpret_cast<ID3D12DescriptorHeap*>(UnwrapD3D12Child(heap));
	}

	static ID3D12Fence *UnwrapFence(ID3D12Fence *fence)
	{
		return reinterpret_cast<ID3D12Fence*>(UnwrapD3D12Child(fence));
	}

	static ID3D12GraphicsCommandList *UnwrapGraphicsCommandList(ID3D12GraphicsCommandList *commandList)
	{
		return reinterpret_cast<ID3D12GraphicsCommandList*>(UnwrapD3D12Child(commandList));
	}

	static ID3D12Heap *UnwrapHeap(ID3D12Heap *heap)
	{
		return reinterpret_cast<ID3D12Heap*>(UnwrapD3D12Child(heap));
	}

	static ID3D12MetaCommand *UnwrapMetaCommand(ID3D12MetaCommand *metaCommand)
	{
		return reinterpret_cast<ID3D12MetaCommand*>(UnwrapD3D12Child(metaCommand));
	}

	static ID3D12Pageable *UnwrapPageable(ID3D12Pageable *pageable)
	{
		return reinterpret_cast<ID3D12Pageable*>(UnwrapD3D12Child(pageable));
	}

	static ID3D12PipelineState *UnwrapPipelineState(ID3D12PipelineState *pipelineState)
	{
		DX12_HOTPATH_TIME_BEGIN(UnwrapObjTotal);
		ID3D12PipelineState *result =
			reinterpret_cast<ID3D12PipelineState*>(UnwrapD3D12Child(pipelineState));
		DX12_HOTPATH_TIME_END(UnwrapObjTotal);
		return result;
	}

	static ID3D12ProtectedResourceSession *UnwrapProtectedResourceSession(
		ID3D12ProtectedResourceSession *session)
	{
		return reinterpret_cast<ID3D12ProtectedResourceSession*>(UnwrapD3D12Child(session));
	}

	static ID3D12QueryHeap *UnwrapQueryHeap(ID3D12QueryHeap *heap)
	{
		return reinterpret_cast<ID3D12QueryHeap*>(UnwrapD3D12Child(heap));
	}

	static ID3D12Resource *UnwrapResource(ID3D12Resource *resource)
	{
		return reinterpret_cast<ID3D12Resource*>(UnwrapD3D12Child(resource));
	}

	static ID3D12RootSignature *UnwrapRootSignature(ID3D12RootSignature *rootSignature)
	{
		return reinterpret_cast<ID3D12RootSignature*>(UnwrapD3D12Child(rootSignature));
	}

	static ID3D12StateObject *UnwrapStateObject(ID3D12StateObject *stateObject)
	{
		return reinterpret_cast<ID3D12StateObject*>(UnwrapD3D12Child(stateObject));
	}

	static const D3D12_GRAPHICS_PIPELINE_STATE_DESC *UnwrapGraphicsPipelineStateDesc(
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC *storage)
	{
		if (!source || !storage)
			return source;
		*storage = *source;
		storage->pRootSignature = UnwrapRootSignature(source->pRootSignature);
		return storage;
	}

	static const D3D12_COMPUTE_PIPELINE_STATE_DESC *UnwrapComputePipelineStateDesc(
		const D3D12_COMPUTE_PIPELINE_STATE_DESC *source,
		D3D12_COMPUTE_PIPELINE_STATE_DESC *storage)
	{
		if (!source || !storage)
			return source;
		*storage = *source;
		storage->pRootSignature = UnwrapRootSignature(source->pRootSignature);
		return storage;
	}

	static void UnwrapStateObjectDesc(
		const D3D12_STATE_OBJECT_DESC *source,
		D3D12_STATE_OBJECT_DESC *descStorage,
		std::vector<D3D12_STATE_SUBOBJECT> *subobjectStorage,
		std::vector<D3D12_GLOBAL_ROOT_SIGNATURE> *globalRootSignatureStorage,
		std::vector<D3D12_LOCAL_ROOT_SIGNATURE> *localRootSignatureStorage,
		std::vector<D3D12_EXISTING_COLLECTION_DESC> *existingCollectionStorage,
		std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> *associationStorage,
		const D3D12_STATE_OBJECT_DESC **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !descStorage || !subobjectStorage || !globalRootSignatureStorage ||
			!localRootSignatureStorage || !existingCollectionStorage || !associationStorage ||
			!source->pSubobjects || !source->NumSubobjects)
			return;

		*descStorage = *source;
		subobjectStorage->assign(source->pSubobjects, source->pSubobjects + source->NumSubobjects);
		globalRootSignatureStorage->clear();
		localRootSignatureStorage->clear();
		existingCollectionStorage->clear();
		associationStorage->clear();
		globalRootSignatureStorage->resize(source->NumSubobjects);
		localRootSignatureStorage->resize(source->NumSubobjects);
		existingCollectionStorage->resize(source->NumSubobjects);
		associationStorage->resize(source->NumSubobjects);

		for (UINT i = 0; i < source->NumSubobjects; ++i) {
			const D3D12_STATE_SUBOBJECT *sourceSubobject = &source->pSubobjects[i];
			D3D12_STATE_SUBOBJECT *activeSubobject = &(*subobjectStorage)[i];
			activeSubobject->pDesc = sourceSubobject->pDesc;

			if (!sourceSubobject->pDesc)
				continue;

			switch (sourceSubobject->Type) {
			case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
				(*globalRootSignatureStorage)[i] =
					*reinterpret_cast<const D3D12_GLOBAL_ROOT_SIGNATURE*>(sourceSubobject->pDesc);
				(*globalRootSignatureStorage)[i].pGlobalRootSignature =
					UnwrapRootSignature((*globalRootSignatureStorage)[i].pGlobalRootSignature);
				activeSubobject->pDesc = &(*globalRootSignatureStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
				(*localRootSignatureStorage)[i] =
					*reinterpret_cast<const D3D12_LOCAL_ROOT_SIGNATURE*>(sourceSubobject->pDesc);
				(*localRootSignatureStorage)[i].pLocalRootSignature =
					UnwrapRootSignature((*localRootSignatureStorage)[i].pLocalRootSignature);
				activeSubobject->pDesc = &(*localRootSignatureStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
				(*existingCollectionStorage)[i] =
					*reinterpret_cast<const D3D12_EXISTING_COLLECTION_DESC*>(sourceSubobject->pDesc);
				(*existingCollectionStorage)[i].pExistingCollection =
					UnwrapStateObject((*existingCollectionStorage)[i].pExistingCollection);
				activeSubobject->pDesc = &(*existingCollectionStorage)[i];
				break;
			case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
				(*associationStorage)[i] =
					*reinterpret_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(sourceSubobject->pDesc);
				for (UINT associatedIndex = 0; associatedIndex < source->NumSubobjects; ++associatedIndex) {
					if ((*associationStorage)[i].pSubobjectToAssociate == &source->pSubobjects[associatedIndex]) {
						(*associationStorage)[i].pSubobjectToAssociate = &(*subobjectStorage)[associatedIndex];
						break;
					}
				}
				activeSubobject->pDesc = &(*associationStorage)[i];
				break;
			default:
				break;
			}
		}

		descStorage->pSubobjects = subobjectStorage->data();
		*result = descStorage;
	}

	static void UnwrapResourceArray(
		ID3D12Resource *const *source,
		UINT count,
		std::vector<ID3D12Resource*> *storage,
		ID3D12Resource *const **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage || !count)
			return;
		storage->resize(count);
		for (UINT i = 0; i < count; ++i)
			(*storage)[i] = UnwrapResource(source[i]);
		*result = storage->data();
	}

	static void UnwrapTextureCopyLocation(
		const D3D12_TEXTURE_COPY_LOCATION *source,
		D3D12_TEXTURE_COPY_LOCATION *storage,
		const D3D12_TEXTURE_COPY_LOCATION **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage)
			return;
		*storage = *source;
		storage->pResource = UnwrapResource(source->pResource);
		*result = storage;
	}

	static void UnwrapResourceBarrier(
		const D3D12_RESOURCE_BARRIER *source,
		D3D12_RESOURCE_BARRIER *storage)
	{
		if (!source || !storage)
			return;
		*storage = *source;
		switch (storage->Type) {
		case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
			storage->Transition.pResource = UnwrapResource(source->Transition.pResource);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
			storage->Aliasing.pResourceBefore = UnwrapResource(source->Aliasing.pResourceBefore);
			storage->Aliasing.pResourceAfter = UnwrapResource(source->Aliasing.pResourceAfter);
			break;
		case D3D12_RESOURCE_BARRIER_TYPE_UAV:
			storage->UAV.pResource = UnwrapResource(source->UAV.pResource);
			break;
		default:
			break;
		}
	}

	static void UnwrapResourceBarrierArray(
		const D3D12_RESOURCE_BARRIER *source,
		UINT count,
		std::vector<D3D12_RESOURCE_BARRIER> *storage,
		const D3D12_RESOURCE_BARRIER **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !storage || !count)
			return;
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierUnwrap(count);
		storage->resize(count);
		for (UINT i = 0; i < count; ++i)
			UnwrapResourceBarrier(&source[i], &(*storage)[i]);
		*result = storage->data();
	}

	static void UnwrapBarrierGroups(
		const D3D12_BARRIER_GROUP *source,
		UINT32 count,
		std::vector<D3D12_BARRIER_GROUP> *groupStorage,
		std::vector<std::vector<D3D12_TEXTURE_BARRIER>> *textureStorage,
		std::vector<std::vector<D3D12_BUFFER_BARRIER>> *bufferStorage,
		const D3D12_BARRIER_GROUP **result)
	{
		if (!result)
			return;
		*result = source;
		if (!source || !groupStorage || !textureStorage || !bufferStorage || !count)
			return;

		groupStorage->resize(count);
		textureStorage->clear();
		bufferStorage->clear();
		textureStorage->resize(count);
		bufferStorage->resize(count);

		for (UINT32 groupIndex = 0; groupIndex < count; ++groupIndex) {
			(*groupStorage)[groupIndex] = source[groupIndex];
			if (source[groupIndex].Type == D3D12_BARRIER_TYPE_TEXTURE &&
				source[groupIndex].pTextureBarriers &&
				source[groupIndex].NumBarriers) {
				std::vector<D3D12_TEXTURE_BARRIER> &textures = (*textureStorage)[groupIndex];
				textures.assign(
					source[groupIndex].pTextureBarriers,
					source[groupIndex].pTextureBarriers + source[groupIndex].NumBarriers);
				for (UINT32 barrierIndex = 0; barrierIndex < source[groupIndex].NumBarriers; ++barrierIndex)
					textures[barrierIndex].pResource =
						UnwrapResource(source[groupIndex].pTextureBarriers[barrierIndex].pResource);
				(*groupStorage)[groupIndex].pTextureBarriers = textures.data();
			} else if (source[groupIndex].Type == D3D12_BARRIER_TYPE_BUFFER &&
				source[groupIndex].pBufferBarriers &&
				source[groupIndex].NumBarriers) {
				std::vector<D3D12_BUFFER_BARRIER> &buffers = (*bufferStorage)[groupIndex];
				buffers.assign(
					source[groupIndex].pBufferBarriers,
					source[groupIndex].pBufferBarriers + source[groupIndex].NumBarriers);
				for (UINT32 barrierIndex = 0; barrierIndex < source[groupIndex].NumBarriers; ++barrierIndex)
					buffers[barrierIndex].pResource =
						UnwrapResource(source[groupIndex].pBufferBarriers[barrierIndex].pResource);
				(*groupStorage)[groupIndex].pBufferBarriers = buffers.data();
			}
		}
		*result = groupStorage->data();
	}

	static void LogCommandListStage(
		const char *method,
		const char *stage,
		ID3D12GraphicsCommandList *commandList)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::ShouldLogHookCall(method))
			return;
		DX12LogManager::Fields("DX12GraphicsCommandListWrapper",
			"\"method\":\"%s\",\"stage\":\"%s\",\"commandList\":\"%p\",\"threadId\":%lu",
			method ? method : "",
			stage ? stage : "",
			commandList,
			GetCurrentThreadId());
#else
		(void)method;
		(void)stage;
		(void)commandList;
#endif
	}

	static void STDMETHODCALLTYPE DirectQueueExecuteCommandLists(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists)
	{
		if (queue)
			queue->ExecuteCommandLists(numCommandLists, commandLists);
	}

	static void ExecuteCommandListsDirectNative(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists)
	{
		ID3D12CommandList *stackUnwrapped[8];
		std::vector<ID3D12CommandList*> heapUnwrapped;
		ID3D12CommandList *const *activeCommandLists = commandLists;
		if (commandLists && numCommandLists) {
			ID3D12CommandList **unwrapped = stackUnwrapped;
			if (numCommandLists > ARRAYSIZE(stackUnwrapped)) {
				heapUnwrapped.resize(numCommandLists);
				unwrapped = heapUnwrapped.data();
			}
			for (UINT i = 0; i < numCommandLists; ++i)
				unwrapped[i] = UnwrapCommandList(commandLists[i]);
			activeCommandLists = unwrapped;
		}
		DirectQueueExecuteCommandLists(queue, numCommandLists, activeCommandLists);
	}

	static void STDMETHODCALLTYPE DirectDrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation)
	{
		if (commandList)
			commandList->DrawInstanced(
				vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
	}

	static void STDMETHODCALLTYPE DirectDrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation)
	{
		if (commandList)
			commandList->DrawIndexedInstanced(
				indexCountPerInstance, instanceCount, startIndexLocation,
				baseVertexLocation, startInstanceLocation);
	}

	static void STDMETHODCALLTYPE DirectDispatch(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ)
	{
		if (commandList)
			commandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	static void STDMETHODCALLTYPE DirectExecuteIndirect(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset)
	{
		if (commandList)
			commandList->ExecuteIndirect(
				commandSignature, maxCommandCount,
				argumentBuffer, argumentBufferOffset,
				countBuffer, countBufferOffset);
	}

	static void STDMETHODCALLTYPE DirectSetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (commandList)
			commandList->IASetIndexBuffer(view);
	}

	static void STDMETHODCALLTYPE DirectSetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (commandList)
			commandList->IASetVertexBuffers(startSlot, count, views);
	}

	static DX12IaReplacementExecutorCallbacks MakeDirectIaReplacementCallbacks()
	{
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = DirectDrawInstanced;
		callbacks.drawIndexedInstanced = DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		return callbacks;
	}

	static bool TryExecutePresentReplacementOnReset(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const DX12IaReplacementExecutorCallbacks &callbacks)
	{
		if (!commandList || !runtimeState)
			return false;
		if (!(DX12HotPathState::HookWorkFlags &
			DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			return false;
		if (DX12InternalReplayState::IsActive())
			return false;
		if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
			return false;
		return DX12IaReplacementExecutor::ExecutePresent(
			commandList, *runtimeState, callbacks);
	}

	static void RegisterSubmittedCommandListDirect(IUnknown *commandList)
	{
		if (!commandList)
			return;
		const bool huntEnabled = DX12HuntIsEnabled();
		const bool captureActive =
			DX12CaptureLedger::CurrentCaptureSessionId() != 0;
		if (!huntEnabled && !captureActive)
			return;

		// Post-submit callback: ID3D12CommandQueue::ExecuteCommandLists has
		// already handed these command lists to the GPU. Recording into,
		// re-barriering, or patching a submitted command list is invalid
		// D3D12 usage - it can hang the driver or trip D3D12Core's
		// InvalidResourceBarrierCall fatal path (observed as a white-screen
		// startup hang in ZenlessZoneZero). This callback must stay pure
		// CPU bookkeeping: no D3D12/COM calls, no locks held across waits.
		ID3D12GraphicsCommandList *baseList =
			reinterpret_cast<ID3D12GraphicsCommandList*>(commandList);
		if (huntEnabled)
			DX12HuntPublishCommandList(baseList);
		if (captureActive)
			DX12BindingTracker::SubmitCommandList(baseList);
	}

	static HRESULT PresentDirect(
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		DX12_PROFILE_FUNCTION(Present);
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (DX12InternalReplayState::IsActive()) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		if (flags & DXGI_PRESENT_TEST) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		if (!DXGIHookRuntime::IsPrimarySwapChain("PresentWrapper", swapChain)) {
			_profile_timer.Pause();
			return swapChain->Present(syncInterval, flags);
		}
		DX12ResourceDumpGpuCopy::DrainRetiredCopies();

		DX12OuterTimingProbeUtils::LogPresentEnter("PresentWrapper", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::ConsumeUiActionsForFrame("PresentWrapper", swapChain);

		static volatile LONG firstPresentLog = 0;
		if (InterlockedCompareExchange(&firstPresentLog, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12PresentFirst",
				"\"api\":\"Present\",\"stage\":\"wrapper_enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll.load());

		// Deferred mod load must run on EVERY present, including the skipAll
		// fast path. When no mod is loaded skipAll==1, so the call cannot live
		// in the normal branch below - it would never execute and the mod would
		// never load (deadlock between skipAll and the deferred load).
		DX12ModMaybeRunDeferredInitialLoad();

		if (DX12HotPathState::skipAll) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			_profile_timer.Pause();
			HRESULT skipHr = swapChain->Present(syncInterval, flags);
			_profile_timer.Resume();
			DX12HangWatchdog::NotifyPresentCompleted();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"PresentWrapper", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DXGIHookRuntime::RunPostPresentRuntime(
				"PresentWrapper",
				swapChain,
				skipHr,
				DX12RuntimeState::GetPresentCount() > 2);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"PresentWrapper", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, skipHr);
			return skipHr;
		}

		DX12_HOTPATH_TIME_BEGIN(PresentTotal);
		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain::Present", swapChain);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperBeforeOriginal", swapChain);
		LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present(syncInterval, flags);
		_profile_timer.Resume();
		DX12HangWatchdog::NotifyPresentCompleted();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "original_present", swapChain, hr,
			presentUs);
		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperAfterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present", swapChain, hr);
		if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12_HOTPATH_TIME_END(PresentTotal);
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, hr);
			return hr;
		}
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DXGIHookRuntime::RunPostPresentRuntime(
			"PresentWrapper", swapChain, hr, true);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperBeforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::CompleteCaptureOrBeginNext("Present", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Finish();
		DX12_HOTPATH_TIME_END(PresentTotal);
		DX12Profiler::FlushFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"PresentWrapper", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present", "wrapperEnd", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("PresentWrapper", swapChain, hr);
		return hr;
	}

	static HRESULT Present1Direct(
		IDXGISwapChain1 *swapChain,
		UINT syncInterval,
		UINT flags,
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
		if (!DXGIHookRuntime::IsPrimarySwapChain("Present1Wrapper", swapChain)) {
			_profile_timer.Pause();
			return swapChain->Present1(syncInterval, flags, presentParameters);
		}
		DX12ResourceDumpGpuCopy::DrainRetiredCopies();

		DX12OuterTimingProbeUtils::LogPresentEnter("Present1Wrapper", swapChain, syncInterval, flags);
		LARGE_INTEGER sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::ConsumeUiActionsForFrame("Present1Wrapper", swapChain);

		static volatile LONG firstPresent1Log = 0;
		if (InterlockedCompareExchange(&firstPresent1Log, 1, 0) == 0)
			DX12LogManager::FieldsFlush("DX12PresentFirst",
				"\"api\":\"Present1\",\"stage\":\"wrapper_enter\",\"swapchain\":\"%p\",\"sync\":%u,\"flags\":\"0x%x\",\"skipAll\":%ld",
				swapChain, syncInterval, flags, DX12HotPathState::skipAll.load());

		// Deferred mod load must run on EVERY present, including the skipAll
		// fast path. When no mod is loaded skipAll==1, so the call cannot live
		// in the normal branch below - it would never execute and the mod would
		// never load (deadlock between skipAll and the deferred load).
		DX12ModMaybeRunDeferredInitialLoad();

		if (DX12HotPathState::skipAll) {
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			_profile_timer.Pause();
			HRESULT skipHr = swapChain->Present1(syncInterval, flags, presentParameters);
			_profile_timer.Resume();
			DX12HangWatchdog::NotifyPresentCompleted();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1Wrapper", "original_present_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			sliceBegin = DX12OuterTimingProbeUtils::Now();
			DX12RuntimeState::IncrementPresentCount();
			DXGIHookRuntime::RunPostPresentRuntime(
				"Present1Wrapper",
				swapChain,
				skipHr,
				DX12RuntimeState::GetPresentCount() > 2);
			DX12HotPathState::Update();
			DX12_HOTPATH_LOG_AND_RESET();
			DX12OuterTimingProbeUtils::LogPresentSlice(
				"Present1Wrapper", "post_original_skip", swapChain, skipHr,
				DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, skipHr);
			return skipHr;
		}

		DXGIHookDiagnostics::LogHookCall("IDXGISwapChain1::Present1", swapChain);

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::LogAndResetPerFrameCounters();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "per_frame_counters", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12Profiling::BeginFrame();
		const bool dumpFrame = DX12FrameAnalysis::EndCapture();
		if (DX12FrameAnalysis::IsCapturing())
			DX12FrameAnalysis::LogJsonFunc("FrameCaptureContinue",
				"\"present\":%ld", DX12RuntimeState::GetPresentCount());
		const bool dumpShaders = DX12ShaderDumpEndCapture();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "capture_bookkeeping", swapChain, S_OK,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperBeforeOriginal", swapChain);
		LARGE_INTEGER presentBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Pause();
		HRESULT hr = swapChain->Present1(syncInterval, flags, presentParameters);
		_profile_timer.Resume();
		DX12HangWatchdog::NotifyPresentCompleted();
		const LONG64 presentUs = DX12OuterTimingProbeUtils::ElapsedUs(presentBegin);
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		DX12LogManager::Fields("DX12Perf",
			"\"scope\":\"PresentWait\",\"us\":%lld",
			static_cast<long long>(presentUs));
	#endif
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "original_present", swapChain, hr,
			presentUs);
		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperAfterOriginal", swapChain, hr);
		DXGIHookDiagnostics::LogPresentDeviceRemovedReason("Present1", swapChain, hr);
		if (FAILED(hr)) {
			_profile_timer.Finish();
			DX12Profiler::FlushFrame();
			DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, hr);
			return hr;
		}
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12RuntimeState::IncrementPresentCount();
		DXGIHookRuntime::RunPostPresentRuntime(
			"Present1Wrapper", swapChain, hr, true);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "post_original_runtime", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperBeforeModBeginFrame", swapChain, hr);
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DX12ModBeginFrame(swapChain);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "mod_begin_frame", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));

		sliceBegin = DX12OuterTimingProbeUtils::Now();
		DXGIHookRuntime::CompleteCaptureOrBeginNext("Present1", swapChain, hr, dumpFrame, dumpShaders);
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "capture_complete_or_begin", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		sliceBegin = DX12OuterTimingProbeUtils::Now();
		_profile_timer.Finish();
		DX12Profiler::FlushFrame();
		DX12OuterTimingProbeUtils::LogPresentSlice(
			"Present1Wrapper", "profiling_and_hotpath_flush", swapChain, hr,
			DX12OuterTimingProbeUtils::ElapsedUs(sliceBegin));
		DXGIHookDiagnostics::LogPresentStage("Present1", "wrapperEnd", swapChain, hr);
		DX12OuterTimingProbeUtils::LogPresentLeave("Present1Wrapper", swapChain, hr);
		return hr;
	}

	static HRESULT Present(
		IDXGISwapChain *swapChain,
		UINT syncInterval,
		UINT flags)
	{
		return PresentDirect(swapChain, syncInterval, flags);
	}

	static HRESULT Present1(
		IDXGISwapChain1 *swapChain,
		UINT syncInterval,
		UINT flags,
		const DXGI_PRESENT_PARAMETERS *presentParameters)
	{
		return Present1Direct(swapChain, syncInterval, flags, presentParameters);
	}

	static HRESULT ResizeBuffers(
		IDXGISwapChain *swapChain,
		UINT bufferCount,
		UINT width,
		UINT height,
		DXGI_FORMAT newFormat,
		UINT swapChainFlags)
	{
		if (!swapChain)
			return DXGI_ERROR_INVALID_CALL;
		if (!DX12InternalReplayState::IsActive())
			DX12NativeOverlayInvalidate(swapChain);
		HRESULT hr = swapChain->ResizeBuffers(bufferCount, width, height, newFormat, swapChainFlags);
		DX12LogManager::Fields("IDXGISwapChain::ResizeBuffers",
			"\"swapchain\":\"%p\",\"buffers\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"flags\":\"0x%x\",\"hr\":\"0x%lx\"",
			swapChain, bufferCount, width, height, static_cast<UINT>(newFormat), swapChainFlags, hr);
		return hr;
	}

	static void ExecuteCommandLists(
		ID3D12CommandQueue *queue,
		UINT numCommandLists,
		ID3D12CommandList *const *commandLists,
		DX12CommandListHookSignatures::QueueExecuteCommandLists original)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		// One QPC pair feeds both the API wall bucket and the ExecuteCL hot-path
		// stage (previously two independent timers measured the same span).
		DX12Profiler::Scope apiWallScope(
			DX12Profiler::ApiStage,
			DX12OuterTimingProbeUtils::ApiExecuteCommandLists,
			static_cast<int>(DX12HotPathStage::ExecuteCLTotal));
#else
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiExecuteCommandLists);
#endif
		LARGE_INTEGER totalBegin = DX12OuterTimingProbeUtils::Now();
		LARGE_INTEGER unwrapBegin = DX12OuterTimingProbeUtils::Now();
		// Use the stack for up to eight command lists, then fall back to the heap.
		ID3D12CommandList *stackUnwrapped[8];
		ID3D12CommandList *const *activeCommandLists = commandLists;
		std::vector<ID3D12CommandList*> heapUnwrapped;
		if (commandLists && numCommandLists) {
			ID3D12CommandList **dest = stackUnwrapped;
			if (numCommandLists > 8) {
				heapUnwrapped.resize(numCommandLists);
				dest = heapUnwrapped.data();
			}
			for (UINT i = 0; i < numCommandLists; ++i)
				dest[i] = UnwrapCommandList(commandLists[i]);
			activeCommandLists = dest;
		}
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"unwrap_command_lists",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(unwrapBegin));
		DX12CommandQueueExecutionFlow::Execute(
			queue,
			numCommandLists,
			activeCommandLists,
			original ? original : DirectQueueExecuteCommandLists,
			RegisterSubmittedCommandListDirect);
		DX12OuterTimingProbeUtils::LogQueueExecuteSlice(
			"wrapper_total",
			queue,
			numCommandLists,
			DX12OuterTimingProbeUtils::ElapsedUs(totalBegin));
	}

	static void DrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawInstanced original)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (DX12HotPathState::HookWorkFlags &
			(DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		if ((DX12HotPathState::HookWorkFlags &
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT) &&
			DX12ModPresentReplacementPendingForCurrentPresent()) {
			const DX12IaReplacementExecutorCallbacks callbacks =
				MakeDirectIaReplacementCallbacks();
			DX12IaReplacementExecutor::ExecutePendingPresentBeforeFastReject(
				commandList, runtimeState, callbacks);
		}
		DX12WrapperRuntimeUtils::DrawInstanced(
			commandList,
			runtimeState,
			vertexCountPerInstance,
			instanceCount,
			startVertexLocation,
			startInstanceLocation,
			original);
	}

	static void DrawInstanced(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawInstanced original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDrawInstanced);
		const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
		if (!(drawFlags & (DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT))) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, vertexCountPerInstance, instanceCount,
					startVertexLocation, startInstanceLocation);
			else
				DirectDrawInstanced(commandList, vertexCountPerInstance, instanceCount,
					startVertexLocation, startInstanceLocation);
			return;
		}
		// Use pre-hook original as callback to bypass vtable hook.
		// Falls back to DirectDrawInstanced (hooked vtable) only if original is null.
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = original ? reinterpret_cast<DX12IaCommandSignatures::DrawInstanced>(original) : DirectDrawInstanced;
		callbacks.drawIndexedInstanced = DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawInstanced(
			vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void DrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawIndexedInstanced original)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (DX12HotPathState::HookWorkFlags &
			(DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT))
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		if ((DX12HotPathState::HookWorkFlags &
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT) &&
			DX12ModPresentReplacementPendingForCurrentPresent()) {
			const DX12IaReplacementExecutorCallbacks callbacks =
				MakeDirectIaReplacementCallbacks();
			DX12IaReplacementExecutor::ExecutePendingPresentBeforeFastReject(
				commandList, runtimeState, callbacks);
		}
		DX12WrapperRuntimeUtils::DrawIndexedInstanced(
			commandList,
			runtimeState,
			indexCountPerInstance,
			instanceCount,
			startIndexLocation,
			baseVertexLocation,
			startInstanceLocation,
			original);
	}

	static void DrawIndexedInstanced(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation,
		DX12CommandListHookSignatures::DrawIndexedInstanced original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDrawIndexedInstanced);
		const UINT64 drawFlags = DX12HotPathState::HookWorkFlags;
		if (!(drawFlags & (DX12HotPathState::MASK_DRAW_WORK |
				DX12HotPathState::FLAG_PRESENT_REPLACEMENT))) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, indexCountPerInstance, instanceCount,
					startIndexLocation, baseVertexLocation, startInstanceLocation);
			else
				DirectDrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount,
					startIndexLocation, baseVertexLocation, startInstanceLocation);
			return;
		}
		DX12IaReplacementExecutorCallbacks callbacks;
		callbacks.drawInstanced = DirectDrawInstanced;
		callbacks.drawIndexedInstanced = original ? reinterpret_cast<DX12IaCommandSignatures::DrawIndexedInstanced>(original) : DirectDrawIndexedInstanced;
		callbacks.dispatch = DirectDispatch;
		callbacks.setIndexBuffer = DirectSetIndexBuffer;
		callbacks.setVertexBuffers = DirectSetVertexBuffers;
		callbacks.recordReplacementDraw = DX12CommandListHookDiagnostics::RecordIaReplacementDrawExecute;
		callbacks.recordReplacementDispatch = DX12CommandListHookDiagnostics::RecordIaReplacementDispatchExecute;
		DX12IaDrawInvocation draw = DX12IaDrawInvocation::ForDrawIndexedInstanced(
			indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
		DX12DrawHookFlow::ExecuteHook(commandList, runtimeState, draw, callbacks);
	}

	static void Dispatch(
		ID3D12GraphicsCommandList *commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn original)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (DX12HotPathState::HookWorkFlags & DX12HotPathState::MASK_DISPATCH_WORK)
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::Dispatch(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			original);
	}

	static void Dispatch(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ,
		DX12DispatchHookFlowDispatch::Fn original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiDispatch);
		const UINT64 dispatchFlags = DX12HotPathState::HookWorkFlags;
		if (!(dispatchFlags & DX12HotPathState::MASK_DISPATCH_WORK)) {
			DX12_PROFILE_FAST_FORWARD();
			if (original)
				original(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			else
				DirectDispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
			return;
		}
		DX12DispatchHookFlow::ExecuteHook(
			commandList,
			runtimeState,
			threadGroupCountX,
			threadGroupCountY,
			threadGroupCountZ,
			original ? original : DirectDispatch);
	}

	static void ExecuteIndirect(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset,
		DX12CommandListHookSignatures::ExecuteIndirect original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiExecuteIndirect);
		ID3D12Resource *unwrappedArgumentBuffer = UnwrapResource(argumentBuffer);
		ID3D12Resource *unwrappedCountBuffer = UnwrapResource(countBuffer);
		ID3D12CommandSignature *unwrappedCommandSignature = UnwrapCommandSignature(commandSignature);
		DX12ExecuteIndirectRuntime::Execute(
			commandList,
			unwrappedCommandSignature,
			maxCommandCount,
			unwrappedArgumentBuffer,
			argumentBufferOffset,
			unwrappedCountBuffer,
			countBufferOffset,
			original ? original : DirectExecuteIndirect);
	}

	static HRESULT QueueSignal(
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		DX12CommandListHookSignatures::QueueSignal original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiQueueSignal);
		ID3D12Fence *unwrappedFence = UnwrapFence(fence);
		const UINT64 completedBefore =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
		HRESULT hr = original ? original(queue, unwrappedFence, value) :
			queue->Signal(unwrappedFence, value);
		const UINT64 completedAfter =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		DX12OuterTimingProbeUtils::LogQueueFenceSlice(
			"Signal",
			"original",
			queue,
			unwrappedFence,
			value,
			completedBefore,
			completedAfter,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(begin));
		return hr;
	}

	static HRESULT QueueWait(
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		DX12CommandListHookSignatures::QueueWait original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiQueueWait);
		ID3D12Fence *unwrappedFence = UnwrapFence(fence);
		const UINT64 completedBefore =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
		HRESULT hr = original ? original(queue, unwrappedFence, value) :
			queue->Wait(unwrappedFence, value);
		const UINT64 completedAfter =
			DX12OuterTimingProbeUtils::TimingFenceCompletedValue(unwrappedFence);
		DX12OuterTimingProbeUtils::LogQueueFenceSlice(
			"Wait",
			"original",
			queue,
			unwrappedFence,
			value,
			completedBefore,
			completedAfter,
			hr,
			DX12OuterTimingProbeUtils::ElapsedUs(begin));
		return hr;
	}

	static HRESULT ResetCommandList(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState,
		DX12CommandListHookSignatures::ResetCommandList original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12WrapperRuntimeUtils::ResetCommandList(
			commandList, runtimeState, allocator, initialState, original);
	}

	static HRESULT ResetCommandList(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12CommandAllocator *allocator,
		ID3D12PipelineState *initialState,
		DX12CommandListHookSignatures::ResetCommandList original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiResetCommandList);
		ID3D12CommandAllocator *unwrappedAllocator = UnwrapCommandAllocator(allocator);
		ID3D12PipelineState *unwrappedInitialState = UnwrapPipelineState(initialState);
		HRESULT hr = original ?
			original(commandList, unwrappedAllocator, unwrappedInitialState) :
			commandList->Reset(unwrappedAllocator, unwrappedInitialState);
		if (SUCCEEDED(hr)) {
			if (runtimeState)
				DX12CommandListRuntime::Reset(runtimeState, unwrappedInitialState);
			else
				DX12CommandListRuntime::Reset(commandList, unwrappedInitialState);
			DX12CommandListLifecycle::Reset(commandList, unwrappedInitialState);
		}
		return hr;
	}

	static HRESULT CloseCommandList(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListHookSignatures::CloseCommandList original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiCloseCommandList);
		return original ? original(commandList) : commandList->Close();
	}

	static void SetPipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState,
		DX12CommandListHookSignatures::SetPipelineState original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetPipelineState(
			commandList, runtimeState, pipelineState, original);
	}

	static void SetPipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12PipelineState *pipelineState,
		DX12CommandListHookSignatures::SetPipelineState original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetPipelineState);
		DX12_HOTPATH_TIME_BEGIN(SetPipelineStateTotal);
		ID3D12PipelineState *unwrappedPipelineState = nullptr;
		if (runtimeState && runtimeState->lastWrappedPipelineState == pipelineState) {
			unwrappedPipelineState = runtimeState->lastUnwrappedPipelineState;
		} else {
			unwrappedPipelineState = UnwrapPipelineState(pipelineState);
			if (runtimeState) {
				runtimeState->lastWrappedPipelineState = pipelineState;
				runtimeState->lastUnwrappedPipelineState = unwrappedPipelineState;
			}
		}
		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateFlow::CapturePipelineState(
					commandList, runtimeState, unwrappedPipelineState);
		if (original)
			original(commandList, activePipelineState);
		else
			commandList->SetPipelineState(activePipelineState);
		DX12_HOTPATH_TIME_END(SetPipelineStateTotal);
	}

	static void ResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		DX12CommandListHookSignatures::ResourceBarrier original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiResourceBarrier);
		DX12_HOTPATH_TIME_BEGIN(ResourceBarrierTotal);
		const D3D12_RESOURCE_BARRIER *activeBarriers = barriers;
		{
			std::vector<D3D12_RESOURCE_BARRIER> localBarriers;
			std::vector<D3D12_RESOURCE_BARRIER> &unwrappedBarriers =
				runtimeState ? runtimeState->barrierScratch : localBarriers;
			DX12_HOTPATH_TIME_BEGIN(BarrierUnwrap);
			UnwrapResourceBarrierArray(barriers, numBarriers, &unwrappedBarriers, &activeBarriers);
			DX12_HOTPATH_TIME_END(BarrierUnwrap);
		}
		LogCommandListStage("ResourceBarrier", "before", commandList);
		DX12_HOTPATH_TIME_BEGIN(BarrierCapture);
		DX12CommandListStateFlow::CaptureResourceBarrier(commandList, numBarriers, activeBarriers);
		DX12_HOTPATH_TIME_END(BarrierCapture);
		if (original)
			original(commandList, numBarriers, activeBarriers);
		else
			commandList->ResourceBarrier(numBarriers, activeBarriers);
		LogCommandListStage("ResourceBarrier", "after", commandList);
		DX12_HOTPATH_TIME_END(ResourceBarrierTotal);
	}

	// Bind-time static IA replacement (plan step 3.2). Hashes the original
	// bound view; an unconditional static geometry override redirects the
	// bind to the mod buffer so the draw path never re-evaluates it. The
	// bookkeeping on DX12ActiveIaState lets the draw fast-path detect a
	// rewritten or recreated game buffer and restore the original binding.
	static bool TryRedirectStaticIaIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view,
		D3D12_INDEX_BUFFER_VIEW *redirectedView)
	{
		if (!redirectedView)
			return false;
		// No table active: single volatile load on the bind path.
		if (!DX12HasStaticIaReplacements())
			return false;
		if (!view || !view->BufferLocation || !view->SizeInBytes)
			return false;
		ID3D12Device *device = runtimeState ? runtimeState->device : nullptr;
		ID3D12Device *queriedDevice = nullptr;
		if (!device && commandList &&
		    SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&queriedDevice)))) {
			device = queriedDevice;
		}
		const uint32_t hash = DX12HashIaBufferContentView(
			view->BufferLocation, view->SizeInBytes, 0,
			static_cast<UINT>(view->Format), 0);
		bool redirected = false;
		if (hash && device) {
			redirected = DX12ResolveStaticIaReplacement(
				device, true, 0, hash, view, redirectedView, nullptr, nullptr);
		}
		if (queriedDevice)
			queriedDevice->Release();
		if (runtimeState) {
			DX12ActiveIaState &ia = runtimeState->ia;
			if (redirected) {
				ia.indexBufferStaticReplaced = true;
				ia.indexBufferStaticOriginal = *view;
				ia.indexBufferStaticReplacedView = *redirectedView;
				ia.indexBufferStaticOriginalResolved =
					DX12ResolveBufferResourceByGpuVa(
						view->BufferLocation, view->SizeInBytes,
						&ia.indexBufferStaticOriginalResource);
				ia.indexBufferStaticContentGeneration =
					ia.indexBufferStaticOriginalResolved ?
						DX12GetBufferContentGenerationForResource(
							ia.indexBufferStaticOriginalResource.resource) : 0;
				ia.indexBufferStaticResolveEpoch =
					DX12GetIaHashResolveGeneration();
				ia.indexBufferStaticMatchedHash = hash;
			} else if (hash && ia.indexBufferStaticReplaced) {
				// The bind was evaluated (hash succeeded) and the table has no
				// replacement for it: drop stale bookkeeping from an earlier
				// redirected binding.
				ia.indexBufferStaticReplaced = false;
			}
		}
		return redirected;
	}

	static bool TryRedirectStaticIaVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		D3D12_VERTEX_BUFFER_VIEW *redirectedViews,
		UINT redirectedCapacity)
	{
		if (!DX12HasStaticIaReplacements() || !views || !count)
			return false;
		if (startSlot >= 32 || count > 32 - startSlot ||
		    count > redirectedCapacity)
			return false;
		ID3D12Device *device = runtimeState ? runtimeState->device : nullptr;
		ID3D12Device *queriedDevice = nullptr;
		if (!device && commandList &&
		    SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&queriedDevice)))) {
			device = queriedDevice;
		}
		bool redirectedAny = false;
		for (UINT index = 0; index < count; ++index) {
			const UINT slot = startSlot + index;
			const D3D12_VERTEX_BUFFER_VIEW &view = views[index];
			redirectedViews[index] = view;
			if (!view.BufferLocation || !view.SizeInBytes ||
			    !view.StrideInBytes) {
				if (runtimeState) {
					runtimeState->ia.vertexBufferStaticReplaced[slot] = false;
					runtimeState->ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
				}
				continue;
			}
			const uint32_t hash = DX12HashIaBufferContentView(
				view.BufferLocation, view.SizeInBytes,
				view.StrideInBytes, 0, slot);
			bool redirected = false;
			if (hash && device) {
				redirected = DX12ResolveStaticIaReplacement(
					device, false, slot, hash,
					nullptr, nullptr, &view, &redirectedViews[index]);
			}
			if (!redirected)
				redirectedViews[index] = view;
			if (runtimeState) {
				DX12ActiveIaState &ia = runtimeState->ia;
				if (redirected) {
					ia.vertexBufferStaticReplaced[slot] = true;
					ia.vertexBufferStaticReplacedMask |= (1u << slot);
					ia.vertexBufferStaticOriginal[slot] = view;
					ia.vertexBufferStaticReplacedView[slot] = redirectedViews[index];
					ia.vertexBufferStaticOriginalResolved[slot] =
						DX12ResolveBufferResourceByGpuVa(
							view.BufferLocation, view.SizeInBytes,
							&ia.vertexBufferStaticOriginalResources[slot]);
					ia.vertexBufferStaticContentGeneration[slot] =
						ia.vertexBufferStaticOriginalResolved[slot] ?
							DX12GetBufferContentGenerationForResource(
								ia.vertexBufferStaticOriginalResources[slot].resource) : 0;
					ia.vertexBufferStaticResolveEpoch[slot] =
						DX12GetIaHashResolveGeneration();
					ia.vertexBufferStaticMatchedHash[slot] = hash;
				} else if (hash && ia.vertexBufferStaticReplaced[slot]) {
					ia.vertexBufferStaticReplaced[slot] = false;
					ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
				}
			}
			redirectedAny = redirectedAny || redirected;
		}
		if (queriedDevice)
			queriedDevice->Release();
		return redirectedAny;
	}

	static void IASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view,
		DX12CommandListHookSignatures::IaSetIndexBuffer original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetIndexBuffer(commandList, runtimeState, view, original);
	}

	static void IASetIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view,
		DX12CommandListHookSignatures::IaSetIndexBuffer original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetIndexBuffer);
		// Plan fix A: a game rebind supersedes a live static-template IA
		// replacement. Restore the snapshotted original views first so slots
		// this call does not cover do not keep the mod view.
		if (runtimeState && runtimeState->staticTemplateIaApplied) {
			DX12IaReplacementExecutorCallbacks callbacks;
			callbacks.setIndexBuffer = DirectSetIndexBuffer;
			callbacks.setVertexBuffers = DirectSetVertexBuffers;
			DX12IaReplacementExecutor::RestoreStaticTemplateIa(
				commandList, runtimeState, callbacks);
		}
		D3D12_INDEX_BUFFER_VIEW redirectedView = {};
		const D3D12_INDEX_BUFFER_VIEW *activeView = view;
		if (TryRedirectStaticIaIndexBuffer(
			    commandList, runtimeState, view, &redirectedView)) {
			activeView = &redirectedView;
		}
		const bool changed = DX12CommandListStateFlow::CaptureIndexBuffer(
			commandList, runtimeState, activeView);
		if (!changed)
			return;
		if (original)
			original(commandList, activeView);
		else
			commandList->IASetIndexBuffer(activeView);
	}

	static void IASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		DX12CommandListHookSignatures::IaSetVertexBuffers original)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetVertexBuffers(
			commandList, runtimeState, startSlot, count, views, original);
	}

	static void IASetVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views,
		DX12CommandListHookSignatures::IaSetVertexBuffers original)
	{
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetVertexBuffers);
		// Plan fix A: a game rebind supersedes a live static-template IA
		// replacement. Restore the snapshotted original views first so slots
		// this call does not cover do not keep the mod view.
		if (runtimeState && runtimeState->staticTemplateIaApplied) {
			DX12IaReplacementExecutorCallbacks callbacks;
			callbacks.setIndexBuffer = DirectSetIndexBuffer;
			callbacks.setVertexBuffers = DirectSetVertexBuffers;
			DX12IaReplacementExecutor::RestoreStaticTemplateIa(
				commandList, runtimeState, callbacks);
		}
		D3D12_VERTEX_BUFFER_VIEW redirectedViews[
			D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
		const D3D12_VERTEX_BUFFER_VIEW *activeViews = views;
		if (DX12PreSkinIaRedirectUtils::RedirectVertexBufferViews(
			    commandList,
			    startSlot,
			    count,
			    views,
			    redirectedViews,
			    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
			    runtimeState ? &runtimeState->ia : nullptr)) {
			activeViews = redirectedViews;
		}
		// Plan step 3.2: bind-time static IA replacement. Only evaluated when
		// the PreSkin redirect above left the original views untouched; when
		// PreSkin takes over a bind, the static bookkeeping for those slots no
		// longer reflects the live binding and must be dropped.
		if (activeViews == views) {
			D3D12_VERTEX_BUFFER_VIEW staticRedirectedViews[
				D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
			if (TryRedirectStaticIaVertexBuffers(
				    commandList,
				    runtimeState,
				    startSlot,
				    count,
				    views,
				    staticRedirectedViews,
				    D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)) {
				activeViews = staticRedirectedViews;
			}
		} else if (runtimeState) {
			for (UINT index = 0; index < count && startSlot + index < 32; ++index) {
				const UINT slot = startSlot + index;
				runtimeState->ia.vertexBufferStaticReplaced[slot] = false;
				runtimeState->ia.vertexBufferStaticReplacedMask &= ~(1u << slot);
			}
		}
		const bool changed = DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, activeViews);
		if (!changed)
			return;
		if (original)
			original(commandList, startSlot, count, activeViews);
		else
			commandList->IASetVertexBuffers(startSlot, count, activeViews);
	}

	static bool ShouldSkipComputeBinding()
	{
		// Probe-only PreSkin still needs the live compute state, but it can use
		// the lightweight runtime resident path instead of the heavy tracker.
		return DX12HotPathState::skipAll &&
			!(DX12HotPathState::HookWorkFlags &
				(DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK));
	}

	static bool ShouldSkipGraphicsBinding()
	{
		// Graphics root tracking is only needed for heavy capture modes or
		// descriptor-aware Mod work. Plain shader replacement can forward calls.
		return DX12HotPathState::skipGraphicsBindings != 0;
	}

	static void SetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12SimulateContext *runtimeState = nullptr;
		if (!ShouldSkipComputeBinding())
			runtimeState = DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetDescriptorHeaps(commandList, runtimeState, count, heaps);
	}

	static void SetDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		DX12_PROFILE_FUNCTION(SetDescriptorHeaps);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetDescriptorHeaps);
		DX12_HOTPATH_TIME_BEGIN(DescriptorHeapsTotal);
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
				commandList, runtimeState, count, heaps);
		}
		commandList->SetDescriptorHeaps(count, heaps);
		DX12_HOTPATH_TIME_END(DescriptorHeapsTotal);
	}

	static void SetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootSignature(
			commandList, runtimeState, rootSignature, nullptr);
	}

	static void SetComputeRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12RootSignature *rootSignature,
		DX12CommandListHookSignatures::SetRootSignature original)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootSignature);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootSignature);
		ID3D12RootSignature *unwrapped = nullptr;
		if (runtimeState && runtimeState->lastWrappedComputeRootSignature == rootSignature)
			unwrapped = runtimeState->lastUnwrappedComputeRootSignature;
		else {
			unwrapped = UnwrapRootSignature(rootSignature);
			if (runtimeState) {
				runtimeState->lastWrappedComputeRootSignature = rootSignature;
				runtimeState->lastUnwrappedComputeRootSignature = unwrapped;
			}
		}
		if (!ShouldSkipComputeBinding()) {
			const UINT64 hookFlags = DX12HotPathState::HookWorkFlags;
			if (runtimeState &&
				DX12HotPathState::skipBindings &&
				!DX12InternalReplayState::IsActive()) {
				// Fast path mirroring CaptureRootSignature's skip branch without
				// the shared fast-skip bookkeeping: keep the deferred pre-skin
				// restore, resident state and binding hash updates, then drop
				// redundant calls.
				DX12CommandListComputeBindingFlow::
					RestoreDeferredPreSkinBeforeBindingMutation(commandList);
				bool changed = true;
				if (DX12HotPathState::NeedsComputeResidentBindingState(hookFlags))
					changed = DX12CommandListRuntime::RememberComputeRootSignature(
						runtimeState, unwrapped);
				if (changed)
					DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
						runtimeState, reinterpret_cast<uint64_t>(unwrapped));
				if (!changed)
					return;
			}
			else {
				const bool changed = DX12CommandListComputeBindingFlow::CaptureRootSignature(
					commandList, runtimeState, unwrapped, false);
				if (!changed)
					return;
			}
		}
		if (original) original(commandList, unwrapped);
		else commandList->SetComputeRootSignature(unwrapped);
	}

	static void SetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootSignature(
			commandList, runtimeState, rootSignature, nullptr);
	}

	static void SetGraphicsRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12RootSignature *rootSignature,
		DX12CommandListHookSignatures::SetRootSignature original)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootSignature);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootSignature);
		LARGE_INTEGER rootsigSegmentBegin = DX12OuterTimingProbeUtils::Now();
		ID3D12RootSignature *unwrapped = nullptr;
		if (runtimeState && runtimeState->lastWrappedGraphicsRootSignature == rootSignature)
			unwrapped = runtimeState->lastUnwrappedGraphicsRootSignature;
		else {
			unwrapped = UnwrapRootSignature(rootSignature);
			if (runtimeState) {
				runtimeState->lastWrappedGraphicsRootSignature = rootSignature;
				runtimeState->lastUnwrappedGraphicsRootSignature = unwrapped;
			}
		}
		DX12OuterTimingProbeUtils::LogCpuStage(
			"RootSig", "unwrap", 1, 1,
			DX12OuterTimingProbeUtils::ElapsedUs(rootsigSegmentBegin));
		if (ShouldSkipGraphicsBinding()) {
			rootsigSegmentBegin = DX12OuterTimingProbeUtils::Now();
			if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"RootSig", "original_skip", 1, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(rootsigSegmentBegin));
		}
		else {
			const UINT64 hookFlags = DX12HotPathState::HookWorkFlags;
			if (runtimeState &&
				!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(hookFlags) &&
				!DX12InternalReplayState::IsActive()) {
				// Fast path mirroring CaptureRootSignature's skip branch without
				// the shared fast-skip bookkeeping: keep the deferred pre-skin
				// restore and resident state update, then drop redundant calls.
				rootsigSegmentBegin = DX12OuterTimingProbeUtils::Now();
				DX12CommandListComputeBindingFlow::
					RestoreDeferredPreSkinBeforeBindingMutation(commandList);
				bool changed = true;
				if (DX12HotPathState::NeedsGraphicsResidentDescriptorState(hookFlags))
					changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
						runtimeState, unwrapped);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"RootSig", "capture_fastpath", 1, 1,
					DX12OuterTimingProbeUtils::ElapsedUs(rootsigSegmentBegin));
				if (!changed)
					return;
				rootsigSegmentBegin = DX12OuterTimingProbeUtils::Now();
				if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
				DX12OuterTimingProbeUtils::LogCpuStage(
					"RootSig", "original", 1, 1,
					DX12OuterTimingProbeUtils::ElapsedUs(rootsigSegmentBegin));
				return;
			}
			rootsigSegmentBegin = DX12OuterTimingProbeUtils::Now();
			const bool changed = DX12CommandListComputeBindingFlow::CaptureRootSignature(
				commandList, runtimeState, unwrapped, true);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"RootSig", "capture", 1, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(rootsigSegmentBegin));
			if (!changed)
				return;
			rootsigSegmentBegin = DX12OuterTimingProbeUtils::Now();
			if (original) original(commandList, unwrapped); else commandList->SetGraphicsRootSignature(unwrapped);
			DX12OuterTimingProbeUtils::LogCpuStage(
				"RootSig", "original", 1, 1,
				DX12OuterTimingProbeUtils::ElapsedUs(rootsigSegmentBegin));
		}
	}

	static void SetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, nullptr);
	}

	static void SetComputeRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		DX12CommandListHookSignatures::SetRootDescriptorTable original)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootDescriptorTable);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptorTable);
		if (!ShouldSkipComputeBinding()) {
			const bool changed = DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor, false);
			if (!changed)
				return;
		}
		if (original) original(commandList, rootParameterIndex, baseDescriptor);
		else commandList->SetComputeRootDescriptorTable(rootParameterIndex, baseDescriptor);
	}

	static void SetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, nullptr);
	}

	static void SetGraphicsRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		DX12CommandListHookSignatures::SetRootDescriptorTable original)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootDescriptorTable);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptorTable);
		if (ShouldSkipGraphicsBinding())
			if (original) original(commandList, rootParameterIndex, baseDescriptor);
			else commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
		else {
			const bool changed = DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor, true);
			if (!changed)
				return;
			if (original) original(commandList, rootParameterIndex, baseDescriptor);
			else commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, baseDescriptor);
		}
	}

	static void SetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootConstantBufferView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootConstantBufferView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsComputeResidentBindingState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					runtimeState, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
			else
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					commandList, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV, bufferLocation);
		}
		commandList->SetComputeRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootConstantBufferView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootConstantBufferView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootConstantBufferView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) ||
			DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags)) {
			DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
				commandList,
				runtimeState,
				rootParameterIndex,
				D3D12_ROOT_PARAMETER_TYPE_CBV,
				bufferLocation,
				true,
				"ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView");
		}
		commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootShaderResourceView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootShaderResourceView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsComputeResidentBindingState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					runtimeState, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
			else
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					commandList, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV, bufferLocation);
		}
		commandList->SetComputeRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootShaderResourceView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootShaderResourceView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootShaderResourceView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) ||
			DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags)) {
			DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
				commandList,
				runtimeState,
				rootParameterIndex,
				D3D12_ROOT_PARAMETER_TYPE_SRV,
				bufferLocation,
				true,
				"ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView");
		}
		commandList->SetGraphicsRootShaderResourceView(rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetComputeRootUnorderedAccessView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetComputeRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetComputeRootUnorderedAccessView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetComputeRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsComputeResidentBindingState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					runtimeState, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
			else
				DX12CommandListRuntime::RememberComputeRootDescriptor(
					commandList, rootParameterIndex,
					D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
		}
		if (!ShouldSkipComputeBinding()) {
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV, bufferLocation);
		}
		commandList->SetComputeRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::SetGraphicsRootUnorderedAccessView(
			commandList, runtimeState, rootParameterIndex, bufferLocation);
	}

	static void SetGraphicsRootUnorderedAccessView(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
	{
		DX12_PROFILE_FUNCTION(SetGraphicsRootUnorderedAccessView);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiSetGraphicsRootDescriptor);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) ||
			DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags)) {
			DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
				commandList,
				runtimeState,
				rootParameterIndex,
				D3D12_ROOT_PARAMETER_TYPE_UAV,
				bufferLocation,
				true,
				"ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView");
		}
		commandList->SetGraphicsRootUnorderedAccessView(rootParameterIndex, bufferLocation);
	}

	static void IASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12WrapperRuntimeUtils::IASetPrimitiveTopology(commandList, runtimeState, topology);
	}

	static void IASetPrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12_PROFILE_FUNCTION(IASetPrimitiveTopology);
		DX12OuterTimingProbeUtils::ApiWallScope apiWallScope(
			DX12OuterTimingProbeUtils::ApiIASetPrimitiveTopology);
		if (DX12HotPathState::skipAll)
			commandList->IASetPrimitiveTopology(topology);
		else {
			if (DX12CommandListStateFlow::CapturePrimitiveTopology(
					commandList, runtimeState, topology))
				commandList->IASetPrimitiveTopology(topology);
		}
	}
};


class DXGIHookCallbacks
{
public:
	static HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void **factory);
	static HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void **factory);
};


class DXGIHookSignatures
{
public:
	using CreateDXGIFactory = HRESULT(WINAPI *)(REFIID, void**);
	using CreateDXGIFactory2 = HRESULT(WINAPI *)(UINT, REFIID, void**);
};


class DXGIHookSlotTypes
{
public:
	class IDXGISwapChain
	{
	public:
		static constexpr UINT Present = 8;
		static constexpr UINT ResizeBuffers = 13;
	};

	class IDXGISwapChain1
	{
	public:
		static constexpr UINT Present1 = 22;
	};

	class IDXGIFactory
	{
	public:
		static constexpr UINT CreateSwapChain = 10;
	};

	class IDXGIFactory2
	{
	public:
		static constexpr UINT CreateSwapChainForHwnd = 15;
		static constexpr UINT CreateSwapChainForCoreWindow = 16;
		static constexpr UINT CreateSwapChainForComposition = 24;
	};
};



class DXGIHookState
{
public:
	inline static bool HookAttempted = false;
	inline static SRWLOCK HookLock = SRWLOCK_INIT;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory OrigCreateDXGIFactory1 = nullptr;
	inline static DXGIHookSignatures::CreateDXGIFactory2 OrigCreateDXGIFactory2 = nullptr;
};


#include <set>

namespace Bunny {

class MigotoIniLoadResult
{
public:
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

class MigotoIniLoader
{
public:
	static bool LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result);
};

} // namespace Bunny



namespace Bunny {

class MigotoIniLoaderPath
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool EndsWithI(const std::wstring &value, const wchar_t *suffix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerSuffix = IniDocumentText::ToLower(suffix);
	if (lower.size() < lowerSuffix.size())
		return false;
	return lower.compare(lower.size() - lowerSuffix.size(), lowerSuffix.size(), lowerSuffix) == 0;
}

static bool IsAbsolutePath(const std::wstring &path)
{
	return path.size() >= 2 && (path[1] == L':' || path[0] == L'\\' || path[0] == L'/');
}

static std::wstring DirectoryOf(const std::wstring &path)
{
	wchar_t buffer[MAX_PATH];
	wcsncpy_s(buffer, path.c_str(), _TRUNCATE);
	PathRemoveFileSpecW(buffer);
	return buffer;
}

static std::wstring JoinPath(const std::wstring &base, const std::wstring &path)
{
	if (path.empty())
		return base;
	if (IsAbsolutePath(path))
		return path;

	wchar_t buffer[MAX_PATH];
	wcsncpy_s(buffer, base.c_str(), _TRUNCATE);
	PathAppendW(buffer, path.c_str());
	return buffer;
}

static std::wstring CanonicalPath(const std::wstring &path)
{
	wchar_t full[MAX_PATH];
	if (!GetFullPathNameW(path.c_str(), ARRAYSIZE(full), full, nullptr))
		return IniDocumentText::ToLower(path);
	return IniDocumentText::ToLower(full);
}

static bool IsDisabledName(const std::wstring &name)
{
	return StartsWithI(name, L"disabled") || StartsWithI(name, L"DISABLED");
}

static bool ShouldExcludeRecursive(const std::wstring &name, const std::vector<std::wstring> &patterns)
{
	if (IsDisabledName(name))
		return true;

	for (const std::wstring &pattern : patterns) {
		std::wstring lowerName = IniDocumentText::ToLower(name);
		std::wstring lowerPattern = IniDocumentText::ToLower(IniDocumentText::Trim(pattern));
		if (lowerPattern.empty())
			continue;
		if (lowerPattern == lowerName)
			return true;
		if (lowerPattern.back() == L'*') {
			lowerPattern.pop_back();
			if (lowerName.rfind(lowerPattern, 0) == 0)
				return true;
		}
	}
	return false;
}

static bool ReadFileText(const std::wstring &path, std::wstring *text)
{
	if (!text)
		return false;
	text->clear();

	FILE *file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 0) {
		fclose(file);
		return true;
	}

	std::string bytes;
	bytes.resize(static_cast<size_t>(size));
	size_t read = fread(&bytes[0], 1, bytes.size(), file);
	fclose(file);
	if (read != bytes.size())
		return false;

	if (bytes.size() >= 3 &&
	    static_cast<unsigned char>(bytes[0]) == 0xef &&
	    static_cast<unsigned char>(bytes[1]) == 0xbb &&
	    static_cast<unsigned char>(bytes[2]) == 0xbf) {
		bytes.erase(0, 3);
	}

	UINT codePage = CP_UTF8;
	int length = MultiByteToWideChar(
		codePage, MB_ERR_INVALID_CHARS, bytes.data(),
		static_cast<int>(bytes.size()), nullptr, 0);
	if (length <= 0) {
		codePage = CP_ACP;
		length = MultiByteToWideChar(
			codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
			nullptr, 0);
	}
	if (length < 0)
		return false;

	text->assign(static_cast<size_t>(length), L'\0');
	if (length > 0) {
		const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
		if (!MultiByteToWideChar(
			    codePage, flags, bytes.data(), static_cast<int>(bytes.size()),
			    &(*text)[0], length))
			return false;
	}
	return true;
}

static std::wstring MakeRelativeNamespace(
	const std::wstring &rootDir, const std::wstring &path)
{
	wchar_t relative[MAX_PATH];
	if (PathRelativePathToW(
		relative, rootDir.c_str(), FILE_ATTRIBUTE_DIRECTORY,
		path.c_str(), FILE_ATTRIBUTE_NORMAL)) {
		std::wstring value = relative;
		if (value.rfind(L".\\", 0) == 0 || value.rfind(L"./", 0) == 0)
			value = value.substr(2);
		return value;
	}
	return path;
}

class LoaderState
{
public:
	std::wstring rootDir;
	std::set<std::wstring> seenFiles;
	IniDocument document;
	std::vector<std::wstring> loadedFiles;
	std::vector<std::wstring> errors;
	std::vector<std::wstring> warnings;
};

static void CollectIncludeEntries(
	const IniDocument &ini, std::vector<std::wstring> *includes,
	std::vector<std::wstring> *recursiveIncludes,
	std::vector<std::wstring> *excludeRecursive)
{
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoIniLoaderPath::StartsWithI(section.name, L"Include"))
			continue;
		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;
			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			if (key == L"include")
				includes->push_back(IniDocumentText::Trim(entry.value));
			else if (key == L"include_recursive")
				recursiveIncludes->push_back(IniDocumentText::Trim(entry.value));
			else if (key == L"exclude_recursive")
				excludeRecursive->push_back(IniDocumentText::Trim(entry.value));
		}
	}
}

static void LoadDirectoryRecursive(
	const std::wstring &dir, const std::vector<std::wstring> &excludeRecursive,
	LoaderState *state)
{
	if (!state)
		return;

	std::wstring search = MigotoIniLoaderPath::JoinPath(dir, L"*");
	WIN32_FIND_DATAW findData = {};
	HANDLE find = FindFirstFileW(search.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE) {
		state->warnings.push_back(L"include_recursive path not found: " + dir);
		return;
	}

	std::vector<std::wstring> iniFiles;
	std::vector<std::wstring> directories;
	do {
		std::wstring name = findData.cFileName;
		if (name == L"." || name == L"..")
			continue;
		if (MigotoIniLoaderPath::ShouldExcludeRecursive(name, excludeRecursive))
			continue;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			directories.push_back(name);
		else if (MigotoIniLoaderPath::EndsWithI(name, L".ini"))
			iniFiles.push_back(name);
	} while (FindNextFileW(find, &findData));
	FindClose(find);

	std::sort(iniFiles.begin(), iniFiles.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });
	std::sort(directories.begin(), directories.end(),
		[](const std::wstring &a, const std::wstring &b) { return IniDocumentText::ToLower(a) < IniDocumentText::ToLower(b); });

	for (const std::wstring &file : iniFiles)
	{
		std::wstring filePath = MigotoIniLoaderPath::JoinPath(dir, file);
		MigotoIniLoaderPath::LoadFileRecursive(filePath, MigotoIniLoaderPath::MakeRelativeNamespace(state->rootDir, filePath), state);
	}
	for (const std::wstring &child : directories)
		MigotoIniLoaderPath::LoadDirectoryRecursive(MigotoIniLoaderPath::JoinPath(dir, child), excludeRecursive, state);
}

static void LoadFileRecursive(
	const std::wstring &path, const std::wstring &iniNamespace, LoaderState *state)
{
	if (!state)
		return;

	std::wstring canonical = MigotoIniLoaderPath::CanonicalPath(path);
	if (!state->seenFiles.insert(canonical).second) {
		state->warnings.push_back(L"ini included multiple times: " + path);
		return;
	}

	std::wstring text;
	if (!MigotoIniLoaderPath::ReadFileText(path, &text)) {
		state->errors.push_back(L"unable to load ini: " + path);
		return;
	}

	state->loadedFiles.push_back(path);
	std::wstring sourceDir = MigotoIniLoaderPath::DirectoryOf(path);
	if (!state->document.AppendParse(text.c_str(), path, sourceDir, iniNamespace)) {
		state->errors.push_back(
			L"unable to parse ini: " + path + L" (" + state->document.Error() + L")");
		return;
	}

	std::vector<std::wstring> includes;
	std::vector<std::wstring> recursiveIncludes;
	std::vector<std::wstring> excludeRecursive;
	IniDocument localIni;
	localIni.Parse(text.c_str());
	MigotoIniLoaderPath::CollectIncludeEntries(localIni, &includes, &recursiveIncludes, &excludeRecursive);

	std::wstring includeBase = MigotoIniLoaderPath::DirectoryOf(path);
	for (const std::wstring &include : includes)
	{
		std::wstring includePath = MigotoIniLoaderPath::JoinPath(includeBase, include);
		MigotoIniLoaderPath::LoadFileRecursive(includePath, MigotoIniLoaderPath::MakeRelativeNamespace(state->rootDir, includePath), state);
	}
	for (const std::wstring &recursive : recursiveIncludes)
		MigotoIniLoaderPath::LoadDirectoryRecursive(MigotoIniLoaderPath::JoinPath(includeBase, recursive), excludeRecursive, state);
}

};

inline bool MigotoIniLoader::LoadWithIncludes(const wchar_t *rootPath, MigotoIniLoadResult *result)
{
	if (!rootPath || !result)
		return false;

	*result = MigotoIniLoadResult();
	MigotoIniLoaderPath::LoaderState state;
	state.rootDir = MigotoIniLoaderPath::DirectoryOf(rootPath);
	MigotoIniLoaderPath::LoadFileRecursive(rootPath, L"", &state);

	result->loadedFiles = std::move(state.loadedFiles);
	result->errors = std::move(state.errors);
	result->warnings = std::move(state.warnings);
	result->document = std::move(state.document);
	return true;
}

} // namespace Bunny




namespace Bunny {

class ResourceConfig
{
public:
	std::wstring section;
	std::wstring name;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring type;
	std::wstring filename;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::wstring format;
	std::wstring data;
	uint32_t stride = 0;
	uint32_t arraySize = 0;
	uint32_t byteWidth = 0;
	bool hasStride = false;
	bool hasArraySize = false;
	bool hasByteWidth = false;
};

using ResourceMap = std::unordered_map<std::wstring, ResourceConfig>;

class MigotoResourceParser
{
public:
	static void ParseSections(const IniDocument &ini, ResourceMap *resources);
};

} // namespace Bunny

namespace Bunny {

class MigotoResourceParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = IniDocumentText::Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *IniDocumentText::Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

};

inline void MigotoResourceParser::ParseSections(const IniDocument &ini, ResourceMap *resources)
{
	if (!resources)
		return;

	resources->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoResourceParserText::StartsWithI(section.name, L"Resource"))
			continue;

		ResourceConfig config;
		config.section = section.name;
		config.name = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;

			std::wstring key = IniDocumentText::ToLower(entry.key);
			if (key == L"type") {
				config.type = IniDocumentText::Trim(entry.value);
			} else if (key == L"filename" || key == L"file") {
				config.filename = IniDocumentText::Trim(entry.value);
			} else if (key == L"format") {
				config.format = IniDocumentText::Trim(entry.value);
			} else if (key == L"data") {
				config.data = IniDocumentText::Trim(entry.value);
			} else if (key == L"stride") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.stride = value;
					config.hasStride = true;
				}
			} else if (key == L"array" || key == L"array_size") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.arraySize = value;
					config.hasArraySize = true;
				}
			} else if (key == L"byte_width" || key == L"width") {
				uint32_t value = 0;
				if (MigotoResourceParserText::ParseUInt(entry.value, &value)) {
					config.byteWidth = value;
					config.hasByteWidth = true;
				}
			}
		}

		(*resources)[config.name] = config;
		if (section.iniNamespace.empty())
			(*resources)[config.originalSection] = config;
	}
}

} // namespace Bunny




namespace Bunny {

class ShaderRegexConfig
{
public:
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<std::string> shaderModels;
	CommandListLinks commandLists;
	std::vector<CommandListAction> actions;
	bool handlingSkip = false;
	bool hasPattern = false;
};

using ShaderRegexMap = std::unordered_map<std::wstring, ShaderRegexConfig>;

class MigotoShaderRegexParser
{
public:
	static void ParseSections(
		const IniDocument &ini, ShaderRegexMap *shaderRegexes);
};

}

namespace Bunny {

class MigotoShaderRegexParserText
{
public:
static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = IniDocumentText::ToLower(value);
	std::wstring lowerPrefix = IniDocumentText::ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static std::wstring ShaderRegexGroupName(const std::wstring &section)
{
	size_t namespaceEnd = 0;
	if (IniDocumentText::ToLower(section).rfind(L"shaderregex\\", 0) == 0) {
		size_t suffixStart = section.find_last_of(L'\\');
		if (suffixStart != std::wstring::npos)
			namespaceEnd = suffixStart + 1;
	}
	size_t dot = section.find(L'.', namespaceEnd);
	if (dot == std::wstring::npos)
		return section;
	return section.substr(0, dot);
}

static bool IsShaderRegexMainSection(const std::wstring &section)
{
	return ShaderRegexGroupName(section) == section;
}

static void AppendShaderModels(
	const std::wstring &value, std::vector<std::string> *shaderModels)
{
	if (!shaderModels)
		return;

	size_t start = 0;
	while (start <= value.size()) {
		size_t space = value.find_first_of(L" \t\r\n", start);
		std::wstring token = space == std::wstring::npos ?
			value.substr(start) : value.substr(start, space - start);
		token = IniDocumentText::ToLower(IniDocumentText::Trim(token));
		if (!token.empty()) {
			std::string model;
			model.reserve(token.size());
			bool ascii = true;
			for (wchar_t ch : token) {
				if (ch > 0x7f) {
					ascii = false;
					break;
				}
				model.push_back(static_cast<char>(ch));
			}
			if (ascii)
				shaderModels->push_back(model);
		}
		if (space == std::wstring::npos)
			break;
		start = space + 1;
	}
}

};

inline void MigotoShaderRegexParser::ParseSections(
	const IniDocument &ini, ShaderRegexMap *shaderRegexes)
{
	if (!shaderRegexes)
		return;

	shaderRegexes->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!MigotoShaderRegexParserText::StartsWithI(section.name, L"ShaderRegex"))
			continue;

		const std::wstring groupName = MigotoShaderRegexParserText::ShaderRegexGroupName(section.name);
		ShaderRegexConfig &config = (*shaderRegexes)[groupName];
		if (config.section.empty()) {
			config.section = groupName;
			config.originalSection = section.originalName.empty() ? groupName : MigotoShaderRegexParserText::ShaderRegexGroupName(section.originalName);
			config.sourcePath = section.sourcePath;
			config.sourceDir = section.sourceDir;
			config.iniNamespace = section.iniNamespace;
		}

		const bool isMain = MigotoShaderRegexParserText::IsShaderRegexMainSection(section.name);
		if (!isMain) {
			config.hasPattern = true;
			DX12LogManager::DebugFields("MigotoShaderRegexParse",
				"\"stage\":\"sub_section\",\"section\":\"%S\",\"group\":\"%S\"",
				section.name.c_str(), groupName.c_str());
			continue;
		}

		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		DX12LogManager::DebugFields("MigotoShaderRegexParse",
			"\"stage\":\"main_section_found\",\"section\":\"%S\",\"group\":\"%S\",\"entries\":%zu",
			section.name.c_str(), groupName.c_str(), section.entries.size());

			for (const IniEntry &entry : section.entries) {
				CommandListAction rawAction;
				if (MigotoCommandListParser::ParseActionFromRawLine(entry.rawLine, &rawAction) &&
					(!entry.hasEquals || rawAction.kind == CommandListActionKind::ControlFlow)) {
					MigotoCommandListParser::ResolveActionReferences(&rawAction, entry.iniNamespace);
					config.actions.push_back(rawAction);
					continue;
				}
				if (!entry.hasEquals) {
					DX12LogManager::DebugFields("MigotoShaderRegexParse",
						"\"stage\":\"entry_no_equals\",\"section\":\"%S\",\"raw\":\"%S\"",
						section.name.c_str(), entry.rawLine.c_str());
					continue;
				}

			std::wstring key = IniDocumentText::ToLower(IniDocumentText::Trim(entry.key));
			std::wstring value = IniDocumentText::Trim(entry.value);
			if (key == L"shader_model") {
				size_t before = config.shaderModels.size();
				MigotoShaderRegexParserText::AppendShaderModels(value, &config.shaderModels);
				DX12LogManager::DebugFields("MigotoShaderRegexParse",
					"\"stage\":\"shader_model\",\"section\":\"%S\",\"value\":\"%S\",\"before\":%zu,\"after\":%zu",
					section.name.c_str(), value.c_str(), before, config.shaderModels.size());
				continue;
			}
			if (key == L"temps" || key == L"filter_index")
				continue;

			if (key == L"handling") {
				std::wstring handling = IniDocumentText::ToLower(IniDocumentText::Trim(value));
				if (handling == L"skip") {
					config.handlingSkip = true;
					CommandListAction action;
					if (MigotoCommandListParser::ParseActionFromEntry(key, handling, &action)) {
						MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
						config.actions.push_back(action);
					}
				}
				continue;
			}

			CommandListAction action;
			if (MigotoCommandListParser::ParseActionFromEntry(key, value, &action)) {
				MigotoCommandListParser::ResolveActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}

			MigotoCommandListParser::ParseLinksFromEntry(key, entry.value, &config.commandLists);
		}

		MigotoCommandListParser::ResolveLinks(&config.commandLists, section.iniNamespace);
	}

	size_t erasedCount = 0;
	for (auto it = shaderRegexes->begin(); it != shaderRegexes->end();) {
		if (it->second.shaderModels.empty()) {
			DX12LogManager::DebugFields("MigotoShaderRegexParse",
				"\"stage\":\"erase_empty_models\",\"section\":\"%S\"",
				it->second.section.c_str());
			it = shaderRegexes->erase(it);
			++erasedCount;
		} else {
			++it;
		}
	}
	DX12LogManager::DebugFields("MigotoShaderRegexParse",
		"\"stage\":\"final\",\"count\":%zu,\"erased\":%zu",
		shaderRegexes->size(), erasedCount);
}

}



