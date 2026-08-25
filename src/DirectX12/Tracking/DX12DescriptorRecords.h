// Descriptor value records, pools and heap state utils.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12DescriptorHeapShard.h"

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
	// Physical descriptor bytes may have been replaced while this record keeps
	// the game's original logical resource for matching and diagnostics.
	UINT64 staticReplacementGeneration = 0;
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
	UINT64 staticReplacementGeneration = 0;
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

