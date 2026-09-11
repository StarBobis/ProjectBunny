// DX12CommandListExecutor.cpp: command list execution entry points, frame begin and present replacement.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"
// Wrapper identity registry lock (DX12WrapperDetail::gWrapperLock), probed by
// the freeze watchdog: a hang inside wrapper lookup (UnwrapResource and
// friends) holds it and is otherwise invisible to the mod-side lock probes.
#include "../Presentation/DX12WrapperCore.h"


#if DX12_TIMING_BUILD
// Frame-boundary breadcrumb. Everything that can block a user-visible freeze
// lives inside DX12ModBeginFrame (the tail of the Present wrapper on the game's
// present thread), so when the game stops dead the last stage marker left
// behind names the blocking call exactly. Written with a relaxed store and
// never formatted unless a marker is requested: no allocation, no lock, no log
// traffic, so it cannot perturb the timing it measures.
namespace
{
	std::atomic<const char *> gFrameBoundaryStage{ nullptr };
}

void DX12MarkStage(const char *stage)
{
	gFrameBoundaryStage.store(stage, std::memory_order_relaxed);
}


// Reads the breadcrumb back for a one-line diagnostic. A frozen process stops
// emitting, so the marker is reported at the START of every frame boundary:
// the line printed by frame N+1 names the stage frame N died in.
void DX12LogFrameBoundaryStage()
{
	const char *stage = gFrameBoundaryStage.load(std::memory_order_relaxed);
	DX12_LOG("DX12KeyDiag",
		"event=frame_stage present=%ld stage=%s",
		DX12RuntimeState::GetPresentCount(), stage ? stage : "unknown");
}


namespace
{
	void MarkFrameBoundaryStage(const char *stage)
	{
		DX12MarkStage(stage);
	}
}
#else
namespace
{
	void MarkFrameBoundaryStage(const char *) {}
}
#endif


#if DX12_TIMING_BUILD
// Freeze watchdog (TIMING builds only). When the present counter stops
// advancing, the game is either deadlocked on one of the mod locks or
// stalled inside a driver/GPU wait - and the frozen thread never publishes
// its TLS log buffer, so the log alone cannot name the blocking call. This
// thread detects the stall externally and dumps the frame-boundary stage
// marker plus non-blocking probes of every lock the mod runtime can wait on
// (TryAcquire only: the probes themselves can never deadlock). The first
// report lands ~1.5s into the stall and repeats while it persists.
// Declared here so DX12ModBeginFrame can arm it; defined below once the
// pending-overlay statics exist.
class DX12FreezeWatchdog
{
public:
	static void Start();

private:
	static constexpr ULONGLONG kReportAfterStallMs = 1500;

	inline static volatile LONG sStarted = 0;

	static DWORD WINAPI ThreadProc(void *parameter);
	static const char *ProbeSrwLock(SRWLOCK *lock);
	static const char *ProbeMutex(std::mutex *mutex);
	static void Report(LONG present, ULONGLONG stalledMs);
};
#endif


bool TextureOverrideMatchCsSatisfied(
	const Bunny::TextureOverrideConfig &config,
	const std::unordered_set<std::wstring> &activePreSkinSections)
{
	if (!config.hasCsHash)
		return true;
	return activePreSkinSections.find(config.section) != activePreSkinSections.end();
}


bool TextureOverrideDependsOnActivePreSkin(
	const Bunny::TextureOverrideConfig &config)
{
	return config.hasCsHash;
}


const std::unordered_set<std::wstring> &EmptyActivePreSkinSections()
{
	static const std::unordered_set<std::wstring> empty;
	return empty;
}


bool IaCandidateIndexesDependOnActivePreSkin(
	const std::vector<size_t> &candidateIndexes)
{
	for (size_t index : candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		if (candidate.config &&
		    TextureOverrideDependsOnActivePreSkin(*candidate.config))
			return true;
	}
	return false;
}


bool IaTriggeredCandidatesDependOnActivePreSkin(
	const std::vector<DX12TriggeredTextureOverride> &candidateTriggers)
{
	for (const DX12TriggeredTextureOverride &triggered : candidateTriggers) {
		if (triggered.config &&
		    TextureOverrideDependsOnActivePreSkin(*triggered.config))
			return true;
	}
	return false;
}


void DX12ModBeginFrame(IDXGISwapChain *swapChain)
{
#if DX12_TIMING_BUILD
	// Arm the freeze watchdog with the first frame boundary; cheap no-op
	// afterwards (single interlocked check per frame).
	DX12FreezeWatchdog::Start();
#endif
	// Report the stage the PREVIOUS frame boundary died in, before overwriting
	// the marker below. A frozen process stops emitting here, so the last line
	// this prints is the frame that never finished - without it the breadcrumb
	// is invisible, because nothing runs after the block to read it back.
	DX12LogFrameBoundaryStage();
	MarkFrameBoundaryStage("begin_frame");
	// Frame-variable collection (window/cursor snapshot + ~20 global writes
	// per frame) only matters when command lists can actually read the
	// variables; skip the whole snapshot when the mod has none.
	if (gHasCommandLists.load(std::memory_order_relaxed))
		DX12VmFrameVariablesUtils::UpdateForSwapChain(swapChain);
	MarkFrameBoundaryStage("key_bindings");
	ProcessDx12KeyBindingsForFrame();
	MarkFrameBoundaryStage("pre_skin_clear");

	std::vector<DX12RetiredLoadedResource> expired;
	std::vector<ID3D12Resource*> releasePreSkinResources;
	// Without pre-skin candidates there is no active-producer set to clear
	// and no idle tracking to maintain: skip the per-frame exclusive lock
	// entirely instead of serializing recording threads on it for nothing.
	if (gHasPreSkinTextureOverrideCandidates != 0) {
		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gPreSkinLock, "Lock.preSkinExclusive");
		MarkFrameBoundaryStage("pre_skin_clear_locked");
		// Track idle frames: if no active pre-skin overrides existed last frame, count idle.
		// If pre-skin was actively matching, reset idle counter immediately.
		if (gActivePreSkinTextureOverrides.empty()) {
			LONG idle = InterlockedIncrement(&gPreSkinBindingIdleFrames);
			if (idle > kPreSkinBindingIdleMax)
				InterlockedExchange(&gPreSkinBindingIdleFrames, kPreSkinBindingIdleMax);
		} else {
			InterlockedExchange(&gPreSkinBindingIdleFrames, 0);
		}
		ClearActivePreSkinTextureOverridesLocked(&releasePreSkinResources);
		ReleaseSRWLockExclusive(&gPreSkinLock);
	}
	// Best-effort: deferring expired-resource retirement by one frame is
	// harmless, and the frame boundary must never block on the global mod
	// lock while recording threads contend for shared acquisitions.
	MarkFrameBoundaryStage("expired_retire");
	if (TryAcquireSRWLockExclusive(&gModLock)) {
		MoveExpiredRetiredLoadedResourcesLocked(&expired);
		ReleaseSRWLockExclusive(&gModLock);
	}
	for (DX12RetiredLoadedResource &resource : expired)
		ReleaseLoadedResourceObjects(&resource.resource);
	MarkFrameBoundaryStage("pre_skin_release");
	ReleaseMovedPreSkinResources(&releasePreSkinResources);

	// Preload pre-skin replacement resources once per mod generation so the
	// first match does not pay synchronous file I/O + resource creation on
	// the command-recording thread. Loading is spread across frames (a fixed
	// batch per Present) so a large mod cannot freeze a single frame; the
	// generation is only marked complete when the batch loop finishes.
	{
		static UINT64 sPreSkinResourcesPreloadedGeneration = 0;
		static UINT64 sOutputResizePreloadGeneration = 0;
		static LONG sOutputResizePreloadIdleFrames = 0;
		static UINT sOutputResizePreloadFrameBudget = 0;
		const UINT64 preloadGeneration = DX12ModGetReloadGenerationFast();
		if (gHasPreSkinTextureOverrideCandidates != 0 &&
			sPreSkinResourcesPreloadedGeneration != preloadGeneration)
		{
			ID3D12Device *preloadDevice = nullptr;
			if (swapChain &&
				SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&preloadDevice))) &&
				preloadDevice)
			{
				MarkFrameBoundaryStage("preload_resources");
				if (PreloadPreSkinReplacementResources(preloadDevice))
					sPreSkinResourcesPreloadedGeneration = preloadGeneration;
				preloadDevice->Release();
			}
		}
		if (gHasPreSkinTextureOverrideCandidates != 0)
		{
			if (sOutputResizePreloadGeneration != preloadGeneration)
			{
				sOutputResizePreloadGeneration = preloadGeneration;
				sOutputResizePreloadIdleFrames = 0;
				sOutputResizePreloadFrameBudget = 0;
			}
			// Idle-frame gate plus a hard frame budget: progress-based
			// re-arming alone could keep the preload loop alive indefinitely
			// when new tracked resources keep appearing, charging the frame
			// boundary with a full resource scan every Present.
			if (sOutputResizePreloadIdleFrames < 30 &&
			    sOutputResizePreloadFrameBudget < 120)
			{
				++sOutputResizePreloadFrameBudget;
				ID3D12Device *preloadDevice = nullptr;
				if (swapChain &&
					SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&preloadDevice))) &&
					preloadDevice)
				{
					MarkFrameBoundaryStage("preload_output_resize");
					const bool progress =
						PreloadPreSkinOutputResizeCache(preloadDevice);
					sOutputResizePreloadIdleFrames =
						progress ? 0 : sOutputResizePreloadIdleFrames + 1;
					preloadDevice->Release();
				}
			}
		}
	}

	MarkFrameBoundaryStage("done");
}


bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	DX12MarkStage("prepare_present_enter");
	const LONG present = DX12RuntimeState::GetPresentCount();
	for (;;) {
		const LONG observed = InterlockedCompareExchange(
			&gPresentCommandListExecutedPresent, 0, 0);
		if (observed == present)
			return false;
		if (InterlockedCompareExchange(
			    &gPresentCommandListExecutedPresent, present, observed) == observed)
			break;
	}

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device) {
		DX12_LOG_FLUSH("DX12PresentDiag",
			"event=prepare_skip reason=no_device present=%ld", present);
		InterlockedCompareExchange(&gPresentCommandListExecutedPresent, present - 1, present);
		return false;
	}

	DX12MarkStage("prepare_run_named");
	std::vector<DX12PendingResourceViewRequest> pendingResourceViews;
	bool changed = RunNamedCommandList(
		device, commandList, L"Present", replacement, &pendingResourceViews);
	DX12MarkStage("prepare_run_named_done");
	DX12IaHashState emptyIa = EmptyIaHashState();
	changed |= ApplyPendingResourceViewsForCommandList(
		device, commandList, emptyIa, pendingResourceViews, replacement);
	device->Release();
	// Custom element records are work too: a [Present] list that only
	// records elements (this UI panel mod does exactly that) never marks the
	// replacement changed, and without them the element replay below would
	// be gated off entirely.
	const bool hasWork = changed || !replacement->customElementRecords.empty();
	DX12_LOG_FLUSH("DX12PresentDiag",
		"event=prepare_ran present=%ld changed=%d elementRecords=%zu",
		present, changed ? 1 : 0, replacement->customElementRecords.size());
	DX12MarkStage("prepare_present_leave");
	return hasWork;
}


// Custom shader element invocations recorded while the [Present] command
// list ran (phase 2.4). Recording happens at the first intercepted draw of
// the frame (the VM must run where a game list is being recorded), but the
// replay must land at Present time - the DX11 runtime executes [Present]
// after the game's rendering, and drawing earlier would both race the back
// buffer's PRESENT state and be overdrawn by the game's own frame. Records
// are therefore parked here and replayed by DX12ModSubmitPresentOverlay,
// called from the Present wrapper right before the original Present.
class DX12PendingPresentOverlay
{
public:
	std::vector<DX12ModIaReplacement::CustomElementRecord> records;
	ID3D12Device *device = nullptr; // AddRef'd while parked

	void Reset()
	{
		records.clear();
		if (device) {
			device->Release();
			device = nullptr;
		}
	}
};

static std::mutex sPendingPresentOverlayLock;
static DX12PendingPresentOverlay sPendingPresentOverlay;

// The game direct queue the overlay submits on, tracked from the
// ExecuteCommandLists wrapper (AddRef'd on change).
static std::atomic<ID3D12CommandQueue *> sGameDirectQueue{ nullptr };

void DX12ModTrackGameDirectQueue(ID3D12CommandQueue *queue)
{
	if (!queue)
		return;
#if DX12_TIMING_BUILD
	// Arm the freeze watchdog from the first submission. Startup runs entirely
	// on the skipAll fast path, which never reaches DX12ModBeginFrame, so a
	// stall before the first mod frame would otherwise go unreported - which is
	// exactly how the earlier device-removal freeze left no watchdog line.
	DX12FreezeWatchdog::Start();
#endif
	// The mod's private upload queue also flows through the ExecuteCommandLists
	// wrapper and is DIRECT type, so without this exclusion the first texture
	// upload would replace the game's queue as the overlay submit target -
	// losing all ordering against the game's rendering. The upload submission
	// always runs on the loader thread with t_uploadSubmitInLock set (the same
	// thread-local the upload-wait bookkeeping uses), which identifies it
	// exactly regardless of which wrapper/raw pointer form arrives here.
	if (DX12DdsTextureLoadUtils::t_uploadSubmitInLock)
		return;
	ID3D12CommandQueue *current =
		sGameDirectQueue.load(std::memory_order_acquire);
	if (current == queue)
		return;
	if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
		return;
	queue->AddRef();
	current = sGameDirectQueue.exchange(queue, std::memory_order_acq_rel);
	if (current && current != queue)
		current->Release();
}

static ID3D12CommandQueue *AcquireGameDirectQueue()
{
	ID3D12CommandQueue *queue =
		sGameDirectQueue.load(std::memory_order_acquire);
	if (queue)
		queue->AddRef();
	return queue;
}

ID3D12CommandQueue *DX12ModAcquireGameDirectQueue()
{
	return AcquireGameDirectQueue();
}


#if DX12_TIMING_BUILD
void DX12FreezeWatchdog::Start()
{
	if (InterlockedCompareExchange(&sStarted, 1, 0) != 0)
		return;
	HANDLE thread = CreateThread(
		nullptr, 0, &DX12FreezeWatchdog::ThreadProc, nullptr, 0, nullptr);
	if (thread)
		CloseHandle(thread);
}

DWORD WINAPI DX12FreezeWatchdog::ThreadProc(void *)
{
	LONG lastPresent = -1;
	ULONGLONG lastAdvanceTick = GetTickCount64();
	for (;;) {
		Sleep(250);
		const LONG present = DX12RuntimeState::GetPresentCount();
		const ULONGLONG now = GetTickCount64();
		if (present != lastPresent) {
			lastPresent = present;
			lastAdvanceTick = now;
			continue;
		}
		const ULONGLONG stalledMs = now - lastAdvanceTick;
		if (stalledMs < kReportAfterStallMs)
			continue;
		Report(present, stalledMs);
		// Keep re-reporting while the stall persists: a lock that is
		// released between reports shows up as a state transition.
		Sleep(static_cast<DWORD>(kReportAfterStallMs));
	}
	return 0;
}

const char *DX12FreezeWatchdog::ProbeSrwLock(SRWLOCK *lock)
{
	if (TryAcquireSRWLockExclusive(lock)) {
		ReleaseSRWLockExclusive(lock);
		return "free";
	}
	return "HELD";
}

const char *DX12FreezeWatchdog::ProbeMutex(std::mutex *mutex)
{
	if (mutex->try_lock()) {
		mutex->unlock();
		return "free";
	}
	return "HELD";
}

void DX12FreezeWatchdog::Report(LONG present, ULONGLONG stalledMs)
{
	const char *stage = gFrameBoundaryStage.load(std::memory_order_relaxed);
	const LONG executedPresent =
		InterlockedCompareExchange(&gPresentCommandListExecutedPresent, 0, 0);
	const UINT64 uploadCompleted =
		DX12DdsTextureLoadUtils::s_uploadFence ?
			DX12DdsTextureLoadUtils::s_uploadFence->GetCompletedValue() : 0;
	const char *overlayLock = "HELD";
	size_t overlayRecords = 0;
	if (sPendingPresentOverlayLock.try_lock()) {
		overlayRecords = sPendingPresentOverlay.records.size();
		sPendingPresentOverlayLock.unlock();
		overlayLock = "free";
	}
	DX12_LOG_FLUSH("DX12FreezeWatchdog",
		"event=stall present=%ld executedPresent=%ld stalledMs=%llu stage=%s "
		"skipAll=%ld hookFlags=0x%llx uploadPending=%d uploadPendingFence=%llu uploadCompleted=%llu "
		"gameQueue=%d overlayRecords=%zu",
		present,
		executedPresent,
		static_cast<unsigned long long>(stalledMs),
		stage ? stage : "unknown",
		static_cast<long>(DX12HotPathState::skipAll.load()),
		static_cast<unsigned long long>(
			DX12HotPathState::HookWorkFlags.load(std::memory_order_relaxed)),
		DX12DdsTextureLoadUtils::s_uploadHasPending.load(
			std::memory_order_acquire) ? 1 : 0,
		static_cast<unsigned long long>(
			DX12DdsTextureLoadUtils::s_uploadPendingFenceValue),
		static_cast<unsigned long long>(uploadCompleted),
		sGameDirectQueue.load(std::memory_order_acquire) ? 1 : 0,
		overlayRecords);
	DX12_LOG_FLUSH("DX12FreezeWatchdog",
		"event=locks mod=%s preskin=%s upload=%s vmGlobals=%s overlay=%s "
		"descriptor=%s wrapper=%s",
		ProbeSrwLock(&gModLock),
		ProbeSrwLock(&gPreSkinLock),
		ProbeSrwLock(&DX12DdsTextureLoadUtils::s_uploadLock),
		ProbeMutex(&MigotoCommandVmUtils::VariableStore::globalsMutex),
		overlayLock,
		ProbeSrwLock(&gDescriptorLock),
		ProbeSrwLock(&DX12WrapperDetail::gWrapperLock));
}
#endif

bool DX12ModQueuePresentCustomElements(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (replacement->customElementRecords.empty())
		return false;

	DX12MarkStage("overlay_queue_enter");
	ID3D12Device *device = AcquireModDeviceFast(commandList, nullptr);
	if (!device)
		return false;

	std::lock_guard<std::mutex> lock(sPendingPresentOverlayLock);
	sPendingPresentOverlay.Reset();
	sPendingPresentOverlay.records.swap(replacement->customElementRecords);
	sPendingPresentOverlay.device = device; // adopts the reference
	DX12_LOG_FLUSH("DX12PresentDiag",
		"event=overlay_queued records=%zu",
		sPendingPresentOverlay.records.size());
	DX12MarkStage("overlay_queue_leave");
	return true;
}

bool DX12ModSubmitPresentOverlay(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;
	DX12MarkStage("overlay_begin");

	std::vector<DX12ModIaReplacement::CustomElementRecord> records;
	ID3D12Device *device = nullptr;
	{
		std::lock_guard<std::mutex> lock(sPendingPresentOverlayLock);
		if (sPendingPresentOverlay.records.empty())
			return false;
		records.swap(sPendingPresentOverlay.records);
		device = sPendingPresentOverlay.device;
		sPendingPresentOverlay.device = nullptr;
	}

	// Lazily retire every custom-shader cache once per mod generation. Doing
	// it here (the only consumer) avoids cross-thread deletion against the
	// reload path; a stale PSO/bytecode would otherwise survive an F10-style
	// reload forever because the pipeline cache keys never expire.
	static std::atomic<UINT64> sCustomShaderCacheGeneration = 0;
	const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
	if (sCustomShaderCacheGeneration.load(std::memory_order_acquire) != reloadGeneration) {
		Bunny::DX12CustomShaderPipelineCache::ClearAll();
		Bunny::DX12IniParamsTexture::ClearAll();
		Bunny::DX12CustomShaderTableHeap::ClearAll();
		Bunny::DX12CustomShaderSurfaceCache::ClearAll();
		Bunny::DX12CustomShaderCompile::ClearCache();
		Bunny::DX12PresentOverlayChannel::ClearAll();
		sCustomShaderCacheGeneration.store(reloadGeneration, std::memory_order_release);
	}

	const UINT64 presentCount =
		static_cast<UINT64>(DX12RuntimeState::GetPresentCount());
	Bunny::DX12IniParamsTexture *iniParamsTexture =
		Bunny::DX12IniParamsTexture::ForDevice(device);
	Bunny::DX12CustomShaderTableHeap *tableHeap =
		Bunny::DX12CustomShaderTableHeap::ForDevice(device);
	Bunny::DX12CustomShaderSurfaceCache *surfaceCache =
		Bunny::DX12CustomShaderSurfaceCache::ForDevice(device);
	ID3D12RootSignature *rootSignature =
		Bunny::DX12CustomShaderPipelineCache::EnsureRootSignature(device);
	Bunny::DX12PresentOverlayChannel *channel =
		Bunny::DX12PresentOverlayChannel::ForDevice(device);
	ID3D12CommandQueue *queue = AcquireGameDirectQueue();
	// Hold the channel's recording lock across BeginRecord..Submit: the hunting
	// text overlay records on this same channel in the same frame, and a second
	// recording thread must not be able to close this recording's slot.
	Bunny::DX12PresentOverlayChannel::RecordScope recordScope(channel);

	bool anyDrawn = false;
	bool submitted = false;
	ID3D12GraphicsCommandList *commandList = nullptr;
	if (!iniParamsTexture || !tableHeap || !surfaceCache || !rootSignature ||
	    !channel || !queue) {
		DX12_LOG_FLUSH("DX12CustomShaderDiag",
			"event=replay_skip reason=overlay_channel_unavailable queue=%d",
			queue ? 1 : 0);
	} else if (!(commandList = channel->BeginRecord())) {
		DX12_LOG_FLUSH("DX12CustomShaderDiag",
			"event=replay_skip reason=overlay_begin_record_failed detail=%s",
			channel->LastFailure());
	} else if (!tableHeap->BeginPresent(presentCount)) {
		DX12_LOG_FLUSH("DX12CustomShaderDiag",
			"event=replay_skip reason=table_heap_unavailable");
		commandList->Close();
		commandList = nullptr;
	}
	if (commandList) {

	// The back buffer sits in PRESENT state at this point; bracket the
	// overlay draws with explicit transitions.
	ID3D12Resource *backBuffer = nullptr;
	UINT backBufferIndex = 0;
	IDXGISwapChain3 *swapChain3 = nullptr;
	if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) &&
	    swapChain3) {
		backBufferIndex = swapChain3->GetCurrentBackBufferIndex();
		swapChain3->Release();
	}
	if (FAILED(swapChain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&backBuffer))))
		backBuffer = nullptr;
	if (backBuffer) {
		D3D12_RESOURCE_BARRIER toRenderTarget = {};
		toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toRenderTarget.Transition.pResource = backBuffer;
		toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &toRenderTarget);
	}

	for (const DX12ModIaReplacement::CustomElementRecord &record : records) {
		const Bunny::DX12CustomShaderElementSpec &spec = record.spec;
		if (!spec.hasStage[0] || !spec.hasStage[4]) {
			DX12_LOG("DX12CustomShaderDiag",
				"event=replay_skip reason=invalid_spec");
			continue;
		}
		// Per-element table region + ini-params upload region (shared
		// ordinal): CPU descriptor writes and upload memcpys land before
		// submission, so two elements must never share a region within a
		// frame. Over capacity the remaining elements are skipped with a
		// diagnostic instead of corrupting earlier draws.
		UINT elementIndex = 0;
		if (!tableHeap->AcquireElementRegion(&elementIndex)) {
			DX12_LOG("DX12CustomShaderDiag",
				"event=replay_skip reason=element_capacity_exceeded section=%S",
				spec.section.c_str());
			break;
		}
		if (!iniParamsTexture->UpdateAndUpload(
			    commandList, presentCount, elementIndex, record.iniParams.data())) {
			DX12_LOG("DX12CustomShaderDiag",
				"event=replay_skip reason=ini_params_upload_failed section=%S",
				spec.section.c_str());
			continue;
		}
		tableHeap->FillDefault(iniParamsTexture->SrvCpuHandle());
		for (const auto &slot : record.textureSlots) {
			DX12LoadedResource *resource =
				DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
					device, commandList, slot.second);
			if (!resource || !resource->drawSrvCpu.ptr)
				continue;
			tableHeap->CopySrv(slot.first, resource->drawSrvCpu);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv = {};
		DXGI_FORMAT backBufferFormat = DXGI_FORMAT_UNKNOWN;
		UINT backBufferWidth = 0;
		UINT backBufferHeight = 0;
		if (!surfaceCache->EnsureBackBufferView(
			    swapChain, &backBufferRtv, &backBufferFormat,
			    &backBufferWidth, &backBufferHeight)) {
			DX12_LOG_FLUSH("DX12CustomShaderDiag",
				"event=replay_skip reason=no_back_buffer section=%S",
				spec.section.c_str());
			continue;
		}
		ID3D12PipelineState *pipeline =
			Bunny::DX12CustomShaderPipelineCache::EnsurePipeline(
				device, spec, backBufferFormat);
		if (!pipeline) {
			DX12_LOG_FLUSH("DX12CustomShaderDiag",
				"event=replay_skip reason=pipeline_unavailable section=%S format=%d",
				spec.section.c_str(), static_cast<int>(backBufferFormat));
			continue;
		}

		// Mod-owned native list: the wrapper hooks never see these calls.
		ID3D12DescriptorHeap *heaps[1] = { tableHeap->Heap() };
		commandList->SetDescriptorHeaps(1, heaps);
		commandList->SetGraphicsRootSignature(rootSignature);
		commandList->SetGraphicsRootDescriptorTable(0, tableHeap->GpuStart());
		commandList->SetPipelineState(pipeline);
		const D3D12_PRIMITIVE_TOPOLOGY topology =
			Bunny::DX12CustomShaderPipelineAssembler::TopologyForBind(
				spec.topology, spec.patchControlPoints);
		commandList->IASetPrimitiveTopology(topology);
		commandList->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);
		D3D12_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(backBufferWidth);
		viewport.Height = static_cast<float>(backBufferHeight);
		viewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &viewport);
		D3D12_RECT scissor = {};
		scissor.right = static_cast<LONG>(backBufferWidth);
		scissor.bottom = static_cast<LONG>(backBufferHeight);
		commandList->RSSetScissorRects(1, &scissor);
		commandList->DrawInstanced(
			record.drawCount ? record.drawCount : 4, 1,
			record.drawStart, 0);
		anyDrawn = true;
		DX12_LOG_FLUSH("DX12CustomShaderDiag",
			"event=element_drawn section=%S w=%u h=%u format=%d textures=%zu",
			spec.section.c_str(), backBufferWidth, backBufferHeight,
			static_cast<int>(backBufferFormat), record.textureSlots.size());
	}

	if (backBuffer) {
		D3D12_RESOURCE_BARRIER toPresent = {};
		toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		toPresent.Transition.pResource = backBuffer;
		toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		commandList->ResourceBarrier(1, &toPresent);
		backBuffer->Release();
	}
	if (anyDrawn) {
		DX12MarkStage("overlay_channel_submit");
		submitted = channel->Submit(queue);
	} else {
		commandList->Close();
	}
	} // if (commandList)
	DX12_LOG_FLUSH("DX12PresentDiag",
		"event=overlay_submit records=%zu drawn=%d submitted=%d",
		records.size(), anyDrawn ? 1 : 0, submitted ? 1 : 0);
	DX12MarkStage("overlay_submit_done");
	if (queue)
		queue->Release();
	if (device)
		device->Release();
	return submitted;
}


bool DX12ModPresentReplacementPendingForCurrentPresent()
{
	if (!DX12ModNeedsPresentReplacement())
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	// Plain volatile probe: the value is only meaningful to the recording
	// thread and is published with interlocked ops, so a LOCK CMPXCHG here
	// (once per Draw while a Present mod is active) buys nothing.
	return gPresentCommandListExecutedPresent != present;
}


DX12TextureOverrideLookupCacheKey MakeIaTextureOverrideLookupCacheKey(
	const Bunny::CommandListTarget &target, uint32_t hash,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 preSkinGeneration,
	const DX12TextureOverrideDrawKeyUsage &usage)
{
	DX12TextureOverrideLookupCacheKey key = {};
	key.reloadGeneration = gReloadGeneration;
	key.preSkinGeneration = preSkinGeneration;
	key.targetKind = static_cast<uint32_t>(target.kind);
	key.shaderStage = static_cast<uint32_t>(target.stage);
	key.slot = target.slot;
	key.hash = hash;
	key.vertexCount = usage.useVertexCount ? vertexCount : 0;
	key.indexCount = usage.useIndexCount ? indexCount : 0;
	key.instanceCount = usage.useInstanceCount ? instanceCount : 0;
	key.firstVertex = usage.useFirstVertex ? firstVertex : 0;
	key.firstIndex = usage.useFirstIndex ? firstIndex : 0;
	key.firstInstance = usage.useFirstInstance ? firstInstance : 0;
	return key;
}


void ReleaseIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource)
		return;
	if (snapshot->retained)
		snapshot->resource->Release();
	snapshot->resource = nullptr;
	snapshot->retained = false;
}


void AddRefIaResourceViewSnapshot(DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot || !snapshot->resource || snapshot->retained)
		return;
	snapshot->resource->AddRef();
	snapshot->retained = true;
}


void SnapshotIaResourceViewLocked(
	DX12LoadedResource *resource, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	ReleaseIaResourceViewSnapshot(snapshot);
	if (!resource)
		return;
	if (resource->uavResource && resource->uavValid &&
		resource->uavValidPresent == DX12RuntimeState::GetPresentCount()) {
		snapshot->resource = resource->uavResource;
		snapshot->metadata = *resource;
		snapshot->uavBarrierPending = resource->uavBarrierPending;
		if (resource->uavState != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
			snapshot->transitionToVertex = true;
			snapshot->stateBefore = resource->uavState;
			resource->uavState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		resource->uavBarrierPending = false;
		return;
	}
	if (resource->srvResource && resource->srvByteWidth > resource->byteWidth) {
		snapshot->resource = resource->srvResource;
		snapshot->metadata = *resource;
		return;
	}
	if (resource->resource) {
		snapshot->resource = resource->resource;
		snapshot->metadata = *resource;
	}
}


static bool IaResourceViewSnapshotRequiresWrite(const DX12LoadedResource &resource)
{
	return resource.uavResource && resource.uavValid &&
		resource.uavValidPresent == DX12RuntimeState::GetPresentCount();
}


static void SnapshotIaResourceViewReadOnlyLocked(
	const DX12LoadedResource &resource, DX12IaResourceViewSnapshot *snapshot)
{
	ReleaseIaResourceViewSnapshot(snapshot);
	if (resource.srvResource && resource.srvByteWidth > resource.byteWidth) {
		snapshot->resource = resource.srvResource;
		snapshot->metadata = resource;
		return;
	}
	if (resource.resource) {
		snapshot->resource = resource.resource;
		snapshot->metadata = resource;
	}
}


void SnapshotIaResourceViewForCommandList(
	const std::wstring &name, DX12IaResourceViewSnapshot *snapshot)
{
	if (!snapshot)
		return;
	// Fast path: most IA replacement resources are plain buffers whose
	// snapshot is a pure read. A shared lock lets concurrent recording
	// threads resolve them in parallel instead of serializing every modded
	// draw on the global exclusive gModLock (a DX11-style single-thread
	// pattern; DX12 resolves resource metadata without a global write lock).
	DX12_LOCK_ACQUIRE_SHARED(&gModLock, "Lock.modShared");
	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end() &&
	    !IaResourceViewSnapshotRequiresWrite(loaded->second)) {
		SnapshotIaResourceViewReadOnlyLocked(loaded->second, snapshot);
		ReleaseSRWLockShared(&gModLock);
		return;
	}
	ReleaseSRWLockShared(&gModLock);

	// UAV-backed entries update tracking state while snapshotting (transition
	// to VERTEX_AND_CONSTANT_BUFFER and clear the pending-barrier flag), which
	// needs the exclusive lock. Re-find and re-check: another thread may have
	// changed the entry between the two acquisitions.
	DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
	loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end())
		SnapshotIaResourceViewLocked(&loaded->second, snapshot);
	ReleaseSRWLockExclusive(&gModLock);
}


UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource)
{
	UINT64 byteWidth = resource.byteWidth;
	if (resource.uavResource && iaResource == resource.uavResource && resource.uavByteWidth)
		byteWidth = resource.uavByteWidth;
	else if (resource.srvResource && iaResource == resource.srvResource && resource.srvByteWidth)
		byteWidth = resource.srvByteWidth;
	return static_cast<UINT>((std::min)(byteWidth, static_cast<UINT64>(UINT_MAX)));
}

void ApplyIaResourceBarriers(
	ID3D12GraphicsCommandList *commandList, const DX12IaResourceViewSnapshot &snapshot)
{
	if (!commandList || !snapshot.resource)
		return;
	if (snapshot.uavBarrierPending) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.UAV.pResource = snapshot.resource;
		commandList->ResourceBarrier(1, &barrier);
	}
	if (snapshot.transitionToVertex) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = snapshot.resource;
		barrier.Transition.StateBefore = snapshot.stateBefore;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
	}
}


bool AppendResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement)
{
	if (!device || !commandList || !replacement)
		return false;

	if (!indexResource.empty())
		EnsureLoadedResourceForCommandList(device, indexResource);
	for (const auto &item : vertexResources)
		EnsureLoadedResourceForCommandList(device, item.resource);

	bool changed = false;
	if (!indexResource.empty()) {
		DX12IaResourceViewSnapshot snapshot;
		{
			DX12LoadedResourceSnapshotReadScope scope;
			SnapshotIaResourceViewForCommandList(indexResource, &snapshot);
			AddRefIaResourceViewSnapshot(&snapshot);
		}
		if (snapshot.resource) {
			ApplyIaResourceBarriers(commandList, snapshot);
			replacement->indexBuffer.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
			replacement->indexBuffer.SizeInBytes =
				IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
			replacement->indexBuffer.Format =
				snapshot.metadata.format == DXGI_FORMAT_R16_UINT ||
				snapshot.metadata.format == DXGI_FORMAT_R32_UINT ?
				snapshot.metadata.format :
				(iaState.hasIndexBuffer && iaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
					iaState.indexView.Format : DXGI_FORMAT_R32_UINT);
			replacement->RetainResource(snapshot.resource);
			replacement->hasIndexBuffer = true;
						changed = true;
		}
		ReleaseIaResourceViewSnapshot(&snapshot);
	}

	if (!vertexResources.empty()) {
		uint32_t minSlot = vertexResources.front().slot;
		uint32_t maxSlot = vertexResources.back().slot;
		if (maxSlot >= minSlot && maxSlot - minSlot < 32) {
			if (replacement->vertexBuffers.empty() ||
			    minSlot < replacement->vertexBufferStartSlot ||
			    maxSlot >= replacement->vertexBufferStartSlot + replacement->vertexBuffers.size()) {
				uint32_t oldStart = replacement->vertexBuffers.empty() ?
					minSlot : replacement->vertexBufferStartSlot;
				uint32_t oldEnd = replacement->vertexBuffers.empty() ?
					maxSlot : replacement->vertexBufferStartSlot +
						static_cast<uint32_t>(replacement->vertexBuffers.size()) - 1;
				uint32_t newStart = (std::min)(oldStart, minSlot);
				uint32_t newEnd = (std::max)(oldEnd, maxSlot);
				if (newEnd - newStart >= 32)
					return changed;

				std::vector<D3D12_VERTEX_BUFFER_VIEW> resized(newEnd - newStart + 1);
				for (size_t i = 0; i < replacement->vertexBuffers.size(); ++i) {
					uint32_t slot = replacement->vertexBufferStartSlot + static_cast<uint32_t>(i);
					resized[slot - newStart] = replacement->vertexBuffers[i];
				}
				replacement->vertexBufferStartSlot = newStart;
				replacement->vertexBuffers.swap(resized);
			}

			for (const auto &item : vertexResources) {
				DX12IaResourceViewSnapshot snapshot;
				{
					DX12LoadedResourceSnapshotReadScope scope;
					SnapshotIaResourceViewForCommandList(item.resource, &snapshot);
					AddRefIaResourceViewSnapshot(&snapshot);
				}
				if (!snapshot.resource) {
					ReleaseIaResourceViewSnapshot(&snapshot);
					continue;
				}
				ApplyIaResourceBarriers(commandList, snapshot);
				D3D12_VERTEX_BUFFER_VIEW &view =
					replacement->vertexBuffers[item.slot - replacement->vertexBufferStartSlot];
				view.BufferLocation = snapshot.resource->GetGPUVirtualAddress();
				view.SizeInBytes = IaViewByteWidthForResource(snapshot.metadata, snapshot.resource);
				const DX12IaBufferHash *sourceSlot = FindIaVertexSlot(iaState, item.slot);
				view.StrideInBytes = snapshot.metadata.stride ? snapshot.metadata.stride :
					(sourceSlot ? sourceSlot->vertexView.StrideInBytes : 0);
				replacement->RetainResource(snapshot.resource);
								changed = true;
				ReleaseIaResourceViewSnapshot(&snapshot);
			}
		}
	}
	return changed;
}


bool ApplyPendingResourceViewsForCommandList(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12IaHashState &iaState,
	const std::vector<DX12PendingResourceViewRequest> &requests,
	DX12ModIaReplacement *replacement)
{
	bool changed = false;
	for (const DX12PendingResourceViewRequest &request : requests) {
		changed |= AppendResourceViewsForCommandList(
			device, commandList, iaState,
			request.indexResource, request.vertexResources, replacement);
	}
	return changed;
}


bool CommandListRuntimeEffectLocked(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::wstring &name)
{
	if (!snapshot)
		return true;
	auto it = snapshot->commandListRuntimeEffect.find(name);
	return it == snapshot->commandListRuntimeEffect.end() || it->second;
}


bool CommandListRuntimeEffectLocked(
	const DX12CommandExecutionRuntimeSnapshot *snapshot,
	const std::wstring &name,
	bool includePost)
{
	if (!snapshot)
		return true;
	const auto &effects = includePost ?
		snapshot->commandListPostRuntimeEffect :
		snapshot->commandListPreRuntimeEffect;
	if (!effects.empty()) {
		auto it = effects.find(name);
		return it == effects.end() || it->second;
	}
	return CommandListRuntimeEffectLocked(snapshot, name);
}



bool CommandListRuntimeEffect(
	const std::wstring &name, bool includePost)
{
	return CommandListRuntimeEffectLocked(
		LoadCommandExecutionRuntimeSnapshot(), name, includePost);
}


void AppendResolvedDescriptorPrefilterSectionIds(
	const std::vector<DX12TriggeredTextureOverride> &source,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	for (const DX12TriggeredTextureOverride &triggered : source) {
		if (!triggered.config || !triggered.sectionId)
			continue;
		sectionIds->push_back(triggered.sectionId);
	}
}


bool HasPotentialResolvedDescriptorPrefilterCandidates(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount)
{
	if (!snapshot.textureOverridesWithoutDrawContext.empty() ||
	    !snapshot.textureOverridesGenericDrawContext.empty())
		return true;
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end() && !indexed->second.empty())
			return true;
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end() && !vertex->second.empty())
			return true;
	}
	return false;
}


void CollectResolvedDescriptorPrefilterSectionIds(
	const DX12CommandExecutionRuntimeSnapshot &snapshot,
	uint32_t vertexCount,
	uint32_t indexCount,
	std::vector<uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;
	sectionIds->clear();
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesWithoutDrawContext,
		sectionIds);
	AppendResolvedDescriptorPrefilterSectionIds(
		snapshot.textureOverridesGenericDrawContext,
		sectionIds);
	if (indexCount) {
		auto indexed = snapshot.textureOverridesByIndexCount.find(indexCount);
		if (indexed != snapshot.textureOverridesByIndexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(indexed->second, sectionIds);
	}
	if (vertexCount) {
		auto vertex = snapshot.textureOverridesByVertexCount.find(vertexCount);
		if (vertex != snapshot.textureOverridesByVertexCount.end())
			AppendResolvedDescriptorPrefilterSectionIds(vertex->second, sectionIds);
	}
	std::sort(sectionIds->begin(), sectionIds->end());
	sectionIds->erase(std::unique(sectionIds->begin(), sectionIds->end()), sectionIds->end());
}


DX12IaHashState EmptyIaHashState()
{
	DX12IaHashState state = {};
	return state;
}


bool RunNamedCommandList(
	ID3D12Device *device,
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement,
	std::vector<DX12PendingResourceViewRequest> *pendingResourceViews)
{
	if (!device || !commandList || !replacement)
		return false;
	if (!CommandListRuntimeEffect(name, true)) {
		DX12_LOG_FLUSH("DX12PresentDiag",
			"event=run_named_skip name=%S reason=no_runtime_effect", name.c_str());
		return false;
	}

	DX12_LOG_FLUSH("DX12PresentDiag",
		"event=run_named_enter name=%S", name.c_str());
	DX12MarkStage("run_named_list");
	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		device, commandList, emptyIa, 0, 0, 0, 0, 0, 0,
		replacement, pendingResourceViews);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, true);
#if DX12_TIMING_BUILD
	const MigotoCommandVmUtils::ExpressionContext vmProbeContext;
	DX12_LOG_FLUSH("DX12PresentDiag",
		"event=run_named name=%S changed=%d vm_menu=%.3f vm_draw=%.3f vm_ts=%.3f",
		name.c_str(), executor.Changed() ? 1 : 0,
		MigotoCommandVmUtils::LookupVariable(vmProbeContext, L"$ssmtui_menu"),
		MigotoCommandVmUtils::LookupVariable(vmProbeContext, L"$ssmtui_draw"),
		MigotoCommandVmUtils::LookupVariable(vmProbeContext, L"$ssmtui_ts"));
#endif
	return executor.Changed();
}


bool RunNamedCommandListVmOnly(const std::wstring &name, bool includePost)
{
	if (name.empty())
		return false;
	if (!CommandListRuntimeEffect(name, includePost))
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		nullptr, nullptr, emptyIa, 0, 0, 0, 0, 0, 0, nullptr, nullptr);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, includePost);
	return true;
}


#if DX12_TIMING_BUILD
// TIMING-build key diagnostics: one line per hotkey press edge (with the
// condition verdict) plus a frame-boundary snapshot of $active0/$swapkey0
// whenever either changes, so hotkey gating questions are answerable from
// d3d12_log.txt. Compiled out of regular Release builds.
static void DX12KeyPressDiagSink(
	const wchar_t *section, const wchar_t *keyText, int type,
	const wchar_t *direction, const wchar_t *conditionText,
	int conditionActive, const wchar_t *variablesState)
{
	DX12MarkStage("key_press_sink");
	DX12_LOG("DX12KeyDiag",
		"event=press section=%S key=%S type=%d direction=%S conditionText=%S condition=%d vars=%S",
		section ? section : L"", keyText ? keyText : L"", type,
		direction ? direction : L"", conditionText ? conditionText : L"",
		conditionActive, variablesState ? variablesState : L"");
}

static void DX12TraceFrameKeyVariables()
{
	static double lastActive0 = -1.0;
	static double lastSwapkey0 = -1.0;
	static LONG framesSinceLastTrace = 0;
	MigotoCommandVmUtils::ExpressionContext context;
	const double active0 =
		MigotoCommandVmUtils::LookupVariable(context, L"$active0");
	const double swapkey0 =
		MigotoCommandVmUtils::LookupVariable(context, L"$swapkey0");
	++framesSinceLastTrace;
	// Log when either value changes or roughly every 4 seconds so steady
	// states stay visible in the log.
	if (active0 == lastActive0 && swapkey0 == lastSwapkey0 &&
	    framesSinceLastTrace < 240)
		return;
	framesSinceLastTrace = 0;
	lastActive0 = active0;
	lastSwapkey0 = swapkey0;
	DX12_LOG("DX12KeyDiag",
		"event=frame_vars active0=%g swapkey0=%g", active0, swapkey0);
}
#endif


void ProcessDx12KeyBindingsForFrame()
{
#if DX12_TIMING_BUILD
	// Register the key diagnostics sink lazily on the first frame so it is
	// armed after the log file is already open.
	{
		static const bool registered = []() {
			Bunny::MigotoKeyBindingUtils::SetKeyPressSink(DX12KeyPressDiagSink);
			return true;
		}();
		(void)registered;
	}
#endif
	std::vector<Bunny::MigotoKeyBindingUtils::RuntimeAction> actions;
	// Key processing is best-effort input handling; it must never block the
	// frame boundary on the global mod lock while recording threads take
	// shared acquisitions on every draw/dispatch. A skipped round simply
	// delays a hotkey by one frame.
	if (!TryAcquireSRWLockExclusive(&gModLock))
		return;
	MarkFrameBoundaryStage("key_bindings_locked");
	if (gLoaded)
		Bunny::MigotoKeyBindingUtils::ProcessBindings(&gKeyBindings, &actions);
	ReleaseSRWLockExclusive(&gModLock);
	MarkFrameBoundaryStage("key_bindings_done");

	for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
		RunNamedCommandListVmOnly(action.commandList, action.includePost);
	MarkFrameBoundaryStage("key_actions_done");
#if DX12_TIMING_BUILD
	DX12TraceFrameKeyVariables();
#endif
	MarkFrameBoundaryStage("key_vars_traced");
}


void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || !configs || !seen)
		return;
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
		return;
	if (!seen->insert(it->second.section).second)
		return;
	configs->push_back(&it->second);
}
