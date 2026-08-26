#pragma once

// Outer-frame timing probe implementation.
// Split out of Core/DX12ModState.h; include that umbrella header from existing code.
#include "../DX12CommandFlow.h"

class DX12OuterTimingProbeUtils
{
public:
	enum ApiWallBucket
	{
		ApiDrawInstanced = 0,
		ApiDrawIndexedInstanced,
		ApiDispatch,
		ApiExecuteIndirect,
		ApiExecuteCommandLists,
		ApiQueueSignal,
		ApiQueueWait,
		ApiResetCommandList,
		ApiCloseCommandList,
		ApiSetPipelineState,
		ApiResourceBarrier,
		ApiSetDescriptorHeaps,
		ApiSetComputeRootSignature,
		ApiSetGraphicsRootSignature,
		ApiSetComputeRootDescriptorTable,
		ApiSetGraphicsRootDescriptorTable,
		ApiSetComputeRootDescriptor,
		ApiSetGraphicsRootDescriptor,
		ApiSetComputeRootConstants,
		ApiSetGraphicsRootConstants,
		ApiIASetIndexBuffer,
		ApiIASetVertexBuffers,
		ApiIASetPrimitiveTopology,
		ApiCopyBufferRegion,
		ApiCopyTextureRegion,
		ApiCopyResource,
		ApiCopyTiles,
		ApiResolveSubresource,
		ApiClearUnorderedAccessView,
		ApiOtherCommandList,
		ApiDeviceCommandCreate,
		ApiDevicePsoCreate,
		ApiDeviceDescriptorHeapCreate,
		ApiDeviceRootSignatureCreate,
		ApiDeviceCreateCbv,
		ApiDeviceCreateSrv,
		ApiDeviceCreateUav,
		ApiDeviceCreateRtv,
		ApiDeviceCreateDsv,
		ApiDeviceCreateSampler,
		ApiDeviceCopyDescriptors,
		ApiDeviceCopyDescriptorsSimple,
		ApiDeviceResourceCreate,
		ApiDeviceHeapCreate,
		ApiDeviceResidency,
		ApiDeviceFenceCreate,
		ApiDeviceQueryHeapCreate,
		ApiDeviceCommandSignatureCreate,
		ApiDevicePipelineLibraryCreate,
		ApiDeviceMisc,
		ApiDeviceMiscPrivateData,
		ApiDeviceMiscSharedHandle,
		ApiDeviceMiscPower,
		ApiDeviceMiscProtectedSession,
		ApiDeviceMiscLifetime,
		ApiDeviceMiscMetaCommand,
		ApiDeviceMiscStateObject,
		ApiDeviceMiscBackgroundProcessing,
		ApiDeviceMiscShaderCache,
		ApiResourceMap,
		ApiResourceUnmap,
		ApiResourceReadWrite,
		ApiResourceQuery,
		ApiDeviceQuery,
		ApiDeviceQueryPrivateData,
		ApiDeviceQueryCapability,
		ApiDeviceQueryDescriptor,
		ApiDeviceQueryAllocation,
		ApiDeviceQueryLayout,
		ApiDeviceQueryTiling,
		ApiDeviceQueryMetaCommand,
		ApiDeviceQueryRaytracing,
		ApiDeviceQueryMisc,
		ApiObjectLifetime,
		ApiWallBucketCount
	};

#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
	class ApiWallScope
	{
	public:
		int bucket = ApiOtherCommandList;
		LARGE_INTEGER begin = {};

		ApiWallScope(int value)
		{
			bucket = value;
			begin = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeBegin(bucket, begin.QuadPart);
		}

		~ApiWallScope()
		{
			const LARGE_INTEGER end = DX12OuterTimingProbeUtils::Now();
			DX12OuterTimingProbeUtils::LogApiScopeEnd(
				bucket,
				begin.QuadPart,
				end.QuadPart);
		}
	};
#else
	class ApiWallScope
	{
	public:
		ApiWallScope(int) {}
	};
#endif

	// Per-thread API wall-clock buckets, claimed once per recording thread
	// and merged once per present. The previous process-wide interlocked
	// buckets serialized every hooked API call from every thread on shared
	// cache lines (~10-20 locked ops per call), which was the dominant
	// TIMING-build stutter. Same pool idiom as DX12Profiling: plain
	// per-thread RMW on the hot path, drain with InterlockedExchange64 at
	// flush; a racing increment can be attributed one frame late
	// (diagnostics only).
	static constexpr UINT kApiWallThreadTables = 64;
	struct alignas(64) ApiWallThreadTable
	{
		DWORD threadId = 0;
		LONG64 calls[ApiWallBucketCount] = {};
		LONG64 totalUs[ApiWallBucketCount] = {};
		LONG64 maxUs[ApiWallBucketCount] = {};
		LONG64 scopeCalls = 0;
		LONG64 scopeNestedCalls = 0;
		LONG64 scopeTotalUs = 0;
	};

	// Per-thread CPU-stage counters, claimed once per recording thread and
	// drained once per present. The previous process-wide interlocked buckets
	// serialized every LogCpuStage call from every thread (render thread +
	// collector workers) on the same cache lines - on the draw hot path that
	// was ~4 locked RMW ops per call. Same pool idiom as
	// DX12ResidentMaintenanceTelemetryUtils: plain per-thread RMW on the hot
	// path, drain with InterlockedExchange64 at flush; a racing increment can
	// be attributed one frame late (diagnostics only).
	static constexpr UINT kCpuStageThreadTables = 64;
	static constexpr UINT kCpuStageEntriesPerTable = 128; // power of two
	struct CpuStageEntry
	{
		const char *category = nullptr;
		const char *stage = nullptr;
		UINT kind = 0;
		UINT64 hash = 0; // 0 marks an empty slot
		LONG64 calls = 0;
		LONG64 totalUs = 0;
		LONG64 maxUs = 0;
		LONG64 items = 0;
	};
	struct alignas(64) CpuStageThreadTable
	{
		DWORD threadId = 0;
		CpuStageEntry entries[kCpuStageEntriesPerTable];
	};
	inline static CpuStageThreadTable
		CpuStageThreadTables[kCpuStageThreadTables] = {};
	inline static volatile LONG CpuStageThreadTableCount = 0;

	// ApiScopeActiveDepth is intentionally retained for the legacy aggregate
	// coverage log, but it cannot distinguish independent recording threads.
	// Keep a small, fixed per-thread timeline alongside it so timing captures
	// can prove whether a frame is spent inside one submitting thread, split
	// across workers, or blocked between API scopes. Slots are claimed once and
	// never recycled: thread registration is not on the hot path.
	struct alignas(64) ApiThreadTimelineSlot
	{
		volatile LONG threadId = 0;
		volatile LONG64 outerCalls = 0;
		volatile LONG64 outerTotalUs = 0;
		volatile LONG64 firstBeginQpc = 0;
		volatile LONG64 lastEndQpc = 0;
		volatile LONG64 maxScopeUs = 0;
		volatile LONG64 maxGapUs = 0;
		volatile LONG maxScopeBucket = ApiOtherCommandList;
	};

	struct ApiThreadTimelineLocal
	{
		ApiThreadTimelineSlot *slot = nullptr;
		DWORD threadId = 0;
		LONG depth = 0;
		LONGLONG outerBeginQpc = 0;
		bool lookupFailed = false;
	};

	static UINT64 HashCpuStageLabel(const char *label)
	{
		// FNV-1a 64-bit; labels are short compile-time constants.
		if (!label)
			label = "";
		UINT64 hash = 0xcbf29ce484222325ull;
		for (const unsigned char *p =
				reinterpret_cast<const unsigned char*>(label); *p; ++p) {
			hash ^= *p;
			hash *= 0x100000001b3ull;
		}
		return hash;
	}

	inline static volatile LONG64 LastPresentEnterQpc = 0;
	inline static volatile LONG64 LastPresentLeaveQpc = 0;
	inline static volatile LONG64 CurrentPresentEnterQpc = 0;
	inline static volatile LONG64 CurrentFrameIntervalUs = 0;
	inline static volatile LONG64 CurrentOutsideBetweenPresentsUs = 0;

	inline static volatile LONG PresentEnterCalls = 0;
	inline static volatile LONG PresentLeaveCalls = 0;
	inline static volatile LONG64 PresentSliceKnownUs = 0;
	inline static volatile LONG64 PresentOriginalUs = 0;
	inline static volatile LONG64 PresentPostOriginalRuntimeUs = 0;
	inline static volatile LONG64 PresentModBeginFrameUs = 0;
	inline static volatile LONG64 PresentProfilingFlushUs = 0;
	inline static volatile LONG64 PresentConsumeUiActionsUs = 0;
	inline static volatile LONG64 PresentPerFrameCountersUs = 0;
	inline static volatile LONG64 PresentCaptureBookkeepingUs = 0;
	inline static volatile LONG64 PresentCaptureCompleteUs = 0;
	inline static volatile LONG64 PresentHotPathUpdateUs = 0;
	inline static volatile LONG64 PresentOtherUs = 0;

	inline static volatile LONG64 QueueExecuteStageSamples = 0;
	inline static volatile LONG64 QueueExecuteWallCalls = 0;
	inline static volatile LONG64 QueueExecuteWallCommandLists = 0;
	inline static volatile LONG64 QueueWrapperTotalUs = 0;
	inline static volatile LONG64 QueueFlowTotalUs = 0;
	inline static volatile LONG64 QueueOriginalUs = 0;
	inline static volatile LONG64 QueueNotifyUs = 0;
	inline static volatile LONG64 QueueRegisterUs = 0;
	inline static volatile LONG64 QueueUnwrapUs = 0;
	inline static volatile LONG64 QueueRebuildWorkFlagsUs = 0;
	inline static volatile LONG64 QueueOtherUs = 0;

	inline static volatile LONG64 QueueFenceCalls = 0;
	inline static volatile LONG64 QueueSignalUs = 0;
	inline static volatile LONG64 QueueWaitUs = 0;
	inline static volatile LONG64 FenceCalls = 0;
	inline static volatile LONG64 FenceTotalUs = 0;
	inline static volatile LONG64 EventWaitCalls = 0;
	inline static volatile LONG64 EventWaitTotalUs = 0;
	inline static volatile LONG64 SystemWaitCalls = 0;
	inline static volatile LONG64 SystemWaitTotalUs = 0;

	inline static volatile LONG64 WorkerTaskCalls = 0;
	inline static volatile LONG64 WorkerTaskUs = 0;
	inline static volatile LONG64 WorkerWaitCalls = 0;
	inline static volatile LONG64 WorkerWaitUs = 0;
	inline static volatile LONG64 WorkerIdleWaitUs = 0;
	inline static volatile LONG64 WorkerDrainWaitUs = 0;
	inline static ApiWallThreadTable
		ApiWallThreadTables[kApiWallThreadTables] = {};
	inline static volatile LONG ApiWallThreadTableCount = 0;
	// Overflow fallback for threads beyond the table pool. Unused in
	// practice (games record on a handful of threads); kept so overflow
	// threads never alias another thread's plain-RMW table.
	inline static volatile LONG64 ApiWallFallbackCalls[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallFallbackTotalUs[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallFallbackMaxUs[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallFallbackScopeCalls = 0;
	inline static volatile LONG64 ApiWallFallbackScopeTotalUs = 0;
	inline static volatile LONG64 ApiWallFallbackScopeNestedCalls = 0;
	// Nesting is a property of one recording thread.  The old process-wide
	// counter classified unrelated scopes on parallel Unity threads as nested,
	// which made exclusive totals lose most of the frame and hid the real cost.
	inline static thread_local LONG ApiScopeActiveDepth = 0;
	inline static volatile LONG64 ApiScopeFirstBeginQpc = 0;
	inline static volatile LONG64 ApiScopeLastEndQpc = 0;
	inline static volatile LONG64 ApiScopeMaxGapUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeginQpc = 0;
	inline static volatile LONG64 ApiScopeMaxGapEndQpc = 0;
	inline static volatile LONG64 ApiScopeLastEndThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeLastEndThreadUserUs = 0;
	// Set by LogApiScopeBegin when a gap is worth recording; the matching End
	// on the same thread then samples thread CPU times once. Keeps the
	// GetThreadTimes syscall (hundreds of ns each) off the per-API-call path.
	// Thread-local because a Begin/End pair always runs on one thread; the
	// previous process-wide flag cross-fired between threads and cost two
	// interlocked ops per API call.
	inline static thread_local LONG ApiScopeEndCpuSamplePending = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeforeThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeforeThreadUserUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapAfterThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapAfterThreadUserUs = 0;
	inline static volatile LONG64 LastFrameLatencyHandle = 0;
	inline static volatile LONG64 LastFrameLatencySwapChain = 0;
	inline static volatile LONG LastFrameLatencyThreadId = 0;
	inline static volatile LONG LastFrameLatencyPresent = 0;
	inline static volatile LONG64 LastFrameLatencyQpc = 0;
	inline static volatile LONG64 LastFenceEventHandle = 0;
	inline static volatile LONG64 LastFenceEventFence = 0;
	inline static volatile LONG64 LastFenceEventValue = 0;
	inline static volatile LONG64 LastFenceEventCompletedBefore = 0;
	inline static volatile LONG64 LastFenceEventCompletedAfter = 0;
	inline static volatile LONG LastFenceEventThreadId = 0;
	inline static volatile LONG LastFenceEventPresent = 0;
	inline static volatile LONG64 LastFenceEventQpc = 0;
	inline static volatile LONG LastFenceEventHr = 0;
	inline static volatile LONG64 LastSystemWaitHandle = 0;
	inline static volatile LONG64 LastSystemWaitSecondaryHandle = 0;
	inline static volatile LONG64 LastSystemWaitQpc = 0;
	inline static volatile LONG64 LastSystemWaitUs = 0;
	inline static volatile LONG LastSystemWaitThreadId = 0;
	inline static volatile LONG LastSystemWaitPresent = 0;
	inline static volatile LONG LastSystemWaitApi = 0;
	inline static volatile LONG LastSystemWaitResult = 0;
	inline static volatile LONG LastSystemWaitError = 0;
	inline static volatile LONG LastSystemWaitMatchesFenceEvent = 0;
	inline static volatile LONG LastSystemWaitMatchesFrameLatency = 0;
	inline static volatile LONG ApiScopeFirstBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeLastBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeLastThreadId = 0;
	inline static volatile LONG ApiScopeMaxGapBeforeBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeMaxGapAfterBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeMaxGapBeforeThreadId = 0;
	inline static volatile LONG ApiScopeMaxGapAfterThreadId = 0;
	static constexpr UINT ApiThreadTimelineSlotCount = 32;
	inline static ApiThreadTimelineSlot
		ApiThreadTimelineSlots[ApiThreadTimelineSlotCount] = {};

	static LARGE_INTEGER Frequency()
	{
		static LARGE_INTEGER frequency = InitFrequency();
		return frequency;
	}

	static LARGE_INTEGER InitFrequency()
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		return frequency;
	}

	static LARGE_INTEGER Now()
	{
		LARGE_INTEGER value = {};
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		QueryPerformanceCounter(&value);
	#endif
		return value;
	}

	static LONG64 DeltaUs(LONGLONG begin, LONGLONG end)
	{
		const LARGE_INTEGER frequency = Frequency();
		if (frequency.QuadPart <= 0 || end <= begin)
			return 0;
		return static_cast<LONG64>((end - begin) * 1000000 / frequency.QuadPart);
	}

	static LONG64 ElapsedUs(const LARGE_INTEGER &begin)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		const LARGE_INTEGER end = Now();
		return DeltaUs(begin.QuadPart, end.QuadPart);
	#else
		(void)begin;
		return 0;
	#endif
	}

	static UINT64 FenceCompletedValue(ID3D12Fence *fence)
	{
		if (!fence)
			return 0;
		return fence->GetCompletedValue();
	}

	static UINT64 TimingFenceCompletedValue(ID3D12Fence *fence)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		return FenceCompletedValue(fence);
	#else
		(void)fence;
		return 0;
	#endif
	}

	static LONG64 ClampUs(LONG64 us)
	{
		return us > 0 ? us : 0;
	}

	static UINT64 FileTimeToUs(const FILETIME &value)
	{
		ULARGE_INTEGER combined = {};
		combined.LowPart = value.dwLowDateTime;
		combined.HighPart = value.dwHighDateTime;
		return combined.QuadPart / 10;
	}

	static void GetCurrentThreadCpuUs(LONG64 *kernelUs, LONG64 *userUs)
	{
		if (kernelUs)
			*kernelUs = 0;
		if (userUs)
			*userUs = 0;
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		FILETIME createTime = {};
		FILETIME exitTime = {};
		FILETIME kernelTime = {};
		FILETIME userTime = {};
		if (!GetThreadTimes(GetCurrentThread(), &createTime, &exitTime, &kernelTime, &userTime))
			return;
		if (kernelUs)
			*kernelUs = static_cast<LONG64>(FileTimeToUs(kernelTime));
		if (userUs)
			*userUs = static_cast<LONG64>(FileTimeToUs(userTime));
#endif
	}

	static bool IsInterestingGap(LONG64 gapUs)
	{
		return gapUs >= 50000;
	}

	static const char *WaitApiName(LONG api)
	{
		switch (api) {
		case 1:
			return "WaitForSingleObject";
		case 2:
			return "WaitForMultipleObjects";
		case 3:
			return "SignalObjectAndWait";
		case 4:
			return "WaitForSingleObjectEx";
		case 5:
			return "WaitForMultipleObjectsEx";
		default:
			return "Unknown";
		}
	}

	static void LogCurrentCallStack(
		const char *reason,
		const char *api,
		HANDLE handle,
		ID3D12Fence *fence,
		UINT64 fenceValue,
		LONG64 waitUs,
		DWORD waitResult,
		DWORD threadId,
		ULONG skipFrames = 0)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		void *frames[24] = {};
		const USHORT captured = CaptureStackBackTrace(
			static_cast<DWORD>(skipFrames + 1),
			ARRAYSIZE(frames),
			frames,
			nullptr);
		for (USHORT i = 0; i < captured; ++i) {
			void *address = frames[i];
			HMODULE module = nullptr;
			char modulePath[MAX_PATH] = {};
			const char *moduleText = "";
			ULONG_PTR moduleBase = 0;
			if (GetModuleHandleExA(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(address),
					&module) && module) {
				moduleBase = reinterpret_cast<ULONG_PTR>(module);
				if (GetModuleFileNameA(module, modulePath, ARRAYSIZE(modulePath)))
					moduleText = modulePath;
			}

			DX12LogManager::Fields("DX12WaitStack",
				"\"reason\":\"%s\",\"api\":\"%s\",\"frame\":%u,\"address\":\"%p\","
				"\"module\":\"%s\",\"moduleBase\":\"0x%llx\",\"moduleOffset\":\"0x%llx\","
				"\"handle\":\"%p\",\"fence\":\"%p\",\"fenceValue\":%llu,"
				"\"waitUs\":%lld,\"waitResult\":\"0x%lx\",\"threadId\":%lu,\"present\":%ld",
				reason ? reason : "",
				api ? api : "",
				static_cast<unsigned>(i),
				address,
				moduleText,
				static_cast<unsigned long long>(moduleBase),
				static_cast<unsigned long long>(
					moduleBase ? reinterpret_cast<ULONG_PTR>(address) - moduleBase : 0),
				handle,
				fence,
				static_cast<unsigned long long>(fenceValue),
				static_cast<long long>(waitUs),
				static_cast<unsigned long>(waitResult),
				static_cast<unsigned long>(threadId),
				DX12RuntimeState::GetPresentCount());
		}
#else
		(void)reason;
		(void)api;
		(void)handle;
		(void)fence;
		(void)fenceValue;
		(void)waitUs;
		(void)waitResult;
		(void)threadId;
		(void)skipFrames;
#endif
	}

	static bool IsTrackedWaitHandle(HANDLE handle, bool *matchesFenceEvent, bool *matchesFrameLatency)
	{
		// Plain volatile reads instead of full-fence CAS reads: the handles
		// are written once per registration (rare), while every wait call in
		// the process reads them. Aligned 64-bit reads are atomic on x64 and
		// a stale diagnostic value is fine - the CAS reads here used to cost
		// two locked ops on shared cache lines per wait call.
		const ULONG_PTR rawHandle = reinterpret_cast<ULONG_PTR>(handle);
		const ULONG_PTR rawFenceHandle = static_cast<ULONG_PTR>(
			ReadLong64(&LastFenceEventHandle));
		const ULONG_PTR rawFrameLatencyHandle = static_cast<ULONG_PTR>(
			ReadLong64(&LastFrameLatencyHandle));
		const bool fenceMatch = rawHandle != 0 && rawHandle == rawFenceHandle;
		const bool frameLatencyMatch = rawHandle != 0 && rawHandle == rawFrameLatencyHandle;
		if (matchesFenceEvent)
			*matchesFenceEvent = fenceMatch;
		if (matchesFrameLatency)
			*matchesFrameLatency = frameLatencyMatch;
		return fenceMatch || frameLatencyMatch;
	}

	// Handles owned by the wrapper itself (the async log writer's 100ms poll
	// event). Waits on them are the wrapper's own sleep, never the game's:
	// skip measurement, registration and logging entirely so the hook does
	// not feed its own log (every idle writer poll used to emit a
	// "system_wait" line through the global log lock).
	static bool IsInternalWaitHandle(HANDLE handle)
	{
		return DX12LogManager::IsInternalEventHandle(handle);
	}

	static void SnapshotLastFenceEvent(
		ID3D12Fence **fence,
		UINT64 *value,
		HANDLE *eventHandle)
	{
		if (fence) {
			*fence = reinterpret_cast<ID3D12Fence*>(
				static_cast<ULONG_PTR>(
					InterlockedCompareExchange64(&LastFenceEventFence, 0, 0)));
		}
		if (value)
			*value = static_cast<UINT64>(
				InterlockedCompareExchange64(&LastFenceEventValue, 0, 0));
		if (eventHandle) {
			*eventHandle = reinterpret_cast<HANDLE>(
				static_cast<ULONG_PTR>(
					InterlockedCompareExchange64(&LastFenceEventHandle, 0, 0)));
		}
	}

	static void RegisterFrameLatencyWaitableObject(IDXGISwapChain *swapChain, HANDLE handle)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		InterlockedExchange64(&LastFrameLatencyHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(handle)));
		InterlockedExchange64(&LastFrameLatencySwapChain,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(swapChain)));
		InterlockedExchange(&LastFrameLatencyThreadId, static_cast<LONG>(GetCurrentThreadId()));
		InterlockedExchange(&LastFrameLatencyPresent, DX12RuntimeState::GetPresentCount());
		InterlockedExchange64(&LastFrameLatencyQpc, Now().QuadPart);
		DX12LogManager::Fields("DX12WaitableObject",
			"\"kind\":\"frame_latency\",\"swapchain\":\"%p\",\"handle\":\"%p\","
			"\"threadId\":%ld,\"present\":%ld",
			swapChain,
			handle,
			static_cast<LONG>(GetCurrentThreadId()),
			DX12RuntimeState::GetPresentCount());
#else
		(void)swapChain;
		(void)handle;
#endif
	}

	static void RegisterFenceEventOnCompletion(
		ID3D12Fence *fence,
		HANDLE eventHandle,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		InterlockedExchange64(&LastFenceEventHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(eventHandle)));
		InterlockedExchange64(&LastFenceEventFence,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(fence)));
		InterlockedExchange64(&LastFenceEventValue, static_cast<LONG64>(value));
		InterlockedExchange64(&LastFenceEventCompletedBefore, static_cast<LONG64>(completedBefore));
		InterlockedExchange64(&LastFenceEventCompletedAfter, static_cast<LONG64>(completedAfter));
		InterlockedExchange(&LastFenceEventThreadId, static_cast<LONG>(GetCurrentThreadId()));
		InterlockedExchange(&LastFenceEventPresent, DX12RuntimeState::GetPresentCount());
		InterlockedExchange64(&LastFenceEventQpc, Now().QuadPart);
		InterlockedExchange(&LastFenceEventHr, static_cast<LONG>(hr));
		DX12LogManager::Fields("DX12WaitableObject",
			"\"kind\":\"fence_event\",\"fence\":\"%p\",\"handle\":\"%p\","
			"\"value\":%llu,\"completedBefore\":%llu,\"completedAfter\":%llu,"
			"\"hr\":\"0x%lx\",\"threadId\":%ld,\"present\":%ld",
			fence,
			eventHandle,
			static_cast<unsigned long long>(value),
			static_cast<unsigned long long>(completedBefore),
			static_cast<unsigned long long>(completedAfter),
			static_cast<unsigned long>(hr),
			static_cast<LONG>(GetCurrentThreadId()),
			DX12RuntimeState::GetPresentCount());
#else
		(void)fence;
		(void)eventHandle;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;
#endif
	}

	static void RegisterSystemWait(
		LONG api,
		HANDLE primaryHandle,
		HANDLE secondaryHandle,
		DWORD waitResult,
		DWORD lastError,
		LONG64 us,
		bool matchesFenceEvent,
		bool matchesFrameLatency)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		InterlockedExchange64(&LastSystemWaitHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(primaryHandle)));
		InterlockedExchange64(&LastSystemWaitSecondaryHandle,
			static_cast<LONG64>(reinterpret_cast<ULONG_PTR>(secondaryHandle)));
		InterlockedExchange64(&LastSystemWaitQpc, Now().QuadPart);
		InterlockedExchange64(&LastSystemWaitUs, us);
		InterlockedExchange(&LastSystemWaitThreadId, static_cast<LONG>(GetCurrentThreadId()));
		InterlockedExchange(&LastSystemWaitPresent, DX12RuntimeState::GetPresentCount());
		InterlockedExchange(&LastSystemWaitApi, api);
		InterlockedExchange(&LastSystemWaitResult, static_cast<LONG>(waitResult));
		InterlockedExchange(&LastSystemWaitError, static_cast<LONG>(lastError));
		InterlockedExchange(&LastSystemWaitMatchesFenceEvent, matchesFenceEvent ? 1 : 0);
		InterlockedExchange(&LastSystemWaitMatchesFrameLatency, matchesFrameLatency ? 1 : 0);
#else
		(void)api;
		(void)primaryHandle;
		(void)secondaryHandle;
		(void)waitResult;
		(void)lastError;
		(void)us;
		(void)matchesFenceEvent;
		(void)matchesFrameLatency;
#endif
	}

	static void LogSystemWaitSlice(
		const char *api,
		HANDLE primaryHandle,
		HANDLE secondaryHandle,
		DWORD waitMs,
		DWORD waitResult,
		DWORD lastError,
		LONG64 us,
		bool matchesFenceEvent,
		bool matchesFrameLatency)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&SystemWaitCalls);
		AddUs(&SystemWaitTotalUs, safeUs);

		const bool interesting = safeUs >= 50000 || matchesFenceEvent || matchesFrameLatency;
		if (!interesting)
			return;

		DX12LogManager::Fields("DX12WaitableObject",
			"\"kind\":\"system_wait\",\"api\":\"%s\",\"handle\":\"%p\",\"secondaryHandle\":\"%p\","
			"\"waitMs\":%lu,\"waitResult\":\"0x%lx\",\"lastError\":%lu,\"us\":%lld,"
			"\"matchesFenceEvent\":%s,\"matchesFrameLatency\":%s,\"threadId\":%lu,\"present\":%ld",
			api ? api : "",
			primaryHandle,
			secondaryHandle,
			static_cast<unsigned long>(waitMs),
			static_cast<unsigned long>(waitResult),
			static_cast<unsigned long>(lastError),
			static_cast<long long>(safeUs),
			matchesFenceEvent ? "true" : "false",
			matchesFrameLatency ? "true" : "false",
			static_cast<unsigned long>(GetCurrentThreadId()),
			DX12RuntimeState::GetPresentCount());
#else
		(void)api;
		(void)primaryHandle;
		(void)secondaryHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;
		(void)us;
		(void)matchesFenceEvent;
		(void)matchesFrameLatency;
#endif
	}

	static bool StageEquals(const char *stage, const char *value)
	{
		return stage && value && strcmp(stage, value) == 0;
	}


	static bool LabelEquals(const char *left, const char *right)
	{
		if (!left)
			left = "";
		if (!right)
			right = "";
		return strcmp(left, right) == 0;
	}

	static LONG64 TakeLong64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static LONG TakeLong(volatile LONG *value)
	{
		return InterlockedExchange(value, 0);
	}

	static void AddUs(volatile LONG64 *target, LONG64 us)
	{
		const LONG64 safeUs = ClampUs(us);
		if (safeUs > 0)
			InterlockedAdd64(target, safeUs);
	}

	static const char *ApiWallBucketName(int bucket)
	{
		switch (bucket) {
		case ApiDrawInstanced: return "DrawInstanced";
		case ApiDrawIndexedInstanced: return "DrawIndexedInstanced";
		case ApiDispatch: return "Dispatch";
		case ApiExecuteIndirect: return "ExecuteIndirect";
		case ApiExecuteCommandLists: return "ExecuteCommandLists";
		case ApiQueueSignal: return "QueueSignal";
		case ApiQueueWait: return "QueueWait";
		case ApiResetCommandList: return "ResetCommandList";
		case ApiCloseCommandList: return "CloseCommandList";
		case ApiSetPipelineState: return "SetPipelineState";
		case ApiResourceBarrier: return "ResourceBarrier";
		case ApiSetDescriptorHeaps: return "SetDescriptorHeaps";
		case ApiSetComputeRootSignature: return "SetComputeRootSignature";
		case ApiSetGraphicsRootSignature: return "SetGraphicsRootSignature";
		case ApiSetComputeRootDescriptorTable: return "SetComputeRootDescriptorTable";
		case ApiSetGraphicsRootDescriptorTable: return "SetGraphicsRootDescriptorTable";
		case ApiSetComputeRootDescriptor: return "SetComputeRootDescriptor";
		case ApiSetGraphicsRootDescriptor: return "SetGraphicsRootDescriptor";
		case ApiSetComputeRootConstants: return "SetComputeRootConstants";
		case ApiSetGraphicsRootConstants: return "SetGraphicsRootConstants";
		case ApiIASetIndexBuffer: return "IASetIndexBuffer";
		case ApiIASetVertexBuffers: return "IASetVertexBuffers";
		case ApiIASetPrimitiveTopology: return "IASetPrimitiveTopology";
		case ApiCopyBufferRegion: return "CopyBufferRegion";
		case ApiCopyTextureRegion: return "CopyTextureRegion";
		case ApiCopyResource: return "CopyResource";
		case ApiCopyTiles: return "CopyTiles";
		case ApiResolveSubresource: return "ResolveSubresource";
		case ApiClearUnorderedAccessView: return "ClearUnorderedAccessView";
		case ApiDeviceCommandCreate: return "DeviceCommandCreate";
		case ApiDevicePsoCreate: return "DevicePsoCreate";
		case ApiDeviceDescriptorHeapCreate: return "DeviceDescriptorHeapCreate";
		case ApiDeviceRootSignatureCreate: return "DeviceRootSignatureCreate";
		case ApiDeviceCreateCbv: return "DeviceCreateCBV";
		case ApiDeviceCreateSrv: return "DeviceCreateSRV";
		case ApiDeviceCreateUav: return "DeviceCreateUAV";
		case ApiDeviceCreateRtv: return "DeviceCreateRTV";
		case ApiDeviceCreateDsv: return "DeviceCreateDSV";
		case ApiDeviceCreateSampler: return "DeviceCreateSampler";
		case ApiDeviceCopyDescriptors: return "DeviceCopyDescriptors";
		case ApiDeviceCopyDescriptorsSimple: return "DeviceCopyDescriptorsSimple";
		case ApiDeviceResourceCreate: return "DeviceResourceCreate";
		case ApiDeviceHeapCreate: return "DeviceHeapCreate";
		case ApiDeviceResidency: return "DeviceResidency";
		case ApiDeviceFenceCreate: return "DeviceFenceCreate";
		case ApiDeviceQueryHeapCreate: return "DeviceQueryHeapCreate";
		case ApiDeviceCommandSignatureCreate: return "DeviceCommandSignatureCreate";
		case ApiDevicePipelineLibraryCreate: return "DevicePipelineLibraryCreate";
		case ApiDeviceMisc: return "DeviceMisc";
		case ApiDeviceMiscPrivateData: return "DeviceMiscPrivateData";
		case ApiDeviceMiscSharedHandle: return "DeviceMiscSharedHandle";
		case ApiDeviceMiscPower: return "DeviceMiscPower";
		case ApiDeviceMiscProtectedSession: return "DeviceMiscProtectedSession";
		case ApiDeviceMiscLifetime: return "DeviceMiscLifetime";
		case ApiDeviceMiscMetaCommand: return "DeviceMiscMetaCommand";
		case ApiDeviceMiscStateObject: return "DeviceMiscStateObject";
		case ApiDeviceMiscBackgroundProcessing: return "DeviceMiscBackgroundProcessing";
		case ApiDeviceMiscShaderCache: return "DeviceMiscShaderCache";
		case ApiResourceMap: return "ResourceMap";
		case ApiResourceUnmap: return "ResourceUnmap";
		case ApiResourceReadWrite: return "ResourceReadWrite";
		case ApiResourceQuery: return "ResourceQuery";
		case ApiDeviceQuery: return "DeviceQuery";
		case ApiDeviceQueryPrivateData: return "DeviceQueryPrivateData";
		case ApiDeviceQueryCapability: return "DeviceQueryCapability";
		case ApiDeviceQueryDescriptor: return "DeviceQueryDescriptor";
		case ApiDeviceQueryAllocation: return "DeviceQueryAllocation";
		case ApiDeviceQueryLayout: return "DeviceQueryLayout";
		case ApiDeviceQueryTiling: return "DeviceQueryTiling";
		case ApiDeviceQueryMetaCommand: return "DeviceQueryMetaCommand";
		case ApiDeviceQueryRaytracing: return "DeviceQueryRaytracing";
		case ApiDeviceQueryMisc: return "DeviceQueryMisc";
		case ApiObjectLifetime: return "ObjectLifetime";
		default: return "OtherCommandList";
		}
	}

	static void MaxUs(volatile LONG64 *target, LONG64 value)
	{
		LONG64 oldValue = InterlockedCompareExchange64(target, 0, 0);
		while (value > oldValue) {
			const LONG64 previous =
				InterlockedCompareExchange64(target, value, oldValue);
			if (previous == oldValue)
				break;
			oldValue = previous;
		}
	}

	// Plain volatile reads: full-fence CAS reads on shared lines are the
	// per-API-call cost this probe exists to avoid; a slightly stale
	// diagnostic value is fine. x86-64 aligned loads are atomic.
	static LONG64 ReadLong64(volatile LONG64 *value)
	{
		return *value;
	}

	static LONG ReadLong(volatile LONG *value)
	{
		return *value;
	}

	static ApiWallThreadTable *AcquireApiWallThreadTable()
	{
		const LONG index = InterlockedIncrement(&ApiWallThreadTableCount) - 1;
		if (index < 0 || index >= static_cast<LONG>(kApiWallThreadTables))
			return nullptr;
		ApiWallThreadTables[index].threadId = GetCurrentThreadId();
		return &ApiWallThreadTables[index];
	}

	static ApiWallThreadTable *GetApiWallThreadTable()
	{
		static thread_local ApiWallThreadTable *table = AcquireApiWallThreadTable();
		return table;
	}

	struct ApiWallDrainResult
	{
		LONG64 calls[ApiWallBucketCount] = {};
		LONG64 totalUs[ApiWallBucketCount] = {};
		LONG64 maxUs[ApiWallBucketCount] = {};
		LONG64 scopeCalls = 0;
		LONG64 scopeNestedCalls = 0;
		LONG64 scopeTotalUs = 0;
	};

	static void DrainApiWallThreadTables(ApiWallDrainResult *drain)
	{
		const LONG tableCount = InterlockedCompareExchange(&ApiWallThreadTableCount, 0, 0);
		const LONG tablesToScan = tableCount < static_cast<LONG>(kApiWallThreadTables)
			? tableCount : static_cast<LONG>(kApiWallThreadTables);
		for (LONG t = 0; t < tablesToScan; ++t) {
			ApiWallThreadTable &table = ApiWallThreadTables[t];
			drain->scopeCalls += InterlockedExchange64(&table.scopeCalls, 0);
			drain->scopeNestedCalls += InterlockedExchange64(&table.scopeNestedCalls, 0);
			drain->scopeTotalUs += InterlockedExchange64(&table.scopeTotalUs, 0);
			for (int bucket = 0; bucket < ApiWallBucketCount; ++bucket) {
				drain->calls[bucket] += InterlockedExchange64(&table.calls[bucket], 0);
				drain->totalUs[bucket] += InterlockedExchange64(&table.totalUs[bucket], 0);
				const LONG64 tableMaxUs = InterlockedExchange64(&table.maxUs[bucket], 0);
				if (tableMaxUs > drain->maxUs[bucket])
					drain->maxUs[bucket] = tableMaxUs;
			}
		}
		// Overflow threads share the interlocked fallback counters.
		drain->scopeCalls += TakeLong64(&ApiWallFallbackScopeCalls);
		drain->scopeNestedCalls += TakeLong64(&ApiWallFallbackScopeNestedCalls);
		drain->scopeTotalUs += TakeLong64(&ApiWallFallbackScopeTotalUs);
		for (int bucket = 0; bucket < ApiWallBucketCount; ++bucket) {
			drain->calls[bucket] += TakeLong64(&ApiWallFallbackCalls[bucket]);
			drain->totalUs[bucket] += TakeLong64(&ApiWallFallbackTotalUs[bucket]);
			const LONG64 fallbackMaxUs = TakeLong64(&ApiWallFallbackMaxUs[bucket]);
			if (fallbackMaxUs > drain->maxUs[bucket])
				drain->maxUs[bucket] = fallbackMaxUs;
		}
	}

	static ApiThreadTimelineLocal &GetApiThreadTimelineLocal()
	{
		static thread_local ApiThreadTimelineLocal local;
		return local;
	}

	static ApiThreadTimelineSlot *GetApiThreadTimelineSlot()
	{
		ApiThreadTimelineLocal &local = GetApiThreadTimelineLocal();
		const DWORD threadId = GetCurrentThreadId();
		if (local.threadId == threadId) {
			if (local.slot || local.lookupFailed)
				return local.slot;
		} else {
			local = ApiThreadTimelineLocal();
			local.threadId = threadId;
		}

		const LONG owner = static_cast<LONG>(threadId);
		for (UINT index = 0; index < ApiThreadTimelineSlotCount; ++index) {
			ApiThreadTimelineSlot &slot = ApiThreadTimelineSlots[index];
			const LONG observed = InterlockedCompareExchange(
				&slot.threadId, 0, 0);
			if (observed == owner ||
				(observed == 0 && InterlockedCompareExchange(
					&slot.threadId, owner, 0) == 0)) {
				local.slot = &slot;
				return local.slot;
			}
		}

		local.lookupFailed = true;
		return nullptr;
	}

	static void RecordApiThreadScopeBegin(LONGLONG beginQpc)
	{
		ApiThreadTimelineLocal &local = GetApiThreadTimelineLocal();
		ApiThreadTimelineSlot *slot = GetApiThreadTimelineSlot();
		if (++local.depth != 1 || !slot)
			return;

		// The slot is claimed once per thread (GetApiThreadTimelineSlot) and
		// written only by its owner; the present-time drain exchanges it once
		// per frame. Plain RMW keeps the per-API-call cost off the interlocked
		// path - same trade the ApiWallThreadTable pool documents: a racing
		// increment during the drain is attributed one frame late.
		local.outerBeginQpc = beginQpc;
		++slot->outerCalls;
		if (!slot->firstBeginQpc)
			slot->firstBeginQpc = beginQpc;
		const LONG64 previousEndQpc = slot->lastEndQpc;
		if (previousEndQpc > 0 && beginQpc > previousEndQpc) {
			const LONG64 gapUs = DeltaUs(previousEndQpc, beginQpc);
			if (gapUs > slot->maxGapUs)
				slot->maxGapUs = gapUs;
		}
	}

	static void RecordApiThreadScopeEnd(int bucket, LONGLONG endQpc)
	{
		ApiThreadTimelineLocal &local = GetApiThreadTimelineLocal();
		if (local.depth <= 0)
			return;
		if (--local.depth != 0 || !local.slot)
			return;

		ApiThreadTimelineSlot *slot = local.slot;
		const LONG64 elapsedUs = DeltaUs(local.outerBeginQpc, endQpc);
		slot->outerTotalUs += elapsedUs;
		if (elapsedUs > slot->maxScopeUs) {
			slot->maxScopeUs = elapsedUs;
			slot->maxScopeBucket = bucket;
		}
		if (endQpc > slot->lastEndQpc)
			slot->lastEndQpc = endQpc;
	}

	static void LogApiScopeBegin(int bucket, LONGLONG beginQpc)
	{
		RecordApiThreadScopeBegin(beginQpc);
		const LONG activeDepth = ++ApiScopeActiveDepth;
		ApiWallThreadTable *table = GetApiWallThreadTable();
		if (activeDepth > 1) {
			if (table)
				++table->scopeNestedCalls;
			else
				InterlockedIncrement64(&ApiWallFallbackScopeNestedCalls);
			return;
		}
		if (table)
			++table->scopeCalls;
		else
			InterlockedIncrement64(&ApiWallFallbackScopeCalls);
		// Plain volatile reads for the shared gap state below: the full-fence
		// CAS reads used here before serialized every recording thread on the
		// same cache lines for every single API call.  A slightly stale read
		// only blurs a diagnostic gap record, never the wall totals.
		if (ReadLong64(&ApiScopeFirstBeginQpc) == 0 &&
				InterlockedCompareExchange64(
					&ApiScopeFirstBeginQpc,
					static_cast<LONG64>(beginQpc),
					0) == 0) {
			InterlockedExchange(&ApiScopeFirstBucket, bucket);
		}
		const LONG64 previousEndQpc = ReadLong64(&ApiScopeLastEndQpc);
		if (previousEndQpc > 0 && beginQpc > previousEndQpc) {
			const LONG64 gapUs = DeltaUs(previousEndQpc, beginQpc);
			const LONG currentThreadId = static_cast<LONG>(GetCurrentThreadId());
			const LONG beforeBucket = ReadLong(&ApiScopeLastBucket);
			const LONG beforeThreadId = ReadLong(&ApiScopeLastThreadId);
			LONG64 beforeKernelUs = 0;
			LONG64 beforeUserUs = 0;
			LONG64 afterKernelUs = 0;
			LONG64 afterUserUs = 0;
			// GetThreadTimes is a syscall (hundreds of ns each); sampling it on
			// every API call dominates the TIMING build cost while the 50ms
			// IsInterestingGap threshold almost never fires. Only sample when
			// this gap can actually be logged or beat the recorded maximum.
			const bool recordGap =
				IsInterestingGap(gapUs) || gapUs > ReadLong64(&ApiScopeMaxGapUs);
			if (recordGap) {
				beforeKernelUs = ReadLong64(&ApiScopeLastEndThreadKernelUs);
				beforeUserUs = ReadLong64(&ApiScopeLastEndThreadUserUs);
				GetCurrentThreadCpuUs(&afterKernelUs, &afterUserUs);
				ApiScopeEndCpuSamplePending = 1;
			}
			const LONG64 threadKernelDeltaUs =
				currentThreadId == beforeThreadId ?
					SafeSubtract(afterKernelUs, beforeKernelUs) : 0;
			const LONG64 threadUserDeltaUs =
				currentThreadId == beforeThreadId ?
					SafeSubtract(afterUserUs, beforeUserUs) : 0;
			const LONG64 threadCpuDeltaUs = threadKernelDeltaUs + threadUserDeltaUs;
			if (recordGap) {
				LONG64 observed = ReadLong64(&ApiScopeMaxGapUs);
				while (gapUs > observed) {
					const LONG64 previous = InterlockedCompareExchange64(
						&ApiScopeMaxGapUs, gapUs, observed);
					if (previous == observed) {
						InterlockedExchange64(&ApiScopeMaxGapBeginQpc, previousEndQpc);
						InterlockedExchange64(&ApiScopeMaxGapEndQpc, beginQpc);
						InterlockedExchange64(&ApiScopeMaxGapBeforeThreadKernelUs, beforeKernelUs);
						InterlockedExchange64(&ApiScopeMaxGapBeforeThreadUserUs, beforeUserUs);
						InterlockedExchange64(&ApiScopeMaxGapAfterThreadKernelUs, afterKernelUs);
						InterlockedExchange64(&ApiScopeMaxGapAfterThreadUserUs, afterUserUs);
						InterlockedExchange(&ApiScopeMaxGapBeforeBucket, beforeBucket);
						InterlockedExchange(&ApiScopeMaxGapAfterBucket, bucket);
						InterlockedExchange(&ApiScopeMaxGapBeforeThreadId, beforeThreadId);
						InterlockedExchange(&ApiScopeMaxGapAfterThreadId, currentThreadId);
						break;
					}
					observed = previous;
				}
			}
			if (IsInterestingGap(gapUs)) {
				DX12LogManager::Fields("DX12CpuGapProbe",
					"\"present\":%ld,\"gapUs\":%lld,\"beforeBucket\":\"%s\","
					"\"afterBucket\":\"%s\",\"beforeThreadId\":%ld,\"afterThreadId\":%ld,"
					"\"sameThread\":%ld,\"threadKernelDeltaUs\":%lld,"
					"\"threadUserDeltaUs\":%lld,\"threadCpuDeltaUs\":%lld,"
					"\"beginQpc\":%lld,\"endQpc\":%lld",
					DX12RuntimeState::GetPresentCount(),
					static_cast<long long>(gapUs),
					ApiWallBucketName(beforeBucket),
					ApiWallBucketName(bucket),
					beforeThreadId,
					currentThreadId,
					static_cast<LONG>(currentThreadId == beforeThreadId),
					static_cast<long long>(threadKernelDeltaUs),
					static_cast<long long>(threadUserDeltaUs),
					static_cast<long long>(threadCpuDeltaUs),
					static_cast<long long>(previousEndQpc),
					static_cast<long long>(beginQpc));
			}
		}
	}

	static void LogApiScopeEnd(int bucket, LONGLONG beginQpc, LONGLONG endQpc)
	{
		RecordApiThreadScopeEnd(bucket, endQpc);
		const LONG64 elapsedUs = DeltaUs(beginQpc, endQpc);
		const LONG activeDepth = ApiScopeActiveDepth;
		if (activeDepth <= 1) {
			ApiWallThreadTable *table = GetApiWallThreadTable();
			const LONG64 safeUs = ClampUs(elapsedUs);
			if (safeUs > 0) {
				if (table)
					table->scopeTotalUs += safeUs;
				else
					InterlockedAdd64(&ApiWallFallbackScopeTotalUs, safeUs);
			}
			// Plain-read gate: skip the CAS entirely when another thread
			// already recorded a later end.
			if (endQpc > ReadLong64(&ApiScopeLastEndQpc)) {
				LONG64 observed = ReadLong64(&ApiScopeLastEndQpc);
				while (endQpc > observed) {
					const LONG64 previous = InterlockedCompareExchange64(
						&ApiScopeLastEndQpc, endQpc, observed);
					if (previous == observed) {
						InterlockedExchange(&ApiScopeLastBucket, bucket);
						InterlockedExchange(&ApiScopeLastThreadId,
							static_cast<LONG>(GetCurrentThreadId()));
						LONG64 kernelUs = 0;
						LONG64 userUs = 0;
						// Only sample when the matching Begin flagged a
						// recordable gap; see ApiScopeEndCpuSamplePending in
						// LogApiScopeBegin.
						if (ApiScopeEndCpuSamplePending) {
							ApiScopeEndCpuSamplePending = 0;
							GetCurrentThreadCpuUs(&kernelUs, &userUs);
							InterlockedExchange64(&ApiScopeLastEndThreadKernelUs, kernelUs);
							InterlockedExchange64(&ApiScopeLastEndThreadUserUs, userUs);
						}
						break;
					}
					observed = previous;
				}
			}
		}
		LogApiWallSlice(bucket, elapsedUs);
		if (ApiScopeActiveDepth > 0)
			--ApiScopeActiveDepth;
	}

	static void LogApiWallSlice(int bucket, LONG64 us)
	{
		if (bucket < 0 || bucket >= ApiWallBucketCount)
			bucket = ApiOtherCommandList;
		const LONG64 safeUs = ClampUs(us);
		if (safeUs <= 0)
			return;
		ApiWallThreadTable *table = GetApiWallThreadTable();
		if (table) {
			++table->calls[bucket];
			table->totalUs[bucket] += safeUs;
			if (safeUs > table->maxUs[bucket])
				table->maxUs[bucket] = safeUs;
			return;
		}
		InterlockedIncrement64(&ApiWallFallbackCalls[bucket]);
		InterlockedAdd64(&ApiWallFallbackTotalUs[bucket], safeUs);
		MaxUs(&ApiWallFallbackMaxUs[bucket], safeUs);
	}

	// Routes one timing-table line through the per-frame batch (single
	// queue lock + SetEvent per present) or straight to the log manager.
	static void LogTimingFields(
		DX12LogManager::LogBatch *batch, const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		if (batch) {
			char extra[4096];
			extra[0] = '\0';
			if (fmt && fmt[0])
				vsnprintf(extra, sizeof(extra), fmt, args);
			batch->Append(func, extra);
		} else {
			DX12LogManager::FieldsV(func, fmt, args, false);
		}
		va_end(args);
	}

	static void LogApiWallTimingState(
		LONG present,
		LONG64 frameWallUs,
		const ApiWallDrainResult &drain,
		DX12LogManager::LogBatch *batch)
	{
		for (int bucket = 0; bucket < ApiWallBucketCount; ++bucket) {
			const LONG64 calls = drain.calls[bucket];
			const LONG64 totalUs = drain.totalUs[bucket];
			const LONG64 maxUs = drain.maxUs[bucket];
			if (!calls && !totalUs && !maxUs)
				continue;
			LogTimingFields(batch, "DX12ApiWallTiming",
				"\"present\":%ld,\"bucket\":\"%s\",\"calls\":%lld,\"totalUs\":%lld,\"maxUs\":%lld,\"frameWallUs\":%lld,\"framePctX100\":%lld",
				present,
				ApiWallBucketName(bucket),
				static_cast<long long>(calls),
				static_cast<long long>(totalUs),
				static_cast<long long>(maxUs),
				static_cast<long long>(frameWallUs),
				static_cast<long long>(
					frameWallUs > 0 ? (totalUs * 10000 / frameWallUs) : 0));
		}
	}

	static void LogApiThreadTimelineState(
		LONG present,
		LONGLONG frameStartQpc,
		LONGLONG frameEndQpc,
		LONG64 frameWallUs,
		DX12LogManager::LogBatch *batch)
	{
		for (UINT index = 0; index < ApiThreadTimelineSlotCount; ++index) {
			ApiThreadTimelineSlot &slot = ApiThreadTimelineSlots[index];
			const LONG threadId = InterlockedCompareExchange(&slot.threadId, 0, 0);
			const LONG64 calls = TakeLong64(&slot.outerCalls);
			const LONG64 totalUs = TakeLong64(&slot.outerTotalUs);
			const LONG64 firstBeginQpc = TakeLong64(&slot.firstBeginQpc);
			const LONG64 lastEndQpc = TakeLong64(&slot.lastEndQpc);
			const LONG64 maxScopeUs = TakeLong64(&slot.maxScopeUs);
			const LONG64 maxGapUs = TakeLong64(&slot.maxGapUs);
			const LONG maxScopeBucket = InterlockedExchange(
				&slot.maxScopeBucket, ApiOtherCommandList);
			if (!calls && !totalUs && !firstBeginQpc && !lastEndQpc)
				continue;

			const LONG64 spanUs = firstBeginQpc > 0 && lastEndQpc > firstBeginQpc ?
				DeltaUs(firstBeginQpc, lastEndQpc) : 0;
			const LONG64 startOffsetUs = frameStartQpc > 0 &&
				firstBeginQpc > frameStartQpc ?
				DeltaUs(frameStartQpc, firstBeginQpc) : 0;
			const LONG64 endSlackUs = frameEndQpc > lastEndQpc && lastEndQpc > 0 ?
				DeltaUs(lastEndQpc, frameEndQpc) : 0;
			LogTimingFields(batch, "DX12CpuThreadTimeline",
				"\"present\":%ld,\"threadId\":%ld,\"outerCalls\":%lld,"
				"\"outerTotalUs\":%lld,\"spanUs\":%lld,\"startOffsetUs\":%lld,"
				"\"endSlackUs\":%lld,\"maxScopeUs\":%lld,"
				"\"maxScopeBucket\":\"%s\",\"maxGapUs\":%lld,"
				"\"frameWallUs\":%lld,\"outerTotalPctX100\":%lld,"
				"\"spanPctX100\":%lld",
				present, threadId,
				static_cast<long long>(calls),
				static_cast<long long>(totalUs),
				static_cast<long long>(spanUs),
				static_cast<long long>(startOffsetUs),
				static_cast<long long>(endSlackUs),
				static_cast<long long>(maxScopeUs),
				ApiWallBucketName(maxScopeBucket),
				static_cast<long long>(maxGapUs),
				static_cast<long long>(frameWallUs),
				static_cast<long long>(frameWallUs > 0 ?
					totalUs * 10000 / frameWallUs : 0),
				static_cast<long long>(frameWallUs > 0 ?
					spanUs * 10000 / frameWallUs : 0));
		}
	}

	static void LogCpuTimelineCoverageState(
		LONG present,
		LONGLONG frameStartQpc,
		LONGLONG frameEndQpc,
		LONG64 frameWallUs,
		LONG64 presentWallUs,
		const ApiWallDrainResult &drain,
		DX12LogManager::LogBatch *batch)
	{
		const LONG64 calls = drain.scopeCalls;
		const LONG64 totalUs = drain.scopeTotalUs;
		const LONG64 nestedCalls = drain.scopeNestedCalls;
		const LONG64 firstBeginQpc = TakeLong64(&ApiScopeFirstBeginQpc);
		const LONG64 lastEndQpc = TakeLong64(&ApiScopeLastEndQpc);
		const LONG64 maxGapUs = TakeLong64(&ApiScopeMaxGapUs);
		const LONG64 maxGapBeginQpc = TakeLong64(&ApiScopeMaxGapBeginQpc);
		const LONG64 maxGapEndQpc = TakeLong64(&ApiScopeMaxGapEndQpc);
		const LONG64 maxGapBeforeThreadKernelUs =
			TakeLong64(&ApiScopeMaxGapBeforeThreadKernelUs);
		const LONG64 maxGapBeforeThreadUserUs =
			TakeLong64(&ApiScopeMaxGapBeforeThreadUserUs);
		const LONG64 maxGapAfterThreadKernelUs =
			TakeLong64(&ApiScopeMaxGapAfterThreadKernelUs);
		const LONG64 maxGapAfterThreadUserUs =
			TakeLong64(&ApiScopeMaxGapAfterThreadUserUs);
		const LONG firstBucket = InterlockedExchange(&ApiScopeFirstBucket, ApiOtherCommandList);
		const LONG lastBucket = InterlockedExchange(&ApiScopeLastBucket, ApiOtherCommandList);
		const LONG maxGapBeforeBucket = InterlockedExchange(
			&ApiScopeMaxGapBeforeBucket, ApiOtherCommandList);
		const LONG maxGapAfterBucket = InterlockedExchange(
			&ApiScopeMaxGapAfterBucket, ApiOtherCommandList);
		const LONG maxGapBeforeThreadId = TakeLong(&ApiScopeMaxGapBeforeThreadId);
		const LONG maxGapAfterThreadId = TakeLong(&ApiScopeMaxGapAfterThreadId);
		TakeLong(&ApiScopeLastThreadId);
		TakeLong64(&ApiScopeLastEndThreadKernelUs);
		TakeLong64(&ApiScopeLastEndThreadUserUs);
		const LONG64 maxGapThreadKernelDeltaUs =
			maxGapBeforeThreadId == maxGapAfterThreadId ?
				SafeSubtract(maxGapAfterThreadKernelUs, maxGapBeforeThreadKernelUs) : 0;
		const LONG64 maxGapThreadUserDeltaUs =
			maxGapBeforeThreadId == maxGapAfterThreadId ?
				SafeSubtract(maxGapAfterThreadUserUs, maxGapBeforeThreadUserUs) : 0;
		const LONG64 maxGapThreadCpuDeltaUs =
			maxGapThreadKernelDeltaUs + maxGapThreadUserDeltaUs;
		const LONG64 lastFrameLatencyHandle = TakeLong64(&LastFrameLatencyHandle);
		const LONG64 lastFrameLatencySwapChain = TakeLong64(&LastFrameLatencySwapChain);
		const LONG lastFrameLatencyThreadId = TakeLong(&LastFrameLatencyThreadId);
		const LONG lastFrameLatencyPresent = TakeLong(&LastFrameLatencyPresent);
		const LONG64 lastFrameLatencyQpc = TakeLong64(&LastFrameLatencyQpc);
		const LONG64 lastFenceEventHandle = TakeLong64(&LastFenceEventHandle);
		const LONG64 lastFenceEventFence = TakeLong64(&LastFenceEventFence);
		const LONG64 lastFenceEventValue = TakeLong64(&LastFenceEventValue);
		const LONG64 lastFenceEventCompletedBefore = TakeLong64(&LastFenceEventCompletedBefore);
		const LONG64 lastFenceEventCompletedAfter = TakeLong64(&LastFenceEventCompletedAfter);
		const LONG lastFenceEventThreadId = TakeLong(&LastFenceEventThreadId);
		const LONG lastFenceEventPresent = TakeLong(&LastFenceEventPresent);
		const LONG64 lastFenceEventQpc = TakeLong64(&LastFenceEventQpc);
		const LONG lastFenceEventHr = TakeLong(&LastFenceEventHr);
		const LONG64 lastSystemWaitHandle = TakeLong64(&LastSystemWaitHandle);
		const LONG64 lastSystemWaitSecondaryHandle = TakeLong64(&LastSystemWaitSecondaryHandle);
		const LONG64 lastSystemWaitQpc = TakeLong64(&LastSystemWaitQpc);
		const LONG64 lastSystemWaitUs = TakeLong64(&LastSystemWaitUs);
		const LONG lastSystemWaitThreadId = TakeLong(&LastSystemWaitThreadId);
		const LONG lastSystemWaitPresent = TakeLong(&LastSystemWaitPresent);
		const LONG lastSystemWaitApi = TakeLong(&LastSystemWaitApi);
		const LONG lastSystemWaitResult = TakeLong(&LastSystemWaitResult);
		const LONG lastSystemWaitError = TakeLong(&LastSystemWaitError);
		const LONG lastSystemWaitMatchesFenceEvent =
			TakeLong(&LastSystemWaitMatchesFenceEvent);
		const LONG lastSystemWaitMatchesFrameLatency =
			TakeLong(&LastSystemWaitMatchesFrameLatency);

		const LONG64 apiSpanUs =
			(firstBeginQpc > 0 && lastEndQpc > firstBeginQpc) ?
				DeltaUs(firstBeginQpc, lastEndQpc) : 0;
		const LONG64 beforeFirstApiUs =
			(frameStartQpc > 0 && firstBeginQpc > frameStartQpc) ?
				DeltaUs(frameStartQpc, firstBeginQpc) : 0;
		const LONG64 afterLastApiUs =
			(lastEndQpc > 0 && frameEndQpc > lastEndQpc) ?
				DeltaUs(lastEndQpc, frameEndQpc) : 0;
		const LONG64 firstBeginToPresentUs =
			(firstBeginQpc > 0 && frameEndQpc > firstBeginQpc) ?
				DeltaUs(firstBeginQpc, frameEndQpc) : 0;
		const LONG64 lastEndToPresentUs =
			(lastEndQpc > 0 && frameEndQpc > lastEndQpc) ?
				DeltaUs(lastEndQpc, frameEndQpc) : 0;

		LogTimingFields(batch, "DX12CpuTimelineCoverage",
			"\"present\":%ld,\"apiScopeCalls\":%lld,\"apiScopeNestedCalls\":%lld,"
			"\"apiScopeTotalUs\":%lld,\"apiScopeSpanUs\":%lld,\"apiScopeMaxGapUs\":%lld,"
			"\"apiScopeFirstBucket\":\"%s\",\"apiScopeLastBucket\":\"%s\","
			"\"apiScopeMaxGapBeforeBucket\":\"%s\",\"apiScopeMaxGapAfterBucket\":\"%s\","
			"\"apiScopeMaxGapBeforeThreadId\":%ld,\"apiScopeMaxGapAfterThreadId\":%ld,"
			"\"apiScopeMaxGapSameThread\":%ld,\"apiScopeMaxGapThreadKernelDeltaUs\":%lld,"
			"\"apiScopeMaxGapThreadUserDeltaUs\":%lld,\"apiScopeMaxGapThreadCpuDeltaUs\":%lld,"
			"\"apiScopeMaxGapBeginQpc\":%lld,\"apiScopeMaxGapEndQpc\":%lld,"
			"\"lastFrameLatencyHandle\":\"0x%llx\",\"lastFrameLatencySwapChain\":\"0x%llx\","
			"\"lastFrameLatencyThreadId\":%ld,\"lastFrameLatencyPresent\":%ld,"
			"\"lastFrameLatencyQpc\":%lld,"
			"\"lastFenceEventHandle\":\"0x%llx\",\"lastFenceEventFence\":\"0x%llx\","
			"\"lastFenceEventValue\":%lld,\"lastFenceEventCompletedBefore\":%lld,"
			"\"lastFenceEventCompletedAfter\":%lld,\"lastFenceEventThreadId\":%ld,"
			"\"lastFenceEventPresent\":%ld,\"lastFenceEventQpc\":%lld,\"lastFenceEventHr\":\"0x%lx\","
			"\"lastSystemWaitApi\":\"%s\",\"lastSystemWaitHandle\":\"0x%llx\","
			"\"lastSystemWaitSecondaryHandle\":\"0x%llx\",\"lastSystemWaitUs\":%lld,"
			"\"lastSystemWaitResult\":\"0x%lx\",\"lastSystemWaitError\":%ld,"
			"\"lastSystemWaitThreadId\":%ld,\"lastSystemWaitPresent\":%ld,"
			"\"lastSystemWaitQpc\":%lld,\"lastSystemWaitMatchesFenceEvent\":%ld,"
			"\"lastSystemWaitMatchesFrameLatency\":%ld,"
			"\"apiBeforeFirstScopeUs\":%lld,\"apiAfterLastScopeUs\":%lld,"
			"\"apiFirstBeginToPresentUs\":%lld,\"apiLastEndToPresentUs\":%lld,"
			"\"frameWallUs\":%lld,\"presentWallUs\":%lld,\"frameMinusApiTotalUs\":%lld,"
			"\"frameMinusApiSpanUs\":%lld,\"apiTotalPctX100\":%lld,\"apiSpanPctX100\":%lld",
			present,
			static_cast<long long>(calls),
			static_cast<long long>(nestedCalls),
			static_cast<long long>(totalUs),
			static_cast<long long>(apiSpanUs),
			static_cast<long long>(maxGapUs),
			ApiWallBucketName(firstBucket),
			ApiWallBucketName(lastBucket),
			ApiWallBucketName(maxGapBeforeBucket),
			ApiWallBucketName(maxGapAfterBucket),
			maxGapBeforeThreadId,
			maxGapAfterThreadId,
			static_cast<LONG>(maxGapBeforeThreadId == maxGapAfterThreadId),
			static_cast<long long>(maxGapThreadKernelDeltaUs),
			static_cast<long long>(maxGapThreadUserDeltaUs),
			static_cast<long long>(maxGapThreadCpuDeltaUs),
			static_cast<long long>(maxGapBeginQpc),
			static_cast<long long>(maxGapEndQpc),
			static_cast<unsigned long long>(lastFrameLatencyHandle),
			static_cast<unsigned long long>(lastFrameLatencySwapChain),
			lastFrameLatencyThreadId,
			lastFrameLatencyPresent,
			static_cast<long long>(lastFrameLatencyQpc),
			static_cast<unsigned long long>(lastFenceEventHandle),
			static_cast<unsigned long long>(lastFenceEventFence),
			static_cast<long long>(lastFenceEventValue),
			static_cast<long long>(lastFenceEventCompletedBefore),
			static_cast<long long>(lastFenceEventCompletedAfter),
			lastFenceEventThreadId,
			lastFenceEventPresent,
			static_cast<long long>(lastFenceEventQpc),
			static_cast<unsigned long>(lastFenceEventHr),
			WaitApiName(lastSystemWaitApi),
			static_cast<unsigned long long>(lastSystemWaitHandle),
			static_cast<unsigned long long>(lastSystemWaitSecondaryHandle),
			static_cast<long long>(lastSystemWaitUs),
			static_cast<unsigned long>(lastSystemWaitResult),
			lastSystemWaitError,
			lastSystemWaitThreadId,
			lastSystemWaitPresent,
			static_cast<long long>(lastSystemWaitQpc),
			lastSystemWaitMatchesFenceEvent,
			lastSystemWaitMatchesFrameLatency,
			static_cast<long long>(beforeFirstApiUs),
			static_cast<long long>(afterLastApiUs),
			static_cast<long long>(firstBeginToPresentUs),
			static_cast<long long>(lastEndToPresentUs),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(presentWallUs),
			static_cast<long long>(SafeSubtract(frameWallUs, totalUs)),
			static_cast<long long>(SafeSubtract(frameWallUs, apiSpanUs)),
			static_cast<long long>(frameWallUs > 0 ? (totalUs * 10000 / frameWallUs) : 0),
			static_cast<long long>(frameWallUs > 0 ? (apiSpanUs * 10000 / frameWallUs) : 0));
	}

	static CpuStageThreadTable *AcquireCpuStageThreadTable()
	{
		const LONG index = InterlockedIncrement(&CpuStageThreadTableCount) - 1;
		if (index < 0 || index >= static_cast<LONG>(kCpuStageThreadTables))
			return nullptr;
		CpuStageThreadTables[index].threadId = GetCurrentThreadId();
		return &CpuStageThreadTables[index];
	}

	static CpuStageThreadTable *GetCpuStageThreadTable()
	{
		static thread_local CpuStageThreadTable *table = AcquireCpuStageThreadTable();
		return table;
	}

	static CpuStageEntry *FindCpuStageEntryCached(
		const char *category, const char *stage, UINT kind)
	{
		// Thread-local open-addressed hash table keyed on label content.
		// The previous design paid a process-wide linear scan + SRW lock +
		// ~4 interlocked RMW per call; on the draw hot path LogCpuStage runs
		// 2-3 times per draw, which made the measurement itself dominate the
		// numbers it was reporting. Each thread owns its table (single
		// writer), so lookup and insert are lock-free plain ops. Pointer
		// equality short-circuits the common case (the same literal is passed
		// from the same call site every time); the content hash + strcmp
		// fallback only runs on collision or distinct-literal aliases.
		CpuStageThreadTable *table = GetCpuStageThreadTable();
		if (!table)
			return nullptr;
		const UINT64 hash = HashCpuStageLabel(category) ^
			(HashCpuStageLabel(stage) * 0x9e3779b97f4a7c15ull) ^
			(static_cast<UINT64>(kind) * 0xbf58476d1ce4e5b9ull);
		const UINT slotMask = static_cast<UINT>(kCpuStageEntriesPerTable) - 1;
		UINT slot = static_cast<UINT>(hash) & slotMask;
		for (UINT probe = 0; probe < kCpuStageEntriesPerTable; ++probe) {
			const CpuStageEntry &entry = table->entries[slot];
			if (entry.hash == 0)
				break; // empty slot: not cached
			if (entry.hash == hash && entry.kind == kind) {
				if (entry.category == category && entry.stage == stage)
					return &table->entries[slot];
				if (LabelEquals(entry.category, category) &&
						LabelEquals(entry.stage, stage))
					return &table->entries[slot];
			}
			slot = (slot + 1) & slotMask;
		}

		// Insert into the owning thread's table (single writer, no lock).
		UINT insertSlot = static_cast<UINT>(hash) & slotMask;
		for (UINT probe = 0; probe < kCpuStageEntriesPerTable; ++probe) {
			CpuStageEntry &entry = table->entries[insertSlot];
			if (entry.hash == 0) {
				entry.hash = hash;
				entry.category = category;
				entry.stage = stage;
				entry.kind = kind;
				return &entry;
			}
			insertSlot = (insertSlot + 1) & slotMask;
		}
		// A full table simply drops the sample (diagnostics only).
		return nullptr;
	}

	static void LogCpuStage(
		const char *category,
		const char *stage,
		UINT kind,
		size_t itemCount,
		LONG64 us)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		const LONG64 safeUs = ClampUs(us);
		CpuStageEntry *entry = FindCpuStageEntryCached(category, stage, kind);
		if (entry) {
			++entry->calls;
			entry->totalUs += safeUs;
			if (safeUs > entry->maxUs)
				entry->maxUs = safeUs;
			entry->items += static_cast<LONG64>(itemCount);
		}
#else
		(void)category;
		(void)stage;
		(void)kind;
		(void)itemCount;
		(void)us;
#endif
	}

	static void LogCpuStageTimingState(
		LONG present, LONG64 frameWallUs, DX12LogManager::LogBatch *batch)
	{
		// Merge every registered thread table into a label-keyed snapshot.
		// Each table is written by exactly one thread; the drain exchanges
		// the counters, so a racing increment lands one frame late.
		struct MergedCpuStage
		{
			const char *category = nullptr;
			const char *stage = nullptr;
			UINT kind = 0;
			UINT64 hash = 0;
			LONG64 calls = 0;
			LONG64 totalUs = 0;
			LONG64 maxUs = 0;
			LONG64 items = 0;
		};
		MergedCpuStage merged[256] = {};
		const UINT mergedMask = static_cast<UINT>(ARRAYSIZE(merged)) - 1;
		const LONG tableCount = InterlockedCompareExchange(&CpuStageThreadTableCount, 0, 0);
		const LONG tablesToScan = tableCount < static_cast<LONG>(kCpuStageThreadTables)
			? tableCount : static_cast<LONG>(kCpuStageThreadTables);
		for (LONG t = 0; t < tablesToScan; ++t) {
			CpuStageThreadTable &table = CpuStageThreadTables[t];
			for (UINT i = 0; i < kCpuStageEntriesPerTable; ++i) {
				CpuStageEntry &src = table.entries[i];
				if (!src.hash)
					continue;
				const LONG64 calls = InterlockedExchange64(
					reinterpret_cast<volatile LONG64*>(&src.calls), 0);
				const LONG64 totalUs = InterlockedExchange64(
					reinterpret_cast<volatile LONG64*>(&src.totalUs), 0);
				const LONG64 maxUs = InterlockedExchange64(
					reinterpret_cast<volatile LONG64*>(&src.maxUs), 0);
				const LONG64 items = InterlockedExchange64(
					reinterpret_cast<volatile LONG64*>(&src.items), 0);
				if (!calls && !totalUs)
					continue;
				UINT slot = static_cast<UINT>(src.hash) & mergedMask;
				for (UINT probe = 0; probe < ARRAYSIZE(merged); ++probe) {
					MergedCpuStage &dst = merged[slot];
					if (dst.hash == src.hash && dst.kind == src.kind &&
							LabelEquals(dst.category, src.category) &&
							LabelEquals(dst.stage, src.stage)) {
						dst.calls += calls;
						dst.totalUs += totalUs;
						if (maxUs > dst.maxUs)
							dst.maxUs = maxUs;
						dst.items += items;
						break;
					}
					if (!dst.hash) {
						dst.hash = src.hash;
						dst.category = src.category;
						dst.stage = src.stage;
						dst.kind = src.kind;
						dst.calls = calls;
						dst.totalUs = totalUs;
						dst.maxUs = maxUs;
						dst.items = items;
						break;
					}
					slot = (slot + 1) & mergedMask;
				}
			}
		}

		for (UINT i = 0; i < ARRAYSIZE(merged); ++i) {
			const MergedCpuStage &entry = merged[i];
			if (!entry.hash || (!entry.calls && !entry.totalUs))
				continue;
			if (entry.totalUs < 40 && entry.maxUs < 10)
				continue;
			LogTimingFields(batch, "DX12CpuStageTiming",
				"\"present\":%ld,\"category\":\"%s\",\"stage\":\"%s\",\"kind\":%u,"
				"\"calls\":%lld,\"items\":%lld,\"totalUs\":%lld,\"maxUs\":%lld,"
				"\"frameWallUs\":%lld,\"framePctX100\":%lld",
				present,
				entry.category,
				entry.stage,
				entry.kind,
				static_cast<long long>(entry.calls),
				static_cast<long long>(entry.items),
				static_cast<long long>(entry.totalUs),
				static_cast<long long>(entry.maxUs),
				static_cast<long long>(frameWallUs),
				static_cast<long long>(
					frameWallUs > 0 ? (entry.totalUs * 10000 / frameWallUs) : 0));
		}
	}

	static void LogLogCostState(
		LONG present, LONG64 frameWallUs, DX12LogManager::LogBatch *batch)
	{
		DX12LogManager::DX12LogCostSnapshot snapshot = {};
		DX12LogManager::TakeLogCostSnapshot(&snapshot);
		if (!snapshot.fieldCalls && !snapshot.textCalls &&
				!snapshot.enqueueCalls && !snapshot.formatUs && !snapshot.enqueueUs &&
				!snapshot.droppedLines) {
			return;
		}
		const LONG64 totalUs = snapshot.formatUs + snapshot.enqueueUs;
		if (totalUs < 40 && !snapshot.droppedLines)
			return;
		LogTimingFields(batch, "DX12LogCost",
			"\"present\":%ld,\"fieldCalls\":%lld,\"textCalls\":%lld,"
			"\"enqueueCalls\":%lld,\"formatUs\":%lld,"
			"\"enqueueUs\":%lld,\"totalUs\":%lld,\"droppedLines\":%lld,"
			"\"frameWallUs\":%lld,\"framePctX100\":%lld",
			present,
			static_cast<long long>(snapshot.fieldCalls),
			static_cast<long long>(snapshot.textCalls),
			static_cast<long long>(snapshot.enqueueCalls),
			static_cast<long long>(snapshot.formatUs),
			static_cast<long long>(snapshot.enqueueUs),
			static_cast<long long>(totalUs),
			static_cast<long long>(snapshot.droppedLines),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(frameWallUs > 0 ? (totalUs * 10000 / frameWallUs) : 0));
	}

	static void LogPresentEnter(const char *api, IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)api;
		(void)swapChain;
		(void)syncInterval;
		(void)flags;

		const LARGE_INTEGER now = Now();
		const LONGLONG previousEnter = InterlockedExchange64(&LastPresentEnterQpc, now.QuadPart);
		const LONGLONG previousLeave = InterlockedCompareExchange64(&LastPresentLeaveQpc, 0, 0);
		LONG64 frameIntervalUs = 0;
		LONG64 outsideUs = 0;
		if (previousEnter > 0)
			frameIntervalUs = DeltaUs(previousEnter, now.QuadPart);
		if (previousLeave > 0)
			outsideUs = DeltaUs(previousLeave, now.QuadPart);
		InterlockedExchange64(&CurrentFrameIntervalUs, frameIntervalUs);
		InterlockedExchange64(&CurrentOutsideBetweenPresentsUs, outsideUs);
		InterlockedExchange64(&CurrentPresentEnterQpc, now.QuadPart);
		InterlockedIncrement(&PresentEnterCalls);
	#else
		(void)api;
		(void)swapChain;
		(void)syncInterval;
		(void)flags;
	#endif
	}

	static void LogPresentLeave(const char *api, IDXGISwapChain *swapChain, HRESULT hr)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		const LARGE_INTEGER now = Now();
		const LONGLONG enter = InterlockedCompareExchange64(&CurrentPresentEnterQpc, 0, 0);
		const LONGLONG previousLeave = InterlockedCompareExchange64(&LastPresentLeaveQpc, 0, 0);
		const LONG64 presentWallUs = enter > 0 ? DeltaUs(enter, now.QuadPart) : 0;
		const LONG64 frameIntervalUs = InterlockedCompareExchange64(&CurrentFrameIntervalUs, 0, 0);
		const LONG64 outsideUs = InterlockedCompareExchange64(&CurrentOutsideBetweenPresentsUs, 0, 0);
		const LONG64 frameWallUs = outsideUs + presentWallUs;

		InterlockedIncrement(&PresentLeaveCalls);
		LogFrameTimeline(
			api,
			swapChain,
			hr,
			frameIntervalUs,
			outsideUs,
			presentWallUs,
			frameWallUs,
			previousLeave,
			now.QuadPart);
		InterlockedExchange64(&LastPresentLeaveQpc, now.QuadPart);
	#else
		(void)api;
		(void)swapChain;
		(void)hr;
	#endif
	}

	static void LogPresentSlice(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)api;
		(void)swapChain;
		(void)hr;

		const LONG64 safeUs = ClampUs(us);
		const bool isPostOriginalRuntimeDetail =
			StageEquals(stage, "present_maintenance") ||
			StageEquals(stage, "hunt_on_present") ||
			StageEquals(stage, "post_present_ui");
		if (!isPostOriginalRuntimeDetail)
			AddUs(&PresentSliceKnownUs, safeUs);
		if (StageEquals(stage, "original_present") || StageEquals(stage, "original_present_skip")) {
			AddUs(&PresentOriginalUs, safeUs);
		} else if (StageEquals(stage, "post_original_runtime") ||
			StageEquals(stage, "post_original_runtime_total") ||
			StageEquals(stage, "post_original_skip")) {
			AddUs(&PresentPostOriginalRuntimeUs, safeUs);
		} else if (StageEquals(stage, "mod_begin_frame")) {
			AddUs(&PresentModBeginFrameUs, safeUs);
		} else if (StageEquals(stage, "profiling_and_hotpath_flush")) {
			AddUs(&PresentProfilingFlushUs, safeUs);
		} else if (StageEquals(stage, "consume_ui_actions") ||
			StageEquals(stage, "consume_ui_actions_poll") ||
			StageEquals(stage, "consume_ui_actions_apply")) {
			AddUs(&PresentConsumeUiActionsUs, safeUs);
		} else if (StageEquals(stage, "per_frame_counters")) {
			AddUs(&PresentPerFrameCountersUs, safeUs);
		} else if (StageEquals(stage, "capture_bookkeeping")) {
			AddUs(&PresentCaptureBookkeepingUs, safeUs);
		} else if (StageEquals(stage, "capture_complete_or_begin")) {
			AddUs(&PresentCaptureCompleteUs, safeUs);
		} else if (StageEquals(stage, "hotpath_update")) {
			AddUs(&PresentHotPathUpdateUs, safeUs);
		} else {
			AddUs(&PresentOtherUs, safeUs);
		}
	#else
		(void)api;
		(void)stage;
		(void)swapChain;
		(void)hr;
		(void)us;
	#endif
	}

	static void LogQueueExecuteSlice(const char *stage, ID3D12CommandQueue *queue, UINT numCommandLists, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)queue;

		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&QueueExecuteStageSamples);
		if (IsQueueWallStage(stage)) {
			InterlockedIncrement64(&QueueExecuteWallCalls);
			InterlockedAdd64(&QueueExecuteWallCommandLists, static_cast<LONG64>(numCommandLists));
		}

		if (StageEquals(stage, "wrapper_total")) {
			AddUs(&QueueWrapperTotalUs, safeUs);
		} else if (StageEquals(stage, "total") ||
			StageEquals(stage, "total_skip") ||
			StageEquals(stage, "total_internal_replay") ||
			StageEquals(stage, "total_no_original")) {
			AddUs(&QueueFlowTotalUs, safeUs);
		} else if (StageEquals(stage, "original") ||
			StageEquals(stage, "original_skip") ||
			StageEquals(stage, "original_internal_replay")) {
			AddUs(&QueueOriginalUs, safeUs);
		} else if (StageEquals(stage, "mod_notify_submitted")) {
			AddUs(&QueueNotifyUs, safeUs);
		} else if (StageEquals(stage, "register_submitted") ||
			StageEquals(stage, "register_submitted_skip")) {
			AddUs(&QueueRegisterUs, safeUs);
		} else if (StageEquals(stage, "unwrap_command_lists")) {
			AddUs(&QueueUnwrapUs, safeUs);
		} else if (StageEquals(stage, "rebuild_work_flags")) {
			AddUs(&QueueRebuildWorkFlagsUs, safeUs);
		} else {
			AddUs(&QueueOtherUs, safeUs);
		}
#else
		(void)stage;
		(void)queue;
		(void)numCommandLists;
		(void)us;
#endif
	}

	static bool IsQueueWallStage(const char *stage)
	{
		return StageEquals(stage, "wrapper_total") ||
			StageEquals(stage, "total") ||
			StageEquals(stage, "total_skip") ||
			StageEquals(stage, "total_internal_replay") ||
			StageEquals(stage, "total_no_original");
	}

	static void LogQueueFenceSlice(
		const char *api,
		const char *stage,
		ID3D12CommandQueue *queue,
		ID3D12Fence *fence,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr,
		LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)stage;
		(void)queue;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;

		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&QueueFenceCalls);
		if (StageEquals(api, "Signal")) {
			AddUs(&QueueSignalUs, safeUs);
		} else if (StageEquals(api, "Wait")) {
			AddUs(&QueueWaitUs, safeUs);
		}
	#else
		(void)api;
		(void)stage;
		(void)queue;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;
		(void)us;
	#endif
	}

	static void LogFenceSlice(
		const char *api,
		const char *stage,
		ID3D12Fence *fence,
		UINT64 value,
		UINT64 completedBefore,
		UINT64 completedAfter,
		HRESULT hr,
		LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)api;
		(void)stage;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;

		InterlockedIncrement64(&FenceCalls);
		AddUs(&FenceTotalUs, us);
	#else
		(void)api;
		(void)stage;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;
		(void)us;
	#endif
	}

	static void LogEventWaitSlice(const char *api, const char *stage, HANDLE eventHandle, DWORD waitMs, DWORD waitResult, DWORD lastError, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)api;
		(void)stage;
		(void)eventHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;

		InterlockedIncrement64(&EventWaitCalls);
		AddUs(&EventWaitTotalUs, us);
	#else
		(void)api;
		(void)stage;
		(void)eventHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;
		(void)us;
	#endif
	}

	static void LogWorkerTask(const char *queueName, const char *stage, UINT taskKind, UINT64 sequence, size_t batchSize, size_t pending, DWORD enqueueThreadId, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)taskKind;
		(void)sequence;
		(void)batchSize;
		(void)pending;
		(void)enqueueThreadId;

		InterlockedIncrement64(&WorkerTaskCalls);
		AddUs(&WorkerTaskUs, us);
		LogCpuStage(queueName, stage, taskKind, batchSize, us);
	#else
		(void)queueName;
		(void)stage;
		(void)taskKind;
		(void)sequence;
		(void)batchSize;
		(void)pending;
		(void)enqueueThreadId;
		(void)us;
	#endif
	}

	static void LogWorkerWait(const char *queueName, const char *reason, const char *mode, UINT64 targetSequence, UINT64 completedSequence, size_t pending, LONG64 us)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && defined(DX12_RELEASE_TIMING)
		(void)queueName;
		(void)reason;
		(void)targetSequence;
		(void)completedSequence;
		(void)pending;

		const LONG64 safeUs = ClampUs(us);
		InterlockedIncrement64(&WorkerWaitCalls);
		AddUs(&WorkerWaitUs, safeUs);
		if (StageEquals(mode, "worker_idle")) {
			AddUs(&WorkerIdleWaitUs, safeUs);
		} else {
			AddUs(&WorkerDrainWaitUs, safeUs);
		}
	#else
		(void)queueName;
		(void)reason;
		(void)mode;
		(void)targetSequence;
		(void)completedSequence;
		(void)pending;
		(void)us;
	#endif
	}

	static LONG64 EstimatedQueueTotalUs(LONG64 queueWrapperTotalUs, LONG64 queueFlowTotalUs)
	{
		return queueWrapperTotalUs > 0 ? queueWrapperTotalUs : queueFlowTotalUs;
	}

	static LONG64 SafeSubtract(LONG64 left, LONG64 right)
	{
		if (left <= right)
			return 0;
		return left - right;
	}

	static void LogFrameTimeline(
		const char *api,
		IDXGISwapChain *swapChain,
		HRESULT hr,
		LONG64 frameIntervalUs,
		LONG64 outsideUs,
		LONG64 presentWallUs,
		LONG64 frameWallUs,
		LONGLONG frameStartQpc,
		LONGLONG frameEndQpc)
	{
		const LONG present = DX12RuntimeState::GetPresentCount();
		// One batch per present: all per-frame timing tables below enqueue
		// under a single queue lock instead of one lock + SetEvent per line.
		DX12LogManager::LogBatch batch;
		ApiWallDrainResult drain;
		DrainApiWallThreadTables(&drain);
		const LONG presentEnterCalls = TakeLong(&PresentEnterCalls);
		const LONG presentLeaveCalls = TakeLong(&PresentLeaveCalls);
		const LONG64 presentSliceKnownUs = TakeLong64(&PresentSliceKnownUs);
		const LONG64 presentOriginalUs = TakeLong64(&PresentOriginalUs);
		const LONG64 presentPostOriginalRuntimeUs = TakeLong64(&PresentPostOriginalRuntimeUs);
		const LONG64 presentModBeginFrameUs = TakeLong64(&PresentModBeginFrameUs);
		const LONG64 presentProfilingFlushUs = TakeLong64(&PresentProfilingFlushUs);
		const LONG64 presentConsumeUiActionsUs = TakeLong64(&PresentConsumeUiActionsUs);
		const LONG64 presentPerFrameCountersUs = TakeLong64(&PresentPerFrameCountersUs);
		const LONG64 presentCaptureBookkeepingUs = TakeLong64(&PresentCaptureBookkeepingUs);
		const LONG64 presentCaptureCompleteUs = TakeLong64(&PresentCaptureCompleteUs);
		const LONG64 presentHotPathUpdateUs = TakeLong64(&PresentHotPathUpdateUs);
		const LONG64 presentOtherUs = TakeLong64(&PresentOtherUs);

		const LONG64 queueExecuteStageSamples = TakeLong64(&QueueExecuteStageSamples);
		const LONG64 queueExecuteWallCalls = TakeLong64(&QueueExecuteWallCalls);
		const LONG64 queueExecuteWallCommandLists = TakeLong64(&QueueExecuteWallCommandLists);
		const LONG64 queueWrapperTotalUs = TakeLong64(&QueueWrapperTotalUs);
		const LONG64 queueFlowTotalUs = TakeLong64(&QueueFlowTotalUs);
		const LONG64 queueOriginalUs = TakeLong64(&QueueOriginalUs);
		const LONG64 queueNotifyUs = TakeLong64(&QueueNotifyUs);
		const LONG64 queueRegisterUs = TakeLong64(&QueueRegisterUs);
		const LONG64 queueUnwrapUs = TakeLong64(&QueueUnwrapUs);
		const LONG64 queueRebuildWorkFlagsUs = TakeLong64(&QueueRebuildWorkFlagsUs);
		const LONG64 queueOtherUs = TakeLong64(&QueueOtherUs);
		const LONG64 queueEstimatedTotalUs = EstimatedQueueTotalUs(queueWrapperTotalUs, queueFlowTotalUs);

		const LONG64 queueFenceCalls = TakeLong64(&QueueFenceCalls);
		const LONG64 queueSignalUs = TakeLong64(&QueueSignalUs);
		const LONG64 queueWaitUs = TakeLong64(&QueueWaitUs);
		const LONG64 fenceCalls = TakeLong64(&FenceCalls);
		const LONG64 fenceTotalUs = TakeLong64(&FenceTotalUs);
		const LONG64 eventWaitCalls = TakeLong64(&EventWaitCalls);
		const LONG64 eventWaitTotalUs = TakeLong64(&EventWaitTotalUs);
		const LONG64 systemWaitCalls = TakeLong64(&SystemWaitCalls);
		const LONG64 systemWaitTotalUs = TakeLong64(&SystemWaitTotalUs);

		const LONG64 workerTaskCalls = TakeLong64(&WorkerTaskCalls);
		const LONG64 workerTaskUs = TakeLong64(&WorkerTaskUs);
		const LONG64 workerWaitCalls = TakeLong64(&WorkerWaitCalls);
		const LONG64 workerWaitUs = TakeLong64(&WorkerWaitUs);
		const LONG64 workerIdleWaitUs = TakeLong64(&WorkerIdleWaitUs);
		const LONG64 workerDrainWaitUs = TakeLong64(&WorkerDrainWaitUs);

		LogCpuTimelineCoverageState(
			present,
			frameStartQpc,
			frameEndQpc,
			frameWallUs,
			presentWallUs,
			drain,
			&batch);
		LogApiThreadTimelineState(
			present,
			frameStartQpc,
			frameEndQpc,
			frameWallUs,
			&batch);
		LogApiWallTimingState(present, frameWallUs, drain, &batch);
		LogCpuStageTimingState(present, frameWallUs, &batch);

		LogTimingFields(&batch, "DX12FrameTimeline",
			"\"present\":%ld,\"api\":\"%s\",\"swapchain\":\"%p\",\"hr\":\"0x%lx\","
			"\"frameIntervalUs\":%lld,\"frameWallUs\":%lld,"
			"\"outsidePresentUs\":%lld,"
			"\"presentWallUs\":%lld,\"presentSliceKnownUs\":%lld,\"presentOriginalUs\":%lld,"
			"\"presentPostOriginalRuntimeUs\":%lld,\"presentModBeginFrameUs\":%lld,"
			"\"presentProfilingFlushUs\":%lld,\"presentConsumeUiActionsUs\":%lld,"
			"\"presentPerFrameCountersUs\":%lld,\"presentCaptureBookkeepingUs\":%lld,"
			"\"presentCaptureCompleteUs\":%lld,\"presentHotPathUpdateUs\":%lld,"
			"\"presentOtherUs\":%lld,\"presentEnterCalls\":%ld,\"presentLeaveCalls\":%ld,"
			"\"queueExecuteStageSamples\":%lld,\"queueExecuteWallCalls\":%lld,"
			"\"queueExecuteWallCommandLists\":%lld,\"queueEstimatedTotalUs\":%lld,"
			"\"queueWrapperTotalUs\":%lld,\"queueFlowTotalUs\":%lld,\"queueOriginalUs\":%lld,"
			"\"queueNotifyUs\":%lld,\"queueRegisterUs\":%lld,\"queueUnwrapUs\":%lld,"
			"\"queueRebuildWorkFlagsUs\":%lld,\"queueOtherUs\":%lld,"
			"\"queueFenceCalls\":%lld,\"queueSignalUs\":%lld,\"queueWaitUs\":%lld,"
			"\"fenceCalls\":%lld,\"fenceTotalUs\":%lld,\"eventWaitCalls\":%lld,"
			"\"eventWaitTotalUs\":%lld,\"systemWaitCalls\":%lld,\"systemWaitTotalUs\":%lld,"
			"\"workerTaskCalls\":%lld,\"workerTaskUs\":%lld,"
			"\"workerWaitCalls\":%lld,\"workerWaitUs\":%lld,\"workerIdleWaitUs\":%lld,"
			"\"workerDrainWaitUs\":%lld,"
			"\"presentWallMinusKnownSlicesUs\":%lld",
			present,
			api ? api : "",
			swapChain,
			static_cast<long>(hr),
			static_cast<long long>(frameIntervalUs),
			static_cast<long long>(frameWallUs),
			static_cast<long long>(outsideUs),
			static_cast<long long>(presentWallUs),
			static_cast<long long>(presentSliceKnownUs),
			static_cast<long long>(presentOriginalUs),
			static_cast<long long>(presentPostOriginalRuntimeUs),
			static_cast<long long>(presentModBeginFrameUs),
			static_cast<long long>(presentProfilingFlushUs),
			static_cast<long long>(presentConsumeUiActionsUs),
			static_cast<long long>(presentPerFrameCountersUs),
			static_cast<long long>(presentCaptureBookkeepingUs),
			static_cast<long long>(presentCaptureCompleteUs),
			static_cast<long long>(presentHotPathUpdateUs),
			static_cast<long long>(presentOtherUs),
			presentEnterCalls,
			presentLeaveCalls,
			static_cast<long long>(queueExecuteStageSamples),
			static_cast<long long>(queueExecuteWallCalls),
			static_cast<long long>(queueExecuteWallCommandLists),
			static_cast<long long>(queueEstimatedTotalUs),
			static_cast<long long>(queueWrapperTotalUs),
			static_cast<long long>(queueFlowTotalUs),
			static_cast<long long>(queueOriginalUs),
			static_cast<long long>(queueNotifyUs),
			static_cast<long long>(queueRegisterUs),
			static_cast<long long>(queueUnwrapUs),
			static_cast<long long>(queueRebuildWorkFlagsUs),
			static_cast<long long>(queueOtherUs),
			static_cast<long long>(queueFenceCalls),
			static_cast<long long>(queueSignalUs),
			static_cast<long long>(queueWaitUs),
			static_cast<long long>(fenceCalls),
			static_cast<long long>(fenceTotalUs),
			static_cast<long long>(eventWaitCalls),
			static_cast<long long>(eventWaitTotalUs),
			static_cast<long long>(systemWaitCalls),
			static_cast<long long>(systemWaitTotalUs),
			static_cast<long long>(workerTaskCalls),
			static_cast<long long>(workerTaskUs),
			static_cast<long long>(workerWaitCalls),
			static_cast<long long>(workerWaitUs),
			static_cast<long long>(workerIdleWaitUs),
			static_cast<long long>(workerDrainWaitUs),
			static_cast<long long>(SafeSubtract(presentWallUs, presentSliceKnownUs)));
		LogLogCostState(present, frameWallUs, &batch);
	}
};
