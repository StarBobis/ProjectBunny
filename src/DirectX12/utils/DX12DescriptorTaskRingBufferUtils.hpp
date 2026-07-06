#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>

// Lock-free MPSC (Multiple Producer, Single Consumer) ring buffer for
// DX12DescriptorCollectTask shared pointers.
//
// Based on Dmitry Vyukov's MPSC queue algorithm:
//   http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
//
// Adapted for array-based bounded ring buffer with sequence-based slot
// ownership.  Every slot has a monotonically increasing sequence number.
//
// Producer:
//   1. Claim next tail slot (atomic fetch_add)
//   2. Spin until slot sequence == tail (slot is free)
//   3. Write data
//   4. Store slot sequence = tail + 1 (publish, release)
//
// Consumer (single thread only):
//   1. Read head slot sequence (acquire)
//   2. If sequence != head + 1, queue is empty
//   3. Move data out
//   4. Store slot sequence = head + Capacity (free slot, release)
//   5. Advance head
//
// The consumer is STRICTLY single-threaded.  Multiple producers are
// supported through atomic tail increment.

template <size_t Capacity>
class DX12DescriptorTaskRingBufferUtils
{
	static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
	static constexpr size_t kMask = Capacity - 1;
	static constexpr size_t kCacheLine = 64;

	struct alignas(kCacheLine) Slot
	{
		std::atomic<uint64_t> sequence;
		std::shared_ptr<class DX12DescriptorCollectTask> task;
	};

	alignas(kCacheLine) std::atomic<uint64_t> mHead;
	alignas(kCacheLine) std::atomic<uint64_t> mTail;
	Slot mSlots[Capacity];

public:
	DX12DescriptorTaskRingBufferUtils()
	{
		mHead.store(0, std::memory_order_relaxed);
		mTail.store(0, std::memory_order_relaxed);
		for (size_t i = 0; i < Capacity; ++i) {
			mSlots[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

	// Push a task into the ring buffer.  Returns true on success,
	// false if the buffer is full (consumer is too far behind).
	bool TryPush(std::shared_ptr<class DX12DescriptorCollectTask> task)
	{
		if (!task)
			return false;

		uint64_t tail = mTail.load(std::memory_order_relaxed);
		for (;;) {
			uint64_t head = mHead.load(std::memory_order_acquire);
			if (tail >= head + Capacity) {
				// Buffer is full -- consumer is blocked or too slow.
				return false;
			}
			if (mTail.compare_exchange_weak(tail, tail + 1,
					std::memory_order_relaxed, std::memory_order_relaxed)) {
				break;
			}
		}

		Slot &slot = mSlots[tail & kMask];

		// Spin until the slot is free (sequence == tail).
		while (slot.sequence.load(std::memory_order_acquire) != tail) {
			YieldProcessor();
		}

		slot.task = std::move(task);
		slot.sequence.store(tail + 1, std::memory_order_release);
		return true;
	}

	// Try to pop one task.  Returns the task or nullptr if empty.
	// MUST be called from a single consumer thread.
	std::shared_ptr<class DX12DescriptorCollectTask> TryPop()
	{
		uint64_t head = mHead.load(std::memory_order_relaxed);
		Slot &slot = mSlots[head & kMask];

		uint64_t seq = slot.sequence.load(std::memory_order_acquire);
		if (seq != head + 1) {
			return nullptr;  // Empty
		}

		std::shared_ptr<class DX12DescriptorCollectTask> task = std::move(slot.task);
		slot.sequence.store(head + Capacity, std::memory_order_release);
		mHead.store(head + 1, std::memory_order_release);
		return task;
	}

	// Spin-wait up to spinCount iterations, yielding between spins.
	// Returns the task or nullptr if still empty after timeout.
	// MUST be called from a single consumer thread.
	std::shared_ptr<class DX12DescriptorCollectTask> TryPopWithSpin(uint32_t spinCount)
	{
		for (uint32_t i = 0; i < spinCount; ++i) {
			std::shared_ptr<class DX12DescriptorCollectTask> task = TryPop();
			if (task)
				return task;
			if (i % 64 == 63) {
				YieldProcessor();
			}
		}
		return nullptr;
	}

	// Approximate number of items currently in the buffer.
	size_t ApproximateSize() const
	{
		uint64_t head = mHead.load(std::memory_order_acquire);
		uint64_t tail = mTail.load(std::memory_order_acquire);
		if (tail >= head)
			return static_cast<size_t>(tail - head);
		return 0;
	}

	// Check if the buffer is completely empty (consumer perspective).
	bool IsEmpty() const
	{
		uint64_t head = mHead.load(std::memory_order_relaxed);
		uint64_t seq = mSlots[head & kMask].sequence.load(std::memory_order_acquire);
		return seq != head + 1;
	}
};
