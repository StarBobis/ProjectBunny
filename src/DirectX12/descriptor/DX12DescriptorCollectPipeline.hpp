#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <cstdint>
#include <string>

#include "DX12ResourceTrackerPrivate.hpp"

enum class DX12DescriptorCollectTaskKind
{
	Descriptor,
	CopyRange
};

class DX12DescriptorCollectTask
{
public:
	DX12DescriptorCollectTaskKind kind = DX12DescriptorCollectTaskKind::Descriptor;
	DescriptorRecord descriptor = {};
	ID3D12Device *device = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_CPU_DESCRIPTOR_HANDLE destStart = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srcStart = {};
	UINT count = 0;
	std::string source;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12DescriptorCollectPipeline
{
public:
	static DX12DescriptorCollectPipeline &Get();
	static void RecordDescriptor(DescriptorRecord &&record, const char *source);
	static void RecordDescriptorCopyRange(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	static void DrainForSnapshot(const char *reason);
	static void Stop();

	void StartWorker();
	void StopWorker();
	bool EnqueueDescriptorTask(DescriptorRecord &&record, const char *source);
	bool EnqueueCopyRangeTask(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	size_t PendingCount();
	void WorkerLoop();
	void DrainPendingOnCaller(const char *reason);
	void ProcessTask(DX12DescriptorCollectTask *task, const char *source);
	void ReleaseTask(DX12DescriptorCollectTask *task);
	void UpdateMaxDepth(size_t depth);
	void LogStats(const char *event, uint64_t sequence, size_t depth);

	DX12DescriptorCollectTask pendingCopy = {};
	bool hasPendingCopy = false;
	uint64_t descriptorWrites = 0;
	uint64_t copyRangeCalls = 0;
	uint64_t copyRangeMerged = 0;
	uint64_t copiedDescriptors = 0;
	uint64_t flushedCopyRanges = 0;
	uint64_t drainCalls = 0;
	uint64_t maxMergedCount = 0;
	SRWLOCK lock = SRWLOCK_INIT;

	void FlushPendingCopyRange(const char *reason);
	bool TryMergePendingCopyRange(
		ID3D12Device *device,
		D3D12_DESCRIPTOR_HEAP_TYPE type,
		D3D12_CPU_DESCRIPTOR_HANDLE destStart,
		D3D12_CPU_DESCRIPTOR_HANDLE srcStart,
		UINT count,
		const char *source);
	void RecordDescriptorWriteStats(const DescriptorRecord &record);
	void MaybeLogInlineStats(const char *reason);
};
