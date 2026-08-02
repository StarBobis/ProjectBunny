#pragma once

#include "Tracking/DX12DescriptorInfrastructure.hpp"

class DX12FrameAnalysis
{
public:
	static bool Begin()
	{
		return DX12FrameAnalysisState::Begin();
	}

	static void End()
	{
		DX12FrameAnalysisState::End();
	}

	static void RequestCapture()
	{
		DX12FrameAnalysisState::RequestCapture();
		DX12HotPathState::Update();
	}

	static void BeginCapture()
	{
		DX12FrameAnalysisState::BeginCapture();
		DX12HotPathState::Update();
	}

	static bool EndCapture()
	{
		return DX12FrameAnalysisState::EndCapture();
	}

	static bool IsActive()
	{
		return DX12FrameAnalysisState::IsActive();
	}

	static bool IsCaptureRequested()
	{
		return DX12FrameAnalysisState::IsCaptureRequested();
	}

	static bool IsCapturing()
	{
		return DX12FrameAnalysisState::IsCapturing();
	}

	static bool GetPath(wchar_t *path, size_t pathCount)
	{
		return DX12FrameAnalysisState::GetPath(path, pathCount);
	}

	static void LogEvent(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"Event\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogInfo(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"LogInfo\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!IsActive())
			return;

		char funcJson[512];
		char extra[4096];
		char fields[4608];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
		extra[0] = '\0';

		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(extra, sizeof(extra), fmt, args);
			va_end(args);
		}

		if (extra[0])
			sprintf_s(fields, sizeof(fields), "\"func\":%s,%s", funcJson, extra);
		else
			sprintf_s(fields, sizeof(fields), "\"func\":%s", funcJson);
		LogJsonFields(fields);
	}

	static void LogJsonFields(const char *fields)
	{
		DX12FrameAnalysisState::LogJsonFields(fields);
	}
};



class DX12ResourceStateTracker
{
public:
	// Microsoft documents ResourceBarrier as the explicit D3D12 transition
	// mechanism. These inference helpers are only used when no observed state
	// is available from creation or barrier tracking.
	static D3D12_RESOURCE_STATES FromBarrierLayout(D3D12_BARRIER_LAYOUT layout)
	{
		switch (layout) {
		case D3D12_BARRIER_LAYOUT_COMMON:
			return D3D12_RESOURCE_STATE_COMMON;
		case D3D12_BARRIER_LAYOUT_GENERIC_READ:
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
			return D3D12_RESOURCE_STATE_DEPTH_READ;
		case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_DEST:
			return D3D12_RESOURCE_STATE_COPY_DEST;
		case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
			return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
			return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		default:
			return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	static D3D12_RESOURCE_STATES InferDescriptorSourceState(const DX12FrameResourceBinding &binding)
	{
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		if (binding.descriptor.kind == DescriptorKind::UAV)
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (binding.descriptor.kind == DescriptorKind::RTV)
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (binding.descriptor.kind == DescriptorKind::DSV)
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	static D3D12_RESOURCE_STATES InferIaBufferSourceState(const DX12FrameIaBufferBinding &buffer)
	{
		if (buffer.role == IaBufferRole::IB)
			return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	}

	static bool HeapTypeAllowsDirectCopy(UINT heapType, bool hasHeapType)
	{
		return hasHeapType &&
			(heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK);
	}
};


struct PlacedResourceInfo
{
	bool hasPlacedHeap = false;
	UINT64 heapOffset = 0;
	UINT64 heapSize = 0;
};

bool DX12ModHasActiveShaderOverrides();
bool DX12ModHasActiveTextureOverrides();
bool DX12ModHasConfiguredTextureOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsShaderDescriptorTracking();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsBusy();
bool DX12HuntNeedsHotPath();

class DX12ResourceTrackingPolicyUtils
{
public:
	static bool NeedsFrameDumpTracking()
	{
		return DX12FrameAnalysisState::IsActive();
	}

	static bool NeedsShaderDumpTracking()
	{
		return DX12ShaderDumpIsBusy();
	}

	static bool NeedsDumpOrHuntingTracking()
	{
		return DX12HuntNeedsHotPath() ||
			NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking();
	}

	static bool NeedsIaContentTracking()
	{
		return DX12ModHasConfiguredTextureOverrides();
	}

	static bool NeedsDescriptorTracking()
	{
		return NeedsDumpOrHuntingTracking() ||
			DX12ModNeedsPreSkinningUavProbe() ||
			DX12ModNeedsShaderDescriptorTracking();
	}

	static bool NeedsFullDescriptorHistory()
	{
		return NeedsFrameDumpTracking();
	}

	static bool NeedsFullResourceMetadata()
	{
		return NeedsDumpOrHuntingTracking();
	}

	static bool NeedsTextureResourceTracking()
	{
		return NeedsFullResourceMetadata() ||
			NeedsIaContentTracking() ||
			NeedsDescriptorTracking();
	}

	// Runtime recording (gRuntimeDescriptorByCpuHandle) is a fast cache for
	// draw-time descriptor queries.  It is only required when capture/dump/hunt
	// is actively inspecting every descriptor.  During normal Mod operation the
	// Worker pipeline + EnsureDescriptorHandlesVisible provides equivalent data
	// via gDescriptorByCpuHandle / gDescriptorHeaps.
	static bool NeedsRuntimeDescriptorRecording()
	{
		return NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking() ||
			DX12HuntNeedsHotPath();
	}
};



// Lock and version storage for one descriptor heap. Each shard occupies its
// own cache line: packing SRWLOCKs or counters together would make unrelated
// descriptor slots contend through false sharing even though their logical
// locks are independent.
template <size_t ShardCountValue>
class DX12DescriptorHeapShardState
{
public:
	static constexpr size_t ShardCount = ShardCountValue;
	static constexpr size_t CacheLineBytes = 64;

	static_assert(ShardCount != 0 && (ShardCount & (ShardCount - 1)) == 0,
		"descriptor shard count must be a power of two");

	struct alignas(CacheLineBytes) LockCell
	{
		SRWLOCK value = SRWLOCK_INIT;
	};

	static_assert(sizeof(LockCell) % CacheLineBytes == 0,
		"descriptor locks must not share cache lines");

	explicit DX12DescriptorHeapShardState(uint32_t maxSlots = 0)
		: maxSlots_(maxSlots),
		  versionPageCount_(maxSlots == 0 ? 0 : static_cast<uint32_t>(
			(static_cast<uint64_t>(maxSlots) + VersionPageSize - 1) /
			VersionPageSize))
	{
		for (LockCell &cell : locks_)
			InitializeSRWLock(&cell.value);
		if (versionPageCount_ == 0)
			return;
		versionPages_ =
			std::make_unique<std::atomic<VersionPage*>[]>(versionPageCount_);
		for (uint32_t index = 0; index < versionPageCount_; ++index)
			versionPages_[index].store(nullptr, std::memory_order_relaxed);
	}

	~DX12DescriptorHeapShardState()
	{
		if (!versionPages_)
			return;
		for (uint32_t index = 0; index < versionPageCount_; ++index)
			delete versionPages_[index].load(std::memory_order_relaxed);
	}

	DX12DescriptorHeapShardState(const DX12DescriptorHeapShardState&) = delete;
	DX12DescriptorHeapShardState &operator=(
		const DX12DescriptorHeapShardState&) = delete;

	static constexpr size_t ShardForSlot(uint32_t slot)
	{
		return static_cast<size_t>(slot) & (ShardCount - 1);
	}

	SRWLOCK *LockForSlot(uint32_t slot)
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void LockAllExclusive()
	{
		for (LockCell &cell : locks_)
			AcquireSRWLockExclusive(&cell.value);
	}

	void UnlockAllExclusive()
	{
		for (size_t i = ShardCount; i > 0; --i)
			ReleaseSRWLockExclusive(&locks_[i - 1].value);
	}

	bool IsActive() const
	{
		return active_.load(std::memory_order_acquire);
	}

	void Deactivate()
	{
		active_.store(false, std::memory_order_release);
	}

	// Dense slot arrays are materialized once per heap lifetime and then
	// stable; a release store here publishes that readiness so lock-free
	// writers/readers can touch slots/occupied without the shard locks.
	bool IsMaterialized() const
	{
		return materialized_.load(std::memory_order_acquire);
	}

	void MarkMaterialized()
	{
		materialized_.store(true, std::memory_order_release);
	}

	const SRWLOCK *LockForSlot(uint32_t slot) const
	{
		return &locks_[ShardForSlot(slot)].value;
	}

	void ResetVersions()
	{
		version_.store(1, std::memory_order_release);
		if (!versionPages_)
			return;
		for (uint32_t pageIndex = 0;
				pageIndex < versionPageCount_; ++pageIndex) {
			VersionPage *page = versionPages_[pageIndex].load(
				std::memory_order_acquire);
			if (!page)
				continue;
			for (std::atomic<uint64_t> &slotVersion : page->versions)
				slotVersion.store(2, std::memory_order_release);
			for (std::atomic<uint64_t> &valueId : page->valueIds)
				valueId.store(0, std::memory_order_release);
			for (std::atomic<uint64_t> &valueId : page->flatValueIds)
				valueId.store(0, std::memory_order_release);
			for (std::atomic<uint64_t> &version : page->flatVersions)
				version.store(2, std::memory_order_release);
		}
	}

	void BumpVersion(uint32_t slot)
	{
		PublishSlotValue(slot, 0);
	}

	void PublishSlotValue(uint32_t slot, uint64_t valueId)
	{
		PublishSlotIdentity(slot, valueId);
		BumpHeapVersion();
	}

	// Range copies publish every slot identity, but consumers only require the
	// heap-wide invalidation token to change once for the completed API call.
	// Splitting these operations removes one contended atomic RMW per copied
	// descriptor without weakening per-slot identity validation.
	void PublishSlotIdentity(uint32_t slot, uint64_t valueId)
	{
		VersionPage *page = EnsureVersionPage(slot);
		if (page) {
			std::atomic<uint64_t> &slotVersion =
				page->versions[slot & (VersionPageSize - 1)];
			// Odd means a publication is in progress. Readers only accept the
			// value when the same even version brackets the Value ID load. Slot
			// writers are serialized by the owning shard lock, so the second
			// contended atomic RMW formerly used here bought no correctness. An
			// exchange starts publication and the release store completes it.
			const uint64_t previous = slotVersion.fetch_or(
				1ull, std::memory_order_acq_rel);
			page->valueIds[slot & (VersionPageSize - 1)].store(
				valueId, std::memory_order_relaxed);
			// Versions are initialized even and only one writer owns this slot.
			// Advancing from the previous even generation preserves cache-key
			// invalidation, including wraparound's even parity.
			const uint64_t completedVersion = (previous + 2ull) & ~1ull;
			slotVersion.store(completedVersion,
				std::memory_order_release);
			// Flat snapshot: written after the version store completes so a
			// reader that observes flatVersions == versions also observes the
			// matching flatValueIds (same thread store order; relaxed loads
			// on x86 observe stores in order).
			page->flatValueIds[slot & (VersionPageSize - 1)].store(
				valueId, std::memory_order_relaxed);
			page->flatVersions[slot & (VersionPageSize - 1)].store(
				completedVersion, std::memory_order_relaxed);
		}
	}

	// Lock-free O(1) check: when the slot's version is unchanged since the
	// last publication, flatValueIds[slot] is the authoritative valueId.
	// Returns false when the slot was never published or has been republished
	// (the caller then falls back to the seqlock path).
	bool ReadFlatValueId(uint32_t slot, uint64_t *valueId) const
	{
		if (!valueId || !versionPages_ || slot >= maxSlots_)
			return false;
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		if (!page)
			return false;
		const uint32_t offset = slot & (VersionPageSize - 1);
		const uint64_t flatVersion =
			page->flatVersions[offset].load(std::memory_order_relaxed);
		if (flatVersion == 0)
			return false;
		if (page->versions[offset].load(std::memory_order_acquire) !=
			flatVersion)
			return false;
		*valueId =
			page->flatValueIds[offset].load(std::memory_order_relaxed);
		return true;
	}

	void BumpHeapVersion()
	{
		version_.fetch_add(1, std::memory_order_release);
	}

	struct SlotIdentity
	{
		uint64_t version = 0;
		uint64_t valueId = 0;
	};

	bool ObserveSlotIdentity(uint32_t slot, SlotIdentity *identity) const
	{
		if (!identity || !versionPages_ || slot >= maxSlots_)
			return false;
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		if (!page) {
			identity->version = 2;
			identity->valueId = 0;
			return true;
		}
		const uint32_t offset = slot & (VersionPageSize - 1);
		const uint64_t before = page->versions[offset].load(
			std::memory_order_acquire);
		if ((before & 1) != 0)
			return false;
		const uint64_t valueId = page->valueIds[offset].load(
			std::memory_order_acquire);
		const uint64_t after = page->versions[offset].load(
			std::memory_order_acquire);
		if (before != after || (after & 1) != 0)
			return false;
		identity->version = after;
		identity->valueId = valueId;
		return true;
	}

	// Creation hooks frequently publish the same compact descriptor value to the
	// same slot thousands of times per frame.  Reject those writes from the
	// seqlock publication itself before taking the destination shard exclusively
	// or touching the sparse descriptor store.
	bool PublishedSlotValueEquals(uint32_t slot, uint64_t valueId) const
	{
		if (valueId == 0)
			return false;
		SlotIdentity identity;
		return ObserveSlotIdentity(slot, &identity) &&
			identity.valueId == valueId;
	}

	uint64_t ReadSlotVersion(uint32_t slot) const
	{
		if (!versionPages_ || slot >= maxSlots_)
			return version_.load(std::memory_order_acquire);
		VersionPage *page = versionPages_[slot / VersionPageSize].load(
			std::memory_order_acquire);
		return page ?
			page->versions[slot & (VersionPageSize - 1)].load(
				std::memory_order_acquire) : 2;
	}

	uint64_t ReadVersion() const
	{
		return version_.load(std::memory_order_acquire);
	}

private:
	static constexpr uint32_t VersionPageSize = 256;
	static_assert((VersionPageSize & (VersionPageSize - 1)) == 0,
		"descriptor version page size must be a power of two");

	struct VersionPage
	{
		std::array<std::atomic<uint64_t>, VersionPageSize> versions;
		std::array<std::atomic<uint64_t>, VersionPageSize> valueIds;
		// Flat post-publication snapshots, written after the seqlock version
		// store completes. Readers compare flatVersions[slot] against the
		// live versions[slot]: when they match, the slot has not been
		// republished since the snapshot, so flatValueIds[slot] is the
		// authoritative current valueId. This gives CopyDescriptorRange a
		// lock-free O(1) "is dest already identical to src" check indexed by
		// slot - the same design as DXVK's views[slot] != resView compare -
		// without the seqlock's three-load read protocol or any hash-table
		// capacity limits.
		std::array<std::atomic<uint64_t>, VersionPageSize> flatValueIds;
		std::array<std::atomic<uint64_t>, VersionPageSize> flatVersions;

		VersionPage()
		{
			for (std::atomic<uint64_t> &version : versions)
				version.store(2, std::memory_order_relaxed);
			for (std::atomic<uint64_t> &valueId : valueIds)
				valueId.store(0, std::memory_order_relaxed);
			for (std::atomic<uint64_t> &valueId : flatValueIds)
				valueId.store(0, std::memory_order_relaxed);
			for (std::atomic<uint64_t> &version : flatVersions)
				version.store(2, std::memory_order_relaxed);
		}
	};

	VersionPage *EnsureVersionPage(uint32_t slot)
	{
		if (!versionPages_ || slot >= maxSlots_)
			return nullptr;
		std::atomic<VersionPage*> &cell =
			versionPages_[slot / VersionPageSize];
		VersionPage *page = cell.load(std::memory_order_acquire);
		if (page)
			return page;
		VersionPage *candidate = new VersionPage();
		VersionPage *expected = nullptr;
		if (cell.compare_exchange_strong(
				expected, candidate,
				std::memory_order_release,
				std::memory_order_acquire))
			return candidate;
		delete candidate;
		return expected;
	}

	std::atomic<bool> materialized_{false};
	std::atomic<bool> active_{true};
	std::array<LockCell, ShardCount> locks_;
	// Consumers only need a non-zero token that changes after any Descriptor
	// State write. Keeping 1024 per-shard counters made every cache validation
	// read 1024 cache lines; one heap-wide token makes that hot read O(1).
	alignas(CacheLineBytes) std::atomic<uint64_t> version_{1};
	uint32_t maxSlots_ = 0;
	uint32_t versionPageCount_ = 0;
	std::unique_ptr<std::atomic<VersionPage*>[]> versionPages_;
};



// Sparse current-value storage used by Descriptor State. The old layout kept
// 1024 unordered_maps per heap so independently locked slots could be updated
// concurrently. Real descriptor rings issue tens of thousands of single-slot
// copies per frame, making two hash-table probes per copy the dominant CPU
// cost. This layout retains sparse allocation, but resolves a slot through a
// pre-sized page-pointer table and one direct array index instead.
//
// The store deliberately contains no per-slot locking. The owning descriptor
// heap supplies that policy. Page publication is atomic because different slot
// locks can touch the same page for the first time concurrently.
template <typename Value>
class DX12SparseDescriptorStore
{
public:
	// Kept in sync with the owning heap's lock striping contract.
	// Descriptor writes are short and command recording normally has only a
	// handful of producer threads.  1024 cache-line-sized locks gave every heap
	// a 64 KiB lock working set; sequential descriptor rings consequently
	// touched a different cache line for virtually every CopyDescriptors slot.
	// 64 stripes still provide substantially more concurrency than the wrapper
	// can use while keeping the complete lock set in a small L1/L2 footprint.
	static constexpr uint32_t ShardCount = 64;
	static constexpr uint32_t PageSize = 256;

	static_assert((ShardCount & (ShardCount - 1)) == 0,
		"descriptor shard count must be a power of two");
	static_assert((PageSize & (PageSize - 1)) == 0,
		"descriptor page size must be a power of two");

	static constexpr uint32_t ShardForSlot(uint32_t slot)
	{
		return slot & (ShardCount - 1);
	}

	struct SnapshotEntry
	{
		bool occupied = false;
		Value value = {};
	};

	DX12SparseDescriptorStore() = default;

	DX12SparseDescriptorStore(const DX12SparseDescriptorStore&) = delete;
	DX12SparseDescriptorStore &operator=(
		const DX12SparseDescriptorStore&) = delete;

	DX12SparseDescriptorStore(DX12SparseDescriptorStore &&other) noexcept
	{
		MoveFrom(std::move(other));
	}

	DX12SparseDescriptorStore &operator=(
		DX12SparseDescriptorStore &&other) noexcept
	{
		if (this == &other)
			return *this;
		ResetStorage();
		MoveFrom(std::move(other));
		return *this;
	}

	~DX12SparseDescriptorStore()
	{
		ResetStorage();
	}

	// Called once when the descriptor heap boundary becomes known. Only the
	// small page-pointer index is allocated here; Value pages remain lazy.
	void Initialize(uint32_t maxSlots)
	{
		ResetStorage();
		maxSlots_ = maxSlots;
		pageCount_ = maxSlots == 0 ? 0 : static_cast<uint32_t>(
			(static_cast<uint64_t>(maxSlots) + PageSize - 1) / PageSize);
		if (pageCount_ == 0)
			return;
		pages_ = std::make_unique<std::atomic<Page*>[]>(pageCount_);
		for (uint32_t index = 0; index < pageCount_; ++index)
			pages_[index].store(nullptr, std::memory_order_relaxed);
	}

	const Value *Find(uint32_t slot) const
	{
		const Page *page = PageForSlot(slot);
		if (!page)
			return nullptr;
		const uint32_t offset = OffsetForSlot(slot);
		return page->occupied[offset] ? &page->values[offset] : nullptr;
	}

	Value *Find(uint32_t slot)
	{
		Page *page = PageForSlot(slot);
		if (!page)
			return nullptr;
		const uint32_t offset = OffsetForSlot(slot);
		return page->occupied[offset] ? &page->values[offset] : nullptr;
	}

	// Returns the existing value or inserts one default value. Callers already
	// serialize the owning slot, so the returned address remains stable.
	Value *FindOrInsertDefault(uint32_t slot, bool *inserted)
	{
		Page *page = EnsurePageForSlot(slot);
		if (!page) {
			if (inserted)
				*inserted = false;
			return nullptr;
		}
		const uint32_t offset = OffsetForSlot(slot);
		const bool created = page->occupied[offset] == 0;
		if (created) {
			page->values[offset] = Value();
			page->occupied[offset] = 1;
		}
		if (inserted)
			*inserted = created;
		return &page->values[offset];
	}

	bool Store(uint32_t slot, Value value)
	{
		bool inserted = false;
		Value *stored = FindOrInsertDefault(slot, &inserted);
		if (!stored)
			return false;
		*stored = std::move(value);
		return inserted;
	}

	bool Erase(uint32_t slot)
	{
		Page *page = PageForSlot(slot);
		if (!page)
			return false;
		const uint32_t offset = OffsetForSlot(slot);
		if (!page->occupied[offset])
			return false;
		page->values[offset] = Value();
		page->occupied[offset] = 0;
		return true;
	}

	void SnapshotRange(
		uint32_t firstSlot,
		uint32_t count,
		std::vector<SnapshotEntry> *snapshot) const
	{
		if (!snapshot)
			return;
		snapshot->clear();
		snapshot->resize(count);
		for (uint32_t i = 0; i < count; ++i) {
			const Value *value = Find(firstSlot + i);
			if (!value)
				continue;
			(*snapshot)[i].occupied = true;
			(*snapshot)[i].value = *value;
		}
	}

	template <typename Visitor>
	void ForEach(Visitor visitor) const
	{
		for (uint32_t pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
			const Page *page = pages_[pageIndex].load(std::memory_order_acquire);
			if (!page)
				continue;
			const uint32_t firstSlot = pageIndex * PageSize;
			const uint32_t slotsInPage =
				maxSlots_ - firstSlot < PageSize ? maxSlots_ - firstSlot : PageSize;
			for (uint32_t offset = 0; offset < slotsInPage; ++offset) {
				if (page->occupied[offset])
					visitor(firstSlot + offset, page->values[offset]);
			}
		}
	}

	size_t Size() const
	{
		size_t size = 0;
		ForEach([&size](uint32_t, const Value&) { ++size; });
		return size;
	}

	bool Empty() const
	{
		for (uint32_t pageIndex = 0; pageIndex < pageCount_; ++pageIndex) {
			const Page *page = pages_[pageIndex].load(std::memory_order_acquire);
			if (!page)
				continue;
			const uint32_t firstSlot = pageIndex * PageSize;
			const uint32_t slotsInPage =
				maxSlots_ - firstSlot < PageSize ? maxSlots_ - firstSlot : PageSize;
			for (uint32_t offset = 0; offset < slotsInPage; ++offset) {
				if (page->occupied[offset])
					return false;
			}
		}
		return true;
	}

	// Releases only allocated pages while retaining the small page index, so a
	// cleared active heap can be populated again without rebuilding capacity.
	void Clear()
	{
		if (!pages_)
			return;
		for (uint32_t index = 0; index < pageCount_; ++index) {
			Page *page = pages_[index].exchange(
				nullptr, std::memory_order_acq_rel);
			delete page;
		}
	}

	uint32_t AllocatedPageCount() const
	{
		uint32_t count = 0;
		for (uint32_t index = 0; index < pageCount_; ++index) {
			if (pages_[index].load(std::memory_order_acquire))
				++count;
		}
		return count;
	}

private:
	struct Page
	{
		std::array<Value, PageSize> values = {};
		std::array<uint8_t, PageSize> occupied = {};
	};

	static constexpr uint32_t OffsetForSlot(uint32_t slot)
	{
		return slot & (PageSize - 1);
	}

	Page *PageForSlot(uint32_t slot)
	{
		return const_cast<Page*>(
			static_cast<const DX12SparseDescriptorStore*>(this)->PageForSlot(slot));
	}

	const Page *PageForSlot(uint32_t slot) const
	{
		if (!pages_ || slot >= maxSlots_)
			return nullptr;
		return pages_[slot / PageSize].load(std::memory_order_acquire);
	}

	Page *EnsurePageForSlot(uint32_t slot)
	{
		if (!pages_ || slot >= maxSlots_)
			return nullptr;
		std::atomic<Page*> &pageCell = pages_[slot / PageSize];
		Page *page = pageCell.load(std::memory_order_acquire);
		if (page)
			return page;

		Page *candidate = new Page();
		Page *expected = nullptr;
		if (pageCell.compare_exchange_strong(
				expected, candidate,
				std::memory_order_release,
				std::memory_order_acquire))
			return candidate;
		delete candidate;
		return expected;
	}

	void ResetStorage()
	{
		Clear();
		pages_.reset();
		pageCount_ = 0;
		maxSlots_ = 0;
	}

	void MoveFrom(DX12SparseDescriptorStore &&other)
	{
		pages_ = std::move(other.pages_);
		pageCount_ = other.pageCount_;
		maxSlots_ = other.maxSlots_;
		other.pageCount_ = 0;
		other.maxSlots_ = 0;
	}

	std::unique_ptr<std::atomic<Page*>[]> pages_;
	uint32_t pageCount_ = 0;
	uint32_t maxSlots_ = 0;
};




struct RootSignatureRecord
{
	ID3D12RootSignature *rootSignature = nullptr;
	UINT64 hash = 0;
	SIZE_T size = 0;
	UINT nodeMask = 0;
	UINT version = 0;
	UINT flags = 0;
	UINT staticSamplerCount = 0;
	bool parsed = false;
	std::vector<DX12RootParameterSummary> parameters;
};

class DX12DescriptorViewPayload
{
public:
	union
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
		D3D12_RENDER_TARGET_VIEW_DESC rtv;
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv;
		D3D12_SAMPLER_DESC sampler;
	};

	DX12DescriptorViewPayload()
	{
		memset(this, 0, sizeof(*this));
	}

	DX12DescriptorViewPayload(const DX12DescriptorViewPayload &other)
	{
		memcpy(this, &other, sizeof(*this));
	}

	DX12DescriptorViewPayload &operator=(const DX12DescriptorViewPayload &other)
	{
		if (this != &other)
			memcpy(this, &other, sizeof(*this));
		return *this;
	}
};

struct DescriptorRecord : public DX12DescriptorViewPayload
{
	std::string kind;
	UINT kindId = 0;
	UINT64 valueId = 0;
	// Draw-time hunt/override content hash, computed once at write time
	// (Create*View) and reused by the draw prefilter. 0 = not cached (the
	// draw path then recomputes, so a missed cache is always safe).
	uint32_t descriptorHuntHash = 0;
	SIZE_T cpuHandle = 0;
	ID3D12Resource *resource = nullptr;
	bool ownsResource = false;
	ID3D12Resource *counterResource = nullptr;
	bool ownsCounterResource = false;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool hasDesc = false;
};

struct DX12ExtendedDescriptorValue : public DX12DescriptorViewPayload
{
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *counterResource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool hasDesc = false;
};

// Immutable descriptor payload kept by normal Mod-mode Descriptor State.
// CBVs are overwhelmingly dominant and need only 40 bytes here. Rich metadata
// is allocated only for SRV/UAV/RTV/DSV/sampler values, while slot identity,
// presentation strings and snapshot ownership are materialized on query.
struct DX12DescriptorValue
{
	UINT kindId = 0;
	UINT64 valueId = 0;
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
	std::unique_ptr<DX12ExtendedDescriptorValue> extended;

	DX12DescriptorValue() = default;
	DX12DescriptorValue(DX12DescriptorValue &&) noexcept = default;
	DX12DescriptorValue &operator=(DX12DescriptorValue &&) noexcept = default;

	DX12DescriptorValue(const DX12DescriptorValue &other)
		: kindId(other.kindId), valueId(other.valueId), cbv(other.cbv)
	{
		if (other.extended)
			extended = std::make_unique<DX12ExtendedDescriptorValue>(
				*other.extended);
	}

	DX12DescriptorValue &operator=(const DX12DescriptorValue &other)
	{
		if (this == &other)
			return *this;
		kindId = other.kindId;
		valueId = other.valueId;
		cbv = other.cbv;
		if (other.extended)
			extended = std::make_unique<DX12ExtendedDescriptorValue>(
				*other.extended);
		else
			extended.reset();
		return *this;
	}
};

class DX12DescriptorValueHashUtils
{
private:
	static UINT64 Mix(UINT64 value)
	{
		value ^= value >> 30;
		value *= 0xbf58476d1ce4e5b9ull;
		value ^= value >> 27;
		value *= 0x94d049bb133111ebull;
		value ^= value >> 31;
		return value;
	}

	static void Append(UINT64 *hash, const void *data, size_t size)
	{
		const uint8_t *bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			*hash ^= bytes[i];
			*hash *= 1099511628211ull;
		}
	}

	static UINT64 Finish(UINT64 hash)
	{
		return hash ? hash : 1;
	}

public:
	static UINT64 HashRecord(const DescriptorRecord &record)
	{
		// CBVs contain only BufferLocation and SizeInBytes. They account for
		// tens of thousands of descriptor writes per frame, so avoid feeding
		// their duplicated fields through the Debug /Od byte-at-a-time path.
		if (record.kindId == DescriptorKind::CBV_ID && record.hasDesc)
			return HashConstantBufferView(record.cbv);
		UINT64 hash = 14695981039346656037ull;
		Append(&hash, &record.kindId, sizeof(record.kindId));
		Append(&hash, &record.resource, sizeof(record.resource));
		Append(&hash, &record.counterResource, sizeof(record.counterResource));
		Append(&hash, &record.gpuVirtualAddress, sizeof(record.gpuVirtualAddress));
		Append(&hash, &record.resourceOffset, sizeof(record.resourceOffset));
		Append(&hash, &record.viewSize, sizeof(record.viewSize));
		Append(&hash, &record.hasDesc, sizeof(record.hasDesc));
		if (record.kindId == DescriptorKind::CBV_ID)
			Append(&hash, &record.cbv, sizeof(record.cbv));
		else if (record.kindId == DescriptorKind::SRV_ID)
			Append(&hash, &record.srv, sizeof(record.srv));
		else if (record.kindId == DescriptorKind::UAV_ID)
			Append(&hash, &record.uav, sizeof(record.uav));
		else if (record.kindId == DescriptorKind::RTV_ID)
			Append(&hash, &record.rtv, sizeof(record.rtv));
		else if (record.kindId == DescriptorKind::DSV_ID)
			Append(&hash, &record.dsv, sizeof(record.dsv));
		else if (record.kindId == DescriptorKind::Sampler_ID)
			Append(&hash, &record.sampler, sizeof(record.sampler));
		return Finish(hash);
	}

	static UINT64 HashConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc)
	{
		const UINT64 location = Mix(
			desc.BufferLocation ^ 0x4342565f49445f31ull);
		const UINT64 size = Mix(
			static_cast<UINT64>(desc.SizeInBytes) ^ 0x9e3779b97f4a7c15ull);
		return Finish(location ^ size);
	}
};

// Normal Mod mode stores one immutable Descriptor Value and lets every copied
// Descriptor State reference it. CopyDescriptors then moves a pointer-sized
// binding instead of duplicating the full DescriptorRecord payload.
struct alignas(MEMORY_ALLOCATION_ALIGNMENT) DX12DescriptorValueNode
{
	SLIST_ENTRY freeEntry = {};
	volatile LONG references = 0;
	DX12DescriptorValue value;
};

class DX12DescriptorValuePool
{
private:
	static BOOL CALLBACK InitializeFreeList(
		PINIT_ONCE, PVOID parameter, PVOID*)
	{
		InitializeSListHead(static_cast<PSLIST_HEADER>(parameter));
		return TRUE;
	}

	static PSLIST_HEADER FreeList()
	{
		alignas(MEMORY_ALLOCATION_ALIGNMENT) static SLIST_HEADER freeList;
		static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
		InitOnceExecuteOnce(&once, InitializeFreeList, &freeList, nullptr);
		return &freeList;
	}

public:
	static DX12DescriptorValueNode *Acquire()
	{
		PSLIST_ENTRY entry = InterlockedPopEntrySList(FreeList());
		DX12DescriptorValueNode *node = entry ?
			CONTAINING_RECORD(entry, DX12DescriptorValueNode, freeEntry) :
			new DX12DescriptorValueNode();
		InterlockedExchange(&node->references, 1);
		return node;
	}

	static void Retain(DX12DescriptorValueNode *node)
	{
		if (node)
			InterlockedIncrement(&node->references);
	}

	static void Release(DX12DescriptorValueNode *node)
	{
		if (!node || InterlockedDecrement(&node->references) != 0)
			return;
		node->value = DX12DescriptorValue();
		InterlockedPushEntrySList(FreeList(), &node->freeEntry);
	}
};

struct DescriptorSlotBinding
{
	DX12DescriptorValueNode *value = nullptr;
};

struct DescriptorHeapRecord
{
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
	using StateLocks = DX12DescriptorHeapShardState<
		DX12SparseDescriptorStore<DescriptorSlotBinding>::ShardCount>;
	// Lock striping follows the heap record when gDescriptorHeaps grows or
	// reuses a slot. Sequential descriptor slots land in different shards.
	std::unique_ptr<StateLocks> stateLocks;
	std::vector<DescriptorRecord> slots;
	std::vector<uint8_t> occupied;
	DX12SparseDescriptorStore<DescriptorSlotBinding> currentDescriptors;
	std::vector<std::vector<DescriptorRecord>> slotHistory;
	std::vector<std::vector<UINT64>> slotHistoryEpochs;
	volatile LONG64 liveSlots = 0;

	bool IsMaterialized() const
	{
		return stateLocks && stateLocks->IsMaterialized();
	}
};

class DX12DescriptorHeapStateUtils
{
public:
static void ResetVersion(DescriptorHeapRecord *record)
{
	if (!record || !record->stateLocks)
		return;
	record->stateLocks->ResetVersions();
}

static void BumpVersion(DescriptorHeapRecord *record, UINT slot)
{
	if (!record || !record->stateLocks)
		return;
	UINT64 valueId = 0;
	if (!record->slots.empty() && slot < record->slots.size() &&
			slot < record->occupied.size() && record->occupied[slot]) {
		valueId = record->slots[slot].valueId;
	} else {
		const DescriptorSlotBinding *binding =
			record->currentDescriptors.Find(slot);
		if (binding && binding->value)
			valueId = binding->value->value.valueId;
	}
	record->stateLocks->PublishSlotValue(slot, valueId);
}

// Copy hot paths already have the authoritative source identity in hand.  Do
// not look the destination up again after every copy merely to publish the
// value that was just stored.
static void PublishValue(DescriptorHeapRecord *record, UINT slot, UINT64 valueId)
{
	if (record && record->stateLocks)
		record->stateLocks->PublishSlotValue(slot, valueId);
}

static void PublishRangeValue(
	DescriptorHeapRecord *record, UINT slot, UINT64 valueId)
{
	if (record && record->stateLocks)
		record->stateLocks->PublishSlotIdentity(slot, valueId);
}

static void CommitPublishedRange(DescriptorHeapRecord *record)
{
	if (record && record->stateLocks)
		record->stateLocks->BumpHeapVersion();
}

static UINT64 ReadVersion(const DescriptorHeapRecord &record)
{
	return record.stateLocks ? record.stateLocks->ReadVersion() : 0;
}

static void FillSummary(const DescriptorHeapRecord &record, DX12DescriptorHeapSummary *summary)
{
	if (!summary)
		return;
	summary->heap = record.heap;
	summary->type = static_cast<UINT>(record.desc.Type);
	summary->numDescriptors = record.desc.NumDescriptors;
	summary->flags = static_cast<UINT>(record.desc.Flags);
	summary->nodeMask = record.desc.NodeMask;
	summary->cpuStart = record.cpuStart;
	summary->gpuStart = record.gpuStart;
	summary->increment = record.increment;
	summary->version = ReadVersion(record);
}
};

struct DescriptorSlotLocation
{
	size_t heapIndex = 0;
	UINT slot = 0;
};

class DX12DescriptorSlotArrayUtils
{
public:
static LONG64 ReadLiveSlots(const DescriptorHeapRecord *heap)
{
	return heap ? InterlockedCompareExchange64(
		const_cast<volatile LONG64*>(&heap->liveSlots), 0, 0) : 0;
}

static void SetLiveSlots(DescriptorHeapRecord *heap, LONG64 value)
{
	if (heap)
		InterlockedExchange64(&heap->liveSlots, value);
}

static void IncrementLiveSlots(DescriptorHeapRecord *heap)
{
	if (heap)
		InterlockedIncrement64(&heap->liveSlots);
}

static void DecrementLiveSlots(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	LONG64 current = ReadLiveSlots(heap);
	while (current > 0) {
		const LONG64 observed = InterlockedCompareExchange64(
			&heap->liveSlots, current - 1, current);
		if (observed == current)
			return;
		current = observed;
	}
}

static void CopyRecordValueForSlot(DescriptorRecord *dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	if (!dest)
		return;
	// Descriptor copies dominate some DX12 games. DescriptorRecord's generated
	// assignment unconditionally assigns std::string even though a slot almost
	// always keeps the same descriptor kind (especially CBV). Copy the POD-like
	// payload explicitly and only touch the string when its value really changes.
	static_cast<DX12DescriptorViewPayload &>(*dest) = src;
	if (dest->kind != src.kind)
		dest->kind = src.kind;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	dest->descriptorHuntHash = src.descriptorHuntHash;
	dest->cpuHandle = destCpuHandle;
	dest->resource = src.resource;
	// Descriptor heap slots mirror descriptor values. Ownership stays with the
	// CPU descriptor ledger/resource tracker, so slot copies do not multiply
	// COM references.
	dest->ownsResource = false;
	dest->counterResource = src.counterResource;
	dest->ownsCounterResource = false;
	dest->resourceDesc = src.resourceDesc;
	dest->hasResourceDesc = src.hasResourceDesc;
	dest->resourceHeapType = src.resourceHeapType;
	dest->hasResourceHeapType = src.hasResourceHeapType;
	dest->gpuVirtualAddress = src.gpuVirtualAddress;
	dest->resourceOffset = src.resourceOffset;
	dest->viewSize = src.viewSize;
	dest->currentState = src.currentState;
	dest->hasCurrentState = src.hasCurrentState;
	dest->stateTrackedByBarrier = src.stateTrackedByBarrier;
	dest->hasDesc = src.hasDesc;
}

static void CopyDescriptorValue(
	DX12DescriptorValue *dest, const DescriptorRecord &src)
{
	if (!dest)
		return;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	if (src.kindId == DescriptorKind::CBV_ID) {
		dest->cbv = src.cbv;
		dest->extended.reset();
		return;
	}
	dest->cbv = {};
	dest->extended = std::make_unique<DX12ExtendedDescriptorValue>();
	DX12ExtendedDescriptorValue &extended = *dest->extended;
	static_cast<DX12DescriptorViewPayload &>(extended) = src;
	extended.resource = src.resource;
	extended.counterResource = src.counterResource;
	extended.resourceDesc = src.resourceDesc;
	extended.hasResourceDesc = src.hasResourceDesc;
	extended.resourceHeapType = src.resourceHeapType;
	extended.hasResourceHeapType = src.hasResourceHeapType;
	extended.gpuVirtualAddress = src.gpuVirtualAddress;
	extended.resourceOffset = src.resourceOffset;
	extended.viewSize = src.viewSize;
	extended.currentState = src.currentState;
	extended.hasCurrentState = src.hasCurrentState;
	extended.stateTrackedByBarrier = src.stateTrackedByBarrier;
	extended.hasDesc = src.hasDesc;
}

static void CopyRecordValueForSlot(
	DescriptorRecord *dest,
	const DX12DescriptorValue &src,
	SIZE_T destCpuHandle)
{
	if (!dest)
		return;
	const char *kind = "";
	switch (src.kindId) {
	case DescriptorKind::CBV_ID: kind = "CBV"; break;
	case DescriptorKind::SRV_ID: kind = "SRV"; break;
	case DescriptorKind::UAV_ID: kind = "UAV"; break;
	case DescriptorKind::RTV_ID: kind = "RTV"; break;
	case DescriptorKind::DSV_ID: kind = "DSV"; break;
	case DescriptorKind::Sampler_ID: kind = "Sampler"; break;
	}
	if (dest->kind != kind)
		dest->kind = kind;
	dest->kindId = src.kindId;
	dest->valueId = src.valueId;
	// Value-path records (CBV fast path) never carry a cached hunt hash.
	dest->descriptorHuntHash = 0;
	dest->cpuHandle = destCpuHandle;
	dest->resource = nullptr;
	dest->ownsResource = false;
	dest->counterResource = nullptr;
	dest->ownsCounterResource = false;
	dest->resourceDesc = {};
	dest->hasResourceDesc = false;
	dest->resourceHeapType = 0;
	dest->hasResourceHeapType = false;
	dest->gpuVirtualAddress = 0;
	dest->resourceOffset = 0;
	dest->viewSize = 0;
	dest->currentState = D3D12_RESOURCE_STATE_COMMON;
	dest->hasCurrentState = false;
	dest->stateTrackedByBarrier = false;
	dest->hasDesc = false;
	if (src.kindId == DescriptorKind::CBV_ID) {
		static_cast<DX12DescriptorViewPayload &>(*dest) =
			DX12DescriptorViewPayload();
		dest->cbv = src.cbv;
		dest->gpuVirtualAddress = src.cbv.BufferLocation;
		dest->viewSize = src.cbv.SizeInBytes;
		dest->hasDesc = true;
		return;
	}
	if (!src.extended) {
		static_cast<DX12DescriptorViewPayload &>(*dest) =
			DX12DescriptorViewPayload();
		return;
	}
	const DX12ExtendedDescriptorValue &extended = *src.extended;
	static_cast<DX12DescriptorViewPayload &>(*dest) = extended;
	dest->resource = extended.resource;
	dest->counterResource = extended.counterResource;
	dest->resourceDesc = extended.resourceDesc;
	dest->hasResourceDesc = extended.hasResourceDesc;
	dest->resourceHeapType = extended.resourceHeapType;
	dest->hasResourceHeapType = extended.hasResourceHeapType;
	dest->gpuVirtualAddress = extended.gpuVirtualAddress;
	dest->resourceOffset = extended.resourceOffset;
	dest->viewSize = extended.viewSize;
	dest->currentState = extended.currentState;
	dest->hasCurrentState = extended.hasCurrentState;
	dest->stateTrackedByBarrier = extended.stateTrackedByBarrier;
	dest->hasDesc = extended.hasDesc;
}

static DX12DescriptorValue MakeConstantBufferViewValue(
	const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc,
	UINT64 valueId)
{
	DX12DescriptorValue value;
	value.kindId = DescriptorKind::CBV_ID;
	value.valueId = valueId;
	value.cbv = desc;
	return value;
}

static bool SameRecordValueAtSlot(const DescriptorRecord &dest, const DescriptorRecord &src, SIZE_T destCpuHandle)
{
	return src.valueId != 0 && dest.valueId == src.valueId &&
		dest.cpuHandle == destCpuHandle;
}

static const DescriptorSlotBinding *FindCurrentBinding(
	const DescriptorHeapRecord &heap, UINT slot)
{
	return heap.currentDescriptors.Find(slot);
}

static bool CopyCurrentRecord(
	const DescriptorHeapRecord &heap, UINT slot, DescriptorRecord *record)
{
	if (!record)
		return false;
	const DescriptorSlotBinding *binding = FindCurrentBinding(heap, slot);
	if (binding && binding->value) {
		CopyRecordValueForSlot(
			record, binding->value->value,
			heap.cpuStart + static_cast<SIZE_T>(slot) * heap.increment);
		return true;
	}
	if (slot < heap.occupied.size() && heap.occupied[slot] &&
			slot < heap.slots.size()) {
		CopyRecordValueForSlot(record, heap.slots[slot], heap.slots[slot].cpuHandle);
		return true;
	}
	return false;
}

static void StoreCurrentRecord(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DescriptorRecord &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors)
		return;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		DescriptorRecord stored;
		CopyRecordValueForSlot(&stored, source, destCpuHandle);
		const bool wasOccupied = heap->occupied[slot] != 0;
		heap->slots[slot] = std::move(stored);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return;
	}
	StoreCurrentRecordIfChanged(heap, slot, source, destCpuHandle);
}

static bool StoreCurrentRecordIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DescriptorRecord &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		const bool wasOccupied = heap->occupied[slot] != 0;
		if (wasOccupied && SameRecordValueAtSlot(
				heap->slots[slot], source, destCpuHandle))
			return false;
		CopyRecordValueForSlot(&heap->slots[slot], source, destCpuHandle);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return true;
	}

	bool inserted = false;
	DescriptorSlotBinding *stored =
		heap->currentDescriptors.FindOrInsertDefault(slot, &inserted);
	if (!inserted && stored->value && source.valueId != 0 &&
			stored->value->value.valueId == source.valueId)
		return false;
	DX12DescriptorValueNode *newValue = DX12DescriptorValuePool::Acquire();
	CopyDescriptorValue(&newValue->value, source);
	DX12DescriptorValueNode *oldValue = stored->value;
	stored->value = newValue;
	DX12DescriptorValuePool::Release(oldValue);
	if (inserted)
		IncrementLiveSlots(heap);
	return true;
}

static bool StoreCurrentValueIfChanged(
	DescriptorHeapRecord *heap,
	UINT slot,
	const DX12DescriptorValue &source,
	SIZE_T destCpuHandle)
{
	if (!heap || slot >= heap->desc.NumDescriptors || source.valueId == 0)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		const bool wasOccupied = heap->occupied[slot] != 0;
		if (wasOccupied && heap->slots[slot].valueId == source.valueId &&
				heap->slots[slot].cpuHandle == destCpuHandle)
			return false;
		CopyRecordValueForSlot(
			&heap->slots[slot], source, destCpuHandle);
		heap->occupied[slot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(heap);
		return true;
	}

	bool inserted = false;
	DescriptorSlotBinding *stored =
		heap->currentDescriptors.FindOrInsertDefault(slot, &inserted);
	if (!inserted && stored->value &&
			stored->value->value.valueId == source.valueId)
		return false;
	DX12DescriptorValueNode *newValue = DX12DescriptorValuePool::Acquire();
	newValue->value = source;
	DX12DescriptorValueNode *oldValue = stored->value;
	stored->value = newValue;
	DX12DescriptorValuePool::Release(oldValue);
	if (inserted)
		IncrementLiveSlots(heap);
	return true;
}

enum class CopyCurrentSlotResult
{
	SourceMissingDestinationEmpty,
	SourceMissingDestinationErased,
	Changed,
	Unchanged
};

static CopyCurrentSlotResult CopyCurrentSlotIfChanged(
	DescriptorHeapRecord *destHeap,
	UINT destSlot,
	const DescriptorHeapRecord &srcHeap,
	UINT srcSlot,
	SIZE_T destCpuHandle,
	UINT64 *publishedValueId = nullptr)
{
	if (publishedValueId)
		*publishedValueId = 0;
	if (!destHeap || destSlot >= destHeap->desc.NumDescriptors ||
			srcSlot >= srcHeap.desc.NumDescriptors) {
		return SourceMissingResult(destHeap, destSlot);
	}

	const DescriptorSlotBinding *sourceBinding = FindCurrentBinding(srcHeap, srcSlot);
	if (sourceBinding && sourceBinding->value && destHeap->slots.empty()) {
		if (publishedValueId)
			*publishedValueId = sourceBinding->value->value.valueId;
		bool inserted = false;
		DescriptorSlotBinding *destBinding =
			destHeap->currentDescriptors.FindOrInsertDefault(destSlot, &inserted);
		if (!inserted && destBinding->value &&
				(destBinding->value == sourceBinding->value ||
				 destBinding->value->value.valueId ==
					sourceBinding->value->value.valueId))
			return CopyCurrentSlotResult::Unchanged;
		DX12DescriptorValuePool::Retain(sourceBinding->value);
		DX12DescriptorValueNode *oldValue = destBinding->value;
		destBinding->value = sourceBinding->value;
		DX12DescriptorValuePool::Release(oldValue);
		if (inserted)
			IncrementLiveSlots(destHeap);
		return CopyCurrentSlotResult::Changed;
	}

	// Dense-to-dense direct copy: when both sides already have their dense
	// slot arrays materialized, copy the record straight from src slot to
	// dest slot in one pass. The previous flow went through a temporary
	// DescriptorRecord (CopyCurrentRecord -> StoreCurrentRecordIfChanged),
	// which paid the full field copy twice per descriptor. This mirrors
	// RenderDoc's single dst.CopyFrom(src) per descriptor.
	if (!srcHeap.slots.empty() && srcSlot < srcHeap.slots.size() &&
			srcSlot < srcHeap.occupied.size() && srcHeap.occupied[srcSlot] &&
			!destHeap->slots.empty() && destSlot < destHeap->slots.size() &&
			destSlot < destHeap->occupied.size()) {
		const DescriptorRecord &srcRecord = srcHeap.slots[srcSlot];
		DescriptorRecord &destRecord = destHeap->slots[destSlot];
		if (publishedValueId)
			*publishedValueId = srcRecord.valueId;
		const bool wasOccupied = destHeap->occupied[destSlot] != 0;
		if (wasOccupied &&
				SameRecordValueAtSlot(destRecord, srcRecord, destCpuHandle))
			return CopyCurrentSlotResult::Unchanged;
		CopyRecordValueForSlot(&destRecord, srcRecord, destCpuHandle);
		destHeap->occupied[destSlot] = 1;
		if (!wasOccupied)
			IncrementLiveSlots(destHeap);
		return CopyCurrentSlotResult::Changed;
	}

	DescriptorRecord source;
	if (!CopyCurrentRecord(srcHeap, srcSlot, &source))
		return SourceMissingResult(destHeap, destSlot);
	if (publishedValueId)
		*publishedValueId = source.valueId;
	return StoreCurrentRecordIfChanged(
		destHeap, destSlot, source, destCpuHandle) ?
		CopyCurrentSlotResult::Changed : CopyCurrentSlotResult::Unchanged;
}

static bool EraseCurrentRecord(DescriptorHeapRecord *heap, UINT slot)
{
	if (!heap)
		return false;
	if (!heap->slots.empty() && slot < heap->slots.size() &&
			slot < heap->occupied.size()) {
		if (!heap->occupied[slot])
			return false;
		heap->slots[slot] = DescriptorRecord();
		heap->occupied[slot] = 0;
		DecrementLiveSlots(heap);
		return true;
	}
	DescriptorSlotBinding *binding = heap->currentDescriptors.Find(slot);
	DX12DescriptorValueNode *oldValue = binding ? binding->value : nullptr;
	const bool erased = heap->currentDescriptors.Erase(slot);
	if (erased)
		DX12DescriptorValuePool::Release(oldValue);
	if (erased)
		DecrementLiveSlots(heap);
	return erased;
}

static void ClearSparseRecords(DescriptorHeapRecord *heap)
{
	if (!heap)
		return;
	heap->currentDescriptors.ForEach(
		[](uint32_t, const DescriptorSlotBinding &binding) {
			DX12DescriptorValuePool::Release(binding.value);
		});
	heap->currentDescriptors.Clear();
}

private:
static CopyCurrentSlotResult SourceMissingResult(
	DescriptorHeapRecord *destHeap, UINT destSlot)
{
	return EraseCurrentRecord(destHeap, destSlot) ?
		CopyCurrentSlotResult::SourceMissingDestinationErased :
		CopyCurrentSlotResult::SourceMissingDestinationEmpty;
}
};

struct ResourceRecord
{
	ID3D12Resource *resource = nullptr;
	UINT64 identityGeneration = 0;
	DX12ResourcePublishedState *publishedState = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
	bool hasHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 size = 0;
	UINT64 gpuVirtualAddressRealEnd = 0;
	UINT64 gpuVirtualAddressOobEnd = 0;
	UINT64 placedHeapOffset = 0;
	UINT64 placedHeapSize = 0;
	bool hasPlacedHeapRange = false;
	D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
};

class DX12ResourcePublishedState
{
public:
	static constexpr UINT32 ActiveFlag = 1u << 0;
	static constexpr UINT32 HasCurrentStateFlag = 1u << 1;
	static constexpr UINT32 StateTrackedByBarrierFlag = 1u << 2;

	std::atomic<UINT64> identityGeneration{0};
	std::atomic<UINT> currentState{static_cast<UINT>(D3D12_RESOURCE_STATE_COMMON)};
	std::atomic<UINT32> flags{0};

	void Publish(const ResourceRecord &record)
	{
		identityGeneration.store(0, std::memory_order_release);
		currentState.store(static_cast<UINT>(record.currentState), std::memory_order_relaxed);
		UINT32 publishedFlags = ActiveFlag;
		if (record.hasCurrentState)
			publishedFlags |= HasCurrentStateFlag;
		if (record.stateTrackedByBarrier)
			publishedFlags |= StateTrackedByBarrierFlag;
		flags.store(publishedFlags, std::memory_order_relaxed);
		identityGeneration.store(record.identityGeneration, std::memory_order_release);
	}

	void PublishState(const ResourceRecord &record)
	{
		currentState.store(static_cast<UINT>(record.currentState), std::memory_order_relaxed);
		UINT32 publishedFlags = ActiveFlag;
		if (record.hasCurrentState)
			publishedFlags |= HasCurrentStateFlag;
		if (record.stateTrackedByBarrier)
			publishedFlags |= StateTrackedByBarrierFlag;
		flags.store(publishedFlags, std::memory_order_release);
	}

	void Invalidate()
	{
		identityGeneration.store(0, std::memory_order_release);
		flags.store(0, std::memory_order_release);
	}

	bool TryRead(
		UINT64 expectedGeneration,
		UINT *state,
		bool *hasState,
		bool *trackedByBarrier) const
	{
		if (!expectedGeneration)
			return false;
		const UINT64 generationBefore =
			identityGeneration.load(std::memory_order_acquire);
		if (generationBefore != expectedGeneration)
			return false;
		const UINT32 publishedFlags = flags.load(std::memory_order_acquire);
		const UINT publishedState = currentState.load(std::memory_order_relaxed);
		const UINT64 generationAfter =
			identityGeneration.load(std::memory_order_acquire);
		if (generationAfter != generationBefore || !(publishedFlags & ActiveFlag))
			return false;
		if (state)
			*state = publishedState;
		if (hasState)
			*hasState = (publishedFlags & HasCurrentStateFlag) != 0;
		if (trackedByBarrier)
			*trackedByBarrier =
				(publishedFlags & StateTrackedByBarrierFlag) != 0;
		return true;
	}
};

class DX12ResourceSnapshotUtils
{
public:
static bool TryAddRef(ID3D12Resource *resource)
{
	if (!resource)
		return false;
	__try {
		resource->AddRef();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void Release(ID3D12Resource *resource)
{
	if (!resource)
		return;
	__try {
		resource->Release();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}
};

struct BufferResolveCacheEntry
{
	UINT64 begin = 0;
	UINT64 end = 0;
	DX12BufferResourceSummary summary = {};
	UINT64 rangeEpoch = 0;
};

struct DX12BufferResolveCacheInvalidationStats
{
	size_t removedEntries = 0;
	bool rangeChanged = false;
};

struct DX12BufferResolveInvalidatedRange
{
	UINT64 epoch = 0;
	UINT64 begin = 0;
	UINT64 end = 0;
};

constexpr size_t kDX12BufferResolveInvalidationHistoryCapacity = 4096;

struct PsoRootRecord
{
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

extern SRWLOCK gResourceLock;
extern SRWLOCK gDescriptorLock;
extern std::vector<RootSignatureRecord> gRootSignatures;
extern std::deque<DescriptorHeapRecord> gDescriptorHeaps;
extern std::unordered_map<ID3D12RootSignature*, size_t> gRootSignatureByPtr;
extern std::unordered_map<ID3D12DescriptorHeap*, size_t> gDescriptorHeapByPtr;
extern std::map<SIZE_T, size_t> gDescriptorHeapByCpuStart;
extern std::unordered_map<SIZE_T, DescriptorRecord> gDescriptorByCpuHandle;
// Heap slots are the single source of truth; the old CPU-handle map was removed.
// Use DX12DescriptorHeapShadowUtils::FindLatestDescriptor instead.
extern std::vector<PsoRootRecord> gPsoRoots;
extern std::unordered_map<UINT64, size_t> gPsoRootByIndex;
extern std::vector<ResourceRecord> gResources;
extern std::vector<size_t> gFreeResourceSlots;
extern size_t gLiveResourceCount;
extern std::unordered_map<ID3D12Resource*, size_t> gResourceByPtr;
extern std::map<UINT64, size_t> gBufferResourceByGpuVa;
extern std::map<UINT64, BufferResolveCacheEntry> gBufferResolveCache;
// Fast GPU VA → ResourceWrapper index: sorted vector for cache-friendly
// binary search.  Stores a direct pointer to the wrapper's cached
// ResourceRecord so the hot lookup path never needs the wrapper type.
struct GpuVaWrapperEntry {
	UINT64 gpuVa;
	void *wrapper;
	const ResourceRecord *record;
};
extern std::vector<GpuVaWrapperEntry> gGpuVaWrappers;
extern SRWLOCK gGpuVaWrappersLock;
void RegisterGpuVaWrapper(UINT64 gpuVa, void *wrapper, const ResourceRecord *record);
void UnregisterGpuVaWrapper(UINT64 gpuVa, void *wrapper);
// Returns the cached ResourceRecord for a resource that has an active wrapper,
// or nullptr.  Skips the gResourceByPtr hash map and gResources array lookup.
ResourceRecord *DX12FindWrapperResourceRecord(ID3D12Resource *resource);
// IA-hash reuse cache stored on ResourceWrapper.  Returns true and fills
// *hash on a full match (view params + generations), false otherwise.
bool DX12TryGetWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 contentGeneration, UINT64 resolveGeneration, uint32_t *hash);
void DX12StoreWrapperIaHash(
	UINT64 gpuVa, UINT64 size, UINT stride, UINT slot,
	UINT64 contentGeneration, UINT64 resolveGeneration, uint32_t hash);
extern std::atomic<UINT64> gBufferResolveGeneration;
extern std::atomic<UINT64> gResourceIdentityGeneration;
extern std::atomic<UINT64> gDescriptorQuerySerial;
extern bool gCleanupRegistered;
extern UINT64 gResourcesRecordedFromCreate;
extern UINT64 gDescriptorRecordsSeen;
extern UINT64 gDescriptorCopyRecordsSeen;
extern UINT64 gDescriptorHeapRecordsSeen;
extern UINT64 gRootSignatureRecordsSeen;
extern UINT64 gPsoRootRecordsSeen;
extern UINT64 gDescriptorEpoch;
extern LONG gLastResourceStatsPresent;
extern UINT64 gBufferResolveCacheInvalidations;
extern UINT64 gBufferResolveCacheInvalidationEntries;
extern UINT64 gBufferResolveTlsHits;
extern UINT64 gBufferResolveGlobalHits;
extern UINT64 gBufferResolveBackwardScanHits;
extern UINT64 gBufferResolveFallbackScanHits;
extern UINT64 gBufferResolveMisses;
extern UINT64 gBufferResolveGenerationResets;
extern UINT64 gBufferResolveRangeInvalidations;
extern UINT64 gBufferResolveRangeInvalidationEntries;
extern UINT64 gBufferResolveIndexNoopUpdates;
extern UINT64 gBufferResolveStateOnlyUpdates;
extern UINT64 gBufferResolveInvalidationEpoch;
extern std::atomic<UINT64> gBufferResolveInvalidationEpochAtomic;
extern UINT64 gBufferResolveInvalidationHistoryLost;
extern DX12BufferResolveInvalidatedRange gBufferResolveInvalidatedRanges[kDX12BufferResolveInvalidationHistoryCapacity];
extern UINT64 gResourceExclusiveLockSlowCount;
extern UINT64 gResourceExclusiveLockSlowMaxUs;
extern UINT64 gDescriptorExclusiveLockSlowCount;
extern UINT64 gDescriptorExclusiveLockSlowMaxUs;

class DX12BufferResolveCacheUtils
{
public:
static bool RangesOverlap(UINT64 beginA, UINT64 endA, UINT64 beginB, UINT64 endB)
{
	return beginA < endB && beginB < endA;
}

static UINT64 CurrentInvalidationEpochLocked()
{
	return gBufferResolveInvalidationEpoch;
}

static UINT64 NormalizeRangeEnd(UINT64 begin, UINT64 end)
{
	if (end > begin)
		return end;
	return UINT64_MAX;
}

static bool SameBufferRange(const ResourceRecord &a, const ResourceRecord &b)
{
	return a.gpuVirtualAddress == b.gpuVirtualAddress &&
		a.size == b.size &&
		a.gpuVirtualAddressRealEnd == b.gpuVirtualAddressRealEnd &&
		a.gpuVirtualAddressOobEnd == b.gpuVirtualAddressOobEnd &&
		a.desc.Dimension == b.desc.Dimension;
}

static bool HasBufferRange(const ResourceRecord &record)
{
	return record.resource &&
		record.desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		record.gpuVirtualAddress != 0 &&
		record.size != 0;
}

static UINT64 RangeEnd(const ResourceRecord &record)
{
	if (record.gpuVirtualAddressRealEnd)
		return record.gpuVirtualAddressRealEnd;
	return record.gpuVirtualAddress + record.size;
}

static size_t PurgeRangeLocked(UINT64 begin, UINT64 end)
{
	if (!begin || end <= begin)
		return 0;

	size_t removed = 0;
	// gBufferResolveCache is keyed by entry begin address.
	// Entries overlapping [begin, end) must satisfy: key < end AND entry.end > begin.
	// Find the entry with the largest key that is <= begin, then iterate forward
	// until key >= end (entries starting at or past 'end' cannot overlap).
	std::map<UINT64, BufferResolveCacheEntry>::iterator it =
		gBufferResolveCache.lower_bound(begin);
	if (it != gBufferResolveCache.begin())
		--it;
	while (it != gBufferResolveCache.end() && it->first < end) {
		if (DX12BufferResolveCacheUtils::RangesOverlap(
				it->second.begin, it->second.end, begin, end)) {
			it = gBufferResolveCache.erase(it);
			removed++;
		} else {
			++it;
		}
	}
	return removed;
}

static void AppendInvalidatedRangeLocked(UINT64 epoch, UINT64 begin, UINT64 end)
{
	if (!epoch || !begin || end <= begin)
		return;
	const size_t slot = static_cast<size_t>(
		epoch % static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity));
	gBufferResolveInvalidatedRanges[slot].epoch = epoch;
	gBufferResolveInvalidatedRanges[slot].begin = begin;
	gBufferResolveInvalidatedRanges[slot].end = end;
}

static bool RangeInvalidatedSinceLocked(UINT64 begin, UINT64 end, UINT64 sinceEpoch)
{
	if (!begin)
		return true;
	end = DX12BufferResolveCacheUtils::NormalizeRangeEnd(begin, end);
	const UINT64 currentEpoch = gBufferResolveInvalidationEpoch;
	if (sinceEpoch == currentEpoch)
		return false;
	if (sinceEpoch > currentEpoch)
		return true;
	if (currentEpoch - sinceEpoch >=
		static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity)) {
		gBufferResolveInvalidationHistoryLost++;
		return true;
	}

	for (UINT64 epoch = sinceEpoch + 1; epoch <= currentEpoch; ++epoch) {
		const size_t slot = static_cast<size_t>(
			epoch % static_cast<UINT64>(kDX12BufferResolveInvalidationHistoryCapacity));
		const DX12BufferResolveInvalidatedRange &range = gBufferResolveInvalidatedRanges[slot];
		if (range.epoch != epoch) {
			gBufferResolveInvalidationHistoryLost++;
			return true;
		}
		if (DX12BufferResolveCacheUtils::RangesOverlap(begin, end, range.begin, range.end))
			return true;
	}
	return false;
}

static UINT64 BumpInvalidationEpochLocked()
{
	gBufferResolveInvalidationEpoch++;
	if (!gBufferResolveInvalidationEpoch)
		gBufferResolveInvalidationEpoch = 1;
	gBufferResolveInvalidationEpochAtomic.store(
		gBufferResolveInvalidationEpoch, std::memory_order_release);
	gBufferResolveGeneration.fetch_add(1, std::memory_order_relaxed);
	return gBufferResolveInvalidationEpoch;
}

static DX12BufferResolveCacheInvalidationStats InvalidateResourceRangeChangeLocked(
	const ResourceRecord *oldRecord,
	const ResourceRecord *newRecord,
	const char *reason,
	ID3D12Resource *resource)
{
	DX12BufferResolveCacheInvalidationStats stats;
	const bool oldHasRange = oldRecord && DX12BufferResolveCacheUtils::HasBufferRange(*oldRecord);
	const bool newHasRange = newRecord && DX12BufferResolveCacheUtils::HasBufferRange(*newRecord);
	if (oldHasRange && newHasRange &&
		DX12BufferResolveCacheUtils::SameBufferRange(*oldRecord, *newRecord)) {
		gBufferResolveIndexNoopUpdates++;
		return stats;
	}

	stats.rangeChanged = oldHasRange || newHasRange;
	if (!stats.rangeChanged) {
		gBufferResolveIndexNoopUpdates++;
		return stats;
	}

	if (oldHasRange)
		stats.removedEntries += DX12BufferResolveCacheUtils::PurgeRangeLocked(
			oldRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*oldRecord));
	if (newHasRange)
		stats.removedEntries += DX12BufferResolveCacheUtils::PurgeRangeLocked(
			newRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*newRecord));
	if (!oldHasRange && stats.removedEntries == 0) {
		gBufferResolveIndexNoopUpdates++;
		DX12LogManager::DebugFields("BufferResolveCacheInvalidated",
			"\"reason\":\"%s\",\"mode\":\"range_noop_new\",\"present\":%ld,\"threadId\":%lu,"
			"\"resource\":\"%p\",\"newBegin\":\"0x%llx\",\"newEnd\":\"0x%llx\","
			"\"gpuVaIndexSize\":%zu,\"liveResources\":%zu",
			reason ? reason : "",
			DX12RuntimeState::GetPresentCount(),
			GetCurrentThreadId(),
			resource,
			newHasRange ? static_cast<unsigned long long>(newRecord->gpuVirtualAddress) : 0ull,
			newHasRange ? static_cast<unsigned long long>(DX12BufferResolveCacheUtils::RangeEnd(*newRecord)) : 0ull,
			gBufferResourceByGpuVa.size(),
			gLiveResourceCount);
		return stats;
	}

	const UINT64 invalidationEpoch =
		DX12BufferResolveCacheUtils::BumpInvalidationEpochLocked();
	if (oldHasRange)
		DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(
			invalidationEpoch,
			oldRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*oldRecord));
	if (newHasRange)
		DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(
			invalidationEpoch,
			newRecord->gpuVirtualAddress,
			DX12BufferResolveCacheUtils::RangeEnd(*newRecord));
	gBufferResolveCacheInvalidations++;
	gBufferResolveCacheInvalidationEntries += stats.removedEntries;
	gBufferResolveRangeInvalidations++;
	gBufferResolveRangeInvalidationEntries += stats.removedEntries;
	DX12LogManager::DebugFields("BufferResolveCacheInvalidated",
		"\"reason\":\"%s\",\"mode\":\"range\",\"present\":%ld,\"threadId\":%lu,"
		"\"entriesRemoved\":%zu,\"generation\":%llu,\"resource\":\"%p\","
		"\"oldBegin\":\"0x%llx\",\"oldEnd\":\"0x%llx\",\"newBegin\":\"0x%llx\",\"newEnd\":\"0x%llx\","
		"\"gpuVaIndexSize\":%zu,\"liveResources\":%zu",
		reason ? reason : "",
		DX12RuntimeState::GetPresentCount(),
		GetCurrentThreadId(),
		stats.removedEntries,
		static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
		resource,
		oldHasRange ? static_cast<unsigned long long>(oldRecord->gpuVirtualAddress) : 0ull,
		oldHasRange ? static_cast<unsigned long long>(DX12BufferResolveCacheUtils::RangeEnd(*oldRecord)) : 0ull,
		newHasRange ? static_cast<unsigned long long>(newRecord->gpuVirtualAddress) : 0ull,
		newHasRange ? static_cast<unsigned long long>(DX12BufferResolveCacheUtils::RangeEnd(*newRecord)) : 0ull,
		gBufferResourceByGpuVa.size(),
		gLiveResourceCount);
	return stats;
}

static void InvalidateAllLocked(const char *reason)
{
	const size_t cacheEntriesBeforeClear = gBufferResolveCache.size();
	gBufferResolveCache.clear();
	const UINT64 invalidationEpoch = DX12BufferResolveCacheUtils::BumpInvalidationEpochLocked();
	DX12BufferResolveCacheUtils::AppendInvalidatedRangeLocked(invalidationEpoch, 1, UINT64_MAX);
	gBufferResolveCacheInvalidations++;
	gBufferResolveCacheInvalidationEntries += cacheEntriesBeforeClear;
	gBufferResolveGenerationResets++;
	DX12LogManager::DebugFields("BufferResolveCacheInvalidated",
		"\"reason\":\"%s\",\"mode\":\"global\",\"present\":%ld,\"threadId\":%lu,"
		"\"cacheEntriesBeforeClear\":%zu,\"generation\":%llu,\"gpuVaIndexSize\":%zu,\"liveResources\":%zu",
		reason ? reason : "",
		DX12RuntimeState::GetPresentCount(),
		GetCurrentThreadId(),
		cacheEntriesBeforeClear,
		static_cast<unsigned long long>(gBufferResolveGeneration.load(std::memory_order_relaxed)),
		gBufferResourceByGpuVa.size(),
		gLiveResourceCount);
}

static void RecordStateOnlyUpdateLocked()
{
	gBufferResolveStateOnlyUpdates++;
}
};

uint32_t HashBytes(uint32_t seed, const void *data, size_t size);
UINT64 Fnv1a64(const void *data, size_t size);
constexpr uint32_t MakeTrackerFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}
constexpr bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
{
	return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
		dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
}
D3D12_RESOURCE_DESC ResourceDescFromDesc1(const D3D12_RESOURCE_DESC1 *desc);
ID3D12Resource *CanonicalResource(ID3D12Resource *resource);
void RecordResource(
	ID3D12Resource *resource,
	const D3D12_RESOURCE_DESC *desc,
	const D3D12_HEAP_PROPERTIES *heapProperties,
	const PlacedResourceInfo *placedInfo,
	D3D12_RESOURCE_STATES initialState);
void CleanupTrackedResources();
void ParseRootSignatureBlob(RootSignatureRecord *record, const void *blob, SIZE_T blobLength);
void FillResourceInfo(DescriptorRecord *record, ID3D12Resource *resource);
void FillResourcePointerInfo(DescriptorRecord *record, ID3D12Resource *resource);
void CaptureDescriptorResourcePointer(DescriptorRecord *record, ID3D12Resource *resource);
void ReleaseDescriptorRecordResources(DescriptorRecord *record);
DescriptorRecord CloneDescriptorRecord(const DescriptorRecord &record);
void StoreCpuDescriptorRecordLocked(DescriptorRecord &&record);
bool ResolveBufferByGpuVa(UINT64 gpuVa, UINT64 size, DescriptorRecord *record);
void SetDescriptorKind(DescriptorRecord *record, UINT kindId);
void FillSrvBufferView(DescriptorRecord *record);
void FillUavBufferView(DescriptorRecord *record);
void PrepareDescriptorValueId(DescriptorRecord *record);
bool DescriptorValueUnchanged(SIZE_T cpuHandle, UINT64 valueId);
void LogResourceTrackerStatsLocked();
void InvalidateDescriptorHeapLocked(ID3D12DescriptorHeap *heap);
void InvalidateResourceLocked(ID3D12Resource *resource);
void InitializeDescriptorHeapRecord(DescriptorHeapRecord *record);
bool EnsureDescriptorHeapStorageLocked(DescriptorHeapRecord *record);
bool ResolveDescriptorSlotForCpuHandleLocked(
	SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE *requiredType,
	DescriptorSlotLocation *location, UINT *slotsAvailable);
void RecordDescriptor(DescriptorRecord &&record);
// RecordDescriptorMerged replaced by DX12DescriptorHeapShadowUtils::WriteDescriptorToSlot
// + WriteToCpuLedger (dual-path: heap-slot fast path or CPU ledger fallback)
void RecordDescriptorBatch(std::vector<DescriptorRecord> *records);
void RecordRuntimeDescriptor(DescriptorRecord &&record);
void DX12RecordRootSignatureImmediate(
	ID3D12RootSignature *rootSignature,
	UINT nodeMask,
	const void *blob,
	SIZE_T blobLength);
void DX12PrimeDescriptorHistoryForHunting();
void BumpDescriptorQuerySerial();
// RecordDescriptorCopyRange and RecordRuntimeDescriptorCopyRange replaced by
// DX12DescriptorHeapShadowUtils::CopyDescriptorRange
void DX12RecordPsoRootSignatureImmediate(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
