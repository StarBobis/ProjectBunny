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

// Normal Mod mode stores one DescriptorRecord per occupied slot directly in the
// heap's chunked dense store. Descriptor copies then move the record with a
// plain field copy (mirroring RenderDoc's dst.CopyFrom(src)) instead of
// duplicating an immutable value node per copy.
struct DescriptorHeapRecord
{
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
	// 64 shard locks: descriptor writes are short and command recording
	// normally has only a handful of producer threads, while sequential
	// descriptor rings touch a different stripe for virtually every slot.
	using StateLocks = DX12DescriptorHeapShardState<64>;
	// Lock striping follows the heap record when gDescriptorHeaps grows or
	// reuses a slot. Sequential descriptor slots land in different shards.
	std::unique_ptr<StateLocks> stateLocks;
	// Chunked dense current-value storage: the primary store in normal Mod
	// mode. Chunk bodies are allocated lazily, so multi-million-slot heaps
	// only pay for the regions the game actually writes.
	DX12ChunkedDescriptorStore<DescriptorRecord> chunkedSlots;
	// Whole-heap dense compatibility adapter, materialized only when
	// Dump/Hunt tracking needs the full descriptor history. The chunked store
	// stays alive (and stale) after materialization; every access prefers the
	// materialized vectors once they exist.
	std::vector<DescriptorRecord> slots;
	std::vector<uint8_t> occupied;
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
		const DescriptorRecord *value = record->chunkedSlots.Find(slot);
		if (value)
			valueId = value->valueId;
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

