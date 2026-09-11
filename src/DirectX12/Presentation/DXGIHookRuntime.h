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
			DX12_LOG_FLUSH("DX12PrimarySwapChain",
				"\"event\":\"select\",\"api\":\"%s\",\"swapchain\":\"%p\",\"identity\":\"%p\"",
				api ? api : "", swapChain, identity);
		} else if (!primary &&
			InterlockedCompareExchange(&SecondarySwapChainLogged, 1, 0) == 0) {
			DX12_LOG_FLUSH("DX12PrimarySwapChain",
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
			DX12_LOG_FLUSH("DX12PrimarySwapChain",
				"\"event\":\"release\",\"swapchain\":\"%p\",\"identity\":\"%p\"",
				swapChain, identity);
	}

	static void RunPostPresentUi(IDXGISwapChain *swapChain)
	{
		DX12NativeOverlayRender(swapChain);
	}

	static void ConsumeUiActionsForFrame(IDXGISwapChain *swapChain)
	{
		DX12_DIAG_QPC(sliceBegin);
		const bool hadActions = DX12UiActions::Consume();
		DX12_PROF_ADD_US("Present.consume_ui_actions_poll", DX12_DIAG_ELAPSED_US(sliceBegin));
		if (!hadActions)
			return;
		DX12_DIAG_QPC_RESET(sliceBegin);
		DX12HotPathState::Update();
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12_PROF_ADD_US("Present.consume_ui_actions_apply", DX12_DIAG_ELAPSED_US(sliceBegin));
	}

	static void RunPostPresentRuntime(IDXGISwapChain *swapChain, bool allowUi)
	{
		DX12_DIAG_QPC(totalBegin);
		DX12_DIAG_QPC(sliceBegin);
		DX12AsyncReadbackFlushFrameEnd();
		DX12_PROF_ADD_US("Present.readback_frame_flush", DX12_DIAG_ELAPSED_US(sliceBegin));

		DX12_DIAG_QPC_RESET(sliceBegin);
		DX12ModRunPresentMaintenance();
		DX12_PROF_ADD_US("Present.present_maintenance", DX12_DIAG_ELAPSED_US(sliceBegin));

		DX12_DIAG_QPC_RESET(sliceBegin);
		DX12HuntOnPresent();
		DX12_PROF_ADD_US("Present.hunt_on_present", DX12_DIAG_ELAPSED_US(sliceBegin));

		if (allowUi) {
			DX12_DIAG_QPC_RESET(sliceBegin);
			RunPostPresentUi(swapChain);
			DX12_PROF_ADD_US("Present.post_present_ui", DX12_DIAG_ELAPSED_US(sliceBegin));
		}

		DX12_PROF_ADD_US("Present.post_original_runtime_total", DX12_DIAG_ELAPSED_US(totalBegin));
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

	static void CompleteCaptureOrBeginNext(IDXGISwapChain *swapChain, bool dumpFrame, bool dumpShaders)
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
		DX12_DIAG_QPC(updateBegin);
		DX12HotPathState::Update();
		DX12_PROF_ADD_US("Present.hotpath_update", DX12_DIAG_ELAPSED_US(updateBegin));
			}
};
