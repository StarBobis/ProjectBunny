#pragma once

#include <vector>
#include <cstddef>

#include "DX12ResourceTrackerPrivate.hpp"

// Thread-local staging buffer for descriptor writes.
// Game threads accumulate descriptors here and flush them in batches
// to pendingDescriptorWrites, reducing mapsMutex acquisitions by ~64x.
class DX12DescriptorTlsBatchUtils
{
public:
	static constexpr size_t kTlsBatchThreshold = 64;

	struct TlsBatch
	{
		std::vector<DescriptorRecord> records;
	};

	static thread_local TlsBatch tlsBatch;

	// Append a descriptor (resources already retained).  Flushes
	// automatically when the batch reaches kTlsBatchThreshold.
	static void AddToBatch(DescriptorRecord &&record)
	{
		tlsBatch.records.push_back(std::move(record));
		if (tlsBatch.records.size() >= kTlsBatchThreshold) {
			FlushBatch();
		}
	}

	// Flush all buffered descriptors into pendingDescriptorWrites
	// under mapsMutex.  Must be called before any operation that
	// reads pendingDescriptorWrites (CopyDescriptors, draw queries,
	// DrainForSnapshot).
	static void FlushBatch();

	// Number of buffered entries.
	static size_t BufferedCount()
	{
		return tlsBatch.records.size();
	}
};
