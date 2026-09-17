// DX12CommandListExecutor.cpp: command list execution entry points, frame begin and present replacement.
// Split from DX12ModRuntime.cpp; implementation unit for its module.
#include "DX12ModRuntimeInternal.h"
#include <tlhelp32.h>
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

// The header declares this as an inline no-op outside TIMING builds, and this
// whole block is TIMING-only, so the signature stays identical either way.
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
	// Stall age at which the first report also snapshots every thread's
	// native stack. The lock probes name a held lock but cannot see a
	// thread parked inside a driver/NGX call; the stack can.
	static constexpr ULONGLONG kStackCaptureAfterStallMs = 1500;

	inline static volatile LONG sStarted = 0;
	// One snapshot per stall: armed by the first report, re-armed when the
	// present counter advances so a later, distinct freeze captures again.
	inline static volatile LONG sStacksCaptured = 0;

	static DWORD WINAPI ThreadProc(void *parameter);
	static const char *ProbeSrwLock(SRWLOCK *lock);
	static const char *ProbeMutex(std::mutex *mutex);
	static void Report(LONG present, ULONGLONG stalledMs);
	static void CaptureThreadStacks();
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
// are therefore parked here and replayed by DX12PresentOverlayCompose,
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
			InterlockedExchange(&sStacksCaptured, 0);
			continue;
		}
		const ULONGLONG stalledMs = now - lastAdvanceTick;
		if (stalledMs < kReportAfterStallMs)
			continue;
		// Symbol resolution is the one stall this watchdog can still disarm:
		// a dbghelp/symsrv copy that loaded after the install-time sweep and is
		// now fetching a PDB over the network is invisible to every lock probe.
		// Re-sweeping costs one module snapshot and installs the retrieval
		// stubs that turn that fetch into an immediate store miss.
		DX12DbgHelpHooks::Rescan();
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
	// Device removal state: a stalled present count is explained far better by
	// "the device is gone" (0x887a0005 removed / 0x887a0006 hung / 0x887a0007
	// reset) than by any proxy-side counter, and it separates a driver/GPU TDR
	// from a submission the proxy swallowed. Queried through the tracked game
	// queue, the only device handle the proxy has before the first Present.
	char removedReasonText[24] = "n/a";
	ID3D12CommandQueue *watchdogQueue = sGameDirectQueue.load(std::memory_order_acquire);
	if (watchdogQueue) {
		ID3D12Device *watchdogDevice = nullptr;
		if (SUCCEEDED(watchdogQueue->GetDevice(IID_PPV_ARGS(&watchdogDevice))) &&
		    watchdogDevice) {
			snprintf(removedReasonText, sizeof(removedReasonText), "0x%lx",
				static_cast<unsigned long>(watchdogDevice->GetDeviceRemovedReason()));
			watchdogDevice->Release();
		}
	}
	DX12_LOG_FLUSH("DX12FreezeWatchdog",
		"event=stall present=%ld executedPresent=%ld stalledMs=%llu stage=%s "
		"skipAll=%ld hookFlags=0x%llx uploadPending=%d uploadPendingFence=%llu uploadCompleted=%llu "
		"gameQueue=%d deviceRemovedReason=%s overlayRecords=%zu",
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
		removedReasonText,
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
	if (stalledMs >= kStackCaptureAfterStallMs &&
	    InterlockedCompareExchange(&sStacksCaptured, 1, 0) == 0)
		CaptureThreadStacks();
}

namespace
{
	constexpr unsigned int kSnapshotMaxFrames = 32;
	// ZZZ fans out well past a hundred job-worker threads; the cap must
	// clear all of them or the one thread that matters (the render thread,
	// created early but enumerated in creation order behind hordes of
	// workers) never gets captured.
	constexpr unsigned int kSnapshotMaxThreads = 256;
	constexpr unsigned int kSnapshotMaxModules = 512;

	// Module table built from a toolhelp module snapshot. Address resolution
	// runs while target threads are suspended, so it must not touch the
	// loader lock - a suspended thread may be holding it. A kernel snapshot
	// plus a plain sorted array keeps the whole probe lock-free.
	struct SnapshotModule
	{
		unsigned long long base = 0;
		unsigned long long end = 0;
		char path[MAX_PATH] = {};
	};

	struct SnapshotFrame
	{
		void *address = nullptr;
		unsigned long long moduleBase = 0;
		unsigned long long moduleOffset = 0;
		char module[256] = {};
	};

	struct SnapshotThreadStack
	{
		unsigned long threadId = 0;
		unsigned int frameCount = 0;
		SnapshotFrame frames[kSnapshotMaxFrames];
	};

	// Static storage: the watchdog is a single thread and captures at most
	// once per stall, so no synchronization is needed around these buffers.
	SnapshotModule sSnapshotModules[kSnapshotMaxModules];
	SnapshotThreadStack sSnapshotStacks[kSnapshotMaxThreads];

	unsigned int BuildModuleTable()
	{
		const unsigned long processId = GetCurrentProcessId();
		HANDLE snapshot = CreateToolhelp32Snapshot(
			TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
		unsigned int count = 0;
		if (snapshot == INVALID_HANDLE_VALUE)
			return 0;
		MODULEENTRY32W entry = {};
		entry.dwSize = sizeof(entry);
		if (Module32FirstW(snapshot, &entry)) {
			do {
				if (count >= kSnapshotMaxModules)
					break;
				SnapshotModule &module = sSnapshotModules[count++];
				module.base = reinterpret_cast<unsigned long long>(entry.modBaseAddr);
				module.end = module.base + entry.modBaseSize;
				WideCharToMultiByte(CP_ACP, 0, entry.szExePath, -1,
					module.path, MAX_PATH, nullptr, nullptr);
			} while (Module32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		// Enumeration order is load order, not base address order; the
		// binary search below requires ascending bases. Nearly sorted input
		// makes insertion sort linear and dependency-free.
		for (unsigned int i = 1; i < count; ++i) {
			SnapshotModule value = sSnapshotModules[i];
			unsigned int j = i;
			while (j > 0 && sSnapshotModules[j - 1].base > value.base) {
				sSnapshotModules[j] = sSnapshotModules[j - 1];
				--j;
			}
			sSnapshotModules[j] = value;
		}
		return count;
	}

	// Resolves an address against the snapshot table; leaves the frame's
	// module fields empty for addresses outside every module (jit, stubs).
	void ResolveFrameModule(SnapshotFrame &frame,
		const SnapshotModule *modules, unsigned int moduleCount)
	{
		const unsigned long long address =
			reinterpret_cast<unsigned long long>(frame.address);
		int low = 0;
		int high = static_cast<int>(moduleCount) - 1;
		while (low <= high) {
			const int mid = low + (high - low) / 2;
			const SnapshotModule &module = modules[mid];
			if (address < module.base) {
				high = mid - 1;
			} else if (address >= module.end) {
				low = mid + 1;
			} else {
				frame.moduleBase = module.base;
				frame.moduleOffset = address - module.base;
				strncpy_s(frame.module, module.path, _TRUNCATE);
				return;
			}
		}
	}

#if defined(_M_X64)
	// Walks one suspended thread's stack with the kernel's own unwind
	// metadata - no dbghelp dependency, no symbol server, no locks. Frames
	// beyond a leaf without unwind metadata are unknowable and stop the walk.
	unsigned int UnwindSuspendedThread(CONTEXT &context, SnapshotFrame *frames,
		unsigned int maxFrames, const SnapshotModule *modules,
		unsigned int moduleCount)
	{
		unsigned int count = 0;
		while (count < maxFrames) {
			SnapshotFrame &frame = frames[count];
			frame.address = reinterpret_cast<void *>(context.Rip);
			ResolveFrameModule(frame, modules, moduleCount);
			++count;
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION function =
				RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
			if (!function)
				break;
			void *handlerData = nullptr;
			DWORD64 establisherFrame = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip,
				function, &context, &handlerData, &establisherFrame, nullptr);
			if (!context.Rip)
				break;
		}
		return count;
	}
#endif
}

void DX12FreezeWatchdog::CaptureThreadStacks()
{
#if defined(_M_X64)
	const unsigned long processId = GetCurrentProcessId();
	const unsigned int moduleCount = BuildModuleTable();

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, processId);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;
	const unsigned long selfThreadId = GetCurrentThreadId();
	const unsigned long writerThreadId = DX12DiagLog::WriterThreadId();
	unsigned int stackCount = 0;
	unsigned int seenCount = 0;
	unsigned int skippedCount = 0;
	THREADENTRY32 entry = {};
	entry.dwSize = sizeof(entry);
	if (Thread32First(snapshot, &entry)) {
		do {
			if (entry.th32OwnerProcessID != processId ||
			    entry.th32ThreadID == selfThreadId ||
			    entry.th32ThreadID == writerThreadId)
				continue;
			++seenCount;
			if (stackCount >= kSnapshotMaxThreads) {
				++skippedCount;
				continue;
			}
			HANDLE thread = OpenThread(
				THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME |
					THREAD_GET_CONTEXT,
				FALSE, entry.th32ThreadID);
			if (!thread)
				continue;
			// Suspend exactly once and resume exactly once: the count we
			// added is ours to remove, whatever the thread was doing.
			if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
				CONTEXT context = {};
				context.ContextFlags = CONTEXT_FULL;
				if (GetThreadContext(thread, &context)) {
					SnapshotThreadStack &stack = sSnapshotStacks[stackCount];
					stack.threadId = entry.th32ThreadID;
					stack.frameCount = UnwindSuspendedThread(context,
						stack.frames, kSnapshotMaxFrames,
						sSnapshotModules, moduleCount);
					++stackCount;
				}
				ResumeThread(thread);
			}
			CloseHandle(thread);
		} while (Thread32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);

	// Everything is suspended-and-resumed before the first log line: the
	// logger may need threads (the writer, lock holders) that a suspended
	// snapshot would otherwise starve.
	DX12_LOG_FLUSH("DX12ThreadStackSummary",
		"\"captured\":%u,\"seen\":%u,\"skipped\":%u,\"modules\":%u",
		stackCount, seenCount, skippedCount, moduleCount);
	for (unsigned int i = 0; i < stackCount; ++i) {
		const SnapshotThreadStack &stack = sSnapshotStacks[i];
		for (unsigned int f = 0; f < stack.frameCount; ++f) {
			const SnapshotFrame &frame = stack.frames[f];
			DX12_LOG_FLUSH("DX12ThreadStack",
				"\"threadId\":%lu,\"frame\":%u,\"address\":\"%p\","
				"\"module\":\"%s\",\"moduleBase\":\"0x%llx\","
				"\"moduleOffset\":\"0x%llx\"",
				stack.threadId, f, frame.address, frame.module,
				frame.moduleBase, frame.moduleOffset);
		}
	}
#endif
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

namespace
{
	// Lazily retire every custom-shader cache once per mod generation. Doing it
	// at present time (the only consumer) avoids cross-thread deletion against
	// the reload path; a stale PSO or bytecode would otherwise survive an
	// F10-style reload forever because the pipeline cache keys never expire.
	//
	// It has to run BEFORE the composer resolves its channel and surface cache,
	// because clearing those leaves the previously fetched pointers dangling.
	void RetireCustomShaderCachesForGeneration()
	{
		static std::atomic<UINT64> sCustomShaderCacheGeneration = 0;
		const UINT64 reloadGeneration = DX12ModGetReloadGenerationFast();
		if (sCustomShaderCacheGeneration.load(std::memory_order_acquire) == reloadGeneration)
			return;
		Bunny::DX12CustomShaderPipelineCache::ClearAll();
		Bunny::DX12IniParamsTexture::ClearAll();
		Bunny::DX12CustomShaderTableHeap::ClearAll();
		Bunny::DX12CustomShaderSurfaceCache::ClearAll();
		Bunny::DX12CustomShaderCompile::ClearCache();
		Bunny::DX12PresentOverlayChannel::ClearAll();
		sCustomShaderCacheGeneration.store(reloadGeneration, std::memory_order_release);
	}

	// Moves the queued [Present] elements out of the hand-off slot. Returns
	// false when the mod has nothing waiting, which is the usual case outside a
	// mod that uses [Present] elements.
	bool TakePendingPresentOverlay(
		std::vector<DX12ModIaReplacement::CustomElementRecord> *records,
		ID3D12Device **device)
	{
		std::lock_guard<std::mutex> lock(sPendingPresentOverlayLock);
		if (sPendingPresentOverlay.records.empty())
			return false;
		records->swap(sPendingPresentOverlay.records);
		*device = sPendingPresentOverlay.device;
		sPendingPresentOverlay.device = nullptr;
		return true;
	}

	void TransitionBackBuffer(
		ID3D12GraphicsCommandList *commandList, ID3D12Resource *backBuffer,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		if (!commandList || !backBuffer)
			return;
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = backBuffer;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		commandList->ResourceBarrier(1, &barrier);
	}
}

// Replays the queued [Present] custom elements into an already-open list. The
// caller owns the back-buffer barrier, the descriptor-heap state and the
// submission, so every overlay source in a frame shares one list.
bool DX12ModRecordPresentOverlay(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Device *device,
	Bunny::DX12CustomShaderSurfaceCache *surfaceCache,
	D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
	DXGI_FORMAT backBufferFormat,
	UINT backBufferWidth,
	UINT backBufferHeight,
	const std::vector<DX12ModIaReplacement::CustomElementRecord> &records)
{
	if (!commandList || !device || !surfaceCache || !backBufferRtv.ptr)
		return false;

	Bunny::DX12CustomShaderTableHeap *tableHeap =
		Bunny::DX12CustomShaderTableHeap::ForDevice(device);
	Bunny::DX12IniParamsTexture *iniParamsTexture =
		Bunny::DX12IniParamsTexture::ForDevice(device);
	ID3D12RootSignature *rootSignature =
		Bunny::DX12CustomShaderPipelineCache::EnsureRootSignature(device);
	if (!tableHeap || !iniParamsTexture || !rootSignature)
		return false;
	if (!tableHeap->BeginPresent(static_cast<UINT64>(DX12RuntimeState::GetPresentCount())))
		return false;

	bool anyDrawn = false;
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
			    commandList, static_cast<UINT64>(DX12RuntimeState::GetPresentCount()),
			    elementIndex, record.iniParams.data())) {
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

		ID3D12PipelineState *pipeline =
			Bunny::DX12CustomShaderPipelineCache::EnsurePipeline(
				device, spec, backBufferFormat);
		if (!pipeline) {
			DX12_LOG_FLUSH("DX12CustomShaderDiag",
				"event=replay_skip reason=pipeline_unavailable section=%S format=%d",
				spec.section.c_str(), static_cast<int>(backBufferFormat));
			continue;
		}

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
	return anyDrawn;
}

// Composes every overlay source for one frame into a single command list and
// submits it exactly once, bracketed by one back-buffer barrier pair.
//
// Before this existed each source took its own slot on the shared channel, so a
// frame with a mod [Present] element and hunting text cost two
// ExecuteCommandLists calls and transitioned the back buffer twice. The
// profiling HUD now rides along for the cost of a few more vertices in a draw
// that was already happening.
bool DX12PresentOverlayCompose(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;
	DX12MarkStage("overlay_begin");

	// Retire first: it can clear the channel and the surface cache, so nothing
	// may hold a pointer to either yet.
	RetireCustomShaderCachesForGeneration();

	std::vector<DX12ModIaReplacement::CustomElementRecord> records;
	ID3D12Device *modDevice = nullptr;
	const bool hasModOverlay = TakePendingPresentOverlay(&records, &modDevice);
	const bool hasOverlayText = DX12NativeOverlayHasContent();
	if (!hasModOverlay && !hasOverlayText) {
		if (modDevice)
			modDevice->Release();
		return false;
	}

	ID3D12Device *device = modDevice;
	if (!device) {
		if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))) || !device)
			return false;
	}
	ID3D12CommandQueue *queue = AcquireGameDirectQueue();
	Bunny::DX12CustomShaderSurfaceCache *surfaceCache =
		Bunny::DX12CustomShaderSurfaceCache::ForDevice(device);
	Bunny::DX12PresentOverlayChannel *channel =
		Bunny::DX12PresentOverlayChannel::ForDevice(device);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	UINT width = 0;
	UINT height = 0;
	const bool hasTarget = surfaceCache &&
		surfaceCache->EnsureBackBufferView(swapChain, &rtv, &format, &width, &height);

	bool submitted = false;
	if (!queue || !channel || !hasTarget) {
		DX12_LOG_FLUSH("DX12CustomShaderDiag",
			"event=replay_skip reason=overlay_channel_unavailable queue=%d channel=%d target=%d",
			queue ? 1 : 0, channel ? 1 : 0, hasTarget ? 1 : 0);
	} else {
		// Prepare the text overlay BEFORE taking the channel's recording lock.
		// Its one-time font atlas upload needs a channel slot of its own, and a
		// nested acquisition of the same non-recursive lock would deadlock.
		const bool textReady = hasOverlayText &&
			DX12NativeOverlayPrepare(swapChain, queue, format);

		// Hold the channel's recording lock across BeginRecord..Submit: the
		// channel hands out a list and closes whichever slot its cursor points
		// at, so a second recording thread in between would close the wrong
		// slot.
		Bunny::DX12PresentOverlayChannel::RecordScope recordScope(channel);
		ID3D12GraphicsCommandList *commandList = channel->BeginRecord();
		if (!commandList) {
			DX12_LOG_FLUSH("DX12CustomShaderDiag",
				"event=replay_skip reason=overlay_begin_record_failed detail=%s",
				channel->LastFailure());
		} else {
			// The back buffer sits in PRESENT state at this point; one barrier
			// pair brackets every source recorded below.
			UINT bufferIndex = 0;
			IDXGISwapChain3 *swapChain3 = nullptr;
			if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) &&
			    swapChain3) {
				bufferIndex = swapChain3->GetCurrentBackBufferIndex();
				swapChain3->Release();
			}
			ID3D12Resource *backBuffer = nullptr;
			if (FAILED(swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer))))
				backBuffer = nullptr;

			TransitionBackBuffer(commandList, backBuffer,
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

			bool anyDrawn = false;
			if (hasModOverlay)
				anyDrawn |= DX12ModRecordPresentOverlay(
					commandList, device, surfaceCache, rtv, format,
					width, height, records);
			// Only the text/HUD record is timed separately: it is the part that
			// does per-frame work of its own (a content-key compare, and a
			// re-layout when the report moved), whereas everything else here is
			// either a one-off or the mod's own replay. Being able to subtract
			// it is what makes the HUD's cost auditable.
			DX12_DIAG_QPC(textBegin);
			const UINT overlayIndices = textReady
				? DX12NativeOverlayRecord(
					commandList, swapChain, queue, rtv, format, width, height)
				: 0u;
			DX12_PROF_ADD_US("Present.overlay_text_record", DX12_DIAG_ELAPSED_US(textBegin));
			anyDrawn |= overlayIndices != 0;

			TransitionBackBuffer(commandList, backBuffer,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			if (backBuffer)
				backBuffer->Release();

			if (anyDrawn) {
				DX12MarkStage("overlay_channel_submit");
				submitted = channel->Submit(queue);
				// Only the text overlay owns a fence, and only its own draw
				// reads the vertex slot: tag it after the submission that
				// actually recorded that draw. The tag has to describe the
				// newest GPU work reading that memory.
				if (overlayIndices)
					DX12NativeOverlayOnSubmitted(swapChain, queue);
			} else {
				commandList->Close();
			}
		}
	}

	DX12_LOG_FLUSH("DX12PresentDiag",
		"event=overlay_submit records=%zu text=%d submitted=%d",
		records.size(), hasOverlayText ? 1 : 0, submitted ? 1 : 0);
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
	// delays a hotkey by one frame. The autosave below still runs in that case
	// (it takes the lock itself only when a persist variable changed), so a
	// busy frame boundary can delay a hotkey but never the save.
	if (TryAcquireSRWLockExclusive(&gModLock)) {
		MarkFrameBoundaryStage("key_bindings_locked");
		if (gLoaded)
			Bunny::MigotoKeyBindingUtils::ProcessBindings(&gKeyBindings, &actions);
		ReleaseSRWLockExclusive(&gModLock);
		MarkFrameBoundaryStage("key_bindings_done");

		for (const Bunny::MigotoKeyBindingUtils::RuntimeAction &action : actions)
			RunNamedCommandListVmOnly(action.commandList, action.includePost);
		MarkFrameBoundaryStage("key_actions_done");
	}
	// Writes triggered by command lists (UI panels, shader overrides) also land
	// in the global table during this frame, so the flush belongs at the frame
	// boundary - the same place DX11's Present hook autosaves from.
	DX12UserConfigSave::TickAutosave();
	MarkFrameBoundaryStage("user_config_autosave_done");
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
