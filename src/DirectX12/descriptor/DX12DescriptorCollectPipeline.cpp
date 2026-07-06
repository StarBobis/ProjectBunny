#include "DX12DescriptorCollectPipeline.hpp"
#include "DX12LogManager.hpp"
#include "utils/DX12OuterTimingProbeUtils.hpp"

void DX12DescriptorCollectPipeline::RecordDescriptor(DescriptorRecord &&record, const char *)
{
	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	::RecordDescriptorMerged(std::move(record));
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"record_descriptor_merged",
		0, 1,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
}

void DX12DescriptorCollectPipeline::RecordDescriptorCopyRange(
	ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart, D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count, const char *)
{
	if (!device || count == 0) return;
	LARGE_INTEGER begin = DX12OuterTimingProbeUtils::Now();
	::RecordRuntimeDescriptorCopyRange(device, type, destStart, srcStart, count);
	DX12OuterTimingProbeUtils::LogCpuStage(
		"DescriptorCollectPipeline",
		"record_runtime_copy_range",
		1, count,
		DX12OuterTimingProbeUtils::ElapsedUs(begin));
	::RecordDescriptorCopyRange(device, type, destStart, srcStart, count);
}

void DX12DescriptorCollectPipeline::EnsureDescriptorHandlesVisible(const SIZE_T *, size_t, const char *) {}
void DX12DescriptorCollectPipeline::FindPendingDescriptorHandles(const SIZE_T *, size_t, uint8_t *p) { if (p) memset(p, 0, 1); }
void DX12DescriptorCollectPipeline::CopyPendingDescriptorWrites(const SIZE_T *, size_t, DescriptorRecord *, uint8_t *f) { if (f) memset(f, 0, 1); }
void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *) {}
void DX12DescriptorCollectPipeline::Stop() { started.store(false, std::memory_order_release); }
void DX12DescriptorCollectPipeline::Start() {}
void DX12DescriptorCollectPipeline::WorkerLoop() {}
size_t DX12DescriptorCollectPipeline::PendingCount() { return 0; }
bool DX12DescriptorCollectPipeline::EnqueueDescriptorTask(DescriptorRecord *, const char *) { return false; }
void DX12DescriptorCollectPipeline::ProcessTask(DX12DescriptorCollectTask *, const char *) {}
void DX12DescriptorCollectPipeline::ReleaseTask(DX12DescriptorCollectTask *) {}
void DX12DescriptorCollectPipeline::LogStats(const char *, uint64_t, size_t) {}
void DX12DescriptorCollectPipeline::EnsureDescriptorWritesVisible(const SIZE_T *, size_t, const char *) {}
uint64_t DX12DescriptorCollectPipeline::RequiredSequenceForHandlesLocked(const SIZE_T *, size_t) { return 0; }
uint64_t DX12DescriptorCollectPipeline::RequiredSequenceForTaskLocked(const DX12DescriptorCollectTask &) { return 0; }
void DX12DescriptorCollectPipeline::WaitForSequence(uint64_t, const char *) {}
bool DX12DescriptorCollectPipeline::TryMergeQueuedCopyTaskLocked(ID3D12Device *, D3D12_DESCRIPTOR_HEAP_TYPE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, UINT, DWORD) { return false; }
bool DX12DescriptorCollectPipeline::QueueDescriptorBatchLocked(DX12DescriptorCollectBatch *) { return false; }
bool DX12DescriptorCollectPipeline::QueueTaskLocked(DX12DescriptorCollectTask *) { return false; }
bool DX12DescriptorCollectPipeline::QueueTaskToRingBuffer(DX12DescriptorCollectTask *) { return false; }
void DX12DescriptorCollectPipeline::FlushPendingDescriptorWrites(const char *) {}
bool DX12DescriptorCollectPipeline::RetainDescriptorResourcesForAsync(DescriptorRecord *) { return false; }
bool DX12DescriptorCollectPipeline::TryStealDescriptorBatchLocked(DX12DescriptorCollectBatch *) { return false; }
void DX12DescriptorCollectPipeline::RecordDescriptorWriteStats(const DescriptorRecord &) {}
void DX12DescriptorCollectPipeline::MaybeLogInlineStats(const char *) {}
bool DX12DescriptorCollectPipeline::CpuHandleInTrackedRange(SIZE_T, SIZE_T, SIZE_T) { return false; }
bool DX12DescriptorCollectPipeline::TaskTouchesCpuHandleLocked(const DX12DescriptorCollectTask &, SIZE_T) { return false; }
bool DX12DescriptorCollectPipeline::ActiveTaskTouchesCpuHandleLocked(SIZE_T) { return false; }
bool DX12DescriptorCollectPipeline::QueuedTasksTouchAnyCpuHandleLocked(const std::vector<SIZE_T> &) { return false; }
bool DX12DescriptorCollectPipeline::TaskTouchesAnyCpuHandleLocked(const DX12DescriptorCollectTask &, const std::vector<SIZE_T> &) { return false; }
bool DX12DescriptorCollectPipeline::ActiveTaskTouchesAnyCpuHandleLocked(const std::vector<SIZE_T> &) { return false; }
bool DX12DescriptorCollectPipeline::PriorQueuedTasksTouchAnyCpuHandleLocked(size_t, const std::vector<SIZE_T> &) { return false; }
bool DX12DescriptorCollectPipeline::CpuHandleInList(SIZE_T, const std::vector<SIZE_T> &) { return false; }
bool DX12DescriptorCollectPipeline::BuildCopyTaskDependencyHandles(const DX12DescriptorCollectTask &, std::vector<SIZE_T> *) { return false; }
void DX12DescriptorCollectPipeline::RegisterTaskIndexLocked(DX12DescriptorCollectTask *) {}
void DX12DescriptorCollectPipeline::RegisterIndexHandleLocked(DX12DescriptorCollectTask *, SIZE_T, size_t, bool) {}
void DX12DescriptorCollectPipeline::RemoveTaskIndexHandleRefLocked(SIZE_T, uint64_t, size_t, bool) {}
void DX12DescriptorCollectPipeline::UnregisterTaskIndexLocked(DX12DescriptorCollectTask *) {}
void DX12DescriptorCollectPipeline::CleanPendingRefsForCpuHandleLocked(SIZE_T) {}
bool DX12DescriptorCollectPipeline::IsTaskRecordConsumed(const DX12DescriptorCollectTask &, size_t) { return false; }
bool DX12DescriptorCollectPipeline::IsTaskFullyConsumed(const DX12DescriptorCollectTask &) { return false; }
void DX12DescriptorCollectPipeline::MarkTaskRecordConsumedAfterMove(DX12DescriptorCollectTask *, size_t) {}
void DX12DescriptorCollectPipeline::MarkDescriptorHandleVisibleAfterInlineLocked(SIZE_T, uint64_t) {}
void DX12DescriptorCollectPipeline::MarkCopyRangeVisibleAfterInlineLocked(const DX12DescriptorCollectTask &) {}
bool DX12DescriptorCollectPipeline::TryPromoteQueuedTasksForHandlesLocked(const std::vector<SIZE_T> &, const char *, size_t *, size_t *, size_t *, bool *, bool *, bool *) { return false; }
bool DX12DescriptorCollectPipeline::TryPromoteHandleRefsBeforeSequenceLocked(SIZE_T, uint64_t, const char *, size_t *, size_t *, size_t *, bool *, bool *, bool *) { return false; }
bool DX12DescriptorCollectPipeline::TryPromoteTaskRefLocked(const DX12DescriptorPendingTaskRef &, const char *, size_t *, size_t *, size_t *, bool *, bool *, bool *) { return false; }
bool DX12DescriptorCollectPipeline::TryPromoteTaskDependenciesLocked(const DX12DescriptorCollectTask &, const char *, size_t *, size_t *, size_t *, bool *, bool *, bool *) { return false; }
void DX12DescriptorCollectPipeline::SetActiveTaskLocked(const DX12DescriptorCollectTask &) {}
void DX12DescriptorCollectPipeline::ClearActiveTaskLocked() {}
bool DX12DescriptorCollectPipeline::BuildUniqueCpuHandleList(const SIZE_T *, size_t, std::vector<SIZE_T> *) { return false; }
bool DX12DescriptorCollectPipeline::TryResolveRequestedPendingWritesInlineLocked(const std::vector<SIZE_T> &, const char *, size_t *, size_t *, bool *, bool *) { return false; }