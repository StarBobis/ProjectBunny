#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"

static constexpr uint64_t kDescriptorInlineStatsInterval = 65536;

DX12DescriptorCollectPipeline &DX12DescriptorCollectPipeline::Get()
{
	static DX12DescriptorCollectPipeline pipeline;
	return pipeline;
}

void DX12DescriptorCollectPipeline::RecordDescriptor(DescriptorRecord &&record, const char *source)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	pipeline.FlushPendingCopyRange("RecordDescriptor");
	AcquireSRWLockExclusive(&pipeline.lock);
	pipeline.RecordDescriptorWriteStats(record);
	ReleaseSRWLockExclusive(&pipeline.lock);
	::RecordDescriptor(std::move(record));
	pipeline.MaybeLogInlineStats(source ? source : "RecordDescriptor");
}

void DX12DescriptorCollectPipeline::RecordDescriptorCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	if (!device || count == 0)
		return;

	DX12DescriptorCollectPipeline &pipeline = Get();
	AcquireSRWLockExclusive(&pipeline.lock);
	pipeline.copyRangeCalls++;
	pipeline.copiedDescriptors += count;
	if (!pipeline.TryMergePendingCopyRange(device, type, destStart, srcStart, count, source)) {
		DX12DescriptorCollectTask flushTask = pipeline.pendingCopy;
		pipeline.pendingCopy = DX12DescriptorCollectTask();
		pipeline.hasPendingCopy = false;
		pipeline.flushedCopyRanges++;
		pipeline.TryMergePendingCopyRange(device, type, destStart, srcStart, count, source);
		ReleaseSRWLockExclusive(&pipeline.lock);
		::RecordDescriptorCopyRange(
			flushTask.device, flushTask.type, flushTask.destStart, flushTask.srcStart, flushTask.count);
		pipeline.MaybeLogInlineStats(source ? source : "RecordCopyRange");
		return;
	}
	ReleaseSRWLockExclusive(&pipeline.lock);
	pipeline.MaybeLogInlineStats(source ? source : "RecordCopyRange");
}

void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *reason)
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	AcquireSRWLockExclusive(&pipeline.lock);
	pipeline.drainCalls++;
	ReleaseSRWLockExclusive(&pipeline.lock);
	pipeline.FlushPendingCopyRange(reason ? reason : "DrainForSnapshot");
	pipeline.MaybeLogInlineStats(reason ? reason : "DrainForSnapshot");
}

void DX12DescriptorCollectPipeline::Stop()
{
	DX12DescriptorCollectPipeline &pipeline = Get();
	pipeline.FlushPendingCopyRange("Stop");
	pipeline.LogStats("StopInline", 0, 0);
}

void DX12DescriptorCollectPipeline::StartWorker()
{
}

void DX12DescriptorCollectPipeline::StopWorker()
{
}

bool DX12DescriptorCollectPipeline::EnqueueDescriptorTask(DescriptorRecord &&record, const char *source)
{
	RecordDescriptor(std::move(record), source);
	return true;
}

bool DX12DescriptorCollectPipeline::EnqueueCopyRangeTask(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	RecordDescriptorCopyRange(device, type, destStart, srcStart, count, source);
	return true;
}

size_t DX12DescriptorCollectPipeline::PendingCount()
{
	return 0;
}

void DX12DescriptorCollectPipeline::WorkerLoop()
{
}

void DX12DescriptorCollectPipeline::DrainPendingOnCaller(const char *reason)
{
	DrainForSnapshot(reason);
}

void DX12DescriptorCollectPipeline::ProcessTask(DX12DescriptorCollectTask *task, const char *source)
{
	if (!task)
		return;
	if (task->kind == DX12DescriptorCollectTaskKind::Descriptor)
		RecordDescriptor(std::move(task->descriptor), source);
	else if (task->kind == DX12DescriptorCollectTaskKind::CopyRange)
		RecordDescriptorCopyRange(task->device, task->type, task->destStart, task->srcStart, task->count, source);
}

void DX12DescriptorCollectPipeline::ReleaseTask(DX12DescriptorCollectTask *task)
{
	if (!task)
		return;
	ReleaseDescriptorRecordResources(&task->descriptor);
}

void DX12DescriptorCollectPipeline::UpdateMaxDepth(size_t)
{
}

void DX12DescriptorCollectPipeline::LogStats(const char *, uint64_t, size_t)
{
	uint64_t descriptorWritesSnapshot = 0;
	uint64_t copyRangeCallsSnapshot = 0;
	uint64_t copyRangeMergedSnapshot = 0;
	uint64_t copiedDescriptorsSnapshot = 0;
	uint64_t flushedCopyRangesSnapshot = 0;
	uint64_t drainCallsSnapshot = 0;
	uint64_t maxMergedCountSnapshot = 0;
	bool hasPendingCopySnapshot = false;
	AcquireSRWLockShared(&lock);
	descriptorWritesSnapshot = descriptorWrites;
	copyRangeCallsSnapshot = copyRangeCalls;
	copyRangeMergedSnapshot = copyRangeMerged;
	copiedDescriptorsSnapshot = copiedDescriptors;
	flushedCopyRangesSnapshot = flushedCopyRanges;
	drainCallsSnapshot = drainCalls;
	maxMergedCountSnapshot = maxMergedCount;
	hasPendingCopySnapshot = hasPendingCopy;
	ReleaseSRWLockShared(&lock);
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"descriptorWrites\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"flushedCopyRanges\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingCopy\":%u,\"threadId\":%lu",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(flushedCopyRangesSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		hasPendingCopySnapshot ? 1 : 0,
		GetCurrentThreadId());
}

void DX12DescriptorCollectPipeline::FlushPendingCopyRange(const char *reason)
{
	DX12DescriptorCollectTask task;
	AcquireSRWLockExclusive(&lock);
	if (!hasPendingCopy) {
		ReleaseSRWLockExclusive(&lock);
		return;
	}
	task = pendingCopy;
	pendingCopy = DX12DescriptorCollectTask();
	hasPendingCopy = false;
	flushedCopyRanges++;
	ReleaseSRWLockExclusive(&lock);
	::RecordDescriptorCopyRange(task.device, task.type, task.destStart, task.srcStart, task.count);
	(void)reason;
}

bool DX12DescriptorCollectPipeline::TryMergePendingCopyRange(
	ID3D12Device *device,
	D3D12_DESCRIPTOR_HEAP_TYPE type,
	D3D12_CPU_DESCRIPTOR_HANDLE destStart,
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
	UINT count,
	const char *source)
{
	if (!device || count == 0)
		return true;
	const UINT increment = device->GetDescriptorHandleIncrementSize(type);
	if (increment == 0)
		return false;

	if (!hasPendingCopy) {
		pendingCopy = DX12DescriptorCollectTask();
		pendingCopy.kind = DX12DescriptorCollectTaskKind::CopyRange;
		pendingCopy.device = device;
		pendingCopy.type = type;
		pendingCopy.destStart = destStart;
		pendingCopy.srcStart = srcStart;
		pendingCopy.count = count;
		pendingCopy.source = source ? source : "";
		pendingCopy.enqueueThreadId = GetCurrentThreadId();
		hasPendingCopy = true;
		if (count > maxMergedCount)
			maxMergedCount = count;
		return true;
	}

	if (pendingCopy.device != device || pendingCopy.type != type ||
			pendingCopy.enqueueThreadId != GetCurrentThreadId())
		return false;
	const SIZE_T expectedDest = pendingCopy.destStart.ptr +
		static_cast<SIZE_T>(pendingCopy.count) * increment;
	const SIZE_T expectedSrc = pendingCopy.srcStart.ptr +
		static_cast<SIZE_T>(pendingCopy.count) * increment;
	if (destStart.ptr != expectedDest || srcStart.ptr != expectedSrc)
		return false;
	const UINT64 merged = static_cast<UINT64>(pendingCopy.count) + count;
	if (merged > UINT_MAX)
		return false;
	pendingCopy.count = static_cast<UINT>(merged);
	copyRangeMerged++;
	if (pendingCopy.count > maxMergedCount)
		maxMergedCount = pendingCopy.count;
	return true;
}

void DX12DescriptorCollectPipeline::RecordDescriptorWriteStats(const DescriptorRecord &)
{
	descriptorWrites++;
}

void DX12DescriptorCollectPipeline::MaybeLogInlineStats(const char *reason)
{
	uint64_t descriptorWritesSnapshot = 0;
	uint64_t copyRangeCallsSnapshot = 0;
	uint64_t copyRangeMergedSnapshot = 0;
	uint64_t copiedDescriptorsSnapshot = 0;
	uint64_t flushedCopyRangesSnapshot = 0;
	uint64_t drainCallsSnapshot = 0;
	uint64_t maxMergedCountSnapshot = 0;
	bool hasPendingCopySnapshot = false;
	AcquireSRWLockShared(&lock);
	const uint64_t total = descriptorWrites + copyRangeCalls + drainCalls;
	if (total == 0 || total % kDescriptorInlineStatsInterval != 0) {
		ReleaseSRWLockShared(&lock);
		return;
	}
	descriptorWritesSnapshot = descriptorWrites;
	copyRangeCallsSnapshot = copyRangeCalls;
	copyRangeMergedSnapshot = copyRangeMerged;
	copiedDescriptorsSnapshot = copiedDescriptors;
	flushedCopyRangesSnapshot = flushedCopyRanges;
	drainCallsSnapshot = drainCalls;
	maxMergedCountSnapshot = maxMergedCount;
	hasPendingCopySnapshot = hasPendingCopy;
	ReleaseSRWLockShared(&lock);
	DX12LogManager::JsonFunc("DX12DescriptorCollectPipelineStats",
		"\"reason\":\"%s\",\"descriptorWrites\":%llu,\"copyRangeCalls\":%llu,\"copyRangeMerged\":%llu,\"copiedDescriptors\":%llu,\"flushedCopyRanges\":%llu,\"drainCalls\":%llu,\"maxMergedCount\":%llu,\"pendingCopy\":%u,\"threadId\":%lu",
		reason ? reason : "",
		static_cast<unsigned long long>(descriptorWritesSnapshot),
		static_cast<unsigned long long>(copyRangeCallsSnapshot),
		static_cast<unsigned long long>(copyRangeMergedSnapshot),
		static_cast<unsigned long long>(copiedDescriptorsSnapshot),
		static_cast<unsigned long long>(flushedCopyRangesSnapshot),
		static_cast<unsigned long long>(drainCallsSnapshot),
		static_cast<unsigned long long>(maxMergedCountSnapshot),
		hasPendingCopySnapshot ? 1 : 0,
		GetCurrentThreadId());
}
