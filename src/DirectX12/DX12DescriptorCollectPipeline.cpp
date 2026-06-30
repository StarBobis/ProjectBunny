#include "DX12DescriptorCollectPipeline.hpp"

#include "DX12LogManager.hpp"

void DX12DescriptorCollectPipeline::RecordDescriptor(DescriptorRecord &&record, const char *source)
{
	DX12LogManager::DebugJsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"RecordDescriptorInline\",\"source\":\"%s\",\"kindId\":%u,\"cpuHandle\":\"0x%zx\",\"resource\":\"%p\",\"threadId\":%lu",
		source ? source : "",
		record.kindId,
		record.cpuHandle,
		record.resource,
		GetCurrentThreadId());
	::RecordDescriptor(std::move(record));
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

	DX12LogManager::DebugJsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"RecordCopyRangeInline\",\"source\":\"%s\",\"device\":\"%p\",\"type\":%u,\"dest\":\"0x%zx\",\"src\":\"0x%zx\",\"count\":%u,\"threadId\":%lu",
		source ? source : "",
		device,
		static_cast<UINT>(type),
		destStart.ptr,
		srcStart.ptr,
		count,
		GetCurrentThreadId());
	::RecordDescriptorCopyRange(device, type, destStart, srcStart, count);
}

void DX12DescriptorCollectPipeline::DrainForSnapshot(const char *reason)
{
	DX12LogManager::DebugJsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"DrainForSnapshotInline\",\"reason\":\"%s\",\"threadId\":%lu",
		reason ? reason : "",
		GetCurrentThreadId());
}

void DX12DescriptorCollectPipeline::Stop()
{
	DX12LogManager::DebugJsonFunc("DX12DescriptorCollectPipeline",
		"\"event\":\"StopInline\",\"threadId\":%lu",
		GetCurrentThreadId());
}

