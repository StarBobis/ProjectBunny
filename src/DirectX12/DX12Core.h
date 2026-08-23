// ============================================================
// DX12Core.h - DX12 layered module header.
// Split from the merged DX12.h; keep declarations in the
// layer that owns them. Later layers include earlier ones.
// ============================================================
#pragma once

#include <Windows.h>
#include <Shlwapi.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <share.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <deque>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


class DX12PsoInputElementInfo
{
public:
	std::string semanticName;
	UINT semanticIndex = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	UINT inputSlot = 0;
	UINT alignedByteOffset = 0;
	bool appendAlignedElement = false;
	UINT resolvedByteOffset = 0;
	D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	UINT instanceDataStepRate = 0;
};

class DX12PsoInputLayoutInfo
{
public:
	UINT64 psoIndex = 0;
	std::vector<DX12PsoInputElementInfo> elements;
};

class DX12PsoShaderInfo
{
public:
	UINT64 psoIndex = 0;
	bool hasVS = false;
	bool hasPS = false;
	bool hasCS = false;
	UINT64 vs = 0;
	UINT64 ps = 0;
	UINT64 cs = 0;
	std::string vsModel;
	std::string psModel;
	std::string csModel;
};

class DX12PsoShaderSummary
{
public:
	UINT64 psoIndex = 0;
	bool hasVS = false;
	bool hasPS = false;
	bool hasCS = false;
	UINT64 vs = 0;
	UINT64 ps = 0;
	UINT64 cs = 0;
	std::string vsModel;
	std::string psModel;
	std::string csModel;
};



class DX12RootDescriptorRangeSummary
{
public:
	UINT rootParameterIndex = 0;
	UINT rangeIndex = 0;
	UINT rangeType = 0;
	UINT numDescriptors = 0;
	UINT baseShaderRegister = 0;
	UINT registerSpace = 0;
	UINT offsetInDescriptorsFromTableStart = 0;
	UINT flags = 0;
	UINT effectiveOffset = 0;
	UINT shaderVisibility = 0;
};

class DX12RootParameterSummary
{
public:
	UINT rootParameterIndex = 0;
	UINT parameterType = 0;
	UINT shaderVisibility = 0;
	UINT shaderRegister = 0;
	UINT registerSpace = 0;
	UINT rootDescriptorFlags = 0;
	UINT num32BitValues = 0;
	std::vector<DX12RootDescriptorRangeSummary> ranges;
};

class DX12RootSignatureSummary
{
public:
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

class DescriptorKind
{
public:
	static constexpr std::string_view CBV = "CBV";
	static constexpr std::string_view SRV = "SRV";
	static constexpr std::string_view UAV = "UAV";
	static constexpr std::string_view RTV = "RTV";
	static constexpr std::string_view DSV = "DSV";
	static constexpr std::string_view Sampler = "Sampler";
	static constexpr UINT CBV_ID = 1;
	static constexpr UINT SRV_ID = 2;
	static constexpr UINT UAV_ID = 3;
	static constexpr UINT RTV_ID = 4;
	static constexpr UINT DSV_ID = 5;
	static constexpr UINT Sampler_ID = 6;
	static constexpr std::string_view CONSTANT_BUFFER_VIEW = "CONSTANT_BUFFER_VIEW";
	static constexpr std::string_view SHADER_RESOURCE_VIEW = "SHADER_RESOURCE_VIEW";
	static constexpr std::string_view UNORDERED_ACCESS_VIEW = "UNORDERED_ACCESS_VIEW";
	static constexpr std::string_view RENDER_TARGET_VIEW = "RENDER_TARGET_VIEW";
	static constexpr std::string_view DEPTH_STENCIL_VIEW = "DEPTH_STENCIL_VIEW";
};

class IaBufferRole
{
public:
	static constexpr std::string_view IB = "IB";
	static constexpr std::string_view VB = "VB";
};

class DX12DescriptorSummary
{
public:
	std::string kind;
	SIZE_T cpuHandle = 0;
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *counterResource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	// Hunt/override content hash cached at descriptor write time; 0 = not
	// cached (the draw path recomputes).
	uint32_t huntHash = 0;
	UINT currentState = 0;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool hasDesc = false;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	bool hasSrvDesc = false;
	UINT viewFormat = 0;
	UINT viewDimension = 0;
	UINT cbvSize = 0;
	UINT64 firstElement = 0;
	UINT numElements = 0;
	UINT structureByteStride = 0;
	UINT64 bufferViewOffset = 0;
	UINT64 bufferViewBytes = 0;
};

class DX12DescriptorHeapSummary
{
public:
	ID3D12DescriptorHeap *heap = nullptr;
	UINT type = 0;
	UINT numDescriptors = 0;
	UINT flags = 0;
	UINT nodeMask = 0;
	SIZE_T cpuStart = 0;
	UINT64 gpuStart = 0;
	UINT increment = 0;
	UINT64 version = 0;
};

class DX12PsoRootSummary
{
public:
	UINT64 psoIndex = 0;
	std::string kind;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *rootSignature = nullptr;
};

class DX12ResourcePublishedState;

class DX12BufferResourceSummary
{
public:
	ID3D12Resource *resource = nullptr;
	// Monotonic Resource Identity generation. Unlike a COM pointer, this token
	// changes when an address is reused or the tracked GPUVA range changes.
	UINT64 resourceGeneration = 0;
	DX12ResourcePublishedState *publishedState = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 resourceOffset = 0;
	UINT64 viewSize = 0;
	UINT currentState = 0;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
};

class DX12ResourceSummary
{
public:
	ID3D12Resource *resource = nullptr;
	D3D12_RESOURCE_DESC resourceDesc = {};
	bool hasResourceDesc = false;
	UINT resourceHeapType = 0;
	bool hasResourceHeapType = false;
	UINT64 gpuVirtualAddress = 0;
	UINT64 size = 0;
	UINT currentState = 0;
	bool hasCurrentState = false;
	bool stateTrackedByBarrier = false;
	bool retained = false;
};





class DX12FrameResourceBinding
{
public:
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo;
	std::string bindSpace;
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT rangeType = UINT_MAX;
	UINT shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	bool rootDescriptor = false;
	ID3D12DescriptorHeap *heap = nullptr;
	UINT heapType = 0;
	UINT64 descriptorIndex = 0;
	UINT64 gpuHandle = 0;
	SIZE_T cpuHandle = 0;
	UINT64 heapGpuStart = 0;
	UINT64 gpuVirtualAddress = 0;
	std::string usageClass;
	UINT64 usageBegin = 0;
	UINT64 usageBytes = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

class DX12FrameIaBufferBinding
{
public:
	std::string bufferId;
	std::string role;
	std::string skinningClass;
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo;
	UINT64 producerEventSerial = 0;
	UINT64 producerDrawId = 0;
	UINT64 producerDispatchId = 0;
	UINT64 producerPsoIndex = 0;
	DX12PsoShaderInfo producerShaderInfo;
	std::string producerBindSpace;
	UINT producerRootParameterIndex = 0;
	UINT producerShaderRegister = UINT_MAX;
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewGpuVa = 0;
	UINT64 viewSize = 0;
	UINT64 viewOffset = 0;
	UINT64 drawGpuVa = 0;
	UINT64 drawSize = 0;
	UINT64 drawOffset = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	UINT drawStartIndex = 0;
	UINT drawIndexCount = 0;
	INT drawBaseVertex = 0;
	UINT drawStartInstance = 0;
	UINT drawInstanceCount = 0;
	D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	DX12PsoInputLayoutInfo inputLayout;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource;
};

class DX12FrameOutputBinding
{
public:
	std::string kind;
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

class DX12CurrentIaBuffer
{
public:
	std::string role;
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource;
};

class DX12CurrentIaState
{
public:
	std::vector<DX12CurrentIaBuffer> vertexBuffers;
	DX12CurrentIaBuffer indexBuffer;
	bool hasIndexBuffer = false;
};

class DX12CurrentComputeUavBinding
{
public:
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	UINT descriptorIncrement = 0;
	UINT tableCopyCount = 0;
	SIZE_T tableCpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE tableGpuStart = {};
	ID3D12DescriptorHeap *tableHeap = nullptr;
	bool rootDescriptor = false;
	UINT64 gpuVirtualAddress = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

using DX12CurrentShaderResourceBinding = DX12CurrentComputeUavBinding;

class DX12CurrentRootConstants
{
public:
	UINT rootParameterIndex = 0;
	UINT maxSet = 0;
	UINT values[64] = {};
	bool valueValid[64] = {};
};





constexpr UINT MaxRootParameters = 64;
constexpr UINT MaxVertexBufferSlots = 32;
constexpr UINT MaxRenderTargetSlots = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;

class RootTableState
{
public:
	bool valid = false;
	UINT rootParameterIndex = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor = {};
	bool runtimeHeapResolved = false;
	ID3D12DescriptorHeap *runtimeHeap = nullptr;
	UINT runtimeHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	SIZE_T runtimeHeapCpuStart = 0;
	UINT64 runtimeHeapGpuStart = 0;
	UINT runtimeHeapIncrement = 0;
	UINT runtimeHeapNumDescriptors = 0;
	UINT64 runtimeHeapVersion = 0;
	UINT64 runtimeTableBaseIndex = 0;
	SIZE_T runtimeTableCpuStart = 0;
	UINT64 runtimeTableGpuStart = 0;
};

class RootDescriptorState
{
public:
	bool valid = false;
	UINT rootParameterIndex = 0;
	D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_CBV;
	D3D12_GPU_VIRTUAL_ADDRESS address = 0;
};

class RootConstantsState
{
public:
	bool valid = false;
	UINT rootParameterIndex = 0;
	UINT maxSet = 0;
	UINT values[64] = {};
	bool valueValid[64] = {};
};

class CommandListBindingState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	DX12PsoShaderInfo pipelineShaderInfo = {};
	bool pipelineShaderInfoValid = false;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	bool runtimeSamplerHeapResolved = false;
	DX12DescriptorHeapSummary runtimeSamplerHeap = {};
	UINT64 computeBindingSerial = 0;
	UINT64 bindingQuerySerial = 1;
	UINT graphicsTableValidCount = 0;
	UINT computeTableValidCount = 0;
	RootTableState graphicsTables[MaxRootParameters] = {};
	RootTableState computeTables[MaxRootParameters] = {};
	RootDescriptorState graphicsRootDescriptors[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState graphicsRootConstants[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[MaxVertexBufferSlots] = {};
	bool vertexBufferValid[MaxVertexBufferSlots] = {};
	DX12BufferResourceSummary vertexBufferResources[MaxVertexBufferSlots] = {};
	bool vertexBufferResolved[MaxVertexBufferSlots] = {};
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferValid = false;
	DX12BufferResourceSummary indexBufferResource = {};
	bool indexBufferResolved = false;
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[MaxRenderTargetSlots] = {};
	bool renderTargetValid[MaxRenderTargetSlots] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE depthStencil = {};
	bool depthStencilValid = false;
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	UINT64 drawSerial = 0;
	UINT64 dispatchSerial = 0;
};

class BindingEvent
{
public:
	UINT64 serial = 0;
	UINT64 captureSessionId = 0;
	std::string kind;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 descriptorEpoch = 0;
	ID3D12GraphicsCommandList *commandList = nullptr;
	ID3D12PipelineState *pipelineState = nullptr;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	DX12PsoShaderInfo shaderInfo = {};
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	RootTableState graphicsTables[MaxRootParameters] = {};
	RootTableState computeTables[MaxRootParameters] = {};
	RootDescriptorState graphicsRootDescriptors[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState graphicsRootConstants[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[MaxVertexBufferSlots] = {};
	bool vertexBufferValid[MaxVertexBufferSlots] = {};
	DX12BufferResourceSummary vertexBufferResources[MaxVertexBufferSlots] = {};
	bool vertexBufferResolved[MaxVertexBufferSlots] = {};
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferValid = false;
	DX12BufferResourceSummary indexBufferResource = {};
	bool indexBufferResolved = false;
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	UINT vertexCountPerInstance = 0;
	UINT indexCountPerInstance = 0;
	UINT instanceCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;
	UINT startInstanceLocation = 0;
	UINT threadGroupCountX = 0;
	UINT threadGroupCountY = 0;
	UINT threadGroupCountZ = 0;
};

class OutputBindingEvent
{
public:
	UINT64 serial = 0;
	UINT64 captureSessionId = 0;
	std::string kind;
	ID3D12GraphicsCommandList *commandList = nullptr;
	ID3D12PipelineState *pipelineState = nullptr;
	DX12PsoShaderInfo shaderInfo = {};
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
};

enum class CommandListRecordedEventKind
{
	Binding,
	Output,
};

class CommandListRecordedEvent
{
public:
	CommandListRecordedEventKind kind = CommandListRecordedEventKind::Binding;
	BindingEvent binding;
	OutputBindingEvent output;
};

constexpr size_t kBindingShardCount = 64;

class BindingShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> states;
	std::unordered_map<ID3D12GraphicsCommandList*, std::vector<CommandListRecordedEvent>> recordedEvents;
	volatile UINT64 generation = 0;
};

// Per-thread read cache for binding tracker: avoids SRWLOCK on repeated identical calls.
// Key insight: D3D12 command lists are single-threaded, so TLS cache is safe.
class BindingTlsTableCache
{
public:
	ID3D12GraphicsCommandList *cl = nullptr;
	bool valid[MaxRootParameters] = {};
	UINT64 gpuHandle[MaxRootParameters] = {};
};

class BindingTlsDescriptorCache
{
public:
	ID3D12GraphicsCommandList *cl = nullptr;
	bool valid[MaxRootParameters] = {};
	UINT64 address[MaxRootParameters] = {};
	D3D12_ROOT_PARAMETER_TYPE type[MaxRootParameters] = {};
};

class BindingTlsDescriptorQueryCache
{
public:
	static constexpr UINT CacheSlotsPerPipeline = 3;
	static constexpr UINT CacheSlotCount = CacheSlotsPerPipeline * 2;

	static UINT CacheSlotIndex(bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType)
	{
		UINT rangeIndex = UINT_MAX;
		switch (rangeType) {
		case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
			rangeIndex = 0;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			rangeIndex = 1;
			break;
		case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			rangeIndex = 2;
			break;
		default:
			return UINT_MAX;
		}
		return (compute ? CacheSlotsPerPipeline : 0) + rangeIndex;
	}

	ID3D12GraphicsCommandList *commandList = nullptr;
	bool compute = false;
	UINT rangeType = UINT_MAX;
	UINT64 bindingQuerySerial = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	std::vector<DX12CurrentComputeUavBinding> bindings;
};

class BindingTlsComputeAllQueryCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	UINT64 computeTableHash = 0;
	UINT64 bindingQuerySerial = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> constants;
};

// TLS cache for direct CommandListBindingState pointer lookup.
// Eliminates ShardFor() hash + AcquireSRWLockExclusive + HashMap::operator[]
// on every state-mutation call (SetVB, SetIB, SetPSO, SetRootSig, etc.).
// Key insight: D3D12 command lists are strictly single-threaded during recording,
// so treating the unordered_map element pointer as stable within a single
// recording session is safe. Generation counter guards against unordered_map
// rehash (which can invalidate pointers) triggered by RegisterCommandList on
// other threads.
class BindingTlsStateCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	CommandListBindingState *state = nullptr;
	UINT64 shardGeneration = 0;
	size_t shardIndex = 0;
};



class DX12BindingTrackerState
{
public:
	static BindingShard Shards[kBindingShardCount];
	static volatile LONG64 GlobalDrawSerial;
	static volatile LONG64 GlobalDispatchSerial;

	static BindingShard &ShardFor(ID3D12GraphicsCommandList *commandList)
	{
		uintptr_t key = reinterpret_cast<uintptr_t>(commandList);
		key ^= key >> 7;
		key *= 0x9e3779b97f4a7c15ull;
		return Shards[(key >> 17) & (kBindingShardCount - 1)];
	}
};


class DX12FrameAnalysisDumpResult
{
public:
	bool ok = false;
	size_t written = 0;
	size_t skipped = 0;
	size_t failed = 0;
	const char *reason = "";
};


uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferContentView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);

class DX12IaRegionDraw
{
public:
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT startVertexLocation = 0;
	UINT startIndexLocation = 0;
	INT baseVertexLocation = 0;

	static DX12IaRegionDraw ForDrawInstanced(UINT vertexCount, UINT startVertexLocation)
	{
		DX12IaRegionDraw draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.startVertexLocation = startVertexLocation;
		return draw;
	}

	static DX12IaRegionDraw ForDrawIndexedInstanced(
		UINT indexCount, UINT startIndexLocation, INT baseVertexLocation)
	{
		DX12IaRegionDraw draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.startIndexLocation = startIndexLocation;
		draw.baseVertexLocation = baseVertexLocation;
		return draw;
	}

	static DX12IaRegionDraw FromDrawArguments(
		bool indexed, UINT vertexCount, UINT indexCount, UINT startVertexLocation,
		UINT startIndexLocation, INT baseVertexLocation)
	{
		return indexed ?
			ForDrawIndexedInstanced(indexCount, startIndexLocation, baseVertexLocation) :
			ForDrawInstanced(vertexCount, startVertexLocation);
	}

	UINT EstimatedVertexCountForRegionHash() const
	{
		if (vertexCount)
			return vertexCount;
		if (!indexCount)
			return 0;
		return (3 * indexCount + 10) / 20 + 3;
	}

	UINT FirstVertexForRegionHash() const
	{
		if (!indexed)
			return startVertexLocation;
		// D3D12 BaseVertexLocation is signed; negative values move indexed reads
		// before index 0, so hashing must not reinterpret them as huge UINT offsets.
		if (baseVertexLocation <= 0)
			return 0;
		return static_cast<UINT>(baseVertexLocation);
	}
};

class DX12IaRegion
{
public:
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewOffset = 0;
	uint32_t hash = 0;

	bool SetFromViewRange(UINT64 viewGpuVa, UINT64 viewBytes, UINT64 offset, UINT64 bytes)
	{
		*this = DX12IaRegion();
		if (!viewGpuVa || !viewBytes || !bytes)
			return false;
		if (offset >= viewBytes)
			return false;
		if (bytes > viewBytes - offset)
			bytes = viewBytes - offset;
		if (!bytes)
			return false;
		gpuVa = viewGpuVa + offset;
		size = bytes;
		viewOffset = offset;
		return true;
	}

	bool SetFromIndexBufferView(
		const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		const UINT stride = IndexFormatStride(view.Format);
		return SetFromViewRange(
			view.BufferLocation,
			view.SizeInBytes,
			static_cast<UINT64>(stride) * draw.startIndexLocation,
			static_cast<UINT64>(stride) * draw.indexCount);
	}

	bool SetFromVertexBufferView(
		UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		(void)slot;
		if (!view.StrideInBytes)
			return false;

		return SetFromViewRange(
			view.BufferLocation,
			view.SizeInBytes,
			static_cast<UINT64>(view.StrideInBytes) * draw.FirstVertexForRegionHash(),
			static_cast<UINT64>(view.StrideInBytes) * draw.EstimatedVertexCountForRegionHash());
	}

	uint32_t HashIndexBuffer(UINT format)
	{
		hash = DX12HashIaBufferContentView(gpuVa, size, 0, format, 0);
		return hash;
	}

	uint32_t HashVertexBuffer(UINT stride, UINT slot)
	{
		hash = DX12HashIaBufferContentView(gpuVa, size, stride, 0, slot);
		return hash;
	}

	static uint32_t HashIndexBufferView(
		const D3D12_INDEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		DX12IaRegion region;
		if (!region.SetFromIndexBufferView(view, draw))
			return 0;
		return region.HashIndexBuffer(static_cast<UINT>(view.Format));
	}

	static uint32_t HashVertexBufferView(
		UINT slot, const D3D12_VERTEX_BUFFER_VIEW &view, const DX12IaRegionDraw &draw)
	{
		DX12IaRegion region;
		if (!region.SetFromVertexBufferView(slot, view, draw))
			return 0;
		return region.HashVertexBuffer(view.StrideInBytes, slot);
	}

	static UINT IndexFormatStride(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_R32_UINT ? 4 : 2;
	}
};




class DX12BindingTableRef
{
public:
	const char *space = "";
	const RootTableState *table = nullptr;
	UINT heapType = 0;
};

class FlatBufferRow
{
public:
	std::string id;
	std::string role;
	std::string skinningClass;
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo = {};
	UINT64 producerEventSerial = 0;
	UINT64 producerDrawId = 0;
	UINT64 producerDispatchId = 0;
	UINT64 producerPsoIndex = 0;
	DX12PsoShaderInfo producerShaderInfo = {};
	std::string producerBindSpace;
	UINT producerRootParameterIndex = 0;
	UINT producerShaderRegister = UINT_MAX;
	UINT64 gpuVa = 0;
	UINT64 size = 0;
	UINT64 viewGpuVa = 0;
	UINT64 viewSize = 0;
	UINT64 viewOffset = 0;
	UINT64 drawGpuVa = 0;
	UINT64 drawSize = 0;
	UINT64 drawOffset = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	UINT drawStartIndex = 0;
	UINT drawIndexCount = 0;
	INT drawBaseVertex = 0;
	UINT drawStartInstance = 0;
	UINT drawInstanceCount = 0;
	D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	DX12PsoInputLayoutInfo inputLayout;
	uint32_t huntHash = 0;
	bool resolved = false;
	DX12BufferResourceSummary resource = {};
};

class BufferProducer
{
public:
	UINT64 eventSerial = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT64 psoIndex = 0;
	DX12PsoShaderInfo shaderInfo = {};
	std::string bindSpace;
	UINT rootParameterIndex = 0;
	UINT shaderRegister = UINT_MAX;
	ID3D12Resource *resource = nullptr;
	UINT64 begin = 0;
	UINT64 end = 0;
};

class DescriptorSlotKey
{
public:
	ID3D12DescriptorHeap *heap = nullptr;
	UINT64 descriptorIndex = 0;

	bool operator==(const DescriptorSlotKey &other) const
	{
		return heap == other.heap && descriptorIndex == other.descriptorIndex;
	}
};

class DescriptorSlotKeyHash
{
public:
	size_t operator()(const DescriptorSlotKey &key) const
	{
		uintptr_t heap = reinterpret_cast<uintptr_t>(key.heap);
		uint64_t mixed = static_cast<uint64_t>(heap >> 4) ^ (key.descriptorIndex * 0x9e3779b97f4a7c15ull);
		mixed ^= mixed >> 33;
		mixed *= 0xff51afd7ed558ccdull;
		mixed ^= mixed >> 33;
		return static_cast<size_t>(mixed);
	}
};

class DescriptorLookup
{
public:
	std::unordered_map<SIZE_T, const DX12DescriptorSummary*> byCpuHandle;
	std::unordered_map<DescriptorSlotKey, const DX12DescriptorSummary*, DescriptorSlotKeyHash> bySlot;
};

class DX12FrameEventSource
{
public:
	const std::vector<BindingEvent> *events = nullptr;
	const std::vector<OutputBindingEvent> *outputEvents = nullptr;
	std::vector<BindingEvent> ownedEvents;
	std::vector<OutputBindingEvent> ownedOutputEvents;
	UINT64 droppedEvents = 0;
	UINT64 droppedOutputEvents = 0;
	UINT64 captureSessionId = 0;

	const std::vector<BindingEvent> &GetEvents() const
	{
		static const std::vector<BindingEvent> emptyEvents;
		return events ? *events : emptyEvents;
	}

	const std::vector<OutputBindingEvent> &GetOutputEvents() const
	{
		static const std::vector<OutputBindingEvent> emptyEvents;
		return outputEvents ? *outputEvents : emptyEvents;
	}
};

class DX12BindingTracker
{
public:
	static bool RegisterCommandList(ID3D12GraphicsCommandList *commandList);
	static bool UnregisterCommandList(ID3D12GraphicsCommandList *commandList);
	static void ResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static bool SetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static bool SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool SetGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool SetComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void SetGraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void SetComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT value);
	static void SetComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT value);
	static void SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void SetComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void SetPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void SetIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void SetVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void RecordOutputTargets(ID3D12GraphicsCommandList *commandList, UINT numRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *renderTargetDescriptors, BOOL singleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *depthStencilDescriptor);
	static void RecordRenderTargetClear(ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView);
	static void RecordDepthStencilClear(ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView);
	static bool GetCurrentIaState(ID3D12GraphicsCommandList *commandList, DX12CurrentIaState *state);
	static bool GetCurrentComputeUavs(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *uavs);
	static bool GetCurrentComputeSrvs(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *srvs);
	static bool GetCurrentComputeCbvs(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *cbvs);
	static bool GetCurrentShaderResourceBindings(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, std::vector<DX12CurrentShaderResourceBinding> *bindings);
	static bool GetCurrentShaderResourceBindingsByRegister(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, UINT shaderRegister, UINT registerSpace, std::vector<DX12CurrentShaderResourceBinding> *bindings);
	static bool GetCurrentComputeRootConstants(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentRootConstants> *constants);
	static bool GetCurrentComputeAll(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *uavs, std::vector<DX12CurrentComputeUavBinding> *srvs, std::vector<DX12CurrentComputeUavBinding> *cbvs, std::vector<DX12CurrentRootConstants> *constants);
	static bool CollectPreSkinBindings(
		ID3D12GraphicsCommandList *commandList,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants,
		UINT64 *dependencyFingerprint = nullptr);
	static bool GetSimulateContextPreSkinBindings(
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12CurrentComputeUavBinding> **uavs,
		const std::vector<DX12CurrentComputeUavBinding> **srvs,
		const std::vector<DX12CurrentComputeUavBinding> **cbvs,
		const std::vector<DX12CurrentRootConstants> **constants,
		UINT64 *dependencyFingerprint = nullptr,
		bool *foundAny = nullptr);
	static UINT64 GetComputeBindingSerial(ID3D12GraphicsCommandList *commandList);
	static UINT64 GetBindingQuerySerial(ID3D12GraphicsCommandList *commandList);
	static bool GetCurrentDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
	static void RecordDrawInstanced(ID3D12GraphicsCommandList *commandList, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
	static void RecordDrawIndexedInstanced(ID3D12GraphicsCommandList *commandList, UINT indexCountPerInstance, UINT instanceCount, UINT startIndexLocation, INT baseVertexLocation, UINT startInstanceLocation);
	static void RecordDispatch(ID3D12GraphicsCommandList *commandList, UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ);
	static void BeginFrame();
	static void DrainPendingEvents(const char *reason);
	static UINT64 CurrentCaptureSessionId();
	static UINT64 CaptureFenceSequence();
	static void SubmitCommandList(ID3D12GraphicsCommandList *commandList);
	static bool PopulateCaptureEventSource(DX12FrameEventSource *source, UINT64 captureSessionId, const char *reason);
	static bool PopulateActiveEventSource(DX12FrameEventSource *source, const char *reason);

	static void ReleaseRetainedIaResources(CommandListBindingState *state);
	static void ReleaseRecordedEvent(CommandListRecordedEvent *event);
	static void ReleaseRecordedEventsLocked(BindingShard *shard, ID3D12GraphicsCommandList *commandList);
	static void RecordTemplateEvent(ID3D12GraphicsCommandList *commandList, const BindingEvent &event);
	static void RecordTemplateOutputEvent(ID3D12GraphicsCommandList *commandList, const OutputBindingEvent &event);
	static void SubmitRecordedEvent(const CommandListRecordedEvent &recorded, UINT64 captureSessionId);
	static void StoreEventInCaptureLedger(const BindingEvent &event);
	static void StoreOutputEventInCaptureLedger(const OutputBindingEvent &event);
	static void RetainStateIaResourcesForAsync(CommandListBindingState *state);
	static void RetainEventIaResources(BindingEvent *event);
	static void ReleaseEventIaResources(BindingEvent *event);
	static void RefreshPipelineShaderInfo(CommandListBindingState *state);
	static void InvalidatePipelineShaderInfo(CommandListBindingState *state);
	static void BumpBindingQuerySerial(CommandListBindingState *state);
	static bool ResolvePipelineShaderInfoOnDemand(CommandListBindingState *state);
	static bool SnapshotBindingState(
		ID3D12GraphicsCommandList *commandList,
		CommandListBindingState *snapshot,
		UINT64 *bindingQuerySerial = nullptr);
	static void ReleaseResolvedIaResource(
		DX12BufferResourceSummary *summary, bool *resolved);
	static void ResolveAndRetainIaResource(
		UINT64 gpuVirtualAddress, UINT64 size,
		DX12BufferResourceSummary *summary, bool *resolved);
	static bool BuildEvent(BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 descriptorEpoch, UINT64 captureSessionId = 0);
	static bool BuildEvent(BindingEvent *event, const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static void StoreEventLocked(const BindingEvent &event);
	static void StoreEventLocked(const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state);
	static void StoreOutputEventLocked(const char *kind, ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT slot, SIZE_T cpuHandle, UINT64 drawId, UINT64 captureSessionId = 0);
	static void StoreCurrentOutputTargetsLocked(ID3D12GraphicsCommandList *commandList, const CommandListBindingState &state, UINT64 captureSessionId = 0);
	static bool GetCurrentDescriptors(ID3D12GraphicsCommandList *commandList, bool compute, D3D12_DESCRIPTOR_RANGE_TYPE rangeType, const char *descriptorKind, D3D12_ROOT_PARAMETER_TYPE rootDescriptorType, std::vector<DX12CurrentComputeUavBinding> *uavs);
	static bool SetRootConstantsLocked(RootConstantsState *constants, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
};

class DX12BindingFrameExport
{
public:
	static void GetCurrentFrameShaderInfos(std::vector<DX12PsoShaderInfo> *shaderInfos);
	static void GetCurrentFrameIaBuffers(std::vector<DX12FrameIaBufferBinding> *buffers);
	static void GetCurrentFrameIaBuffersWithResourceBindings(
		std::vector<DX12FrameIaBufferBinding> *buffers,
		const std::vector<DX12FrameResourceBinding> &resourceBindings);
	static void BuildIaBufferFileName(const DX12FrameIaBufferBinding &buffer, wchar_t *fileName, size_t fileNameCount);
	static DX12FrameAnalysisDumpResult DumpBindingTrace(const wchar_t *dir);

	static void WriteShaderInfo(FILE *file, const DX12PsoShaderInfo &info);
	static void FormatOptionalHashText(bool hasHash, UINT64 hash, char *text, size_t textCount);
	static void AppendShaderHashNamePart(wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash);
	static void BuildShaderHashNamePart(const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount);
	static void WriteRootTables(FILE *file, const RootTableState *tables);
	static void WriteVertexBufferSlots(FILE *file, const BindingEvent &event);
	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology);
	static std::string NextBufferId(const char *prefix, UINT *nextId);
	static DX12IaRegionDraw IaRegionDrawFromEvent(const BindingEvent &event);
	static bool IaRegionForEvent(const BindingEvent &event, const char *role, UINT slot, DX12IaRegion *region);
	static uint32_t IaRegionHashForEvent(const BindingEvent &event, const char *role, UINT slot);
	static bool ProjectResolvedIaResourceToView(
		UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
		const DX12BufferResourceSummary &resolvedDrawResource,
		DX12BufferResourceSummary *viewResource);
	static bool ResolveIaBufferResourceSummary(
		UINT64 viewGpuVa, UINT64 viewSize, UINT64 drawGpuVa, UINT64 drawSize,
		const DX12BufferResourceSummary &existingResource, bool existingResolved,
		DX12BufferResourceSummary *resource);
	static bool ResolveIaBufferResourceSummaryFromCombinedRange(
		UINT64 viewGpuVa,
		UINT64 viewSize,
		UINT64 drawGpuVa,
		UINT64 drawSize,
		DX12BufferResourceSummary *resource);
	static bool ResolveIaBufferResourceSummary(
		const DX12FrameIaBufferBinding &buffer, DX12BufferResourceSummary *resource);
	static void AddFrameIaBufferRow(std::vector<FlatBufferRow> *rows, UINT *nextVbId, UINT *nextIbId, const BindingEvent &event, const char *role, UINT slot);
	static void CollectFrameIaBuffersForEvents(const std::vector<BindingEvent> &events, std::vector<FlatBufferRow> *buffers);
	static bool RangesOverlap(UINT64 aBegin, UINT64 aSize, UINT64 bBegin, UINT64 bSize);
	static bool BindingIsUavBufferProducer(const DX12FrameResourceBinding &binding);
	static BufferProducer ProducerFromBinding(const DX12FrameResourceBinding &binding);
	static bool IsHuntingUavStateProducer(const BufferProducer &producer);
	static bool ProducerMatchesBuffer(const FlatBufferRow &row, const BufferProducer &producer);
	static bool ProducerIsPreferredForIaBuffer(const BufferProducer &candidate, const BufferProducer *best);
	static const BufferProducer *FindLatestProducerForBuffer(const FlatBufferRow &row, const std::vector<BufferProducer> &producers);
	static const BufferProducer *FindLatestHuntingUavStateProducerForBuffer(const FlatBufferRow &row, const std::vector<BufferProducer> &producers);
	static void ClassifyIaBufferSkinning(std::vector<FlatBufferRow> *buffers, const std::vector<DX12FrameResourceBinding> &resourceBindings);
	static DX12FrameIaBufferBinding FrameIaBufferFromFlatRow(const FlatBufferRow &row);
};

class DX12BindingResourceExport
{
public:
	static void GetCurrentFrameResourceBindings(std::vector<DX12FrameResourceBinding> *bindings);
	static void GetCurrentFrameOutputBindings(std::vector<DX12FrameOutputBinding> *bindings);

	static bool InsertBindingSeen(std::unordered_set<std::string> *seen, const BindingEvent &event, const char *bindSpace, UINT rootParameterIndex, UINT rangeIndex, UINT rangeType, ID3D12DescriptorHeap *heap, UINT64 descriptorIndex, bool rootDescriptor);
	static bool DescriptorMatchesRangeType(const DX12DescriptorSummary *descriptor, UINT rangeType);
	static const DX12DescriptorSummary *DescriptorForExpectedRange(const DX12DescriptorSummary *descriptor, UINT rangeType);
	static const char *DescriptorKindForRangeType(UINT rangeType);
	static const char *ExpectedBindingKind(const DX12FrameResourceBinding &binding);
	static bool IsBufferResourceSummary(const DX12ResourceSummary &resource);
	static void CollectHuntingUavStateResourceUsages(std::vector<DX12FrameResourceBinding> *bindings, const std::vector<BindingEvent> &events);
	static void CollectActivePreSkinProducerBindings(std::vector<DX12FrameResourceBinding> *bindings);
	static bool DescriptorBelongsToHeap(const DX12DescriptorSummary &descriptor, const DX12DescriptorHeapSummary &heap, UINT64 *descriptorIndex);
	static void BuildDescriptorLookup(const std::vector<DX12DescriptorSummary> &descriptors, const std::vector<DX12DescriptorHeapSummary> &heaps, DescriptorLookup *lookup);
	static const DX12DescriptorSummary *FindDescriptorForSlot(const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, SIZE_T cpuHandle);
	static const DX12DescriptorSummary *FindDescriptorForEventSlot(DX12DescriptorSummary *historyStorage, const BindingEvent &event, const DescriptorLookup &lookup, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, SIZE_T cpuHandle, UINT rangeType, size_t *historyLookups = nullptr, size_t *historyHits = nullptr);
	static UINT UnknownTableRangeType(UINT heapType);
	static bool MakeTableBinding(DX12FrameResourceBinding *binding, const BindingEvent &event, const char *bindSpace, const RootTableState &table, UINT rangeIndex, UINT rangeType, UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor);
	static bool CollectDescriptorTableEntry(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const char *bindSpace, const RootTableState &table, UINT rangeIndex, UINT rangeType, UINT shaderVisibility, UINT shaderRegister, UINT registerSpace, UINT descriptorOffset, const DX12DescriptorHeapSummary *heap, UINT64 descriptorIndex, UINT64 gpuHandle, SIZE_T cpuHandle, const DX12DescriptorSummary *descriptor, std::unordered_set<std::string> *seen, size_t *untrackedSkipped);
	static bool VisitTableStartOnly(const RootTableState &table, UINT heapType, const std::vector<DX12DescriptorHeapSummary> &heaps, UINT64 *descriptorIndex, UINT64 *gpuHandle, SIZE_T *cpuHandle, const DX12DescriptorHeapSummary **heap);
	static bool IsCompatibleRangeForSpace(const DX12RootDescriptorRangeSummary &range, const char *bindSpace);
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension);
	static void CollectFrameResourceBinding(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const char *bindSpace, const RootTableState &table, UINT heapType, const std::vector<DX12DescriptorHeapSummary> &heaps, const DescriptorLookup &descriptors, std::unordered_set<std::string> *seen, size_t *untrackedSkipped, size_t *historyLookups = nullptr, size_t *historyHits = nullptr);
	static void CollectFrameRootDescriptorBinding(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const char *bindSpace, const RootDescriptorState &rootDescriptor, std::unordered_set<std::string> *seen);
	static void CountEventTables(const BindingEvent &event, const std::vector<DX12DescriptorHeapSummary> &heaps, size_t *graphicsTables, size_t *computeTables, size_t *graphicsTablesWithRoots, size_t *computeTablesWithRoots, size_t *rootRangeTables, size_t *tableStartOnly, size_t *unboundedRanges, size_t *unboundedDescriptors, size_t *graphicsTablesWithoutRoots, size_t *computeTablesWithoutRoots, size_t *descriptorTablesWithoutRanges);
	static void CollectEventResourceBindings(std::vector<DX12FrameResourceBinding> *bindings, const BindingEvent &event, const std::vector<DX12DescriptorHeapSummary> &heaps, const DescriptorLookup &descriptorLookup, std::unordered_set<std::string> *seen, size_t *untrackedSkipped, size_t *historyLookups = nullptr, size_t *historyHits = nullptr);
};


// DX11-aligned global log utilities
//
// DX12LogInfo and DX12LogDebug use direct fprintf without timestamps.
// Timestamped variants prefix the line with GetTickCount64.
//
// Format (TS variants):  raw_tick<TAB>message<NEWLINE>
// Format (no-TS variants): message as-is, caller controls newline
//
// All variants: no queue, no thread, no lock. Just like DX11 LogInfo.

class DX12LogUtils
{
public:
	static FILE *LogFile;

	static bool Open(const wchar_t *path)
	{
		LogFile = _wfsopen(path, L"w", _SH_DENYNO);
		return LogFile != nullptr;
	}

	static void Close()
	{
		if (LogFile) {
			fclose(LogFile);
			LogFile = nullptr;
		}
	}

	static void Flush()
	{
		if (LogFile)
			fflush(LogFile);
	}

	static void MakeLogTimePlain(char *buffer, size_t bufferSize)
	{
		if (!buffer || !bufferSize)
			return;
		snprintf(buffer, bufferSize, "%llu",
			static_cast<unsigned long long>(GetTickCount64()));
	}
};

// No-timestamp macros pure fprintf, DX11 speed
// Prefixed DX12 to avoid collision with DX12FrameAnalysis::LogInfo etc.

#if defined(_DEBUG)
#define DX12LogDebug(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)
#else
#define DX12LogDebug(fmt, ...) do {} while (0)
#endif

#define DX12LogInfo(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)

// Timestamped macros GetTickCount64() prefix + direct fprintf
// Format:  raw_tick\tmessage\n
// Uses string-literal concatenation: "%llu\t" fmt

#if defined(_DEBUG)
#define DX12LogDebugTS(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, "%llu\t" fmt, \
		static_cast<unsigned long long>(GetTickCount64()), __VA_ARGS__); } while (0)
#else
#define DX12LogDebugTS(fmt, ...) do {} while (0)
#endif

#define DX12LogInfoTS(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, "%llu\t" fmt, \
		static_cast<unsigned long long>(GetTickCount64()), __VA_ARGS__); } while (0)


enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

class DX12RuntimeState
{
public:
	inline static HINSTANCE module = nullptr;
	inline static std::atomic<LONG> presentCount{ 0 };
	inline static std::atomic<LONG> runtimeMode{ static_cast<LONG>(DX12RuntimeMode::Mod) };

	// Per-frame draw/dispatch counters feed diagnostics logs only. Sharded by
	// recording thread so the per-draw increment cannot ping-pong one cache
	// line across cores when the game records command lists from workers.
	struct alignas(64) FrameCounterShard
	{
		std::atomic<LONG> draws{ 0 };
		std::atomic<LONG> dispatches{ 0 };
	};
	static constexpr UINT FrameCounterShardCount = 64;
	inline static FrameCounterShard frameCounterShards[FrameCounterShardCount] = {};
	inline static std::atomic<UINT> frameCounterShardNext{ 0 };

	static void SetModule(HINSTANCE module)
	{
		DX12RuntimeState::module = module;
	}

	static HINSTANCE GetModule()
	{
		return module;
	}

	static LONG IncrementPresentCount()
	{
		return presentCount.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	static LONG GetPresentCount()
	{
		return presentCount.load(std::memory_order_relaxed);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = runtimeMode.load(std::memory_order_relaxed);
		return mode == static_cast<LONG>(DX12RuntimeMode::Hunting) ?
			DX12RuntimeMode::Hunting : DX12RuntimeMode::Mod;
	}

	static void SetRuntimeMode(DX12RuntimeMode mode);

	static bool IsModMode()
	{
		return GetRuntimeMode() == DX12RuntimeMode::Mod;
	}

	static bool IsHuntingMode()
	{
		return GetRuntimeMode() == DX12RuntimeMode::Hunting;
	}

	static FrameCounterShard *FrameCounterShardForThread()
	{
		static thread_local FrameCounterShard *shard = nullptr;
		if (!shard) {
			const UINT index =
				frameCounterShardNext.fetch_add(1, std::memory_order_relaxed) %
				FrameCounterShardCount;
			shard = &frameCounterShards[index];
		}
		return shard;
	}

	static void IncrementFrameDraws()
	{
		FrameCounterShardForThread()->draws.fetch_add(1, std::memory_order_relaxed);
	}

	static void IncrementFrameDispatches()
	{
		FrameCounterShardForThread()->dispatches.fetch_add(1, std::memory_order_relaxed);
	}

	static LONG TakeFrameDraws()
	{
		LONG total = 0;
		for (UINT i = 0; i < FrameCounterShardCount; ++i)
			total += frameCounterShards[i].draws.exchange(0, std::memory_order_relaxed);
		return total;
	}

	static LONG TakeFrameDispatches()
	{
		LONG total = 0;
		for (UINT i = 0; i < FrameCounterShardCount; ++i)
			total += frameCounterShards[i].dispatches.exchange(0, std::memory_order_relaxed);
		return total;
	}
};

class DX12InternalReplayState
{
public:
	static UINT &Depth()
	{
		static thread_local UINT depth = 0;
		return depth;
	}

	static void Enter()
	{
		++Depth();
	}

	static void Leave()
	{
		UINT &depth = Depth();
		if (depth)
			--depth;
	}

	static bool IsActive()
	{
		return Depth() != 0;
	}
};

class DX12InternalReplayScope
{
public:
	DX12InternalReplayScope() { DX12InternalReplayState::Enter(); }
	~DX12InternalReplayScope() { DX12InternalReplayState::Leave(); }
	DX12InternalReplayScope(const DX12InternalReplayScope&) = delete;
	DX12InternalReplayScope &operator=(const DX12InternalReplayScope&) = delete;
};


// ---------------------------------------------------------------------------
// Log mode - fixed at compile time.
//
//   Debug         - full diagnostics: verbose hook/present logs + detailed
//                   timing tables.
//   Release       - startup and mod-load lifecycle logs only (allowlist in
//                   ReleaseShouldLogFunction); no per-frame diagnostics.
//   ReleaseTiming - detailed timing tables and diagnostics, but no verbose
//                   per-call/per-present spam (kVerboseEnabled is false).
//
// These constants let call sites gate with a single folded bool; Release and
// Timing builds compile the dead branches out entirely.
// ---------------------------------------------------------------------------
enum class DX12LogMode : unsigned char
{
	Debug = 0,
	Release,
	ReleaseTiming,
};

constexpr DX12LogMode gLogMode =
#if defined(DX12_RELEASE_TIMING)
	DX12LogMode::ReleaseTiming;
#elif defined(_DEBUG)
	DX12LogMode::Debug;
#else
	DX12LogMode::Release;
#endif

// Verbose per-call/per-present diagnostics: Debug only.
constexpr bool kVerboseEnabled = gLogMode == DX12LogMode::Debug;

// Detailed timing tables (ApiWall/HotPath/FrameTimeline): Debug + Timing.
constexpr bool kTimingEnabled =
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
	true;
#else
	false;
#endif



class DX12LogManager
{
public:
	class DX12LogCostSnapshot
	{
	public:
		LONG64 fieldCalls = 0;
		LONG64 textCalls = 0;
		LONG64 debugFieldCalls = 0;
		LONG64 enqueueCalls = 0;
		LONG64 formatUs = 0;
		LONG64 enqueueUs = 0;
		LONG64 droppedLines = 0;
	};

	FILE *log = nullptr;
	HANDLE thread = nullptr;
	HANDLE event = nullptr;
	SRWLOCK queueLock;
	std::deque<std::string> queue;
	volatile LONG droppedLines = 0;
	volatile LONG stopRequested = 0;
#if !defined(_DEBUG)
	std::atomic<bool> releaseStartupLogging{ true };
#endif
	inline static volatile LONG64 LogCostFieldCalls = 0;
	inline static volatile LONG64 LogCostTextCalls = 0;
	inline static volatile LONG64 LogCostDebugFieldCalls = 0;
	inline static volatile LONG64 LogCostEnqueueCalls = 0;
	inline static volatile LONG64 LogCostFormatUs = 0;
	inline static volatile LONG64 LogCostEnqueueUs = 0;
	inline static volatile LONG64 LogCostDroppedLines = 0;

	DX12LogManager()
	{
		InitializeSRWLock(&queueLock);
	}

	static DX12LogManager &Get()
	{
		static DX12LogManager state;
		return state;
	}

	static size_t MaxQueuedLogLines()
	{
#if defined(_DEBUG)
		return 262144;
#else
		return 8192;
#endif
	}

	static LARGE_INTEGER LogFrequency()
	{
		static LARGE_INTEGER frequency = InitLogFrequency();
		return frequency;
	}

	static LARGE_INTEGER InitLogFrequency()
	{
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		return frequency;
	}

	static LARGE_INTEGER LogNow()
	{
		LARGE_INTEGER value = {};
		QueryPerformanceCounter(&value);
		return value;
	}

	static LONG64 LogDeltaUs(const LARGE_INTEGER &begin, const LARGE_INTEGER &end)
	{
		const LARGE_INTEGER frequency = LogFrequency();
		if (frequency.QuadPart <= 0 || end.QuadPart <= begin.QuadPart)
			return 0;
		return static_cast<LONG64>(
			(end.QuadPart - begin.QuadPart) * 1000000 / frequency.QuadPart);
	}

	static void AddLogCost(volatile LONG64 *target, LONG64 us)
	{
		if (target && us > 0)
			InterlockedAdd64(target, us);
	}

	static LONG64 TakeLong64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static void TakeLogCostSnapshot(DX12LogCostSnapshot *snapshot)
	{
		if (!snapshot)
			return;
		snapshot->fieldCalls = TakeLong64(&LogCostFieldCalls);
		snapshot->textCalls = TakeLong64(&LogCostTextCalls);
		snapshot->debugFieldCalls = TakeLong64(&LogCostDebugFieldCalls);
		snapshot->enqueueCalls = TakeLong64(&LogCostEnqueueCalls);
		snapshot->formatUs = TakeLong64(&LogCostFormatUs);
		snapshot->enqueueUs = TakeLong64(&LogCostEnqueueUs);
		snapshot->droppedLines = TakeLong64(&LogCostDroppedLines);
	}

	static DWORD WINAPI ThreadProc(void*)
	{
		DX12LogManager &state = Get();
		for (;;) {
			if (state.event)
				WaitForSingleObject(state.event, 100);
			if (state.event)
				ResetEvent(state.event);

			std::vector<std::string> pending;
			AcquireSRWLockExclusive(&state.queueLock);
			while (!state.queue.empty()) {
				pending.push_back(std::move(state.queue.front()));
				state.queue.pop_front();
			}
			const bool stopping = InterlockedCompareExchange(&state.stopRequested, 0, 0) != 0;
			ReleaseSRWLockExclusive(&state.queueLock);

			if (state.log) {
				const LONG dropped = InterlockedExchange(&state.droppedLines, 0);
				if (dropped > 0) {
					char droppedLine[256];
					char timePlain[32];
					DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
					snprintf(droppedLine, sizeof(droppedLine),
						"time=%s,func=DX12LogQueue,count=%ld\n", timePlain, dropped);
					fputs(droppedLine, state.log);
				}
				for (const std::string &line : pending)
					fputs(line.c_str(), state.log);
				if (!pending.empty() || stopping)
					fflush(state.log);
			}

			if (stopping && pending.empty())
				break;
		}
		return 0;
	}

	static bool Open()
	{
		DX12LogManager &state = Get();
#if !defined(_DEBUG)
		state.releaseStartupLogging.store(true, std::memory_order_relaxed);
#endif
		wchar_t path[MAX_PATH];
		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, MAX_PATH))
			return false;

		PathRemoveFileSpecW(path);
		// Keep a timestamped copy of every run so normal vs broken sessions
		// can be compared: the log is overwritten on each launch otherwise.
		wchar_t logPath[MAX_PATH];
		wchar_t historyPath[MAX_PATH];
		wsprintfW(logPath, L"%s\\d3d12_log.txt", path);
		wsprintfW(historyPath, L"%s\\d3d12_log_%llu.txt", path,
			static_cast<unsigned long long>(GetTickCount64()));
		if (PathFileExistsW(logPath)) {
			CopyFileW(logPath, historyPath, FALSE);
		}
		PathAppendW(path, L"d3d12_log.txt");
		state.log = _wfsopen(path, L"w", _SH_DENYNO);
		if (!state.log)
			return false;

		InterlockedExchange(&state.stopRequested, 0);
		state.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!state.event) {
			fclose(state.log);
			state.log = nullptr;
			return false;
		}
		state.thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
		if (!state.thread) {
			CloseHandle(state.event);
			state.event = nullptr;
			fclose(state.log);
			state.log = nullptr;
			return false;
		}
		return true;
	}

	static void Close()
	{
		DX12LogManager &state = Get();
		InterlockedExchange(&state.stopRequested, 1);
		if (state.event)
			SetEvent(state.event);
		if (state.thread) {
			WaitForSingleObject(state.thread, 5000);
			CloseHandle(state.thread);
			state.thread = nullptr;
		}
		if (state.event) {
			CloseHandle(state.event);
			state.event = nullptr;
		}
		if (state.log) {
			fclose(state.log);
			state.log = nullptr;
		}
		AcquireSRWLockExclusive(&state.queueLock);
		state.queue.clear();
		ReleaseSRWLockExclusive(&state.queueLock);
	}

	static void EndReleaseStartupLogging()
	{
#if !defined(_DEBUG)
		Get().releaseStartupLogging.store(false, std::memory_order_relaxed);
#endif
	}

	static bool ShouldLogHookCall(const char *api)
	{
		if (!kVerboseEnabled)
			return false;
		if (!api || !api[0] || DX12InternalReplayState::IsActive())
			return false;
		return true;
	}

	static bool ReleaseShouldLogFunction(const char *func)
	{
#if defined(DX12_RELEASE_TIMING)
		// Timing builds keep every Fields log (verbose per-call spam is gated
		// by kVerboseEnabled instead), so the timing tables are never dropped.
		(void)func;
		return true;
#elif !defined(_DEBUG)
		if (Get().releaseStartupLogging.load(std::memory_order_relaxed))
			return true;
		if (!func)
			return false;
		// Release keeps only startup and mod-load lifecycle logs.
		static const char *const kReleaseAllowlist[] = {
			"Log",                        // startup text messages
			"HookInstall",                // hook installation
			"DeviceHooksReady",           // device hooks finished
			"D3D12CreateDevice",          // device creation
			"CreateDXGIFactory",          // factory creation (1/2 included)
			"IDXGIFactory",               // factory/swapchain lifecycle
			"IDXGISwapChain",             // swapchain lifecycle
			"ID3D12CommandQueue",         // command queue lifecycle
			"DX12CommandQueueState",      // queue accept/ignore decisions
			"DX12ExecuteCommandListsFirst", // first command list submit
			"DX12PresentFirst",           // first present
			"DX12Config",                 // d3dx.ini configuration load
			"DX12ModRuntime",             // mod runtime load/reload/deferred
			"DX12ResourceLoad",           // mod resource loading
		};
		for (const char *name : kReleaseAllowlist) {
			if (!strncmp(func, name, strlen(name)))
				return true;
		}
		return false;
#else
		(void)func;
		return true;
#endif
	}

	static void EnqueueLogLine(const char *line)
	{
		if (!line || !line[0])
			return;

		const LARGE_INTEGER begin = LogNow();
		DX12LogManager &state = Get();
		InterlockedIncrement64(&LogCostEnqueueCalls);
		AcquireSRWLockExclusive(&state.queueLock);
		if (state.queue.size() < MaxQueuedLogLines()) {
			state.queue.emplace_back(line);
		} else {
			InterlockedIncrement(&state.droppedLines);
			InterlockedIncrement64(&LogCostDroppedLines);
		}
		ReleaseSRWLockExclusive(&state.queueLock);
		if (state.event)
			SetEvent(state.event);
		AddLogCost(&LogCostEnqueueUs, LogDeltaUs(begin, LogNow()));
	}

	// Plain text fields: time=...,func=...,key=value,key2=value2.
	// Existing JSON-like field fragments are normalized without allocation.
	static void NormalizeFields(const char *input, char *output, size_t outputSize)
	{
		if (!output || outputSize == 0)
			return;
		output[0] = '\0';
		if (!input)
			return;

		bool quoted = false;
		bool escaped = false;
		size_t out = 0;
		for (const char *cursor = input; *cursor && out + 1 < outputSize; ++cursor) {
			char ch = *cursor;
			if (escaped) {
				output[out++] = ch;
				escaped = false;
				continue;
			}
			if (quoted && ch == '\\') {
				escaped = true;
				continue;
			}
			if (ch == '"') {
				quoted = !quoted;
				continue;
			}
			if (!quoted && (ch == '{' || ch == '}'))
				continue;
			if (!quoted && ch == ':')
				ch = '=';
			else if (quoted && ch == ',')
				ch = ';';
			else if (ch == '\r' || ch == '\n' || ch == '\t')
				ch = ' ';
			output[out++] = ch;
		}
		output[out] = '\0';
	}

	static void WriteLogLine(
		const char *func, const char *extra, bool flush)
	{
		if (!Get().log)
			return;

		const LARGE_INTEGER formatBegin = LogNow();
		char timePlain[32] = {};
		DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
		char line[8192];
		char fields[6144];
		NormalizeFields(extra, fields, sizeof(fields));
		if (extra && extra[0]) {
			snprintf(line, sizeof(line), "time=%s,func=%s,%s\n", timePlain, func, fields);
		} else {
			snprintf(line, sizeof(line), "time=%s,func=%s\n", timePlain, func);
		}
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		EnqueueLogLine(line);
		if (flush)
			Flush();
	}

	static void Text(const char *fmt, ...)
	{
		if (!Get().log)
			return;
#if !defined(_DEBUG)
		if (!ReleaseShouldLogFunction("Log"))
			return;
#endif

		va_list args;
		char message[2048];
		InterlockedIncrement64(&LogCostTextCalls);
		const LARGE_INTEGER formatBegin = LogNow();
		va_start(args, fmt);
		vsnprintf(message, sizeof(message), fmt, args);
		va_end(args);
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));

		char fields[2304];
		snprintf(fields, sizeof(fields), "message=%s", message);
		WriteLogLine("Log", fields, false);
	}

	static void FieldsV(const char *func, const char *fmt, va_list args, bool flush, bool debugCall)
	{
		if (!Get().log)
			return;
#if !defined(_DEBUG)
		if (!ReleaseShouldLogFunction(func))
			return;
#endif

		const char *safeFunc = func ? func : "Unknown";
		char extra[4096];
		extra[0] = '\0';
		if (debugCall)
			InterlockedIncrement64(&LogCostDebugFieldCalls);
		else
			InterlockedIncrement64(&LogCostFieldCalls);

		if (fmt && fmt[0]) {
			const LARGE_INTEGER formatBegin = LogNow();
			vsnprintf(extra, sizeof(extra), fmt, args);
			AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		}

		WriteLogLine(safeFunc, extra, flush);
	}

	static void Fields(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, false, false);
		va_end(args);
	}

	static void FieldsFlush(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, true, false);
		va_end(args);
	}

#if defined(_DEBUG)
	static void DebugFields(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !kVerboseEnabled)
			return;
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, false, true);
		va_end(args);
	}

	static void DebugFieldsFlush(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !kVerboseEnabled)
			return;
		va_list args;
		va_start(args, fmt);
		FieldsV(func, fmt, args, true, true);
		va_end(args);
	}
#else
	static void DebugFields(const char *func, const char *fmt, ...)
	{
		(void)func;
		(void)fmt;
	}

	static void DebugFieldsFlush(const char *func, const char *fmt, ...)
	{
		(void)func;
		(void)fmt;
	}
#endif

	static void Flush()
	{
		if (Get().event)
			SetEvent(Get().event);
	}
};


class DX12JsonUtils
{
public:
	static void AppendText(char *dst, size_t dstSize, const char *text)
	{
		if (!dst || dstSize == 0 || !text)
			return;
		const size_t len = strlen(dst);
		if (len >= dstSize - 1)
			return;
		sprintf_s(dst + len, dstSize - len, "%s", text);
	}

	static void AppendStringField(char *dst, size_t dstSize, const char *name, const char *value)
	{
		char escaped[2048];
		EscapeString(escaped, sizeof(escaped), value ? value : "");
		AppendText(dst, dstSize, ",\"");
		AppendText(dst, dstSize, name);
		AppendText(dst, dstSize, "\":");
		AppendText(dst, dstSize, escaped);
	}

	static void AppendWStringField(
		char *dst, size_t dstSize, const char *name, const wchar_t *value)
	{
		char escaped[2048];
		EscapeWString(escaped, sizeof(escaped), value ? value : L"");
		AppendText(dst, dstSize, ",\"");
		AppendText(dst, dstSize, name);
		AppendText(dst, dstSize, "\":");
		AppendText(dst, dstSize, escaped);
	}

	static void AppendRawField(char *dst, size_t dstSize, const char *name, const char *value)
	{
		AppendText(dst, dstSize, ",\"");
		AppendText(dst, dstSize, name);
		AppendText(dst, dstSize, "\":");
		AppendText(dst, dstSize, value ? value : "0");
	}

	static bool IsIntegerText(const char *value)
	{
		if (!value || !*value)
			return false;
		if (*value == '-')
			value++;
		if (!*value)
			return false;
		for (; *value; ++value) {
			if (!isdigit(static_cast<unsigned char>(*value)))
				return false;
		}
		return true;
	}

	static bool IsHexText(const char *value)
	{
		if (!value || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
			return false;
		value += 2;
		if (!*value)
			return false;
		for (; *value; ++value) {
			if (!isxdigit(static_cast<unsigned char>(*value)))
				return false;
		}
		return true;
	}

	static bool IsIdentifierKeyChar(char c)
	{
		return isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	static bool IsSkippablePunctuation(char c)
	{
		return c == ':' || c == ',' || c == ';';
	}

	static void EscapeString(char *dst, size_t dstSize, const char *src)
	{
		if (!dst || dstSize == 0)
			return;

		size_t pos = 0;
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (src) {
			for (; *src && pos + 6 < dstSize; ++src) {
				switch (*src) {
				case '"': dst[pos++] = '\\'; dst[pos++] = '"'; break;
				case '\\': dst[pos++] = '\\'; dst[pos++] = '\\'; break;
				case '\b': dst[pos++] = '\\'; dst[pos++] = 'b'; break;
				case '\f': dst[pos++] = '\\'; dst[pos++] = 'f'; break;
				case '\n': dst[pos++] = '\\'; dst[pos++] = 'n'; break;
				case '\r': dst[pos++] = '\\'; dst[pos++] = 'r'; break;
				case '\t': dst[pos++] = '\\'; dst[pos++] = 't'; break;
				default:
					if (static_cast<unsigned char>(*src) < 0x20) {
						sprintf_s(dst + pos, dstSize - pos, "\\u%04x",
							static_cast<unsigned char>(*src));
						pos += 6;
					} else {
						dst[pos++] = *src;
					}
				}
			}
		}
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (pos < dstSize)
			dst[pos] = '\0';
	}

	static void EscapeWString(char *dst, size_t dstSize, const wchar_t *src)
	{
		if (!dst || dstSize == 0)
			return;

		size_t pos = 0;
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (src) {
			for (; *src && pos + 6 < dstSize; ++src) {
				wchar_t c = *src;
				if (c < 0x20) {
					sprintf_s(dst + pos, dstSize - pos, "\\u%04x",
						static_cast<unsigned int>(c));
					pos += 6;
				} else if (c == L'"') {
					dst[pos++] = '\\';
					dst[pos++] = '"';
				} else if (c == L'\\') {
					dst[pos++] = '\\';
					dst[pos++] = '\\';
				} else if (c < 0x80) {
					dst[pos++] = static_cast<char>(c);
				} else if (c < 0x800 && pos + 2 < dstSize) {
					dst[pos++] = static_cast<char>(0xC0 | (c >> 6));
					dst[pos++] = static_cast<char>(0x80 | (c & 0x3F));
				} else if (pos + 3 < dstSize) {
					dst[pos++] = static_cast<char>(0xE0 | (c >> 12));
					dst[pos++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
					dst[pos++] = static_cast<char>(0x80 | (c & 0x3F));
				}
			}
		}
		if (pos + 1 < dstSize)
			dst[pos++] = '"';
		if (pos < dstSize)
			dst[pos] = '\0';
	}

	static void AppendLogFieldsFromText(char *dst, size_t dstSize, const char *text)
	{
		if (!dst || dstSize == 0)
			return;

		if (!text)
			text = "";

		char clean[2048];
		strncpy_s(clean, text, _TRUNCATE);
		size_t cleanLen = strlen(clean);
		while (cleanLen > 0 && (clean[cleanLen - 1] == '\n' || clean[cleanLen - 1] == '\r')) {
			clean[--cleanLen] = '\0';
		}

		const char *cursor = clean;
		const char *firstKey = nullptr;
		for (const char *scan = clean; *scan; ++scan) {
			if ((scan == clean || !IsIdentifierKeyChar(*(scan - 1))) && IsIdentifierKeyChar(*scan)) {
				const char *keyEnd = scan;
				while (IsIdentifierKeyChar(*keyEnd))
					keyEnd++;
				if (*keyEnd == '=') {
					firstKey = scan;
					break;
				}
				scan = keyEnd;
				if (!*scan)
					break;
			}
		}
		if (firstKey)
			cursor = firstKey;
		else
			cursor += strlen(cursor);

		char prefix[512] = "";
		if (cursor > clean) {
			size_t prefixLen = static_cast<size_t>(cursor - clean);
			if (prefixLen >= sizeof(prefix))
				prefixLen = sizeof(prefix) - 1;
			memcpy(prefix, clean, prefixLen);
			prefix[prefixLen] = '\0';
			while (prefixLen > 0 && isspace(static_cast<unsigned char>(prefix[prefixLen - 1])))
				prefix[--prefixLen] = '\0';
		}

		bool wroteKeyValue = false;
		bool wroteText = false;
		if (prefix[0]) {
			AppendStringField(dst, dstSize, "text", prefix);
			wroteText = true;
		}

		while (*cursor) {
			while (*cursor && (isspace(static_cast<unsigned char>(*cursor)) ||
				IsSkippablePunctuation(*cursor))) {
				cursor++;
			}
			if (!*cursor)
				break;

			const char *keyStart = cursor;
			while (IsIdentifierKeyChar(*cursor))
				cursor++;
			if (cursor == keyStart || *cursor != '=')
				break;

			char key[64];
			size_t keyLen = static_cast<size_t>(cursor - keyStart);
			if (keyLen >= sizeof(key))
				keyLen = sizeof(key) - 1;
			strncpy_s(key, keyLen + 1, keyStart, keyLen);
			cursor++;

			const char *valueStart = cursor;
			while (*cursor && !isspace(static_cast<unsigned char>(*cursor)))
				cursor++;

			char value[512];
			size_t valueLen = static_cast<size_t>(cursor - valueStart);
			if (valueLen >= sizeof(value))
				valueLen = sizeof(value) - 1;
			strncpy_s(value, valueLen + 1, valueStart, valueLen);

			while (valueLen > 0 && (value[valueLen - 1] == ',' || value[valueLen - 1] == ';')) {
				value[--valueLen] = '\0';
			}

			if (!strcmp(value, "true") || !strcmp(value, "false") || IsIntegerText(value))
				AppendRawField(dst, dstSize, key, value);
			else
				AppendStringField(dst, dstSize, key, value);
			wroteKeyValue = true;
		}

		if (!wroteKeyValue && !wroteText)
			AppendStringField(dst, dstSize, "text", clean);
	}
};



class DX12CommandListFlowDiagnostics
{
public:
#if defined(_DEBUG)
	static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;

		char detail[1024];
		detail[0] = '\0';
		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(detail, sizeof(detail), fmt, args);
			va_end(args);
		}

		char apiJson[256];
		char detailJson[2048];
		DX12JsonUtils::EscapeString(apiJson, sizeof(apiJson), api ? api : "");
		DX12JsonUtils::EscapeString(detailJson, sizeof(detailJson), detail);
		DX12LogManager::DebugFields("DX12HookCall",
			"\"api\":%s,\"present\":%ld,\"this\":\"%p\",\"detail\":%s",
			apiJson, DX12RuntimeState::GetPresentCount(), object, detailJson);
	}
#else
	static __forceinline void LogDX12Call(const char *, const void *, const char * = nullptr, ...) {}
#endif

	static void LogQueueStage(
		const char *api, const char *stage, ID3D12CommandQueue *queue,
		UINT count = 0, ID3D12Fence *fence = nullptr, UINT64 value = 0, HRESULT hr = S_OK,
		ULONGLONG elapsedMs = 0)
	{
#if defined(_DEBUG)
		if (!kVerboseEnabled)
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && api)
			return;
		DX12LogManager::DebugFields("DX12QueueStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
			"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), queue,
			count, fence, static_cast<unsigned long long>(value), hr,
			static_cast<unsigned long long>(elapsedMs));
#else
		(void)api;
		(void)stage;
		(void)queue;
		(void)count;
		(void)fence;
		(void)value;
		(void)hr;
		(void)elapsedMs;
#endif
	}
};


#include <memory>


class DX12ModIaReplacement;

class DX12ActiveIaState
{
public:
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferHashValid = false;
	uint32_t indexBufferHash = 0;
	UINT64 indexBufferHashContentGeneration = 0;
	UINT64 indexBufferHashResolveGeneration = 0;
	bool indexBufferResolved = false;
	DX12BufferResourceSummary indexBufferResource = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	bool vertexBufferHashValid[32] = {};
	uint32_t vertexBufferHashes[32] = {};
	UINT64 vertexBufferHashContentGeneration[32] = {};
	UINT64 vertexBufferHashResolveGeneration[32] = {};
	bool vertexBufferResolved[32] = {};
	DX12BufferResourceSummary vertexBufferResources[32] = {};
	bool preSkinRedirectNegativeValid[32] = {};
	UINT64 preSkinRedirectNegativeGeneration[32] = {};
	D3D12_VERTEX_BUFFER_VIEW preSkinRedirectNegativeSource[32] = {};
	// Bind-time static IA replacement bookkeeping (plan step 3.2). When a
	// bound IB/VB content hash matched an unconditional static geometry
	// override at IASet time, the stored view above points at the mod buffer
	// and these fields remember the original binding plus the identity used
	// for staleness checks, so a rewritten or recreated game buffer can be
	// detected and re-bound with the original view.
	bool indexBufferStaticReplaced = false;
	D3D12_INDEX_BUFFER_VIEW indexBufferStaticOriginal = {};
	D3D12_INDEX_BUFFER_VIEW indexBufferStaticReplacedView = {};
	DX12BufferResourceSummary indexBufferStaticOriginalResource = {};
	bool indexBufferStaticOriginalResolved = false;
	UINT64 indexBufferStaticContentGeneration = 0;
	UINT64 indexBufferStaticResolveEpoch = 0;
	uint32_t indexBufferStaticMatchedHash = 0;
	bool vertexBufferStaticReplaced[32] = {};
	// Aggregate of vertexBufferStaticReplaced for a single-load draw-path gate.
	UINT32 vertexBufferStaticReplacedMask = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferStaticOriginal[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBufferStaticReplacedView[32] = {};
	DX12BufferResourceSummary vertexBufferStaticOriginalResources[32] = {};
	bool vertexBufferStaticOriginalResolved[32] = {};
	UINT64 vertexBufferStaticContentGeneration[32] = {};
	UINT64 vertexBufferStaticResolveEpoch[32] = {};
	uint32_t vertexBufferStaticMatchedHash[32] = {};
	// Plan fix B: lazily re-check static-replacement staleness only when the
	// global content/resolve generations moved since the last deep check on
	// this command list. Without this gate every draw pays a per-slot locked
	// map lookup (DX12GetBufferContentGenerationForResource) whenever a
	// bind-time static replacement is live.
	UINT64 staticReplacedLastCheckedContentGeneration = 0;
	UINT64 staticReplacedLastCheckedResolveEpoch = 0;
	// Plan fix F: pre-skin IA replay gate. ReplayActiveVertexBuffers (GPUVA
	// lookups + IASet) only needs to run once per output-resize/active
	// generation per IA state; further matching draws on the same command list
	// reuse the already-replayed views. The IA view fingerprint guards against
	// game rebinds between replays (rebinding identical views keeps the same
	// fingerprint, unlike iaEpoch which bumps on every rebind).
	UINT64 preSkinReplayGeneration = 0;
	UINT64 preSkinReplayIaFingerprint = 0;
	bool preSkinReplayApplied = false;
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

class DX12DrawMatchUsage
{
public:
	bool useVertexCount = false;
	bool useIndexCount = false;
	bool useInstanceCount = false;
	bool useFirstVertex = false;
	bool useFirstIndex = false;
	bool useFirstInstance = false;

	void RequireAll()
	{
		useVertexCount = true;
		useIndexCount = true;
		useInstanceCount = true;
		useFirstVertex = true;
		useFirstIndex = true;
		useFirstInstance = true;
	}
};


enum class DX12CompiledDispatchDecisionKind : uint32_t
{
	NoComputeShader = 0,
	NoPreSkinCandidate = 1,
	NeedsPreSkinProbe = 2,
	NeedsShaderOverrideWork = 3
};

class DX12CompiledDispatchDecision
{
public:
	DX12CompiledDispatchDecisionKind kind = DX12CompiledDispatchDecisionKind::NoComputeShader;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	uint64_t computeShaderHash = 0;
	bool resolved = false;
	bool hasComputeShader = false;
	bool hasPreSkinCandidate = false;
	bool hasShaderOverrideSkip = false;
	bool hasActiveShaderOverrides = false;
	bool hasActivePreSkinProbe = false;

	void Reset()
	{
		kind = DX12CompiledDispatchDecisionKind::NoComputeShader;
		pipelineState = nullptr;
		reloadGeneration = 0;
		computeShaderHash = 0;
		resolved = false;
		hasComputeShader = false;
		hasPreSkinCandidate = false;
		hasShaderOverrideSkip = false;
		hasActiveShaderOverrides = false;
		hasActivePreSkinProbe = false;
	}

	void SetNoComputeShader(
		ID3D12PipelineState *state,
		UINT64 generation,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NoComputeShader;
		pipelineState = state;
		reloadGeneration = generation;
		resolved = true;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	void SetNoPreSkinCandidate(
		ID3D12PipelineState *state,
		UINT64 generation,
		uint64_t csHash,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NoPreSkinCandidate;
		pipelineState = state;
		reloadGeneration = generation;
		computeShaderHash = csHash;
		resolved = true;
		hasComputeShader = csHash != 0;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	void SetNeedsPreSkinProbe(
		ID3D12PipelineState *state,
		UINT64 generation,
		uint64_t csHash,
		bool shaderOverrideSkip,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NeedsPreSkinProbe;
		pipelineState = state;
		reloadGeneration = generation;
		computeShaderHash = csHash;
		resolved = true;
		hasComputeShader = csHash != 0;
		hasPreSkinCandidate = true;
		hasShaderOverrideSkip = shaderOverrideSkip;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	void SetNeedsShaderOverrideWork(
		ID3D12PipelineState *state,
		UINT64 generation,
		uint64_t csHash,
		bool shaderOverrideSkip,
		bool activeShaderOverrides,
		bool activePreSkinProbe)
	{
		Reset();
		kind = DX12CompiledDispatchDecisionKind::NeedsShaderOverrideWork;
		pipelineState = state;
		reloadGeneration = generation;
		computeShaderHash = csHash;
		resolved = true;
		hasComputeShader = csHash != 0;
		hasShaderOverrideSkip = shaderOverrideSkip;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasActivePreSkinProbe = activePreSkinProbe;
	}

	bool NeedsPreSkinProbe() const
	{
		return kind == DX12CompiledDispatchDecisionKind::NeedsPreSkinProbe &&
			hasPreSkinCandidate;
	}

	bool NeedsShaderOverrideWork() const
	{
		return kind == DX12CompiledDispatchDecisionKind::NeedsShaderOverrideWork ||
			hasShaderOverrideSkip;
	}

	bool IsPreSkinRejected() const
	{
		return kind == DX12CompiledDispatchDecisionKind::NoComputeShader ||
			kind == DX12CompiledDispatchDecisionKind::NoPreSkinCandidate ||
			kind == DX12CompiledDispatchDecisionKind::NeedsShaderOverrideWork;
	}

	bool MatchesGlobals(bool activeShaderOverrides, bool activePreSkinProbe) const
	{
		return hasActiveShaderOverrides == activeShaderOverrides &&
			hasActivePreSkinProbe == activePreSkinProbe;
	}
};



class DX12IaHashTargetPlan
{
public:
	class DescriptorTarget
	{
	public:
		uint32_t kind = 0;
		uint32_t stage = 0;
		uint32_t slot = 0;
	};

	bool requireAllIaBuffers = true;
	bool requireIndexBuffer = false;
	bool requireIndexBufferView = false;
	UINT requireVertexBufferMask = 0;
	bool hasDescriptorTargets = false;
	bool hasTextureOverrideChecks = false;
	std::vector<DescriptorTarget> descriptorTargets;
	mutable UINT64 cachedFingerprint = 0;

	void ResetKnown()
	{
		requireAllIaBuffers = false;
		requireIndexBuffer = false;
		requireIndexBufferView = false;
		hasDescriptorTargets = false;
		hasTextureOverrideChecks = false;
		descriptorTargets.clear();
		requireVertexBufferMask = 0;
		cachedFingerprint = 0;
	}

	void RequireAll()
	{
		requireAllIaBuffers = true;
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		descriptorTargets.clear();
		requireVertexBufferMask = UINT_MAX;
		cachedFingerprint = 0;
	}

	void RequireIndexBuffer()
	{
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		hasTextureOverrideChecks = true;
		cachedFingerprint = 0;
	}

	void RequireIndexBufferView()
	{
		requireIndexBufferView = true;
		cachedFingerprint = 0;
	}

	void RequireVertexBuffer(UINT slot)
	{
		hasTextureOverrideChecks = true;
		if (slot >= 32) {
			RequireAll();
			return;
		}
		requireVertexBufferMask |= 1u << slot;
		cachedFingerprint = 0;
	}

	void RequireDescriptorTarget(
		uint32_t kind,
		uint32_t stage,
		uint32_t slot)
	{
		hasDescriptorTargets = true;
		hasTextureOverrideChecks = true;
		for (const DescriptorTarget &target : descriptorTargets) {
			if (target.kind == kind &&
			    target.stage == stage &&
			    target.slot == slot)
				return;
		}
		DescriptorTarget target;
		target.kind = kind;
		target.stage = stage;
		target.slot = slot;
		descriptorTargets.push_back(target);
		cachedFingerprint = 0;
	}

	bool RequiresIndexBufferHash(bool indexed) const
	{
		return indexed && (requireAllIaBuffers || requireIndexBuffer);
	}

	bool RequiresIndexBuffer(bool indexed) const
	{
		return requireIndexBufferView || RequiresIndexBufferHash(indexed);
	}

	bool RequiresVertexBuffer(UINT slot) const
	{
		return requireAllIaBuffers ||
			(slot < 32 && (requireVertexBufferMask & (1u << slot)) != 0);
	}

	bool HasIaTargets() const
	{
		if (requireAllIaBuffers || requireIndexBuffer || requireIndexBufferView)
			return true;
		return requireVertexBufferMask != 0;
	}

	bool HasSameIaHashTargets(const DX12IaHashTargetPlan &other) const
	{
		if (requireAllIaBuffers != other.requireAllIaBuffers ||
			requireIndexBuffer != other.requireIndexBuffer ||
			requireIndexBufferView != other.requireIndexBufferView)
			return false;
		return requireVertexBufferMask == other.requireVertexBufferMask;
	}

	UINT64 Fingerprint() const
	{
		if (cachedFingerprint)
			return cachedFingerprint;
		UINT64 key = requireAllIaBuffers ? 0xf17a11b7a11b7a11ull : 0x27a7b125f00d1234ull;
		key ^= requireIndexBuffer ? 0x9e3779b97f4a7c15ull : 0ull;
		key ^= requireIndexBufferView ? 0xb7e151628aed2a6bull : 0ull;
		key ^= hasDescriptorTargets ? 0x51ed1234cafe9876ull : 0ull;
		for (UINT slot = 0; slot < 32; ++slot) {
			if ((requireVertexBufferMask & (1u << slot)) == 0)
				continue;
			key ^= static_cast<UINT64>(slot + 1) * 0x100000001b3ull;
			key ^= key >> 33;
			key *= 0xff51afd7ed558ccdull;
		}
		cachedFingerprint = key ? key : 1;
		return cachedFingerprint;
	}
};



enum class DX12CompiledDrawDecisionKind : uint32_t
{
	NoopNeverMatches = 0,
	NeedsIaHash = 1,
	NeedsDescriptorMatch = 2,
	NeedsReplacement = 3,
	SkipOriginal = 4
};

class DX12CompiledDrawDecision
{
public:
	DX12CompiledDrawDecisionKind kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	bool resolved = false;
	bool hasActiveShaderOverrides = false;
	bool hasShaderOverrides = false;
	bool hasShaderOverrideSkip = false;
	bool targetPlanResolved = false;
	bool targetPlanHasIaTargets = false;
	bool targetPlanHasDescriptorTargets = false;
	bool directTargetPlanResolved = false;
	bool directTextureChecksOnly = false;
	bool negativeCacheSafe = false;
	bool shaderOverrideNegativeCacheAllowed = false;
	// Every descriptor texture target in this decision's plans is an
	// unconditional static replacement handled at descriptor write time
	// (CreateShaderResourceView / CopyDescriptors), so the draw path never
	// needs to resolve SRV bindings for them.
	bool descriptorTargetsStatic = false;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;
	DX12DrawMatchUsage targetDrawMatchUsage;
	DX12DrawMatchUsage directTargetDrawMatchUsage;
	std::shared_ptr<const DX12ModIaReplacement> staticReplacementSnapshot;
	bool staticReplacementReady = false;

	void Reset()
	{
		kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
		pipelineState = nullptr;
		reloadGeneration = 0;
		resolved = false;
		hasActiveShaderOverrides = false;
		hasShaderOverrides = false;
		hasShaderOverrideSkip = false;
		targetPlanResolved = false;
		targetPlanHasIaTargets = false;
		targetPlanHasDescriptorTargets = false;
		directTargetPlanResolved = false;
		directTextureChecksOnly = false;
		negativeCacheSafe = false;
		shaderOverrideNegativeCacheAllowed = false;
		descriptorTargetsStatic = false;
		targetPlan = DX12IaHashTargetPlan();
		directTargetPlan = DX12IaHashTargetPlan();
		targetDrawMatchUsage = DX12DrawMatchUsage();
		directTargetDrawMatchUsage = DX12DrawMatchUsage();
		staticReplacementSnapshot.reset();
		staticReplacementReady = false;
	}

	void SetNoopNeverMatches(
		ID3D12PipelineState *state,
		UINT64 generation,
		bool activeShaderOverrides)
	{
		Reset();
		kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
		pipelineState = state;
		reloadGeneration = generation;
		resolved = true;
		hasActiveShaderOverrides = activeShaderOverrides;
	}

	void SetFromTargetPlan(
		ID3D12PipelineState *state,
		UINT64 generation,
		bool activeShaderOverrides,
		bool shaderOverrides,
		bool shaderOverrideSkip,
		const DX12IaHashTargetPlan &plan,
		const DX12IaHashTargetPlan &directPlan,
		const DX12DrawMatchUsage &drawMatchUsage,
		const DX12DrawMatchUsage &directDrawMatchUsage,
		bool directChecksOnly,
		bool descriptorTrackingNeeded)
	{
		Reset();
		pipelineState = state;
		reloadGeneration = generation;
		resolved = true;
		hasActiveShaderOverrides = activeShaderOverrides;
		hasShaderOverrides = shaderOverrides;
		hasShaderOverrideSkip = shaderOverrideSkip;
		targetPlan = plan;
		directTargetPlan = directPlan;
		targetDrawMatchUsage = drawMatchUsage;
		directTargetDrawMatchUsage = directDrawMatchUsage;
		targetPlanResolved = true;
		directTargetPlanResolved = true;
		targetPlanHasIaTargets = targetPlan.HasIaTargets();
		targetPlanHasDescriptorTargets = targetPlan.hasDescriptorTargets;
		directTextureChecksOnly = directChecksOnly;
		// Negative draw results are unsafe only when this PSO's own command
		// lists can observe descriptor contents. A global descriptor-tracking
		// need from another PSO should not disable IA-only draw miss caching.
		(void)descriptorTrackingNeeded;
		negativeCacheSafe = !targetPlanHasDescriptorTargets &&
			(targetPlan.hasTextureOverrideChecks || !targetPlanHasIaTargets);
		// The interpreter can cache only misses that prove they did not reach a
		// descriptor target. The outer PSO cache remains descriptor-conservative.
		shaderOverrideNegativeCacheAllowed =
			negativeCacheSafe || targetPlan.hasTextureOverrideChecks;

		if (!hasShaderOverrideSkip &&
			!targetPlanHasIaTargets &&
			!targetPlanHasDescriptorTargets) {
			kind = DX12CompiledDrawDecisionKind::NoopNeverMatches;
		} else if (hasShaderOverrideSkip) {
			kind = DX12CompiledDrawDecisionKind::SkipOriginal;
		} else if (targetPlanHasDescriptorTargets) {
			kind = DX12CompiledDrawDecisionKind::NeedsDescriptorMatch;
		} else if (targetPlanHasIaTargets) {
			kind = DX12CompiledDrawDecisionKind::NeedsIaHash;
		} else {
			kind = DX12CompiledDrawDecisionKind::NeedsReplacement;
		}
	}

	bool NeedsIaHash() const
	{
		return targetPlanHasIaTargets;
	}

	bool NeedsDescriptorMatch() const
	{
		return targetPlanHasDescriptorTargets;
	}

	bool MatchesGlobals(bool activeShaderOverrides) const
	{
		return hasActiveShaderOverrides == activeShaderOverrides;
	}
};

using DX12SimulateContextDispatchExecutionTemplate =
	DX12CompiledDispatchDecision;
using DX12SimulateContextDrawExecutionTemplate =
	DX12CompiledDrawDecision;

class DX12CommandListTrackingState
{
public:
	bool trackBindings = false;
	bool trackIaBindings = false;
	bool trackPipelineStateBindings = false;
	bool recordBindingEvents = false;
	bool trackHuntIa = false;
	bool trackPsoState = false;
};



class DX12CommandListTrackingCache : public DX12CommandListTrackingState
{
public:
	UINT64 generation = 0;
};


class DX12PreSkinBindingCacheIdentity
{
public:
	std::uint64_t bindingSerial = 0;
	std::uint64_t descriptorHeapVersion = 0;
	std::uint64_t bufferResolveGeneration = 0;

	bool operator==(const DX12PreSkinBindingCacheIdentity &other) const
	{
		return bindingSerial == other.bindingSerial &&
			descriptorHeapVersion == other.descriptorHeapVersion &&
			bufferResolveGeneration == other.bufferResolveGeneration;
	}

	bool operator!=(const DX12PreSkinBindingCacheIdentity &other) const
	{
		return !(*this == other);
	}
};

class DX12PreSkinCacheIdentityUtils
{
public:
	static constexpr std::uint64_t NoDescriptorHeapVersion =
		~std::uint64_t{0};

	static bool CanReuseBindings(
		bool hasCachedBindings,
		const DX12PreSkinBindingCacheIdentity &cached,
		const DX12PreSkinBindingCacheIdentity &current)
	{
		// A bound heap with version 0 is unknown to the resource tracker. Reusing
		// its descriptor snapshot could hide a later descriptor update from Mod
		// matching, so keep the conservative rebuild behavior in that case.
		return hasCachedBindings && current.descriptorHeapVersion != 0 &&
			cached == current;
	}

	static std::uint64_t MakeProbeFingerprint(
		std::uint64_t bindingFingerprint,
		std::uint64_t iaContentEpoch)
	{
		// PreSkin candidate selection depends on CS identity and the stable
		// Binding State fingerprint. Dispatch/Copy activity does not alter that
		// plan; positive hits still execute replacement commands every dispatch.
		(void)iaContentEpoch;
		return bindingFingerprint ? bindingFingerprint : 1;
	}
};


class DX12IaBufferHash
{
public:
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};




class DX12IaHashState
{
public:
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	std::vector<DX12IaBufferHash> vertexBuffers;
	// Compute dispatch bindings: UAV/SRV/CBV buffer hashes for PreSkin matching.
	std::vector<DX12IaBufferHash> computeBuffers;
};


class DX12IaDrawInvocation
{
public:
	bool indexed = false;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	static DX12IaDrawInvocation ForDrawInstanced(
		uint32_t vertexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = false;
		draw.vertexCount = vertexCount;
		draw.instanceCount = instanceCount;
		draw.firstVertex = firstVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}

	static DX12IaDrawInvocation ForDrawIndexedInstanced(
		uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstIndex, INT baseVertex, uint32_t firstInstance)
	{
		DX12IaDrawInvocation draw;
		draw.indexed = true;
		draw.indexCount = indexCount;
		draw.instanceCount = instanceCount;
		draw.firstIndex = firstIndex;
		draw.baseVertex = baseVertex;
		draw.firstInstance = firstInstance;
		return draw;
	}
};



class DX12ResidentCacheGrowthUtils
{
public:
	static size_t NextBudget(size_t currentBudget, size_t minRequired)
	{
		size_t next = currentBudget ? currentBudget : 256;
		while (next < minRequired) {
			const size_t doubled = next * 2;
			if (doubled <= next) {
				next = minRequired;
				break;
			}
			next = doubled;
		}
		return next;
	}

	template <class AssocContainer>
	static void EnsureBudget(
		size_t *budget,
		AssocContainer *entries,
		size_t minRequired)
	{
		if (!budget || !entries || minRequired <= *budget)
			return;
		const size_t next = NextBudget(*budget, minRequired);
		entries->reserve(next);
		*budget = next;
	}
};

class DX12DirectIaFastRejectCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	bool requiresIndexBuffer = false;
	UINT32 requiredVertexMask = 0;
	bool hasIndexBuffer = false;
	UINT64 indexBufferLocation = 0;
	UINT indexBufferSize = 0;
	UINT indexBufferFormat = 0;
	UINT32 vertexBufferMask = 0;
	std::array<UINT64, 32> vertexBufferLocations = {};
	std::array<UINT, 32> vertexBufferSizes = {};
	std::array<UINT, 32> vertexBufferStrides = {};
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;

	bool operator==(const DX12DirectIaFastRejectCacheKey &other) const
	{
		if (reloadGeneration != other.reloadGeneration ||
			preSkinGeneration != other.preSkinGeneration ||
			targetPlanFingerprint != other.targetPlanFingerprint ||
			iaViewFingerprint != other.iaViewFingerprint ||
			contentGeneration != other.contentGeneration ||
			resolveGeneration != other.resolveGeneration ||
			requiresIndexBuffer != other.requiresIndexBuffer ||
			requiredVertexMask != other.requiredVertexMask ||
			hasIndexBuffer != other.hasIndexBuffer ||
			indexBufferLocation != other.indexBufferLocation ||
			indexBufferSize != other.indexBufferSize ||
			indexBufferFormat != other.indexBufferFormat ||
			vertexBufferMask != other.vertexBufferMask ||
			indexed != other.indexed ||
			vertexCount != other.vertexCount ||
			indexCount != other.indexCount ||
			instanceCount != other.instanceCount ||
			firstVertex != other.firstVertex ||
			firstIndex != other.firstIndex ||
			baseVertex != other.baseVertex ||
			firstInstance != other.firstInstance)
			return false;

		// MakeKey leaves non-required slots zeroed; only required slots belong to
		// the logical key, so avoid comparing three complete 32-element arrays.
		UINT32 slots = requiredVertexMask;
		while (slots) {
			unsigned long slot = 0;
			_BitScanForward(&slot, slots);
			if (vertexBufferLocations[slot] != other.vertexBufferLocations[slot] ||
					vertexBufferSizes[slot] != other.vertexBufferSizes[slot] ||
					vertexBufferStrides[slot] != other.vertexBufferStrides[slot])
				return false;
			slots &= slots - 1;
		}
		return true;
	}
};

class DX12DirectIaFastRejectCacheKeyHash
{
public:
	static size_t Mix(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}

	size_t operator()(const DX12DirectIaFastRejectCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Mix(hash, key.reloadGeneration);
		hash = Mix(hash, key.preSkinGeneration);
		hash = Mix(hash, key.targetPlanFingerprint);
		hash = Mix(hash, key.iaViewFingerprint);
		hash = Mix(hash, key.contentGeneration);
		hash = Mix(hash, key.resolveGeneration);
		hash = Mix(hash, key.indexed ? 1 : 0);
		hash = Mix(hash, key.vertexCount);
		hash = Mix(hash, key.indexCount);
		hash = Mix(hash, key.instanceCount);
		hash = Mix(hash, key.firstVertex);
		hash = Mix(hash, key.firstIndex);
		hash = Mix(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		return Mix(hash, key.firstInstance);
	}
};

class DX12DirectIaFastRejectCache
{
public:
	static constexpr size_t MaxEntries = 16384;

	DX12DirectIaFastRejectCache() = default;

	DX12DirectIaFastRejectCache(const DX12DirectIaFastRejectCache &)
	{
	}

	DX12DirectIaFastRejectCache &operator=(const DX12DirectIaFastRejectCache &other)
	{
		if (this != &other) {
			entries.clear();
			hasScope = false;
			scopeReloadGeneration = 0;
		}
		return *this;
	}

	DX12DirectIaFastRejectCache(DX12DirectIaFastRejectCache &&) noexcept = default;
	DX12DirectIaFastRejectCache &operator=(DX12DirectIaFastRejectCache &&) noexcept = default;

	std::unordered_set<
		DX12DirectIaFastRejectCacheKey,
		DX12DirectIaFastRejectCacheKeyHash> entries;
	size_t growthBudget = 16384;
	bool hasScope = false;
	UINT64 scopeReloadGeneration = 0;
};

class DX12DirectIaFastRejectCacheUtils
{
public:
	static DX12DirectIaFastRejectCache &ThreadCache()
	{
		static thread_local DX12DirectIaFastRejectCache cache;
		return cache;
	}

	static UINT64 MixViewValue(UINT64 fingerprint, UINT64 value)
	{
		return fingerprint ^ (value + 0x9e3779b97f4a7c15ull +
			(fingerprint << 6) + (fingerprint >> 2));
	}

	static DX12DirectIaFastRejectCacheKey MakeKey(
		UINT64 reloadGeneration,
		UINT64 preSkinGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaDrawInvocation &draw)
	{
		DX12DirectIaFastRejectCacheKey key;
		key.reloadGeneration = reloadGeneration;
		key.preSkinGeneration = preSkinGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.contentGeneration = contentGeneration;
		key.resolveGeneration = resolveGeneration;
		key.requiresIndexBuffer = targetPlan.RequiresIndexBuffer(draw.indexed);
		UINT64 viewFingerprint = 0x84222325a9582d75ull;
		if (key.requiresIndexBuffer) {
			viewFingerprint = MixViewValue(
				viewFingerprint, ia.hasIndexBuffer ? 1 : 0);
			if (ia.hasIndexBuffer) {
				viewFingerprint = MixViewValue(
					viewFingerprint, ia.indexBuffer.BufferLocation);
				viewFingerprint = MixViewValue(
					viewFingerprint, ia.indexBuffer.SizeInBytes);
				viewFingerprint = MixViewValue(
					viewFingerprint, ia.indexBuffer.Format);
			}
		}
		key.hasIndexBuffer = key.requiresIndexBuffer && ia.hasIndexBuffer;
		if (key.hasIndexBuffer) {
			key.indexBufferLocation = ia.indexBuffer.BufferLocation;
			key.indexBufferSize = ia.indexBuffer.SizeInBytes;
			key.indexBufferFormat = ia.indexBuffer.Format;
		}
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (!targetPlan.RequiresVertexBuffer(slot))
				continue;
			key.requiredVertexMask |= 1u << slot;
			viewFingerprint = MixViewValue(viewFingerprint, slot + 1);
			viewFingerprint = MixViewValue(
				viewFingerprint, ia.hasVertexBuffer[slot] ? 1 : 0);
			if (!ia.hasVertexBuffer[slot])
				continue;
			const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
			key.vertexBufferMask |= 1u << slot;
			key.vertexBufferLocations[slot] = view.BufferLocation;
			key.vertexBufferSizes[slot] = view.SizeInBytes;
			key.vertexBufferStrides[slot] = view.StrideInBytes;
			viewFingerprint = MixViewValue(viewFingerprint, view.BufferLocation);
			viewFingerprint = MixViewValue(viewFingerprint, view.SizeInBytes);
			viewFingerprint = MixViewValue(viewFingerprint, view.StrideInBytes);
		}
		key.iaViewFingerprint = viewFingerprint;
		key.indexed = draw.indexed;
		key.vertexCount = draw.vertexCount;
		key.indexCount = draw.indexCount;
		key.instanceCount = draw.instanceCount;
		key.firstVertex = draw.firstVertex;
		key.firstIndex = draw.firstIndex;
		key.baseVertex = draw.baseVertex;
		key.firstInstance = draw.firstInstance;
		return key;
	}

	static bool Contains(
		DX12DirectIaFastRejectCache *cache,
		const DX12DirectIaFastRejectCacheKey &key)
	{
		if (!cache)
			return false;
		PrepareScope(cache, key.reloadGeneration);
		return cache->entries.find(key) != cache->entries.end();
	}

	static bool Store(
		DX12DirectIaFastRejectCache *cache,
		DX12DirectIaFastRejectCacheKey key)
	{
		if (!cache)
			return false;
		PrepareScope(cache, key.reloadGeneration);
		if (cache->entries.size() >= DX12DirectIaFastRejectCache::MaxEntries &&
		    !cache->entries.empty()) {
			cache->entries.erase(cache->entries.begin());
		}
		DX12ResidentCacheGrowthUtils::EnsureBudget(
			&cache->growthBudget, &cache->entries,
			(std::min)(cache->entries.size() + 1, DX12DirectIaFastRejectCache::MaxEntries));
		return cache->entries.insert(std::move(key)).second;
	}

	static void Clear(DX12DirectIaFastRejectCache *cache)
	{
		if (cache) {
			cache->entries.clear();
			cache->hasScope = false;
			cache->scopeReloadGeneration = 0;
		}
	}

private:
	static void PrepareScope(
		DX12DirectIaFastRejectCache *cache,
		UINT64 reloadGeneration)
	{
		if (cache->hasScope &&
				cache->scopeReloadGeneration == reloadGeneration)
			return;
		cache->entries.clear();
		cache->hasScope = true;
		cache->scopeReloadGeneration = reloadGeneration;
	}
};




class DX12IaHashStateCacheKey
{
public:
	UINT64 targetPlanFingerprint = 0;
	UINT64 iaViewFingerprint = 0;
	UINT64 iaContentEpoch = 0;
	UINT64 bufferContentGeneration = 0;
	UINT64 bufferResolveGeneration = 0;
	bool usesGlobalFallback = false;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;

	__forceinline bool operator==(const DX12IaHashStateCacheKey &other) const
	{
		return targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
			iaContentEpoch == other.iaContentEpoch &&
			bufferContentGeneration == other.bufferContentGeneration &&
			bufferResolveGeneration == other.bufferResolveGeneration &&
			usesGlobalFallback == other.usesGlobalFallback &&
			indexed == other.indexed &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex;
	}
};

class DX12IaHashStateCacheKeyHash
{
public:
	static __forceinline size_t Combine(size_t hash, UINT64 value)
	{
		hash ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2);
		return hash;
	}

	__forceinline size_t operator()(const DX12IaHashStateCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Combine(hash, key.targetPlanFingerprint);
		hash = Combine(hash, key.iaViewFingerprint);
		hash = Combine(hash, key.iaContentEpoch);
		hash = Combine(hash, key.bufferContentGeneration);
		hash = Combine(hash, key.bufferResolveGeneration);
		hash = Combine(hash, key.usesGlobalFallback ? 1 : 0);
		hash = Combine(hash, key.indexed ? 1 : 0);
		hash = Combine(hash, key.vertexCount);
		hash = Combine(hash, key.indexCount);
		hash = Combine(hash, key.firstVertex);
		hash = Combine(hash, key.firstIndex);
		hash = Combine(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		return hash;
	}
};

class DX12IaHashStateCacheEntry
{
public:
	bool hasIaHashState = false;
	DX12IaHashState state;
	std::list<DX12IaHashStateCacheKey>::iterator lruPosition;
};

class DX12IaHashKeyPrefixCacheEntry
{
public:
	bool valid = false;
	bool indexed = false;
	UINT64 iaEpoch = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT64 globalContentGeneration = 0;
	UINT64 globalResolveGeneration = 0;
	bool usesGlobalFallback = false;
	DX12IaHashStateCacheKey prefix;
};

class DX12IaHashKeyPrefixCacheUtils
{
public:
	static __forceinline bool TryLoadStable(
		const DX12IaHashKeyPrefixCacheEntry &entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
		DX12IaHashStateCacheKey *key)
	{
		if (!key || !entry.valid || entry.iaEpoch != iaEpoch ||
				entry.targetPlanFingerprint != targetPlanFingerprint ||
				entry.indexed != indexed || entry.usesGlobalFallback)
			return false;
		*key = entry.prefix;
		return true;
	}

	static __forceinline bool TryLoad(
		const DX12IaHashKeyPrefixCacheEntry &entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
			UINT64 globalContentGeneration,
			UINT64 globalResolveGeneration,
			DX12IaHashStateCacheKey *key)
	{
		if (!key || !entry.valid || entry.iaEpoch != iaEpoch ||
				entry.targetPlanFingerprint != targetPlanFingerprint ||
				entry.indexed != indexed)
			return false;
		if (entry.usesGlobalFallback &&
				(entry.globalContentGeneration != globalContentGeneration ||
				 entry.globalResolveGeneration != globalResolveGeneration))
			return false;
		*key = entry.prefix;
		return true;
	}

	static __forceinline void Store(
		DX12IaHashKeyPrefixCacheEntry *entry,
		UINT64 iaEpoch,
		UINT64 targetPlanFingerprint,
		bool indexed,
		UINT64 globalContentGeneration,
		UINT64 globalResolveGeneration,
		const DX12IaHashStateCacheKey &prefix)
	{
		if (!entry)
			return;
		entry->iaEpoch = iaEpoch;
		entry->targetPlanFingerprint = targetPlanFingerprint;
		entry->indexed = indexed;
		entry->globalContentGeneration = globalContentGeneration;
		entry->globalResolveGeneration = globalResolveGeneration;
		entry->usesGlobalFallback = prefix.usesGlobalFallback;
		entry->prefix = prefix;
		entry->valid = true;
	}
};

class DX12IaHashStateCache
{
public:
	static constexpr size_t MaxEntries = 16384;

	DX12IaHashStateCache() = default;

	// Cache contents are derived from runtime state and do not belong in value snapshots.
	// Dropping them on copy also avoids copying list iterators into the source list.
	DX12IaHashStateCache(const DX12IaHashStateCache &)
	{
	}

	DX12IaHashStateCache &operator=(const DX12IaHashStateCache &other)
	{
		if (this != &other) {
			entries.clear();
			lruKeys.clear();
		}
		return *this;
	}

	DX12IaHashStateCache(DX12IaHashStateCache &&) noexcept = default;
	DX12IaHashStateCache &operator=(DX12IaHashStateCache &&) noexcept = default;

	std::list<DX12IaHashStateCacheKey> lruKeys;
	std::unordered_map<
		DX12IaHashStateCacheKey,
		DX12IaHashStateCacheEntry,
		DX12IaHashStateCacheKeyHash> entries;
	size_t growthBudget = 16384;
};

class DX12IaHashStateCacheUtils
{
public:
	// Command lists recorded on the same CPU thread commonly reference identical
	// immutable IA regions. The full key already carries view identity, resource
	// lifetime/content generations, target plan, and draw range, so cache ownership
	// does not need to be restricted to one command list.
	static DX12IaHashStateCache &ThreadCache()
	{
		static thread_local DX12IaHashStateCache cache;
		return cache;
	}

	static __forceinline bool TryLoad(
		DX12IaHashStateCache *cache,
		const DX12IaHashStateCacheKey &key,
		const DX12IaHashState **state,
		bool *hasIaHashState)
	{
		if (!cache || !state || !hasIaHashState)
			return false;

		auto found = cache->entries.find(key);
		if (found == cache->entries.end())
			return false;

		cache->lruKeys.splice(
			cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
		found->second.lruPosition = cache->lruKeys.begin();
		*hasIaHashState = found->second.hasIaHashState;
		*state = &found->second.state;
		return true;
	}

	static const DX12IaHashState *Store(
		DX12IaHashStateCache *cache,
		const DX12IaHashStateCacheKey &key,
		bool hasIaHashState,
		DX12IaHashState state,
		bool *evicted)
	{
		if (evicted)
			*evicted = false;
		if (!cache)
			return nullptr;

		auto found = cache->entries.find(key);
		if (found != cache->entries.end()) {
			cache->lruKeys.splice(
				cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
			found->second.lruPosition = cache->lruKeys.begin();
			found->second.hasIaHashState = hasIaHashState;
			found->second.state = std::move(state);
			return &found->second.state;
		}
		if (cache->entries.size() >= DX12IaHashStateCache::MaxEntries &&
		    !cache->lruKeys.empty()) {
			const DX12IaHashStateCacheKey &oldest = cache->lruKeys.back();
			cache->entries.erase(oldest);
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}
		DX12ResidentCacheGrowthUtils::EnsureBudget(
			&cache->growthBudget, &cache->entries,
			(std::min)(cache->entries.size() + 1, DX12IaHashStateCache::MaxEntries));

		cache->lruKeys.push_front(key);
		DX12IaHashStateCacheEntry entry;
		entry.hasIaHashState = hasIaHashState;
		entry.state = std::move(state);
		entry.lruPosition = cache->lruKeys.begin();
		auto inserted = cache->entries.emplace(key, std::move(entry));
		return inserted.second ? &inserted.first->second.state : nullptr;
	}

	static void Clear(DX12IaHashStateCache *cache)
	{
		if (!cache)
			return;
		cache->entries.clear();
		cache->lruKeys.clear();
	}
};




// Exact, command-list-local negative result. Unlike the legacy unordered cache,
// this key uses the IA mutation epoch as the identity of all bound views. The
// global generations cover writes/resource reuse, so no content-hash result is
// reused after any dependency can have changed.
class DX12ResidentDrawNegativeEntry
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 iaEpoch = 0;
	UINT64 graphicsDescriptorQuerySerial = 0;
	ID3D12DescriptorHeap *graphicsDescriptorHeap = nullptr;
	UINT64 graphicsDescriptorHeapVersion = 0;
	UINT64 targetPlanFingerprint = 0;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;
	bool indexed = false;
	bool valid = false;
};

// Top-level draw-result cache that captures the full bound-state identity.
// When nothing changes (PSO / IA / root descriptors / heap), the per-draw
// override decision must be identical. A hit here skips the entire multi-layer
// dynamic decision chain (IA hash, descriptor match, resource hash, etc.).
class DX12BoundDrawResultCache
{
public:
	bool valid = false;
	bool hasOverride = false;
	UINT64 iaEpoch = 0;
	UINT64 iaContentEpoch = 0;
	// Global buffer-content generation (gBufferContentGeneration): the
	// per-command-list iaContentEpoch only bumps on that list's own copy
	// calls, so content written by another list (upload list, Map/Unmap on
	// a shared buffer) would otherwise leave this cache hit with a stale
	// decision across command-list Reset / scene transitions - the root
	// cause of intermittent wrong IA replacement (shadow mismatch flicker).
	UINT64 bufferContentGeneration = 0;
	UINT64 graphicsDescriptorQuerySerial = 0;
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 preSkinGeneration = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	bool indexed = false;
	UINT vertexCount = 0;
	UINT indexCount = 0;
	UINT instanceCount = 0;
	UINT firstVertex = 0;
	UINT firstIndex = 0;
	INT baseVertex = 0;
	UINT firstInstance = 0;

	void Reset()
	{
		valid = false;
		hasOverride = false;
	}

	bool Matches(
		UINT64 currentIaEpoch,
		UINT64 currentIaContentEpoch,
		UINT64 currentBufferContentGeneration,
		UINT64 currentGraphicsDescriptorQuerySerial,
		ID3D12PipelineState *currentPso,
		UINT64 currentReloadGeneration,
		UINT64 currentPreSkinGeneration,
		ID3D12DescriptorHeap *currentHeap,
		UINT64 currentHeapVersion,
		ID3D12RootSignature *currentRootSig,
		const DX12IaDrawInvocation &draw,
		bool compareDescriptorState,
		bool compareDrawArgs) const
	{
		return valid &&
			iaEpoch == currentIaEpoch &&
			iaContentEpoch == currentIaContentEpoch &&
			bufferContentGeneration == currentBufferContentGeneration &&
			pipelineState == currentPso &&
			reloadGeneration == currentReloadGeneration &&
			(!compareDescriptorState ||
				(graphicsDescriptorQuerySerial == currentGraphicsDescriptorQuerySerial &&
				 preSkinGeneration == currentPreSkinGeneration &&
				 cbvSrvUavHeap == currentHeap &&
				 cbvSrvUavHeapVersion == currentHeapVersion &&
				 graphicsRootSignature == currentRootSig)) &&
			(!compareDrawArgs ||
				(indexed == draw.indexed &&
				 vertexCount == draw.vertexCount &&
				 indexCount == draw.indexCount &&
				 instanceCount == draw.instanceCount &&
				 firstVertex == draw.firstVertex &&
				 firstIndex == draw.firstIndex &&
				 baseVertex == draw.baseVertex &&
				 firstInstance == draw.firstInstance));
	}

	void Capture(
		UINT64 currentIaEpoch,
		UINT64 currentIaContentEpoch,
		UINT64 currentBufferContentGeneration,
		UINT64 currentGraphicsDescriptorQuerySerial,
		ID3D12PipelineState *currentPso,
		UINT64 currentReloadGeneration,
		UINT64 currentPreSkinGeneration,
		ID3D12DescriptorHeap *currentHeap,
		UINT64 currentHeapVersion,
		ID3D12RootSignature *currentRootSig,
		const DX12IaDrawInvocation &draw,
		bool overrideResult)
	{
		valid = true;
		hasOverride = overrideResult;
		iaEpoch = currentIaEpoch;
		iaContentEpoch = currentIaContentEpoch;
		bufferContentGeneration = currentBufferContentGeneration;
		graphicsDescriptorQuerySerial = currentGraphicsDescriptorQuerySerial;
		pipelineState = currentPso;
		reloadGeneration = currentReloadGeneration;
		preSkinGeneration = currentPreSkinGeneration;
		cbvSrvUavHeap = currentHeap;
		cbvSrvUavHeapVersion = currentHeapVersion;
		graphicsRootSignature = currentRootSig;
		indexed = draw.indexed;
		vertexCount = draw.vertexCount;
		indexCount = draw.indexCount;
		instanceCount = draw.instanceCount;
		firstVertex = draw.firstVertex;
		firstIndex = draw.firstIndex;
		baseVertex = draw.baseVertex;
		firstInstance = draw.firstInstance;
	}
};

class DX12SimulateContextDrawTemplate
{
public:
	static constexpr size_t DirectIaCandidateSetCount = 512;
	static constexpr size_t DirectIaCandidateWayCount = 8;
	static constexpr size_t ResidentDrawNegativeEntryCount = 128;
	static_assert((DirectIaCandidateSetCount & (DirectIaCandidateSetCount - 1)) == 0,
		"Direct IA candidate set count must be a power of two");
	static_assert((ResidentDrawNegativeEntryCount & (ResidentDrawNegativeEntryCount - 1)) == 0,
		"Resident draw negative entry count must be a power of two");

	class ResolvedPlanState
	{
	public:
		bool dirty = true;
		UINT64 invalidationSerial = 1;
	};

	class CachedIaHashBuildEntry
	{
	public:
		bool valid = false;
		bool hasState = false;
		DX12IaHashStateCacheKey key;
		DX12IaHashState state;
	};

	class DirectIaCandidateEntry
	{
	public:
		bool valid = false;
		bool hasCandidate = false;
		UINT64 age = 0;
		size_t tag = 0;
		DX12IaHashStateCacheKey iaKey;
		UINT instanceCount = 0;
		UINT firstInstance = 0;
	};

	class DirectIaCandidateCache
	{
	public:
		using Entries = std::array<DirectIaCandidateEntry,
			DirectIaCandidateSetCount * DirectIaCandidateWayCount>;

		DirectIaCandidateCache() = default;

		DirectIaCandidateCache(const DirectIaCandidateCache &)
		{
		}

		DirectIaCandidateCache &operator=(const DirectIaCandidateCache &other)
		{
			if (this != &other) {
				entries.reset();
				clock = 0;
			}
			return *this;
		}

		DirectIaCandidateCache(DirectIaCandidateCache &&) noexcept = default;
		DirectIaCandidateCache &operator=(DirectIaCandidateCache &&) noexcept = default;

		Entries *EnsureEntries()
		{
			if (!entries)
				entries = std::make_unique<Entries>();
			return entries.get();
		}

		std::unique_ptr<Entries> entries;
		UINT64 clock = 0;
	};

	class IaTargetPresenceEntry
	{
	public:
		bool valid = false;
		bool hasTarget = false;
		UINT64 iaEpoch = 0;
		UINT64 targetPlanFingerprint = 0;
		bool indexed = false;
	};

	// IA-state-level negative cache: when the compiled prefilter tables are
	// draw-count independent (gIaPrefilterCountSensitive == 0), "this IA state
	// has no override candidate" is a property of the bound buffer views alone
	// (DX11: hash saved at bind time, map lookup miss ends the draw). Caching it
	// here lets every draw that shares the same IA bindings skip the whole
	// per-draw candidate chain.
	class IaStateNegativeCache
	{
	public:
		bool valid = false;
		bool noCandidate = false;
		UINT64 iaEpoch = 0;
		UINT64 iaContentEpoch = 0;
		// Global buffer-content generation: per-list iaContentEpoch misses
		// content written by other lists / Map, which would otherwise leave
		// a stale "no candidate" verdict across list Reset (missing mesh
		// replacement on shadow passes after scene transitions).
		UINT64 bufferContentGeneration = 0;
		UINT64 reloadGeneration = 0;
		UINT64 targetPlanFingerprint = 0;
		ID3D12PipelineState *pipelineState = nullptr;
	};

	IaStateNegativeCache iaStateNegative;

	DX12IaHashStateCache iaHashStateCache;
	// Direct-target and full-target key prefixes. Resolved IA resources carry
	// their own content/lifetime generations; global epochs are only consulted
	// for unresolved fallback keys.
	std::array<DX12IaHashKeyPrefixCacheEntry, 2> iaHashKeyPrefixes;
	CachedIaHashBuildEntry lastDirectIaHashBuild;
	CachedIaHashBuildEntry lastFullIaHashBuild;
	// Candidate matching is separate from IA-state materialization. Entries are
	// keyed by the complete resolved IA identity plus the draw inputs that do not
	// belong to the IA hash state itself.
	DirectIaCandidateCache directIaCandidateCache;
	// IA target-presence checks depend only on the resident IA bindings, the
	// compiled target plan, and indexed mode. Keep both semantics separately:
	// direct checks consider descriptor targets while full checks do not.
	IaTargetPresenceEntry directIaTargetPresence;
	IaTargetPresenceEntry fullIaTargetPresence;
	// Exact direct-mapped resident cache: the common repeated negative draw is a
	// handful of integer comparisons, with no 32-slot scan or unordered lookup.
	std::array<DX12ResidentDrawNegativeEntry,
		ResidentDrawNegativeEntryCount> residentDrawNegativeCache;
	// Unlike IA caches, a resolved plan describes the current command-list
	// bindings and must never survive Reset or a graphics-state transition.
	ResolvedPlanState plan;
	// Bound-state draw-result cache: captures the full PSO/IA/descriptor
	// identity so repeated draws with unchanged state skip the entire
	// dynamic decision chain (IA hash, descriptor match, resource hash).
	DX12BoundDrawResultCache boundDrawResult;
};

class DX12ResolvedDrawRuntimeStateUtils
{
public:
	static void Clear(DX12SimulateContextDrawTemplate *state);
	static void InvalidatePlan(DX12SimulateContextDrawTemplate *state);
	static void EnsureScene(DX12SimulateContextDrawTemplate *state);
	static void NoteLookup(
		DX12SimulateContextDrawTemplate *state,
		bool hit);
};

class DX12SimulateContextComputeTemplate
{
public:
	bool bindingsValid = false;
	bool bindingsFoundAny = false;
	bool bindingsUseRootValues = false;
	UINT64 bindingsSerial = 0;
	UINT64 bindingsDescriptorVersion = 0;
	UINT64 bindingsDescriptorQuerySerial = 0;
	UINT64 bindingsDependencyFingerprint = 0;
	ID3D12DescriptorHeap *bindingsHeap = nullptr;
	ID3D12RootSignature *bindingsRootSignature = nullptr;
	// Exact root-binding identity snapshot for cache validation. The game
	// re-binds compute root tables on every dispatch (and bumps
	// computeBindingSerial / root revisions), which made the previous
	// serial/revision validation miss on every dispatch and forced a full
	// binding re-collect per dispatch. Same root signature + same root
	// table/descriptor identities + same observed slot versions prove the
	// collected bindings are identical, so the cache hits across re-binds.
	std::array<UINT64, MaxRootParameters> bindingsRootIdentities;
	std::array<bool, MaxRootParameters> bindingsRootValid;
	std::vector<UINT> bindingsRootParameters;
	std::vector<UINT64> bindingsRootRevisions;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
	// Content-addressed per-slot snapshot for cache validation across
	// descriptor-ring rotation: (root, offset-from-table-base, version,
	// valueId). Validation recomputes the current absolute slot from the
	// current table base, so the cache can hit when the game re-binds the
	// same descriptor values at a different ring location. This is the DX12
	// model - value identity is authoritative and the GPU handle / ring slot
	// is transient - instead of DX11's binding-slot identity.
	struct RuntimeObservedSlot
	{
		UINT root = 0;
		UINT offset = 0;
		UINT64 version = 0;
		UINT64 valueId = 0;
	};
	std::vector<RuntimeObservedSlot> bindingsObservedSlots;
	// Table GPU base pointer captured at store time. Validation may skip the
	// slot identity read when the descriptor query serial is unchanged AND
	// every observed table still points at the same base; a moved table means
	// the ring rotated and the new slots must be re-validated even if no
	// descriptor write happened since the last check.
	std::array<UINT64, MaxRootParameters> bindingsRootTableBases = {};
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;
	// Local lock-free probe verdict cache: (csHash, probeFingerprint,
	// reloadGeneration) -> applied + dispatch override. Recording is
	// single-threaded per command list and this template survives Reset, so
	// repeated dispatches across frames hit here instead of taking the
	// global mod lock + map lookup on every dispatch.
	struct LocalProbeCacheEntry
	{
		UINT64 reloadGeneration = 0;
		uint64_t csHash = 0;
		UINT64 fingerprint = 0;
		UINT64 bindingDependencyFingerprint = 0;
		UINT64 bufferContentGeneration = 0;
		UINT64 iaContentEpoch = 0;
		UINT64 bindingSerial = 0;
		bool applied = false;
		bool valid = false;
		bool handlingSkip = false;
		bool hasDispatch = false;
		UINT groupsX = 0;
		UINT groupsY = 0;
		UINT groupsZ = 0;
	};
	static constexpr size_t LocalProbeCacheCount = 32;
	std::array<LocalProbeCacheEntry, LocalProbeCacheCount> localProbeCache = {};

	static size_t LocalProbeCacheSlot(
		UINT64 fingerprint, uint64_t csHash)
	{
		return static_cast<size_t>(
			(fingerprint ^ (csHash * 0x9e3779b97f4a7c15ull)) &
			(LocalProbeCacheCount - 1));
	}

	UINT64 bindingFingerprintDependency = 0;
	UINT64 bindingFingerprint = 0;
	UINT64 probeBindingDependencyFingerprint = 0;
	UINT64 probeFingerprint = 0;
	uint64_t probeCsHash = 0;
	UINT64 probeReloadGeneration = 0;
	UINT64 probeBindingSerial = 0;
	UINT64 probeIaContentEpoch = 0;
	// Global buffer-content generation at probe time: per-list epochs miss
	// content rewritten in place via CopyDescriptors / Map by other lists,
	// which would otherwise let the negative probe cache hit with a stale
	// "no replacement" verdict and make PreSkin replacement flicker on/off.
	UINT64 probeBufferContentGeneration = 0;
	bool probeResultValid = false;
	bool probeApplied = false;
	bool probeDispatchHandlingSkip = false;
	bool probeDispatchHasDispatch = false;
	UINT probeGroupsX = 0;
	UINT probeGroupsY = 0;
	UINT probeGroupsZ = 0;
};

class DX12ResolvedDescriptorQueryRuntimeState
{
public:
	bool valid = false;
	bool foundAny = false;
	bool compute = false;
	bool usesRootRevisions = false;
	UINT rangeType = UINT_MAX;
	UINT64 bindingQuerySerial = 0;
	UINT64 descriptorQuerySerial = 0;
	ID3D12DescriptorHeap *bindingsHeap = nullptr;
	// Root-binding snapshot used for cache validation. Revisions/serial bump
	// on every re-bind even when the game binds the same descriptors again,
	// which forced a full re-collect on every draw in binding-churn games
	// (e.g. ZZZ rebinds per draw; heapVersion changes thousands of times per
	// frame). Instead validate the exact bound identity: the GPU handle of
	// each root table / root descriptor. Same identity + same observed slot
	// versions => the collected bindings are identical, so the cache can hit
	// across re-binds. Content changes of observed slots are caught by
	// observedSlotVersions below.
	ID3D12RootSignature *rootSignature = nullptr;
	std::array<UINT64, MaxRootParameters> rootBindingIdentities;
	std::array<bool, MaxRootParameters> rootBindingValid;
	std::vector<UINT> rootParameters;
	std::vector<UINT64> rootRevisions;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
	std::vector<DX12CurrentComputeUavBinding> bindings;
};

class DX12ResolvedGraphicsSrvBinding
{
public:
	DX12CurrentShaderResourceBinding binding;
	uint32_t resourceHash = 0;
	uint32_t descriptorHash = 0;
};

class DX12ResolvedGraphicsSrvRuntimeState
{
public:
	bool valid = false;
	bool usesRootRevisions = false;
	UINT64 descriptorQuerySerial = 0;
	std::vector<UINT> rootParameters;
	std::vector<UINT64> rootRevisions;
	std::vector<DX12ResolvedGraphicsSrvBinding> bindings;
};

class DX12SimulateContext
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	IUnknown *commandListIdentity = nullptr;
	// Wrapper-side object translation cache. These pointers are only compared;
	// ownership remains with the application/wrapper objects.
	ID3D12PipelineState *lastWrappedPipelineState = nullptr;
	ID3D12PipelineState *lastUnwrappedPipelineState = nullptr;
	ID3D12RootSignature *lastWrappedComputeRootSignature = nullptr;
	ID3D12RootSignature *lastUnwrappedComputeRootSignature = nullptr;
	ID3D12RootSignature *lastWrappedGraphicsRootSignature = nullptr;
	ID3D12RootSignature *lastUnwrappedGraphicsRootSignature = nullptr;
	// Reused barrier scratch storage avoids one allocation/free cycle per API call.
	std::vector<D3D12_RESOURCE_BARRIER> barrierScratch;
	volatile UINT64 computeBindingSerial = 0;
	DX12ActiveIaState ia;
	UINT64 iaEpoch = 1;
	UINT64 iaContentEpoch = 1;
	DX12SimulateContextDrawTemplate resolvedDraw;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	// Cached CBV/SRV/UAV heap content version.  Re-read on every SetDescriptorHeaps
	// instead of on every draw: DX12GetDescriptorHeapVersion takes the global
	// descriptor lock plus a hash-map lookup, and draws outnumber heap changes by
	// ~250x (988 vs 4 per frame in the target game).
	UINT64 cbvSrvUavHeapVersion = 0;
	// Last raw SetDescriptorHeaps input. Lets CaptureDescriptorHeaps skip
	// redundant GetDesc resolution when the game re-binds the identical heap
	// set (the resident heap state already reflects it). Only recorded after
	// the resident state was actually updated, and cleared with the rest of
	// the context on command-list Reset.
	UINT lastSetHeapsCount = 0xFFFFFFFF;
	ID3D12DescriptorHeap *lastSetHeapsInput[2] = {};
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	bool runtimeSamplerHeapResolved = false;
	DX12DescriptorHeapSummary runtimeSamplerHeap = {};
	UINT64 bindingQuerySerial = 1;
	UINT64 graphicsDescriptorQuerySerial = 1;
	UINT64 computeDescriptorQuerySerial = 1;
	UINT64 computeDescriptorBindingVersion = 1;
	// DXVK-style per-root-parameter dirty tracking for compute bindings.
	// RememberComputeRoot*() sets the bit for the changed root parameter
	// instead of bumping one global serial; probe/collect paths clear the
	// mask after they consume it. This keeps "binding churn on unrelated root
	// parameters" from invalidating the whole compute binding cache, exactly
	// like DXVK's bindingsDirty bitmask vs bindingsUsed intersection.
	UINT64 computeRootDirtyMask = 0;
	// Root parameters this compute root signature actually exposes to the
	// shader (from the parsed root signature summary). Only dirty bits that
	// intersect this mask invalidate the binding cache.
	UINT64 computeRootUsedMask = 0;
	bool computeRootUsedMaskValid = false;
	std::array<UINT64, MaxRootParameters> graphicsRootBindingRevisions = {};
	std::array<UINT64, MaxRootParameters> computeRootBindingRevisions = {};
	UINT graphicsRootTableValidCount = 0;
	UINT computeRootTableValidCount = 0;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	RootTableState graphicsRootTables[MaxRootParameters] = {};
	RootTableState computeRootTables[MaxRootParameters] = {};
	RootDescriptorState graphicsRootDescriptors[MaxRootParameters] = {};
	RootDescriptorState computeRootDescriptors[MaxRootParameters] = {};
	RootConstantsState graphicsRootConstants[MaxRootParameters] = {};
	RootConstantsState computeRootConstants[MaxRootParameters] = {};

	// Per-PSO override cache aligned with HotPathState generation.
	volatile LONG psoHasOverridesGeneration = -1;
	bool psoHasShaderOverrides = true;

	// Replacement PSO cache. First use may query global records; repeated use is direct.
	ID3D12PipelineState *cachedReplacementPsoKey = nullptr;
	ID3D12PipelineState *cachedReplacementPso = nullptr;

	// Compute shader hash cache, equivalent to DX11 current compute shader state.
	uint64_t cachedCsHash = 0;
	bool cachedCsHashValid = false;
	bool cachedCsHashResolved = false;

	// Stable compute binding fingerprint used as a dispatch probe cache key.
	uint64_t computeBindingHash = 0;

	// Compiled per-PSO dispatch policy. Dispatch consumes this instead of
	// rediscovering non-candidate PreSkin state on every command.
	DX12SimulateContextDispatchExecutionTemplate dispatchDecision;

	// Compiled per-PSO draw policy. Draw consumes this instead of rediscovering
	// PSO shader override metadata on every command.
	DX12SimulateContextDrawExecutionTemplate drawDecision;

	// Static replacement template cache, deliberately independent of
	// drawDecision: drawDecision.Reset() runs on every PSO transition, which
	// would discard an otherwise reusable replacement template (the game
	// changes PSO thousands of times per frame, so the template would be
	// regenerated on every encounter of the same PSO). Keyed by PSO pointer +
	// reload generation; a stale PSO pointer is only ever compared, never
	// dereferenced, matching the existing drawDecision.pipelineState pattern.
	ID3D12PipelineState *staticReplacementPso = nullptr;
	UINT64 staticReplacementReloadGeneration = 0;
	std::shared_ptr<const DX12ModIaReplacement> staticReplacementSnapshot;

	// Plan fix A: lazy static-template IA application. The template's IA
	// replacement is applied once when the template first becomes active on
	// this command list and kept live until the PSO changes or the game
	// explicitly rebinds IA, instead of applying + restoring on every draw
	// (which also bumped iaEpoch every draw and defeated every IA-keyed
	// negative cache). staticTemplateSavedIa snapshots the IA state right
	// before the apply so the restore can put back the exact original views.
	std::shared_ptr<const DX12ModIaReplacement> staticTemplateReplacement;
	bool staticTemplateIaApplied = false;
	DX12ActiveIaState staticTemplateSavedIa;

	// Cached device pointer to eliminate per-draw AcquireModDevice QI overhead.
	// Set during Reset from the command list; device must outlive the command list
	// per DX12 spec (device -> command list is a parent-child relationship).
	ID3D12Device *device = nullptr;

	DX12SimulateContextComputeTemplate resolvedCompute;
	// Resident descriptor-query results, keyed by descriptor-only serials so
	// unrelated root-constant churn does not invalidate SRV slot lookups.
	std::array<DX12ResolvedDescriptorQueryRuntimeState, 6> resolvedDescriptorQueries;
	DX12ResolvedGraphicsSrvRuntimeState resolvedGraphicsSrvs;
};




class DX12CommandListRuntimeShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext*> directStates;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12SimulateContext> states;
};



class DX12CommandListRuntimeTlsCache
{
public:
	// Per-thread command-list state cache. Games commonly interleave 3+
	// command lists on one recording thread (scene / shadow / UI / compute);
	// a single "previous" slot forced a shard lock + unordered_map lookup on
	// every alternation past two lists. A small move-to-front history keeps
	// the common interleave sets on the lock-free path: the hot slot is
	// checked first (two loads), then the history slots are scanned linearly
	// (cheap compared to the locked fallback).
	static constexpr size_t kHistorySlotCount = 7;

	class Slot
	{
	public:
		ID3D12GraphicsCommandList *commandList = nullptr;
		DX12CommandListRuntimeShard *shard = nullptr;
		bool stateIsDirect = false;
		UINT64 trackingGeneration = 0;
		DX12CommandListTrackingCache tracking;
		DX12SimulateContext *statePtr = nullptr;
	};

	// Hot slot, kept as direct members so the common single-list fast path
	// stays a couple of loads and compares.
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12CommandListRuntimeShard *shard = nullptr;
	bool stateIsDirect = false;
	UINT64 trackingGeneration = 0;
	DX12CommandListTrackingCache tracking;
	DX12SimulateContext *statePtr = nullptr;

	// History slots for additional interleaved command lists. A hit swaps
	// the matching slot into the hot position (move-to-front); a demoted hot
	// entry overwrites slots[nextSlot] ring-style, preferring an existing
	// entry for the same list so each list appears at most once.
	Slot slots[kHistorySlotCount];
	UINT32 nextSlot = 0;

	void SwapHotWith(Slot &slot)
	{
		ID3D12GraphicsCommandList *tmpCommandList = commandList;
		commandList = slot.commandList;
		slot.commandList = tmpCommandList;

		DX12CommandListRuntimeShard *tmpShard = shard;
		shard = slot.shard;
		slot.shard = tmpShard;

		bool tmpStateIsDirect = stateIsDirect;
		stateIsDirect = slot.stateIsDirect;
		slot.stateIsDirect = tmpStateIsDirect;

		UINT64 tmpTrackingGeneration = trackingGeneration;
		trackingGeneration = slot.trackingGeneration;
		slot.trackingGeneration = tmpTrackingGeneration;

		DX12CommandListTrackingCache tmpTracking = tracking;
		tracking = slot.tracking;
		slot.tracking = tmpTracking;

		DX12SimulateContext *tmpStatePtr = statePtr;
		statePtr = slot.statePtr;
		slot.statePtr = tmpStatePtr;
	}

	void ClearHot()
	{
		commandList = nullptr;
		shard = nullptr;
		stateIsDirect = false;
		trackingGeneration = 0;
		statePtr = nullptr;
	}
};




class DX12CommandListRuntime
{
public:
	static constexpr size_t kRuntimeShardCount = 64;

	static DX12CommandListRuntimeShard Shards[kRuntimeShardCount];
	static volatile UINT64 TrackingGenerationValue;
	static thread_local DX12CommandListRuntimeTlsCache Tls;

	static void RegisterDirectState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *state);
	static void UnregisterDirectState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext *GetDirectStatePtr(ID3D12GraphicsCommandList *commandList);
	static IUnknown *GetCommandListIdentity(ID3D12GraphicsCommandList *commandList);

	static UINT64 TrackingGeneration();
	static void BumpTrackingGeneration();
	static DX12CommandListRuntimeShard &ShardFor(ID3D12GraphicsCommandList *commandList);
	static void RefreshTrackingCache(DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState MakeTrackingState(const DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext *state);
	static void TlsRefreshFromState(
		DX12SimulateContext *state,
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeShard *shard,
		bool stateIsDirect);
	static DX12SimulateContext *TlsLookup(ID3D12GraphicsCommandList *commandList);
	static void TlsRefresh(DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList);
	static void GetTrackingCacheFast(ID3D12GraphicsCommandList *commandList, DX12CommandListTrackingCache &out);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext *GetStatePtrFast(ID3D12GraphicsCommandList *commandList);
	static void TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList);

	static void Register(ID3D12GraphicsCommandList *commandList);
	static bool Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(DX12SimulateContext *state, ID3D12PipelineState *initialState);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static DX12CompiledDispatchDecision BuildDispatchDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static DX12CompiledDrawDecision BuildDrawDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides);
	static const DX12CompiledDispatchDecision *EnsureDispatchDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static const DX12CompiledDrawDecision *EnsureDrawDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides);
	static void CompileDispatchDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static void CompileDrawDecision(
		DX12SimulateContext *state,
		bool hasShaderOverrides);
	static void InvalidateResolvedDrawPlan(DX12SimulateContext *state);
	static void RememberPipelineState(DX12SimulateContext *state, ID3D12PipelineState *pipelineState);
	static void RememberPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void ResetIa(DX12SimulateContext *state);
	static void ResetIa(ID3D12GraphicsCommandList *commandList);
	static bool RememberIndexBuffer(DX12SimulateContext *state, const D3D12_INDEX_BUFFER_VIEW *view);
	static bool RememberIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static bool RememberVertexBuffers(DX12SimulateContext *state, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool RememberVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static bool RememberPrimitiveTopology(DX12SimulateContext *state, D3D12_PRIMITIVE_TOPOLOGY topology);
	static bool RememberPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void BumpIaEpoch(DX12SimulateContext *state);
	static void BumpIaContentEpoch(DX12SimulateContext *state);
	static void BumpIaContentEpoch(ID3D12GraphicsCommandList *commandList);
	static bool BuildIaHashState(const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static bool BuildIaHashStateForTargets(const DX12ActiveIaState &ia, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static bool BuildIaHashStateForTargetsCached(DX12ActiveIaState *ia, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargets(const DX12ActiveIaState &ia, UINT64 iaContentEpoch, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);

	// Cross-TU cached per-view IA hash builders (DX11-style: hash saved at bind
	// time, reused at draw time, invalidated by content generation).
	static uint32_t GetOrBuildCachedIndexViewHash(DX12ActiveIaState *ia);
	static uint32_t GetOrBuildCachedVertexViewHash(DX12ActiveIaState *ia, UINT slot);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargetsCached(DX12SimulateContext *state, UINT cacheSlot, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);
	static ID3D12PipelineState *GetPipelineState(ID3D12GraphicsCommandList *commandList);
	static DX12SimulateContext GetState(ID3D12GraphicsCommandList *commandList);
	static const DX12SimulateContext *GetStatePtr(ID3D12GraphicsCommandList *commandList);
	static void SetDescriptorHeaps(DX12SimulateContext *state, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static bool RememberGraphicsRootSignature(DX12SimulateContext *state, ID3D12RootSignature *rootSignature);
	static bool RememberGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool RememberComputeRootSignature(DX12SimulateContext *state, ID3D12RootSignature *rootSignature);
	static bool RememberComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static bool RememberRootDescriptorTable(DX12SimulateContext *state, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool RememberRootDescriptorTable(ID3D12GraphicsCommandList *commandList, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void RememberGraphicsRootDescriptor(DX12SimulateContext *state, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberGraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberComputeRootDescriptor(DX12SimulateContext *state, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void RememberGraphicsRoot32BitConstants(DX12SimulateContext *state, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberGraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberComputeRoot32BitConstants(DX12SimulateContext *state, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static void RememberComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT destOffset, UINT count, const void *values);
	static bool GetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
};



class DX12CommandListStateCapture
{
public:
	static bool ShouldTrackBindings();
	static bool ShouldTrackIaBindings();
	static bool ShouldTrackPipelineStateBindings();
	static bool ShouldTrackResourceStates();
	static bool ShouldRecordBindingEvents();
	static bool ShouldTrackHuntIa();
	static bool ShouldTrackPsoState();
	static DX12CommandListTrackingState MakeTrackingState();
	static DX12CommandListTrackingState GetTrackingState(DX12SimulateContext *runtimeState);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState);

	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, D3D12_PRIMITIVE_TOPOLOGY topology);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12PipelineState *pipelineState);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, ID3D12RootSignature *rootSignature);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, const D3D12_INDEX_BUFFER_VIEW *view);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, DX12SimulateContext *runtimeState, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
};



class DX12FrameAnalysisState
{
public:
	inline static SRWLOCK lock = SRWLOCK_INIT;
	inline static bool active = false;
	inline static bool captureRequested = false;
	inline static bool capturing = false;
	inline static bool dumpPending = false;
	inline static UINT captureFramesRemaining = 0;
	inline static std::atomic<LONG> fastStateFlags{ 0 };
	inline static wchar_t path[MAX_PATH] = {};
	inline static FILE *log = nullptr;
	inline static unsigned long long lineNo = 0;
	inline static constexpr UINT CaptureFramesPerDump = 1;
	static constexpr LONG FLAG_ACTIVE = 1 << 0;
	static constexpr LONG FLAG_CAPTURE_REQUESTED = 1 << 1;
	static constexpr LONG FLAG_CAPTURING = 1 << 2;

	static void PublishFastStateLocked()
	{
		LONG flags = 0;
		if (active)
			flags |= FLAG_ACTIVE;
		if (captureRequested)
			flags |= FLAG_CAPTURE_REQUESTED;
		if (capturing)
			flags |= FLAG_CAPTURING;
		fastStateFlags.store(flags, std::memory_order_release);
	}

	static LONG GetFastFlags()
	{
		return fastStateFlags.load(std::memory_order_acquire);
	}

	static bool Begin()
	{
		wchar_t modulePath[MAX_PATH];
		wchar_t subdir[MAX_PATH];
		__time64_t now;
		struct tm localTime;

		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), modulePath, ARRAYSIZE(modulePath)))
			return false;
		PathRemoveFileSpecW(modulePath);

		_time64(&now);
		if (_localtime64_s(&localTime, &now) != 0)
			return false;
		wcsftime(subdir, ARRAYSIZE(subdir), L"FrameAnalysis-%Y-%m-%d-%H%M%S", &localTime);
		PathAppendW(modulePath, subdir);

		if (!CreateDirectoryW(modulePath, nullptr)) {
			DWORD error = GetLastError();
			DX12LogManager::Text(
				"Error creating frame analysis directory: %lu path=%S\n", error, modulePath);
			return false;
		}

		AcquireSRWLockExclusive(&lock);
		CloseLogLocked();
		wcsncpy_s(path, modulePath, _TRUNCATE);
		lineNo = 0;
		active = true;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		PublishFastStateLocked();
		ReleaseSRWLockExclusive(&lock);

		DX12LogManager::Text("Frame analysis directory: %S\n", modulePath);
		return true;
	}

	static void End()
	{
		AcquireSRWLockExclusive(&lock);
		active = false;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		PublishFastStateLocked();
		CloseLogLocked();
		ReleaseSRWLockExclusive(&lock);
	}

	static void RequestCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && !capturing && !dumpPending) {
			captureRequested = true;
			PublishFastStateLocked();
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static void BeginCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && captureRequested && !capturing && !dumpPending) {
			captureRequested = false;
			capturing = true;
			captureFramesRemaining = CaptureFramesPerDump;
			PublishFastStateLocked();
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static bool EndCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && capturing) {
			if (captureFramesRemaining > 1) {
				captureFramesRemaining--;
				ReleaseSRWLockExclusive(&lock);
				return false;
			}
			capturing = false;
			dumpPending = true;
			captureFramesRemaining = 0;
			PublishFastStateLocked();
			ReleaseSRWLockExclusive(&lock);
			return true;
		}
		ReleaseSRWLockExclusive(&lock);
		return false;
	}

	static bool IsActive()
	{
		return (GetFastFlags() & FLAG_ACTIVE) != 0;
	}

	static bool IsCaptureRequested()
	{
		return (GetFastFlags() & FLAG_CAPTURE_REQUESTED) != 0;
	}

	static bool IsCapturing()
	{
		return (GetFastFlags() & FLAG_CAPTURING) != 0;
	}

	static bool GetPath(wchar_t *outPath, size_t pathCount)
	{
		if (!outPath || pathCount == 0)
			return false;

		AcquireSRWLockShared(&lock);
		bool ok = path[0] != 0;
		if (ok)
			wcsncpy_s(outPath, pathCount, path, _TRUNCATE);
		ReleaseSRWLockShared(&lock);
		return ok;
	}

	static bool OpenLogLocked()
	{
		wchar_t filename[MAX_PATH];

		if (log)
			return true;
		if (!active || !path[0])
			return false;

		swprintf_s(filename, L"%s\\log.jsonl", path);
		log = _wfsopen(filename, L"w", _SH_DENYNO);
		if (!log) {
			DX12LogManager::Text("Error opening frame analysis log: %S\n", filename);
			return false;
		}

		fprintf(log,
			"{\"index\":0,\"func\":\"Header\",\"schema\":\"dx12-fa-log/3\",\"present_count_at_start\":%ld}\n",
			DX12RuntimeState::GetPresentCount());
		return true;
	}

	static void CloseLogLocked()
	{
		if (log) {
			fclose(log);
			log = nullptr;
		}
	}

	static void LogJsonFields(const char *fields)
	{
		AcquireSRWLockExclusive(&lock);
		if (!active || !OpenLogLocked()) {
			ReleaseSRWLockExclusive(&lock);
			return;
		}

		fprintf(log, "{\"index\":%llu,%s}\n", ++lineNo,
			fields ? fields : "\"func\":\"Unknown\"");
		fflush(log);
		ReleaseSRWLockExclusive(&lock);
	}
};



bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsBusy();
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModPresentReplacementPendingForCurrentPresent();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsShaderDescriptorTracking();
UINT64 DX12ModGetReloadGenerationFast();
bool DX12ModHasPotentialIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);
bool DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);
enum DX12ModHotPathFlagBits : UINT32
{
	DX12_MOD_HOT_SHADER_OVERRIDES = 1u << 0,
	DX12_MOD_HOT_PRESENT_REPLACEMENT = 1u << 1,
	DX12_MOD_HOT_PRE_SKIN_PROBE = 1u << 2,
	DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK = 1u << 3,
	DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK = 1u << 4,
	DX12_MOD_HOT_IA_RESIDENT_STATE = 1u << 5,
	DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT = 1u << 6
};
UINT32 DX12ModGetHotPathFlags();
void DX12ActivateCommandListHotPathHooks();

class DX12HotPathState
{
public:
	inline static std::atomic<LONG> skipAll{ 1 };
	inline static std::atomic<LONG> skipBindings{ 1 };
	inline static std::atomic<LONG> skipGraphicsBindings{ 1 };
	inline static std::atomic<LONG> trackResourceMetadata{ 0 };
	inline static std::atomic<LONG> stateMask{ -1 };

	// A single flags word replaces nine independent hot-path checks.
	// Update publishes it from Present-time frame actions and state transitions.
	inline static std::atomic<UINT64> HookWorkFlags{ 0 };

	// Individual work bits.
	static constexpr UINT64 FLAG_NEEDS_MOD_WORK          = 1ull << 0;
	static constexpr UINT64 FLAG_HUNT_ENABLED            = 1ull << 1;
	static constexpr UINT64 FLAG_FRAME_ANALYSIS_ACTIVE   = 1ull << 2;
	static constexpr UINT64 FLAG_SHADER_DUMP_ACTIVE      = 1ull << 3;
	static constexpr UINT64 FLAG_PRE_SKIN_PROBE          = 1ull << 4;
	static constexpr UINT64 FLAG_PRE_SKIN_BINDING_TRACK  = 1ull << 5;
	static constexpr UINT64 FLAG_SHADER_OVERRIDES         = 1ull << 6;
	static constexpr UINT64 FLAG_PRESENT_REPLACEMENT      = 1ull << 7;
	static constexpr UINT64 FLAG_IA_RESIDENT_STATE        = 1ull << 8;
	static constexpr UINT64 FLAG_SHADER_DESCRIPTOR_TRACK  = 1ull << 9;
	static constexpr UINT64 FLAG_DRAW_SRV_SLOT_REPLACEMENT = 1ull << 10;

	// Combined masks allow one branch for draw or dispatch hook work.
	static constexpr UINT64 MASK_DRAW_WORK =
		FLAG_NEEDS_MOD_WORK | FLAG_HUNT_ENABLED |
		FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE;
	static constexpr UINT64 MASK_DISPATCH_WORK =
		MASK_DRAW_WORK | FLAG_PRE_SKIN_PROBE;

	class SourceState
	{
	public:
		bool huntEnabled = false;
		bool frameAnalysisActive = false;
		bool shaderDumpActive = false;
		bool shaderOverrides = false;
		bool presentReplacement = false;
		bool preSkinProbe = false;
		bool preSkinBindingTrack = false;
		bool shaderDescriptorTrack = false;
		bool iaResidentState = false;
		bool drawSrvSlotReplacement = false;

		static SourceState Capture()
		{
			SourceState state;
			state.huntEnabled = DX12HuntNeedsHotPath();
			state.frameAnalysisActive = DX12FrameAnalysisState::IsActive();
			state.shaderDumpActive = DX12ShaderDumpIsBusy();
			const UINT32 modFlags = DX12ModGetHotPathFlags();
			state.shaderOverrides =
				(modFlags & DX12_MOD_HOT_SHADER_OVERRIDES) != 0;
			state.presentReplacement =
				(modFlags & DX12_MOD_HOT_PRESENT_REPLACEMENT) != 0;
			state.preSkinProbe =
				(modFlags & DX12_MOD_HOT_PRE_SKIN_PROBE) != 0;
			state.preSkinBindingTrack =
				(modFlags & DX12_MOD_HOT_PRE_SKIN_BINDING_TRACK) != 0;
			state.shaderDescriptorTrack =
				(modFlags & DX12_MOD_HOT_SHADER_DESCRIPTOR_TRACK) != 0;
			state.iaResidentState =
				(modFlags & DX12_MOD_HOT_IA_RESIDENT_STATE) != 0;
			state.drawSrvSlotReplacement =
				(modFlags & DX12_MOD_HOT_DRAW_SRV_SLOT_REPLACEMENT) != 0;
			return state;
		}

		UINT64 BuildHookWorkFlags() const
		{
			UINT64 flags = 0;
			if (NeedsModDrawWork())
				flags |= FLAG_NEEDS_MOD_WORK;
			if (shaderOverrides)
				flags |= FLAG_SHADER_OVERRIDES;
			if (presentReplacement)
				flags |= FLAG_PRESENT_REPLACEMENT;
			if (huntEnabled)
				flags |= FLAG_HUNT_ENABLED;
			if (frameAnalysisActive)
				flags |= FLAG_FRAME_ANALYSIS_ACTIVE;
			if (shaderDumpActive)
				flags |= FLAG_SHADER_DUMP_ACTIVE;
			if (preSkinProbe)
				flags |= FLAG_PRE_SKIN_PROBE;
			if (preSkinBindingTrack)
				flags |= FLAG_PRE_SKIN_BINDING_TRACK;
			if (iaResidentState)
				flags |= FLAG_IA_RESIDENT_STATE;
			if (shaderDescriptorTrack)
				flags |= FLAG_SHADER_DESCRIPTOR_TRACK;
			if (drawSrvSlotReplacement)
				flags |= FLAG_DRAW_SRV_SLOT_REPLACEMENT;
			return flags;
		}

		bool HasAnyWork() const
		{
			return huntEnabled ||
				frameAnalysisActive ||
				shaderDumpActive ||
				shaderOverrides ||
				presentReplacement ||
				preSkinProbe ||
				preSkinBindingTrack ||
				shaderDescriptorTrack ||
				iaResidentState ||
				drawSrvSlotReplacement;
		}

		bool NeedsHeavyTracking() const
		{
			return huntEnabled || shaderDumpActive;
		}

		bool NeedsRecordWork() const
		{
			return huntEnabled || NeedsModDrawWork();
		}

		bool NeedsModDrawWork() const
		{
			return shaderOverrides ||
				shaderDescriptorTrack ||
				iaResidentState ||
				drawSrvSlotReplacement;
		}

		bool NeedsBindingState() const
		{
			// Pre-skin keeps its compute bindings resident in the lightweight
			// runtime snapshot and no longer needs the full tracked-binding path.
			return NeedsHeavyTracking();
		}

		bool NeedsGraphicsBindingState() const
		{
			return NeedsHeavyTracking() || shaderDescriptorTrack ||
				drawSrvSlotReplacement;
		}

		const char *ModeName() const
		{
			return huntEnabled ? "Hunting" : "Mod";
		}
	};

	static bool HasAnyFlag(UINT64 flags, UINT64 mask)
	{
		return (flags & mask) != 0;
	}

	static bool NeedsResidentIaState(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_HUNT_ENABLED | FLAG_IA_RESIDENT_STATE);
	}

	static bool NeedsGraphicsResidentDescriptorState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_DRAW_SRV_SLOT_REPLACEMENT |
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsResidentRootDescriptorState(UINT64 flags)
	{
		// Descriptor-triggered Mod matching can still query graphics root
		// descriptors from the lightweight runtime snapshot, but draw-time SRV
		// table patching does not need them.
		return HasAnyFlag(
			flags,
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsResidentRootValueState(UINT64 flags)
	{
		// DX11-style Mod matching for graphics texture replacement only needs the
		// live descriptor heaps/tables/root signature. Outside capture/debug, the
		// only graphics root values that still matter are root descriptors for
		// descriptor-triggered Mod lookups; root constants remain capture-only.
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsGraphicsTrackedBindingCapture(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsComputeResidentDescriptorState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsComputeResidentBindingState(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE);
	}

	static bool NeedsAnyResidentDescriptorState(UINT64 flags)
	{
		return NeedsGraphicsResidentDescriptorState(flags) ||
			NeedsComputeResidentDescriptorState(flags);
	}

	static bool NeedsHuntOnlyPipelineObservation(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_HUNT_ENABLED) &&
			!HasAnyFlag(flags, FLAG_SHADER_OVERRIDES);
	}

	static bool NeedsPreSkinBindingTracking(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_PRE_SKIN_BINDING_TRACK);
	}

	static bool NeedsPreSkinProbe(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_PRE_SKIN_PROBE);
	}

	static bool NeedsIaBufferContentTracking(UINT64 flags)
	{
		return HasAnyFlag(flags, FLAG_IA_RESIDENT_STATE);
	}

	static bool NeedsTextureHashTracking(UINT64 flags)
	{
		// Only maintain the resource-content hash cache on copy paths when a
		// consumer can actually read it. DX11 gates the same work behind
		// track_texture_updates / hunting; running the full top-mip hash plus
		// a global lock insert on every streaming copy with no mods loaded is
		// pure overhead (wrapper-mode CopyTextureRegion was doing exactly that).
		return HasAnyFlag(
			flags,
			FLAG_SHADER_OVERRIDES |
			FLAG_IA_RESIDENT_STATE |
			FLAG_HUNT_ENABLED |
			FLAG_FRAME_ANALYSIS_ACTIVE |
			FLAG_SHADER_DUMP_ACTIVE |
			FLAG_PRE_SKIN_PROBE |
			FLAG_PRE_SKIN_BINDING_TRACK |
			FLAG_SHADER_DESCRIPTOR_TRACK |
			FLAG_DRAW_SRV_SLOT_REPLACEMENT);
	}

	static bool NeedsBindingEventRecording(UINT64 flags)
	{
		return HasAnyFlag(
			flags,
			FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE);
	}

	static void Update()
	{
		const SourceState source = SourceState::Capture();
		if (skipAll && !source.HasAnyWork())
			return;

		// As in DX11, skipAll controls draw, state, and copy hooks.
		// PreSkin only needs dispatch hooks and compute binding tracking;
		// skipBindings and the dispatch-specific check handle that case.
		const LONG skipAll =
			(source.NeedsHeavyTracking() || source.NeedsRecordWork()) ? 0 : 1;
		const LONG skipBindings = source.NeedsBindingState() ? 0 : 1;
		const LONG skipGraphicsBindings =
			source.NeedsGraphicsBindingState() ? 0 : 1;
		const LONG trackResourceMetadata =
			source.NeedsHeavyTracking() ? 1 : 0;
		const UINT64 flags = source.BuildHookWorkFlags();

		DX12HotPathState::skipAll.store(skipAll);
		DX12HotPathState::skipBindings.store(skipBindings);
		DX12HotPathState::skipGraphicsBindings.store(skipGraphicsBindings);
		DX12HotPathState::trackResourceMetadata.store(trackResourceMetadata);
		HookWorkFlags.store(flags);

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		if (stateMask.exchange(mask) != mask) {
			// Command lists cache binding-capture decisions. Mode or capture-state
			// changes must invalidate that cache, otherwise Hunting can keep using
			// a Mod-era "do not track bindings" decision and lose SRV texture tables.
			DX12CommandListRuntime::BumpTrackingGeneration();
			DX12LogManager::Fields("DX12HotPathState",
				"\"mode\":\"%s\",\"skipAll\":%ld,\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,\"trackResourceMetadata\":%ld",
				source.ModeName(),
				skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata);
		}
	}

	static bool HooksNeeded()
	{
		return skipAll.load() == 0 ||
			HasAnyFlag(HookWorkFlags.load(), FLAG_HUNT_ENABLED);
	}
};


#include <dbghelp.h>


// In-process hang watchdog with self-minidump support.
//
// The game is protected by anti-cheat, so external debuggers / Task Manager
// dumps cannot capture the frozen process. Writing the dump from inside the
// wrapper (MiniDumpWriteDump on self) needs no debug privilege and is the same
// mechanism in-process crash handlers use, so anti-cheat cannot block it.
//
// Triggers:
//  - No Present completion for more than ZZMIDX12_HANG_DUMP_MS (default 8000ms)
//    after the first present. This catches the startup/character-load hang
//    where the render thread stops presenting.
//  - No present at all for 30s after watchdog start (freeze before first frame).
//
// The dump is written next to d3d12.dll as hang_YYYYMMDD_HHMMSS.dmp with module
// data segments (covers the wrapper's SRW lock globals), thread info and stacks,
// so WinDbg can resolve which lock/wait the render thread is blocked on.
class DX12HangWatchdog
{
public:
	// Called right after the original Present/Present1 returns (both the
	// normal and the skipAll fast paths). Cheap: one GetTickCount64 + two
	// atomic stores.
	static void NotifyPresentCompleted()
	{
		const ULONGLONG now = GetTickCount64();
		InterlockedExchange64(
			&sLastPresentCompletionTick, static_cast<LONGLONG>(now));
		InterlockedExchange(&sHasSeenPresent, 1);
	}

	static void Start()
	{
		if (InterlockedCompareExchange(&sStarted, 1, 0) != 0)
			return;
		sInitTick = GetTickCount64();
		InterlockedExchange(&sStopRequested, 0);
		sThresholdMs = DefaultThresholdMs();
		sThread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
	}

	static void Stop()
	{
		InterlockedExchange(&sStopRequested, 1);
		if (sThread) {
			WaitForSingleObject(sThread, 5000);
			CloseHandle(sThread);
			sThread = nullptr;
		}
		InterlockedExchange(&sStarted, 0);
	}

private:
	static DWORD DefaultThresholdMs()
	{
		DWORD value = 8000;
		char env[32] = {};
		const DWORD len = GetEnvironmentVariableA(
			"ZZMIDX12_HANG_DUMP_MS", env, sizeof(env));
		if (len > 0 && len < sizeof(env)) {
			const int parsed = atoi(env);
			if (parsed > 0)
				value = static_cast<DWORD>(parsed);
		}
		return value;
	}

	static void WriteMinidump(const char *reason)
	{
		wchar_t directory[MAX_PATH] = {};
		const HMODULE module = DX12RuntimeState::GetModule();
		if (!module || !GetModuleFileNameW(module, directory, MAX_PATH))
			return;
		PathRemoveFileSpecW(directory);

		SYSTEMTIME st = {};
		GetLocalTime(&st);
		wchar_t path[MAX_PATH] = {};
		swprintf_s(path,
			L"%s\\hang_%04u%02u%02u_%02u%02u%02u.dmp",
			directory,
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond);

		HANDLE file = CreateFileW(
			path, GENERIC_WRITE, FILE_SHARE_READ,
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"open_failed\",\"reason\":\"%s\",\"path\":\"%S\",\"error\":%lu",
				reason, path, GetLastError());
			return;
		}

		// DataSegs keeps every loaded module's data segment so the SRW lock
		// globals (gModLock/gResourceLock/...) are present in the dump;
		// thread stacks and thread info capture where each thread is blocked.
		const MINIDUMP_TYPE flags = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithDataSegs |
			MiniDumpWithThreadInfo |
			MiniDumpWithProcessThreadData |
			MiniDumpWithHandleData |
			MiniDumpWithUnloadedModules);
		const BOOL ok = MiniDumpWriteDump(
			GetCurrentProcess(), GetCurrentProcessId(), file,
			flags, nullptr, nullptr, nullptr);
		const DWORD error = GetLastError();
		CloseHandle(file);

		if (ok) {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"written\",\"reason\":\"%s\",\"path\":\"%S\"",
				reason, path);
		} else {
			DX12LogManager::FieldsFlush("DX12HangWatchdog",
				"\"stage\":\"write_failed\",\"reason\":\"%s\",\"error\":%lu",
				reason, error);
		}
	}

	static void LogHangDetected(ULONGLONG elapsedMs)
	{
		DX12LogManager::FieldsFlush("DX12HangWatchdog",
			"\"stage\":\"detected\",\"elapsedMs\":%llu,\"presentSeen\":%ld",
			static_cast<unsigned long long>(elapsedMs), sHasSeenPresent);
	}

	static DWORD WINAPI ThreadProc(void *)
	{
		for (; InterlockedCompareExchange(&sStopRequested, 0, 0) == 0;) {
			Sleep(1000);
			const ULONGLONG now = GetTickCount64();
			if (!sHasSeenPresent) {
				// Freeze before the first present (early startup hang).
				if (now - sInitTick > 30000 &&
				    InterlockedCompareExchange(&sDumpAttempted, 1, 0) == 0) {
					LogHangDetected(now - sInitTick);
					WriteMinidump("no_present");
				}
				continue;
			}
			const LONGLONG last = InterlockedCompareExchange64(
				&sLastPresentCompletionTick, 0, 0);
			if (last == 0)
				continue;
			const ULONGLONG elapsed = now - static_cast<ULONGLONG>(last);
			if (elapsed > sThresholdMs &&
			    InterlockedCompareExchange(&sDumpAttempted, 1, 0) == 0) {
				LogHangDetected(elapsed);
				WriteMinidump("present_stall");
			}
		}
		return 0;
	}

	inline static volatile LONG sStarted = 0;
	inline static volatile LONG sStopRequested = 0;
	inline static volatile LONG sHasSeenPresent = 0;
	inline static volatile LONG sDumpAttempted = 0;
	inline static volatile LONGLONG sLastPresentCompletionTick = 0;
	inline static ULONGLONG sInitTick = 0;
	inline static DWORD sThresholdMs = 8000;
	inline static HANDLE sThread = nullptr;
};


class DX12Profiling
{
public:
	enum class Mode {
		NONE = 0,
		SUMMARY,
	};

	class Counter
	{
	public:
		volatile LONG calls = 0;
		volatile LONG64 cpuTicks = 0;

		void Reset();
		void AccumulateFrom(const Counter &src);
		void IncrementCalls();
	};

	struct State
	{
		Counter *counter = nullptr;
		LARGE_INTEGER startTime = {};
		bool counted = false;
	};

	Counter DrawInstanced;
	Counter DrawIndexedInstanced;
	Counter Dispatch;
	Counter SetPipelineState;
	Counter SetDescriptorHeaps;
	Counter SetComputeRootSignature;
	Counter SetGraphicsRootSignature;
	Counter SetComputeRootDescriptorTable;
	Counter SetGraphicsRootDescriptorTable;
	Counter SetComputeRootConstantBufferView;
	Counter SetGraphicsRootConstantBufferView;
	Counter SetComputeRootShaderResourceView;
	Counter SetGraphicsRootShaderResourceView;
	Counter SetComputeRootUnorderedAccessView;
	Counter SetGraphicsRootUnorderedAccessView;
	Counter IASetIndexBuffer;
	Counter IASetVertexBuffers;
	Counter IASetPrimitiveTopology;
	Counter ResourceBarrier;
	Counter ExecuteCommandLists;
	Counter Present;
	Counter ResetCommandList;
	Counter CloseCommandList;
	Counter IaDrawOverrideChecks;
	Counter IaHashStateHits;
	Counter IaHashStateMisses;
	Counter IaTextureMayHits;
	Counter IaTextureMayMisses;
	Counter IaPrepareCalls;
	Counter IaApplied;
	Counter IaCandidateTests;
	Counter IaCandidateHits;
	Counter IaHuntIaUpdates;
	Counter IaReplacementDraws;
	Counter IaReplacementIndexedDraws;
	Counter IaReplacementDispatches;
	Counter PreSkinCsTests;
	Counter PreSkinUavTests;
	Counter PreSkinUavHits;
	Counter PreSkinApplied;
	Counter PreSkinDispatchResized;
	Counter AllDraws;
	Counter AllDispatches;
	Counter AllBindings;
	Counter AllCommands;
	Counter FastForwardHits;
	volatile LONG64 BindingQueueTaskUs = 0;
	volatile LONG64 BindingQueueDrainUs = 0;
	volatile LONG64 BindingQueueMaxTaskUs = 0;
	volatile LONG64 BindingQueueMaxDrainUs = 0;
	volatile LONG BindingQueueTasks = 0;
	volatile LONG BindingQueueDrains = 0;
	volatile LONG BindingQueueMaxPending = 0;

	Mode mode = Mode::SUMMARY;
	volatile LONG collectCounters = 0;
	volatile LONG collectTiming = 0;
	volatile LONG framePresentCount = 0;
	LARGE_INTEGER frameStartTime = {};
	LARGE_INTEGER perfFrequency = {};
	wchar_t overlayText[2048] = L"";

	static DX12Profiling &Get();
	static void Init();
	static void BeginFrame();
	static void EndFrame();
	static void Toggle();
	static bool IsActive();
	static bool ShouldCollectCounters();
	static void Start(State *state, Counter *counter);
	static void End(State *state);
	static void RecordIaDrawOverrideCheck();
	static void RecordIaHashStateResult(bool hit);
	static void RecordIaTextureMayMatchResult(bool hit);
	static void RecordIaPrepareCall();
	static void RecordIaApplied();
	static void RecordIaCandidateTest(bool hit);
	static void RecordIaHuntIaUpdate();
	static void RecordIaReplacementDraw(bool indexed);
	static void RecordIaReplacementDispatch();
	static void RecordPreSkinCsTest(bool hit);
	static void RecordPreSkinUavTest(bool hit);
	static void RecordPreSkinApplied();
	static void RecordPreSkinDispatchResized();
	static void LogFrameStats();
	static void FlushFrame();
	static const wchar_t *OverlayText();
	static void RecordFastForwardHit();
	static void RecordBindingQueueTaskUs(LONG64 elapsedUs);
	static void RecordBindingQueueDrainUs(LONG64 elapsedUs, LONG pending);

	void InitState();
	void BeginFrameState();
	void EndFrameState();
	void ToggleState();
	bool IsActiveState() const;
	void ResetFrame();
	void LogFrameStatsState();
	void FlushFrameState();
	double TicksToUs(LONG64 ticks) const;
	const wchar_t *FormatUs(double us, wchar_t *buf, size_t bufSize) const;
};

class DX12Profiler;

#define DX12_PROFILE_FUNCTION(counter) \
	DX12Profiler::Scope _profile_timer( \
		DX12Profiler::Counter, &DX12Profiling::Get().counter)

#define DX12_PROFILE_FAST_FORWARD() do { \
	DX12Profiling::RecordFastForwardHit(); \
} while (0)

inline DX12Profiling &DX12Profiling::Get()
{
	static DX12Profiling profiling;
	return profiling;
}

inline void DX12Profiling::Counter::Reset()
{
	InterlockedExchange(&calls, 0);
	InterlockedExchange64(&cpuTicks, 0);
}

inline void DX12Profiling::Counter::AccumulateFrom(const Counter &src)
{
	InterlockedAdd(&calls, src.calls);
	InterlockedAdd64(&cpuTicks, src.cpuTicks);
}

inline void DX12Profiling::Counter::IncrementCalls()
{
	InterlockedIncrement(&calls);
}

inline void DX12Profiling::Start(State *state, Counter *counter)
{
	if (!state || !counter || !ShouldCollectCounters())
		return;
	state->counter = counter;
	if (!state->counted) {
		counter->IncrementCalls();
		state->counted = true;
	}
	state->startTime.QuadPart = 0;
	if (Get().collectTiming)
		QueryPerformanceCounter(&state->startTime);
}

inline void DX12Profiling::End(State *state)
{
	if (!state || !state->counter || state->startTime.QuadPart == 0)
		return;
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);
	InterlockedAdd64(
		&state->counter->cpuTicks,
		endTime.QuadPart - state->startTime.QuadPart);
	state->startTime.QuadPart = 0;
}

inline void DX12Profiling::Init()
{
	Get().InitState();
}

inline void DX12Profiling::BeginFrame()
{
	Get().BeginFrameState();
}

inline void DX12Profiling::EndFrame()
{
	Get().EndFrameState();
}

inline void DX12Profiling::Toggle()
{
	Get().ToggleState();
}

inline bool DX12Profiling::IsActive()
{
	return Get().IsActiveState();
}

inline bool DX12Profiling::ShouldCollectCounters()
{
	return Get().collectCounters != 0;
}

inline void DX12Profiling::RecordIaDrawOverrideCheck()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaDrawOverrideChecks.IncrementCalls();
}

inline void DX12Profiling::RecordIaHashStateResult(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	(hit ? profiling.IaHashStateHits : profiling.IaHashStateMisses).IncrementCalls();
}

inline void DX12Profiling::RecordIaTextureMayMatchResult(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	(hit ? profiling.IaTextureMayHits : profiling.IaTextureMayMisses).IncrementCalls();
}

inline void DX12Profiling::RecordIaPrepareCall()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaPrepareCalls.IncrementCalls();
}

inline void DX12Profiling::RecordIaApplied()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaApplied.IncrementCalls();
}

inline void DX12Profiling::RecordIaCandidateTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	profiling.IaCandidateTests.IncrementCalls();
	if (hit)
		profiling.IaCandidateHits.IncrementCalls();
}

inline void DX12Profiling::RecordIaHuntIaUpdate()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaHuntIaUpdates.IncrementCalls();
}

inline void DX12Profiling::RecordIaReplacementDraw(bool indexed)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	profiling.IaReplacementDraws.IncrementCalls();
	if (indexed)
		profiling.IaReplacementIndexedDraws.IncrementCalls();
}

inline void DX12Profiling::RecordIaReplacementDispatch()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaReplacementDispatches.IncrementCalls();
}

inline void DX12Profiling::RecordPreSkinCsTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	Get().PreSkinCsTests.IncrementCalls();
	(void)hit;
}

inline void DX12Profiling::RecordPreSkinUavTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	profiling.PreSkinUavTests.IncrementCalls();
	if (hit)
		profiling.PreSkinUavHits.IncrementCalls();
}

inline void DX12Profiling::RecordPreSkinApplied()
{
	if (!ShouldCollectCounters())
		return;
	Get().PreSkinApplied.IncrementCalls();
}

inline void DX12Profiling::RecordPreSkinDispatchResized()
{
	if (!ShouldCollectCounters())
		return;
	Get().PreSkinDispatchResized.IncrementCalls();
}

inline void DX12Profiling::LogFrameStats()
{
	Get().LogFrameStatsState();
}

inline void DX12Profiling::FlushFrame()
{
	Get().FlushFrameState();
}

inline const wchar_t *DX12Profiling::OverlayText()
{
	return Get().overlayText;
}

inline void DX12Profiling::RecordFastForwardHit()
{
	if (!ShouldCollectCounters())
		return;
	Get().FastForwardHits.IncrementCalls();
}

inline void DX12Profiling::RecordBindingQueueTaskUs(LONG64 elapsedUs)
{
	if (!ShouldCollectCounters() || elapsedUs <= 0)
		return;
	DX12Profiling &profiling = Get();
	InterlockedIncrement(&profiling.BindingQueueTasks);
	InterlockedAdd64(&profiling.BindingQueueTaskUs, elapsedUs);
	LONG64 currentMax = profiling.BindingQueueMaxTaskUs;
	while (elapsedUs > currentMax &&
		InterlockedCompareExchange64(&profiling.BindingQueueMaxTaskUs, elapsedUs, currentMax) != currentMax) {
		currentMax = profiling.BindingQueueMaxTaskUs;
	}
}

inline void DX12Profiling::RecordBindingQueueDrainUs(LONG64 elapsedUs, LONG pending)
{
	if (!ShouldCollectCounters() || elapsedUs <= 0)
		return;
	DX12Profiling &profiling = Get();
	InterlockedIncrement(&profiling.BindingQueueDrains);
	InterlockedAdd64(&profiling.BindingQueueDrainUs, elapsedUs);
	LONG64 currentMaxUs = profiling.BindingQueueMaxDrainUs;
	while (elapsedUs > currentMaxUs &&
		InterlockedCompareExchange64(&profiling.BindingQueueMaxDrainUs, elapsedUs, currentMaxUs) != currentMaxUs) {
		currentMaxUs = profiling.BindingQueueMaxDrainUs;
	}
	LONG currentMaxPending = profiling.BindingQueueMaxPending;
	while (pending > currentMaxPending &&
		InterlockedCompareExchange(&profiling.BindingQueueMaxPending, pending, currentMaxPending) != currentMaxPending) {
		currentMaxPending = profiling.BindingQueueMaxPending;
	}
}

inline void DX12Profiling::InitState()
{
	QueryPerformanceFrequency(&perfFrequency);

#if defined(_DEBUG)
	InterlockedExchange(&collectCounters, 1);
	InterlockedExchange(&collectTiming, 1);
#else
	// Release hooks run for every D3D12 binding call. Keep the atomic profiling
	// counters off until the user explicitly enables profiling with F11.
	mode = Mode::NONE;
	InterlockedExchange(&collectCounters, 0);
	InterlockedExchange(&collectTiming, 0);
#endif
}

inline void DX12Profiling::BeginFrameState()
{
	if (mode != Mode::SUMMARY)
		return;

	QueryPerformanceCounter(&frameStartTime);
}

inline void DX12Profiling::EndFrameState()
{
	InterlockedIncrement(&framePresentCount);

	if (mode == Mode::SUMMARY) {
		FlushFrameState();
	} else if (kVerboseEnabled) {
		LogFrameStatsState();
	}

	if (ShouldCollectCounters())
		ResetFrame();
}

inline void DX12Profiling::ToggleState()
{
	if (mode == Mode::NONE) {
		mode = Mode::SUMMARY;
		InterlockedExchange(&collectCounters, 1);
		DX12LogManager::Text("DX12 profiling enabled (SUMMARY mode) - press F11 to toggle\n");
		DX12LogManager::Fields("DX12Profiling", "\"status\":\"enabled\",\"mode\":\"SUMMARY\"");
	} else {
		if (mode == Mode::SUMMARY)
			FlushFrameState();
		mode = Mode::NONE;
		InterlockedExchange(&collectCounters, 0);
		if (!ShouldCollectCounters())
			ResetFrame();
		DX12LogManager::Text("DX12 profiling disabled\n");
		DX12LogManager::Fields("DX12Profiling", "\"status\":\"disabled\"");
	}
}

inline bool DX12Profiling::IsActiveState() const
{
	return mode != Mode::NONE;
}

inline void DX12Profiling::ResetFrame()
{
	DrawInstanced.Reset();
	DrawIndexedInstanced.Reset();
	Dispatch.Reset();
	SetPipelineState.Reset();
	SetDescriptorHeaps.Reset();
	SetComputeRootSignature.Reset();
	SetGraphicsRootSignature.Reset();
	SetComputeRootDescriptorTable.Reset();
	SetGraphicsRootDescriptorTable.Reset();
	SetComputeRootConstantBufferView.Reset();
	SetGraphicsRootConstantBufferView.Reset();
	SetComputeRootShaderResourceView.Reset();
	SetGraphicsRootShaderResourceView.Reset();
	SetComputeRootUnorderedAccessView.Reset();
	SetGraphicsRootUnorderedAccessView.Reset();
	IASetIndexBuffer.Reset();
	IASetVertexBuffers.Reset();
	IASetPrimitiveTopology.Reset();
	ResourceBarrier.Reset();
	ExecuteCommandLists.Reset();
	Present.Reset();
	ResetCommandList.Reset();
	CloseCommandList.Reset();
	FastForwardHits.Reset();
	IaDrawOverrideChecks.Reset();
	IaHashStateHits.Reset();
	IaHashStateMisses.Reset();
	IaTextureMayHits.Reset();
	IaTextureMayMisses.Reset();
	IaPrepareCalls.Reset();
	IaApplied.Reset();
	IaCandidateTests.Reset();
	IaCandidateHits.Reset();
	IaHuntIaUpdates.Reset();
	IaReplacementDraws.Reset();
	IaReplacementIndexedDraws.Reset();
	IaReplacementDispatches.Reset();
	PreSkinCsTests.Reset();
	PreSkinUavTests.Reset();
	PreSkinUavHits.Reset();
	PreSkinApplied.Reset();
	PreSkinDispatchResized.Reset();
	InterlockedExchange64(&BindingQueueTaskUs, 0);
	InterlockedExchange64(&BindingQueueDrainUs, 0);
	InterlockedExchange64(&BindingQueueMaxTaskUs, 0);
	InterlockedExchange64(&BindingQueueMaxDrainUs, 0);
	InterlockedExchange(&BindingQueueTasks, 0);
	InterlockedExchange(&BindingQueueDrains, 0);
	InterlockedExchange(&BindingQueueMaxPending, 0);
}

inline double DX12Profiling::TicksToUs(LONG64 ticks) const
{
	if (perfFrequency.QuadPart == 0)
		return 0.0;
	return static_cast<double>(ticks) * 1000000.0 /
		static_cast<double>(perfFrequency.QuadPart);
}

inline const wchar_t *DX12Profiling::FormatUs(double us, wchar_t *buf, size_t bufSize) const
{
	if (us < 1000.0)
		swprintf_s(buf, bufSize, L"%6.1f us", us);
	else if (us < 1000000.0)
		swprintf_s(buf, bufSize, L"%6.1f ms", us / 1000.0);
	else
		swprintf_s(buf, bufSize, L"%6.1f  s", us / 1000000.0);
	return buf;
}

inline void DX12Profiling::LogFrameStatsState()
{
	LONG present = framePresentCount;

	LONG draws = DrawInstanced.calls + DrawIndexedInstanced.calls;
	LONG bindings =
		SetDescriptorHeaps.calls +
		SetComputeRootSignature.calls + SetGraphicsRootSignature.calls +
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls +
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls +
		SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls +
		SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls +
		IASetIndexBuffer.calls + IASetVertexBuffers.calls + IASetPrimitiveTopology.calls;
	LONG total = draws + Dispatch.calls + SetPipelineState.calls + bindings +
		ResourceBarrier.calls;

	DX12LogManager::DebugFields("DX12FrameStats",
		"\"present\":%ld,"
		"\"draws\":%ld,\"dispatch\":%ld,\"setPSO\":%ld,\"bindings\":%ld,"
		"\"rootTables\":%ld,\"rootCBV_SRV_UAV\":%ld,"
		"\"iaSet\":%ld,\"barriers\":%ld,"
		"\"execCL\":%ld,\"reset\":%ld,"
		"\"total\":%ld,\"ffHits\":%ld,"
		"\"iaOverrideChecks\":%ld,\"iaHashHits\":%ld,\"iaHashMisses\":%ld,"
		"\"iaMayHits\":%ld,\"iaMayMisses\":%ld,\"iaPrepareCalls\":%ld,\"iaApplied\":%ld,"
		"\"iaCandidateTests\":%ld,\"iaCandidateHits\":%ld,\"iaHuntIaUpdates\":%ld,"
		"\"iaReplacementDraws\":%ld,\"iaReplacementIndexedDraws\":%ld,\"iaReplacementDispatches\":%ld,"
		"\"preSkinCsTests\":%ld,\"preSkinUavTests\":%ld,\"preSkinUavHits\":%ld,"
		"\"preSkinApplied\":%ld,\"preSkinDispatchResized\":%ld",
		present,
		draws, Dispatch.calls, SetPipelineState.calls, bindings,
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls,
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls +
			SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls +
			SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls,
		IASetIndexBuffer.calls + IASetVertexBuffers.calls + IASetPrimitiveTopology.calls,
		ResourceBarrier.calls,
		ExecuteCommandLists.calls, ResetCommandList.calls,
		total, FastForwardHits.calls,
		IaDrawOverrideChecks.calls, IaHashStateHits.calls, IaHashStateMisses.calls,
		IaTextureMayHits.calls, IaTextureMayMisses.calls,
		IaPrepareCalls.calls, IaApplied.calls,
		IaCandidateTests.calls, IaCandidateHits.calls, IaHuntIaUpdates.calls,
		IaReplacementDraws.calls, IaReplacementIndexedDraws.calls,
		IaReplacementDispatches.calls,
		PreSkinCsTests.calls, PreSkinUavTests.calls, PreSkinUavHits.calls,
		PreSkinApplied.calls, PreSkinDispatchResized.calls);
}

inline void DX12Profiling::FlushFrameState()
{
	if (mode != Mode::SUMMARY)
		return;

	LONG present = framePresentCount;

	Counter draws = {};
	draws.AccumulateFrom(DrawInstanced);
	draws.AccumulateFrom(DrawIndexedInstanced);

	Counter bindings = {};
	bindings.AccumulateFrom(SetDescriptorHeaps);
	bindings.AccumulateFrom(SetComputeRootSignature);
	bindings.AccumulateFrom(SetGraphicsRootSignature);
	bindings.AccumulateFrom(SetComputeRootDescriptorTable);
	bindings.AccumulateFrom(SetGraphicsRootDescriptorTable);
	bindings.AccumulateFrom(SetComputeRootConstantBufferView);
	bindings.AccumulateFrom(SetGraphicsRootConstantBufferView);
	bindings.AccumulateFrom(SetComputeRootShaderResourceView);
	bindings.AccumulateFrom(SetGraphicsRootShaderResourceView);
	bindings.AccumulateFrom(SetComputeRootUnorderedAccessView);
	bindings.AccumulateFrom(SetGraphicsRootUnorderedAccessView);
	bindings.AccumulateFrom(IASetIndexBuffer);
	bindings.AccumulateFrom(IASetVertexBuffers);
	bindings.AccumulateFrom(IASetPrimitiveTopology);

	Counter all = {};
	all.AccumulateFrom(draws);
	all.AccumulateFrom(Dispatch);
	all.AccumulateFrom(SetPipelineState);
	all.AccumulateFrom(bindings);
	all.AccumulateFrom(ResourceBarrier);

	DX12LogManager::DebugFields("DX12ProfilingFrame",
		"\"present\":%ld,"
		"\"drawInstanced\":%ld,\"drawIndexedInstanced\":%ld,\"drawTotal\":%ld,"
		"\"dispatch\":%ld,"
		"\"setPipelineState\":%ld,"
		"\"setDescriptorHeaps\":%ld,"
		"\"setRootSignatures\":%ld,"
		"\"setRootDescriptorTables\":%ld,"
		"\"setRootCBV\":%ld,\"setRootSRV\":%ld,\"setRootUAV\":%ld,"
		"\"iaSetIndexBuffer\":%ld,\"iaSetVertexBuffers\":%ld,\"iaSetPrimitiveTopology\":%ld,"
		"\"bindingsTotal\":%ld,"
		"\"resourceBarrier\":%ld,"
		"\"executeCommandLists\":%ld,\"resetCommandList\":%ld,\"presentCalls\":%ld,"
		"\"commandsTotal\":%ld,"
		"\"fastForwardHits\":%ld,"
		"\"cpuUs_total\":%.1f,\"cpuUs_draws\":%.1f,\"cpuUs_bindings\":%.1f,\"cpuUs_dispatch\":%.1f,"
		"\"bindingQueueTasks\":%ld,\"bindingQueueTaskUs\":%.1f,\"bindingQueueMaxTaskUs\":%.1f,"
		"\"bindingQueueDrains\":%ld,\"bindingQueueDrainUs\":%.1f,\"bindingQueueMaxDrainUs\":%.1f,"
		"\"bindingQueueMaxPending\":%ld",
		present,
		DrawInstanced.calls, DrawIndexedInstanced.calls, draws.calls,
		Dispatch.calls,
		SetPipelineState.calls,
		SetDescriptorHeaps.calls,
		SetComputeRootSignature.calls + SetGraphicsRootSignature.calls,
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls,
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls,
		SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls,
		SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls,
		IASetIndexBuffer.calls, IASetVertexBuffers.calls, IASetPrimitiveTopology.calls,
		bindings.calls,
		ResourceBarrier.calls,
		ExecuteCommandLists.calls, ResetCommandList.calls, Present.calls,
		all.calls,
		FastForwardHits.calls,
		TicksToUs(all.cpuTicks),
		TicksToUs(draws.cpuTicks),
		TicksToUs(bindings.cpuTicks),
		TicksToUs(Dispatch.cpuTicks),
		BindingQueueTasks,
		static_cast<double>(BindingQueueTaskUs),
		static_cast<double>(BindingQueueMaxTaskUs),
		BindingQueueDrains,
		static_cast<double>(BindingQueueDrainUs),
		static_cast<double>(BindingQueueMaxDrainUs),
		BindingQueueMaxPending);

	wchar_t drawsBuf[32];
	wchar_t dispatchBuf[32];
	wchar_t setPsoBuf[32];
	wchar_t bindingsBuf[32];
	wchar_t queueTaskBuf[32];
	wchar_t queueDrainBuf[32];
	overlayText[0] = L'\0';
	swprintf_s(overlayText, L"DX12 Profiling Frame %ld\n"
		L"  Draws: %ld (I %ld + II %ld)  %s\n"
		L"  Dispatch: %ld  %s\n"
		L"  SetPSO: %ld  %s\n"
		L"  Bindings: %ld  %s\n"
		L"    DH:%ld RS:%ld RT:%ld CBV:%ld SRV:%ld UAV:%ld\n"
		L"    IB:%ld VB:%ld Topo:%ld\n"
		L"  Barriers: %ld\n"
		L"  ExecCL:%ld Reset:%ld Present:%ld\n"
		L"  BindingQueue: tasks=%ld %s drains=%ld %s maxPending=%ld\n"
		L"  Total: %ld  FF:%ld(%.0f%%)",
		present,
		draws.calls, DrawInstanced.calls, DrawIndexedInstanced.calls,
		FormatUs(TicksToUs(draws.cpuTicks), drawsBuf, ARRAYSIZE(drawsBuf)),
		Dispatch.calls,
		FormatUs(TicksToUs(Dispatch.cpuTicks), dispatchBuf, ARRAYSIZE(dispatchBuf)),
		SetPipelineState.calls,
		FormatUs(TicksToUs(SetPipelineState.cpuTicks), setPsoBuf, ARRAYSIZE(setPsoBuf)),
		bindings.calls,
		FormatUs(TicksToUs(bindings.cpuTicks), bindingsBuf, ARRAYSIZE(bindingsBuf)),
		SetDescriptorHeaps.calls,
		SetComputeRootSignature.calls + SetGraphicsRootSignature.calls,
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls,
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls,
		SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls,
		SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls,
		IASetIndexBuffer.calls, IASetVertexBuffers.calls, IASetPrimitiveTopology.calls,
		ResourceBarrier.calls,
		ExecuteCommandLists.calls, ResetCommandList.calls, Present.calls,
		BindingQueueTasks,
		FormatUs(static_cast<double>(BindingQueueTaskUs), queueTaskBuf, ARRAYSIZE(queueTaskBuf)),
		BindingQueueDrains,
		FormatUs(static_cast<double>(BindingQueueDrainUs), queueDrainBuf, ARRAYSIZE(queueDrainBuf)),
		BindingQueueMaxPending,
		all.calls,
		FastForwardHits.calls,
		all.calls > 0 ? (double)FastForwardHits.calls * 100.0 / (double)all.calls : 0.0);
}


void DX12ModRestorePreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList);

// Forward-declared so the hot binding flow above can count descriptor-record
// fast skips before DX12ResidentMaintenanceTelemetryUtils is defined.
static inline void DX12RecordDescriptorRecordFastSkip();

class DX12CommandListComputeBindingFlow
{
public:
	static void RestoreDeferredPreSkinBeforeBindingMutation(
		ID3D12GraphicsCommandList *commandList)
	{
		if (!DX12InternalReplayState::IsActive())
			DX12ModRestorePreSkinningUavReplacement(commandList);
	}

	static void ResolveDescriptorHeaps(
		UINT count,
		ID3D12DescriptorHeap *const *heaps,
		ID3D12DescriptorHeap **cbvSrvUav,
		ID3D12DescriptorHeap **sampler)
	{
		if (cbvSrvUav)
			*cbvSrvUav = nullptr;
		if (sampler)
			*sampler = nullptr;
		if (!heaps)
			return;

		for (UINT i = 0; i < count; ++i) {
			if (!heaps[i])
				continue;
			D3D12_DESCRIPTOR_HEAP_DESC desc = heaps[i]->GetDesc();
			if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
				if (cbvSrvUav)
					*cbvSrvUav = heaps[i];
			} else if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
				if (sampler)
					*sampler = heaps[i];
			}
		}
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (!DX12HotPathState::NeedsAnyResidentDescriptorState(flags) &&
			DX12HotPathState::skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
			commandList, runtimeState, count, heaps);
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool skipComputeBinding =
			DX12HotPathState::skipAll &&
			!(flags &
				(DX12HotPathState::FLAG_PRE_SKIN_PROBE |
				 DX12HotPathState::FLAG_PRE_SKIN_BINDING_TRACK));
		// Probe-only PreSkin keeps skipAll set, but it still needs the resident
		// descriptor heaps for compute-side matching.
		if (skipComputeBinding) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}

		// Fast path: identical heap set as the last resident capture and no
		// tracked binding recording needed. The resident heap state already
		// reflects this exact input, so skip the per-heap GetDesc resolution
		// entirely. Games re-bind the same heap set thousands of times per
		// frame; this is the highest-frequency binding entry point.
		if (runtimeState && heaps && count <= 2 &&
		    DX12HotPathState::skipBindings &&
		    runtimeState->lastSetHeapsCount == count &&
		    (count == 0 ||
		     (runtimeState->lastSetHeapsInput[0] == heaps[0] &&
		      (count < 2 || runtimeState->lastSetHeapsInput[1] == heaps[1])))) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}

		ID3D12DescriptorHeap *cbvSrvUav = nullptr;
		ID3D12DescriptorHeap *sampler = nullptr;
		ResolveDescriptorHeaps(count, heaps, &cbvSrvUav, &sampler);
		if (DX12HotPathState::NeedsAnyResidentDescriptorState(flags)) {
			if (runtimeState)
				DX12CommandListRuntime::SetDescriptorHeaps(runtimeState, cbvSrvUav, sampler);
			else
				DX12CommandListRuntime::SetDescriptorHeaps(commandList, cbvSrvUav, sampler);

			// Only remember the input once the resident state reflects it;
			// recording without updating would let a later flag change skip a
			// resident update that never happened.
			if (runtimeState) {
				if (heaps && count <= 2) {
					runtimeState->lastSetHeapsCount = count;
					runtimeState->lastSetHeapsInput[0] = count > 0 ? heaps[0] : nullptr;
					runtimeState->lastSetHeapsInput[1] = count > 1 ? heaps[1] : nullptr;
				} else {
					runtimeState->lastSetHeapsCount = 0xFFFFFFFF;
				}
			}
		}
		if (DX12HotPathState::skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList,
			" count=%u", count);
		DX12CommandListStateCapture::DescriptorHeaps(
			commandList, runtimeState, cbvSrvUav, sampler);
	}

	static void UpdateComputeBindingHash(
		DX12SimulateContext *runtimeState,
		uint64_t hashComponent)
	{
		if (!runtimeState || !hashComponent)
			return;
		runtimeState->computeBindingHash ^= hashComponent;
		runtimeState->computeBindingHash ^= runtimeState->computeBindingHash >> 33;
		runtimeState->computeBindingHash *= 0xff51afd7ed558ccdull;
	}

	static void UpdateComputeBindingHash(
		ID3D12GraphicsCommandList *commandList,
		uint64_t hashComponent)
	{
		if (!hashComponent)
			return;
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(runtimeState, hashComponent);
	}

	static bool CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (!needsResidentBinding && skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return true;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, runtimeState, rootSignature, graphics);
	}

	static bool CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		if (DX12InternalReplayState::IsActive())
			return true;
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		bool changed = true;
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
						runtimeState, rootSignature);
				else
					changed = DX12CommandListRuntime::RememberGraphicsRootSignature(
						commandList, rootSignature);
			} else {
				if (runtimeState)
					changed = DX12CommandListRuntime::RememberComputeRootSignature(
						runtimeState, rootSignature);
				else
					changed = DX12CommandListRuntime::RememberComputeRootSignature(
						commandList, rootSignature);
			}
		}
		if (!graphics && changed) {
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState, reinterpret_cast<uint64_t>(rootSignature));
		}
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return changed;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRootSignature" :
				"ID3D12GraphicsCommandList::SetComputeRootSignature",
			commandList, " rootSignature=%p", rootSignature);
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootSignature(
				commandList, runtimeState, rootSignature);
		} else {
			DX12CommandListStateCapture::ComputeRootSignature(
				commandList, runtimeState, rootSignature);
		}
		return true;
	}

	static bool CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentTable = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentDescriptorState(flags);
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (!needsResidentTable && skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return true;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, graphics);
	}

	static bool CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentTable = graphics ?
			DX12HotPathState::NeedsGraphicsResidentDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentDescriptorState(flags);
		bool changed = true;
		if (needsResidentTable) {
			if (runtimeState) {
				changed = DX12CommandListRuntime::RememberRootDescriptorTable(
					runtimeState, graphics, rootParameterIndex, baseDescriptor);
			} else {
				changed = DX12CommandListRuntime::RememberRootDescriptorTable(
					commandList, graphics, rootParameterIndex, baseDescriptor);
			}
		}
		if (!graphics && changed) {
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState,
				static_cast<uint64_t>(rootParameterIndex) ^ 0x9e3779b97f4a7c15ull);
		}
		const bool skipTrackedCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return changed;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable" :
				"ID3D12GraphicsCommandList::SetComputeRootDescriptorTable",
			commandList, " root=%u gpu=0x%llx",
			rootParameterIndex, static_cast<unsigned long long>(baseDescriptor.ptr));
		if (graphics) {
			DX12CommandListStateCapture::GraphicsRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListStateCapture::ComputeRootDescriptorTable(
				commandList, runtimeState, rootParameterIndex, baseDescriptor);
		}
		return true;
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (!needsResidentBinding && skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, runtimeState, rootParameterIndex, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset, 1, &srcData);
				else
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						commandList, rootParameterIndex, destOffset, 1, &srcData);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset, 1, &srcData);
				else
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						commandList, rootParameterIndex, destOffset, 1, &srcData);
			}
		}
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant" :
				"ID3D12GraphicsCommandList::SetComputeRoot32BitConstant",
			commandList, " root=%u destOffset=%u", rootParameterIndex, destOffset);
		if (graphics)
			DX12CommandListStateCapture::GraphicsRoot32BitConstant(
				commandList, runtimeState, rootParameterIndex, srcData, destOffset);
		else
			DX12CommandListStateCapture::ComputeRoot32BitConstant(
				commandList, runtimeState, rootParameterIndex, srcData, destOffset);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (!needsResidentBinding && skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, runtimeState, rootParameterIndex,
			num32BitValuesToSet, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootValueState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
				else
					DX12CommandListRuntime::RememberGraphicsRoot32BitConstants(
						commandList, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						runtimeState, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
				else
					DX12CommandListRuntime::RememberComputeRoot32BitConstants(
						commandList, rootParameterIndex, destOffset,
						num32BitValuesToSet, srcData);
			}
		}
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			graphics ? "ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants" :
				"ID3D12GraphicsCommandList::SetComputeRoot32BitConstants",
			commandList, " root=%u values=%u destOffset=%u",
			rootParameterIndex, num32BitValuesToSet, destOffset);
		if (graphics)
			DX12CommandListStateCapture::GraphicsRoot32BitConstants(
				commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
		else
			DX12CommandListStateCapture::ComputeRoot32BitConstants(
				commandList, runtimeState, rootParameterIndex, num32BitValuesToSet, srcData, destOffset);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (!needsResidentBinding && skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, runtimeState, rootParameterIndex, type, address, graphics, api);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		RestoreDeferredPreSkinBeforeBindingMutation(commandList);
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		const bool needsResidentBinding = graphics ?
			DX12HotPathState::NeedsGraphicsResidentRootDescriptorState(flags) :
			DX12HotPathState::NeedsComputeResidentBindingState(flags);
		if (needsResidentBinding) {
			if (graphics) {
				if (runtimeState)
					DX12CommandListRuntime::RememberGraphicsRootDescriptor(
						runtimeState, rootParameterIndex, type, address);
				else
					DX12CommandListRuntime::RememberGraphicsRootDescriptor(
						commandList, rootParameterIndex, type, address);
			} else {
				if (runtimeState)
					DX12CommandListRuntime::RememberComputeRootDescriptor(
						runtimeState, rootParameterIndex, type, address);
				else
					DX12CommandListRuntime::RememberComputeRootDescriptor(
						commandList, rootParameterIndex, type, address);
			}
		}
		const bool skipBindingCapture = graphics ?
			!DX12HotPathState::NeedsGraphicsTrackedBindingCapture(flags) :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
			DX12RecordDescriptorRecordFastSkip();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			api, commandList, " root=%u gpu=0x%llx",
			rootParameterIndex, static_cast<unsigned long long>(address));
		if (graphics)
			DX12CommandListStateCapture::GraphicsRootDescriptor(
				commandList, runtimeState, rootParameterIndex, type, address);
		else
			DX12CommandListStateCapture::ComputeRootDescriptor(
				commandList, runtimeState, rootParameterIndex, type, address);
	}
};


class DX12CommandListLifecycle
{
public:
	static void Register(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void ClearState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void InvalidateAfterExecuteBundle(ID3D12GraphicsCommandList *commandList);
};


class DX12CommandListCommandRecorder
{
public:
	static void RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
	static void RecordExecuteBundle(ID3D12GraphicsCommandList *bundle, ID3D12PipelineState *initialState);
	static void RecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
};



enum DX12HuntListCategory
{
	DX12_HUNT_LIST_VS,
	DX12_HUNT_LIST_PS,
	DX12_HUNT_LIST_CS,
	DX12_HUNT_LIST_IB,
	DX12_HUNT_LIST_VB,
};

struct DX12HuntListItem
{
	wchar_t label[64];
	UINT64 hash;
	bool selected;
};

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
void DX12HuntUnregisterCommandList(ID3D12GraphicsCommandList *commandList);
void DX12HuntObservePipelineState(ID3D12PipelineState *pipelineState);
void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex);
void DX12HuntPublishCommandList(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList);
void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex);
bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList);
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12HuntShouldDrawOverlay();
void DX12HuntToggle();
void DX12HuntOnPresent();
void DX12HuntPreviousVS();
void DX12HuntNextVS();
void DX12HuntPreviousPS();
void DX12HuntNextPS();
void DX12HuntPreviousCS();
void DX12HuntNextCS();
void DX12HuntPreviousIB();
void DX12HuntNextIB();
void DX12HuntPreviousVB();
void DX12HuntNextVB();
void DX12HuntCopySelectedHash();
void DX12HuntResetSelection();
void DX12HuntRefreshOverlay();
size_t DX12HuntGetListItems(
	DX12HuntListCategory category, DX12HuntListItem *items, size_t itemCount);
void DX12HuntSelectListItem(DX12HuntListCategory category, size_t index);
void DX12HuntSelectListHash(DX12HuntListCategory category, UINT64 hash);



class DX12ResidentMaintenanceTelemetryUtils
{
public:
	inline static volatile LONG PsoCalls = 0;
	inline static volatile LONG PsoChanged = 0;
	inline static volatile LONG PsoNoChange = 0;
	inline static volatile LONG RuntimePsoCalls = 0;
	inline static volatile LONG RuntimePsoChanged = 0;
	inline static volatile LONG RuntimePsoNoChange = 0;
	inline static volatile LONG RuntimeLookupTlsHits = 0;
	inline static volatile LONG RuntimeLookupDirectHits = 0;
	inline static volatile LONG RuntimeLookupShardHits = 0;
	inline static volatile LONG RuntimeLookupMisses = 0;

	inline static volatile LONG GraphicsRootSignatureCalls = 0;
	inline static volatile LONG GraphicsRootSignatureChanged = 0;
	inline static volatile LONG GraphicsRootSignatureNoChange = 0;
	inline static volatile LONG ComputeRootSignatureCalls = 0;
	inline static volatile LONG ComputeRootSignatureChanged = 0;
	inline static volatile LONG ComputeRootSignatureNoChange = 0;

	inline static volatile LONG DescriptorHeapResolveCalls = 0;
	inline static volatile LONG64 DescriptorHeapResolveSlots = 0;
	inline static volatile LONG DescriptorHeapCalls = 0;
	inline static volatile LONG DescriptorHeapChanged = 0;
	inline static volatile LONG DescriptorHeapNoChange = 0;
	inline static volatile LONG64 DescriptorHeapInvalidatedGraphicsTables = 0;
	inline static volatile LONG64 DescriptorHeapInvalidatedComputeTables = 0;

	inline static volatile LONG GraphicsRootTableCalls = 0;
	inline static volatile LONG GraphicsRootTableTlsHits = 0;
	inline static volatile LONG GraphicsRootTableChanged = 0;
	inline static volatile LONG GraphicsRootTableNoChange = 0;
	inline static volatile LONG ComputeRootTableCalls = 0;
	inline static volatile LONG ComputeRootTableTlsHits = 0;
	inline static volatile LONG ComputeRootTableChanged = 0;
	inline static volatile LONG ComputeRootTableNoChange = 0;

	inline static volatile LONG GraphicsRootDescriptorCalls = 0;
	inline static volatile LONG GraphicsRootDescriptorTlsHits = 0;
	inline static volatile LONG GraphicsRootDescriptorChanged = 0;
	inline static volatile LONG GraphicsRootDescriptorNoChange = 0;
	inline static volatile LONG ComputeRootDescriptorCalls = 0;
	inline static volatile LONG ComputeRootDescriptorTlsHits = 0;
	inline static volatile LONG ComputeRootDescriptorChanged = 0;
	inline static volatile LONG ComputeRootDescriptorNoChange = 0;

	inline static volatile LONG GraphicsRootConstantsCalls = 0;
	inline static volatile LONG64 GraphicsRootConstantsValues = 0;
	inline static volatile LONG ComputeRootConstantsCalls = 0;
	inline static volatile LONG64 ComputeRootConstantsValues = 0;

	inline static volatile LONG PrimitiveTopologyCalls = 0;
	inline static volatile LONG PrimitiveTopologyChanged = 0;
	inline static volatile LONG PrimitiveTopologyNoChange = 0;
	inline static volatile LONG RuntimePrimitiveTopologyCalls = 0;
	inline static volatile LONG RuntimePrimitiveTopologyChanged = 0;
	inline static volatile LONG RuntimePrimitiveTopologyNoChange = 0;

	inline static volatile LONG IndexBufferCalls = 0;
	inline static volatile LONG IndexBufferChanged = 0;
	inline static volatile LONG IndexBufferNoChange = 0;
	inline static volatile LONG IndexBufferResolvedReleased = 0;
	inline static volatile LONG RuntimeIndexBufferCalls = 0;
	inline static volatile LONG RuntimeIndexBufferChanged = 0;
	inline static volatile LONG RuntimeIndexBufferNoChange = 0;

	inline static volatile LONG VertexBufferCalls = 0;
	inline static volatile LONG64 VertexBufferSlots = 0;
	inline static volatile LONG64 VertexBufferChangedSlots = 0;
	inline static volatile LONG64 VertexBufferNoChangeSlots = 0;
	inline static volatile LONG VertexBufferResolvedReleased = 0;
	inline static volatile LONG RuntimeVertexBufferCalls = 0;
	inline static volatile LONG64 RuntimeVertexBufferSlots = 0;
	inline static volatile LONG64 RuntimeVertexBufferChangedSlots = 0;
	inline static volatile LONG64 RuntimeVertexBufferNoChangeSlots = 0;

	inline static volatile LONG BarrierCaptureCalls = 0;
	inline static volatile LONG64 BarrierCaptureItems = 0;
	inline static volatile LONG BarrierCaptureSkippedInternalReplay = 0;
	inline static volatile LONG BarrierCaptureSkippedHotPath = 0;
	inline static volatile LONG BarrierCaptureSkippedStatePolicy = 0;
	inline static volatile LONG BarrierRecordedCalls = 0;
	inline static volatile LONG64 BarrierRecordedItems = 0;
	inline static volatile LONG BarrierUnwrapCalls = 0;
	inline static volatile LONG64 BarrierUnwrapItems = 0;

	inline static volatile LONG PreSkinBindingCacheCalls = 0;
	inline static volatile LONG PreSkinBindingCacheHits = 0;
	inline static volatile LONG PreSkinBindingCacheMisses = 0;
	inline static volatile LONG PreSkinBindingCacheStores = 0;
	inline static volatile LONG64 PreSkinBindingRootTables = 0;
	inline static volatile LONG64 PreSkinBindingRanges = 0;
	inline static volatile LONG64 PreSkinBindingLightDescriptorLookups = 0;
	inline static volatile LONG64 PreSkinBindingFullDescriptorLookups = 0;
	inline static volatile LONG64 PreSkinBindingConstants = 0;
	inline static volatile LONG64 PreSkinBindingRootDescriptors = 0;
	inline static volatile LONG64 PreSkinBindingRuntimeResolved = 0;
	inline static volatile LONG64 PreSkinBindingRuntimeUnresolved = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorHits = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorMisses = 0;
	inline static volatile LONG64 PreSkinBindingDescriptorPending = 0;
	inline static volatile LONG64 PreSkinBindingWaitAvoided = 0;
	inline static volatile LONG64 RuntimeDescriptorWrites = 0;
	inline static volatile LONG64 RuntimeDescriptorCopyCalls = 0;
	inline static volatile LONG64 RuntimeDescriptorCopied = 0;
	inline static volatile LONG64 RuntimeDescriptorCopyMisses = 0;

	inline static volatile LONG DispatchDecisionCacheHits = 0;
	inline static volatile LONG DispatchDecisionCacheMisses = 0;
	inline static volatile LONG DispatchDecisionPsoCacheHits = 0;
	inline static volatile LONG DispatchDecisionPsoCacheMisses = 0;
	inline static volatile LONG DispatchDecisionBuilds = 0;
	inline static volatile LONG DispatchDecisionNoCompute = 0;
	inline static volatile LONG DispatchDecisionNoPreSkinCandidate = 0;
	inline static volatile LONG DispatchDecisionPreSkinCandidate = 0;
	inline static volatile LONG DispatchDecisionPreSkinCandidateDispatches = 0;
	inline static volatile LONG DispatchDecisionShaderOverrideWork = 0;
	inline static volatile LONG DispatchDecisionFastRejects = 0;

	inline static volatile LONG PreSkinPlanCacheLookups = 0;
	inline static volatile LONG PreSkinPlanCacheHits = 0;
	inline static volatile LONG PreSkinPlanCacheNegativeHits = 0;
	inline static volatile LONG PreSkinPlanCachePositiveHits = 0;
	inline static volatile LONG PreSkinPlanCacheMisses = 0;
	inline static volatile LONG PreSkinPlanCacheStores = 0;
	inline static volatile LONG PreSkinPlanFingerprintBuilds = 0;

	inline static volatile LONG DrawDecisionCacheHits = 0;
	inline static volatile LONG DrawDecisionCacheMisses = 0;
	inline static volatile LONG DrawDecisionPsoCacheHits = 0;
	inline static volatile LONG DrawDecisionPsoCacheMisses = 0;
	inline static volatile LONG DrawDecisionBuilds = 0;
	inline static volatile LONG DrawDecisionNoPso = 0;
	inline static volatile LONG DrawDecisionNoShaderOverride = 0;
	inline static volatile LONG DrawDecisionNoDrawOverride = 0;
	inline static volatile LONG DrawDecisionNeedsIaHash = 0;
	inline static volatile LONG DrawDecisionNeedsDescriptorMatch = 0;
	inline static volatile LONG DrawDecisionNeedsReplacement = 0;
	inline static volatile LONG DrawDecisionSkipOriginal = 0;
	inline static volatile LONG DrawDecisionIaHashDraws = 0;
	inline static volatile LONG DrawDecisionIaHashCacheLookups = 0;
	inline static volatile LONG DrawDecisionIaHashCacheHits = 0;
	inline static volatile LONG DrawDecisionIaHashCacheMisses = 0;
	inline static volatile LONG DrawDecisionIaHashCacheStores = 0;
	inline static volatile LONG DrawDecisionIaHashCacheEvictions = 0;
	inline static volatile LONG DrawDecisionIaHashCacheNegativeHits = 0;
	inline static volatile LONG DrawDecisionDescriptorDraws = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheLookups = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheHits = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheMisses = 0;
	inline static volatile LONG DrawDecisionDirectProbeCachePositiveHits = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheNegativeHits = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheStores = 0;
	inline static volatile LONG DrawDecisionDirectProbeCacheEvictions = 0;

	// Draw-hook no-op forwarding diagnostics. These are per-frame counters in
	// timing builds so a rejected PSO-level fast path never requires per-draw
	// logging to diagnose.
	inline static volatile LONG DrawNoopFastPathAttempts = 0;
	inline static volatile LONG DrawNoopFastPathForwarded = 0;
	inline static volatile LONG DrawNoopFastPathHuntBlocked = 0;
	inline static volatile LONG DrawNoopFastPathCaptureBlocked = 0;
	inline static volatile LONG DrawNoopFastPathShaderDumpBlocked = 0;
	inline static volatile LONG DrawNoopFastPathNoRuntime = 0;
	inline static volatile LONG DrawNoopFastPathNoDecision = 0;
	inline static volatile LONG DrawNoopFastPathNonNoopDecision = 0;

	// Zero-work-path verification (docs/DX11_ARCHITECTURE_MIGRATION_TO_DX12.md
	// Stage A): binding events, descriptor epoch writes and Hunt visited-set
	// writes must stay 0 in plain Mod mode; descriptor record fast-skips should
	// cover nearly every binding call when no capture is active.
	inline static volatile LONG BindingEventsRecorded = 0;
	inline static volatile LONG DescriptorEpochWrites = 0;
	inline static volatile LONG HuntVisitedWrites = 0;
	inline static volatile LONG DescriptorRecordFastSkips = 0;

	static void Inc(volatile LONG *value)
	{
	// Match the hot-path timing macro convention (DX12CommandFlow.hpp): the
	// counters only run in an actual timing build. Defining
	// DX12_ENABLE_DETAILED_TIMING alone must never enable atomic counting in
	// a plain Release build.
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		InterlockedIncrement(value);
	#else
		(void)value;
	#endif
	}

	static void Add64(volatile LONG64 *value, LONG64 amount)
	{
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		if (amount)
			InterlockedExchangeAdd64(value, amount);
	#else
		(void)value;
		(void)amount;
	#endif
	}

	static LONG Take(volatile LONG *value)
	{
		return InterlockedExchange(value, 0);
	}

	static LONG64 Take64(volatile LONG64 *value)
	{
		return InterlockedExchange64(value, 0);
	}

	static void RecordPsoCall()
	{
		Inc(&PsoCalls);
	}

	static void RecordPsoChanged()
	{
		Inc(&PsoChanged);
	}

	static void RecordPsoNoChange()
	{
		Inc(&PsoNoChange);
	}

	static void RecordRuntimePso(bool changed)
	{
		Inc(&RuntimePsoCalls);
		if (changed)
			Inc(&RuntimePsoChanged);
		else
			Inc(&RuntimePsoNoChange);
	}

	static void RecordRuntimeLookupTlsHit()
	{
		Inc(&RuntimeLookupTlsHits);
	}

	static void RecordRuntimeLookupDirectHit()
	{
		Inc(&RuntimeLookupDirectHits);
	}

	static void RecordRuntimeLookupShardHit()
	{
		Inc(&RuntimeLookupShardHits);
	}

	static void RecordRuntimeLookupMiss()
	{
		Inc(&RuntimeLookupMisses);
	}

	static void RecordGraphicsRootSignature(bool changed)
	{
		Inc(&GraphicsRootSignatureCalls);
		if (changed)
			Inc(&GraphicsRootSignatureChanged);
		else
			Inc(&GraphicsRootSignatureNoChange);
	}

	static void RecordComputeRootSignature(bool changed)
	{
		Inc(&ComputeRootSignatureCalls);
		if (changed)
			Inc(&ComputeRootSignatureChanged);
		else
			Inc(&ComputeRootSignatureNoChange);
	}

	static void RecordDescriptorHeapResolve(UINT count)
	{
		Inc(&DescriptorHeapResolveCalls);
		Add64(&DescriptorHeapResolveSlots, static_cast<LONG64>(count));
	}

	static void RecordDescriptorHeapsChanged(
		UINT graphicsInvalidated,
		UINT computeInvalidated)
	{
		Inc(&DescriptorHeapCalls);
		Inc(&DescriptorHeapChanged);
		Add64(&DescriptorHeapInvalidatedGraphicsTables, static_cast<LONG64>(graphicsInvalidated));
		Add64(&DescriptorHeapInvalidatedComputeTables, static_cast<LONG64>(computeInvalidated));
	}

	static void RecordDescriptorHeapsNoChange()
	{
		Inc(&DescriptorHeapCalls);
		Inc(&DescriptorHeapNoChange);
	}

	static void RecordGraphicsRootTableCall()
	{
		Inc(&GraphicsRootTableCalls);
	}

	static void RecordGraphicsRootTableTlsHit()
	{
		Inc(&GraphicsRootTableTlsHits);
	}

	static void RecordGraphicsRootTableChanged()
	{
		Inc(&GraphicsRootTableChanged);
	}

	static void RecordGraphicsRootTableNoChange()
	{
		Inc(&GraphicsRootTableNoChange);
	}

	static void RecordComputeRootTableCall()
	{
		Inc(&ComputeRootTableCalls);
	}

	static void RecordComputeRootTableTlsHit()
	{
		Inc(&ComputeRootTableTlsHits);
	}

	static void RecordComputeRootTableChanged()
	{
		Inc(&ComputeRootTableChanged);
	}

	static void RecordComputeRootTableNoChange()
	{
		Inc(&ComputeRootTableNoChange);
	}

	static void RecordGraphicsRootDescriptorCall()
	{
		Inc(&GraphicsRootDescriptorCalls);
	}

	static void RecordGraphicsRootDescriptorTlsHit()
	{
		Inc(&GraphicsRootDescriptorTlsHits);
	}

	static void RecordGraphicsRootDescriptorChanged()
	{
		Inc(&GraphicsRootDescriptorChanged);
	}

	static void RecordGraphicsRootDescriptorNoChange()
	{
		Inc(&GraphicsRootDescriptorNoChange);
	}

	static void RecordComputeRootDescriptorCall()
	{
		Inc(&ComputeRootDescriptorCalls);
	}

	static void RecordComputeRootDescriptorTlsHit()
	{
		Inc(&ComputeRootDescriptorTlsHits);
	}

	static void RecordComputeRootDescriptorChanged()
	{
		Inc(&ComputeRootDescriptorChanged);
	}

	static void RecordComputeRootDescriptorNoChange()
	{
		Inc(&ComputeRootDescriptorNoChange);
	}

	static void RecordGraphicsRootConstants(UINT count)
	{
		Inc(&GraphicsRootConstantsCalls);
		Add64(&GraphicsRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordComputeRootConstants(UINT count)
	{
		Inc(&ComputeRootConstantsCalls);
		Add64(&ComputeRootConstantsValues, static_cast<LONG64>(count));
	}

	static void RecordPrimitiveTopology(bool changed)
	{
		Inc(&PrimitiveTopologyCalls);
		if (changed)
			Inc(&PrimitiveTopologyChanged);
		else
			Inc(&PrimitiveTopologyNoChange);
	}

	static void RecordRuntimePrimitiveTopology(bool changed)
	{
		Inc(&RuntimePrimitiveTopologyCalls);
		if (changed)
			Inc(&RuntimePrimitiveTopologyChanged);
		else
			Inc(&RuntimePrimitiveTopologyNoChange);
	}

	static void RecordIndexBuffer(bool changed)
	{
		Inc(&IndexBufferCalls);
		if (changed)
			Inc(&IndexBufferChanged);
		else
			Inc(&IndexBufferNoChange);
	}

	static void RecordIndexBufferResolvedReleased()
	{
		Inc(&IndexBufferResolvedReleased);
	}

	static void RecordRuntimeIndexBuffer(bool changed)
	{
		Inc(&RuntimeIndexBufferCalls);
		if (changed)
			Inc(&RuntimeIndexBufferChanged);
		else
			Inc(&RuntimeIndexBufferNoChange);
	}

	static void RecordVertexBufferCall(UINT slots)
	{
		Inc(&VertexBufferCalls);
		Add64(&VertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(&VertexBufferChangedSlots, 1);
		else
			Add64(&VertexBufferNoChangeSlots, 1);
	}

	static void RecordVertexBufferResolvedReleased()
	{
		Inc(&VertexBufferResolvedReleased);
	}

	static void RecordRuntimeVertexBufferCall(UINT slots)
	{
		Inc(&RuntimeVertexBufferCalls);
		Add64(&RuntimeVertexBufferSlots, static_cast<LONG64>(slots));
	}

	static void RecordRuntimeVertexBufferSlot(bool changed)
	{
		if (changed)
			Add64(&RuntimeVertexBufferChangedSlots, 1);
		else
			Add64(&RuntimeVertexBufferNoChangeSlots, 1);
	}

	static void RecordBarrierCaptureCall(UINT count)
	{
		Inc(&BarrierCaptureCalls);
		Add64(&BarrierCaptureItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierCaptureSkippedInternalReplay()
	{
		Inc(&BarrierCaptureSkippedInternalReplay);
	}

	static void RecordBarrierCaptureSkippedHotPath()
	{
		Inc(&BarrierCaptureSkippedHotPath);
	}

	static void RecordBarrierCaptureSkippedStatePolicy()
	{
		Inc(&BarrierCaptureSkippedStatePolicy);
	}

	static void RecordBarrierRecorded(UINT count)
	{
		Inc(&BarrierRecordedCalls);
		Add64(&BarrierRecordedItems, static_cast<LONG64>(count));
	}

	static void RecordBarrierUnwrap(UINT count)
	{
		Inc(&BarrierUnwrapCalls);
		Add64(&BarrierUnwrapItems, static_cast<LONG64>(count));
	}

	static void RecordPreSkinBindingCacheCall()
	{
		Inc(&PreSkinBindingCacheCalls);
	}

	static void RecordPreSkinBindingCacheHit()
	{
		Inc(&PreSkinBindingCacheHits);
	}

	static void RecordPreSkinBindingCacheMiss()
	{
		Inc(&PreSkinBindingCacheMisses);
	}

	static void RecordPreSkinBindingCacheStore()
	{
		Inc(&PreSkinBindingCacheStores);
	}

	static void RecordPreSkinBindingRootTable()
	{
		Add64(&PreSkinBindingRootTables, 1);
	}

	static void RecordPreSkinBindingRange()
	{
		Add64(&PreSkinBindingRanges, 1);
	}

	static void RecordPreSkinBindingLightDescriptorLookup()
	{
		Add64(&PreSkinBindingLightDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingFullDescriptorLookup()
	{
		Add64(&PreSkinBindingFullDescriptorLookups, 1);
	}

	static void RecordPreSkinBindingConstant()
	{
		Add64(&PreSkinBindingConstants, 1);
	}

	static void RecordPreSkinBindingRootDescriptor()
	{
		Add64(&PreSkinBindingRootDescriptors, 1);
	}

	static void RecordPreSkinBindingRuntimeTable(bool resolved)
	{
		if (resolved)
			Add64(&PreSkinBindingRuntimeResolved, 1);
		else
			Add64(&PreSkinBindingRuntimeUnresolved, 1);
	}

	static void RecordPreSkinBindingDescriptorLookupResult(
		LONG64 hitCount,
		LONG64 missCount,
		LONG64 pendingCount)
	{
		Add64(&PreSkinBindingDescriptorHits, hitCount);
		Add64(&PreSkinBindingDescriptorMisses, missCount);
		Add64(&PreSkinBindingDescriptorPending, pendingCount);
		Add64(&PreSkinBindingWaitAvoided, hitCount + missCount + pendingCount);
	}

	static void RecordRuntimeDescriptorWrite()
	{
		Add64(&RuntimeDescriptorWrites, 1);
	}

	static void RecordRuntimeDescriptorCopy(UINT copied, UINT misses)
	{
		Add64(&RuntimeDescriptorCopyCalls, 1);
		Add64(&RuntimeDescriptorCopied, static_cast<LONG64>(copied));
		Add64(&RuntimeDescriptorCopyMisses, static_cast<LONG64>(misses));
	}

	static void RecordDispatchDecisionCacheHit()
	{
		Inc(&DispatchDecisionCacheHits);
	}

	static void RecordDispatchDecisionCacheMiss()
	{
		Inc(&DispatchDecisionCacheMisses);
	}

	static void RecordDispatchDecisionPsoCacheHit()
	{
		Inc(&DispatchDecisionPsoCacheHits);
	}

	static void RecordDispatchDecisionPsoCacheMiss()
	{
		Inc(&DispatchDecisionPsoCacheMisses);
	}

	static void RecordDispatchDecisionBuild()
	{
		Inc(&DispatchDecisionBuilds);
	}

	static void RecordDispatchDecisionNoCompute()
	{
		Inc(&DispatchDecisionNoCompute);
	}

	static void RecordDispatchDecisionNoPreSkinCandidate()
	{
		Inc(&DispatchDecisionNoPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidate()
	{
		Inc(&DispatchDecisionPreSkinCandidate);
	}

	static void RecordDispatchDecisionPreSkinCandidateDispatch()
	{
		Inc(&DispatchDecisionPreSkinCandidateDispatches);
	}

	static void RecordDispatchDecisionShaderOverrideWork()
	{
		Inc(&DispatchDecisionShaderOverrideWork);
	}

	static void RecordDispatchDecisionFastReject()
	{
		Inc(&DispatchDecisionFastRejects);
	}

	static void RecordPreSkinPlanCacheLookup()
	{
		Inc(&PreSkinPlanCacheLookups);
	}

	static void RecordPreSkinPlanCacheHit(bool positive)
	{
		Inc(&PreSkinPlanCacheHits);
		if (positive)
			Inc(&PreSkinPlanCachePositiveHits);
		else
			Inc(&PreSkinPlanCacheNegativeHits);
	}

	static void RecordPreSkinPlanCacheMiss()
	{
		Inc(&PreSkinPlanCacheMisses);
	}

	static void RecordPreSkinPlanCacheStore()
	{
		Inc(&PreSkinPlanCacheStores);
	}

	static void RecordPreSkinPlanFingerprintBuild()
	{
		Inc(&PreSkinPlanFingerprintBuilds);
	}

	static void RecordDrawDecisionCacheHit()
	{
		Inc(&DrawDecisionCacheHits);
	}

	static void RecordDrawDecisionCacheMiss()
	{
		Inc(&DrawDecisionCacheMisses);
	}

	static void RecordDrawDecisionPsoCacheHit()
	{
		Inc(&DrawDecisionPsoCacheHits);
	}

	static void RecordDrawDecisionPsoCacheMiss()
	{
		Inc(&DrawDecisionPsoCacheMisses);
	}

	static void RecordDrawDecisionBuild()
	{
		Inc(&DrawDecisionBuilds);
	}

	static void RecordDrawDecisionNoPso()
	{
		Inc(&DrawDecisionNoPso);
	}

	static void RecordDrawDecisionNoShaderOverride()
	{
		Inc(&DrawDecisionNoShaderOverride);
	}

	static void RecordDrawDecisionNoDrawOverride()
	{
		Inc(&DrawDecisionNoDrawOverride);
	}

	static void RecordDrawDecisionNeedsIaHash()
	{
		Inc(&DrawDecisionNeedsIaHash);
	}

	static void RecordDrawDecisionNeedsDescriptorMatch()
	{
		Inc(&DrawDecisionNeedsDescriptorMatch);
	}

	static void RecordDrawDecisionNeedsReplacement()
	{
		Inc(&DrawDecisionNeedsReplacement);
	}

	static void RecordDrawDecisionSkipOriginal()
	{
		Inc(&DrawDecisionSkipOriginal);
	}

	static void RecordDrawDecisionIaHashDraw()
	{
		Inc(&DrawDecisionIaHashDraws);
	}

	static void RecordDrawDecisionIaHashCacheLookup()
	{
		Inc(&DrawDecisionIaHashCacheLookups);
	}

	static void RecordDrawDecisionIaHashCacheHit(bool hasIaHashState)
	{
		Inc(&DrawDecisionIaHashCacheHits);
		if (!hasIaHashState)
			Inc(&DrawDecisionIaHashCacheNegativeHits);
	}

	static void RecordDrawDecisionIaHashCacheMiss()
	{
		Inc(&DrawDecisionIaHashCacheMisses);
	}

	static void RecordDrawDecisionIaHashCacheStore()
	{
		Inc(&DrawDecisionIaHashCacheStores);
	}

	static void RecordDrawDecisionIaHashCacheEviction()
	{
		Inc(&DrawDecisionIaHashCacheEvictions);
	}

	static void RecordDrawDecisionDescriptorDraw()
	{
		Inc(&DrawDecisionDescriptorDraws);
	}

	static void RecordDrawDecisionDirectProbeCacheLookup()
	{
		Inc(&DrawDecisionDirectProbeCacheLookups);
	}

	static void RecordDrawDecisionDirectProbeCacheHit(bool hasDirectCandidate)
	{
		Inc(&DrawDecisionDirectProbeCacheHits);
		if (hasDirectCandidate)
			Inc(&DrawDecisionDirectProbeCachePositiveHits);
		else
			Inc(&DrawDecisionDirectProbeCacheNegativeHits);
	}

	static void RecordDrawDecisionDirectProbeCacheMiss()
	{
		Inc(&DrawDecisionDirectProbeCacheMisses);
	}

	static void RecordDrawDecisionDirectProbeCacheStore(bool evicted)
	{
		Inc(&DrawDecisionDirectProbeCacheStores);
		if (evicted)
			Inc(&DrawDecisionDirectProbeCacheEvictions);
	}

	static void RecordDrawNoopFastPathAttempt()
	{
		Inc(&DrawNoopFastPathAttempts);
	}

	static void RecordDrawNoopFastPathForwarded()
	{
		Inc(&DrawNoopFastPathForwarded);
	}

	static void RecordDrawNoopFastPathHuntBlocked()
	{
		Inc(&DrawNoopFastPathHuntBlocked);
	}

	static void RecordDrawNoopFastPathCaptureBlocked()
	{
		Inc(&DrawNoopFastPathCaptureBlocked);
	}

	static void RecordDrawNoopFastPathShaderDumpBlocked()
	{
		Inc(&DrawNoopFastPathShaderDumpBlocked);
	}

	static void RecordDrawNoopFastPathNoRuntime()
	{
		Inc(&DrawNoopFastPathNoRuntime);
	}

	static void RecordDrawNoopFastPathNoDecision()
	{
		Inc(&DrawNoopFastPathNoDecision);
	}

	static void RecordDrawNoopFastPathNonNoopDecision()
	{
		Inc(&DrawNoopFastPathNonNoopDecision);
	}

	static void LogPso()
	{
		LONG psoCalls = Take(&PsoCalls);
		LONG psoChanged = Take(&PsoChanged);
		LONG psoNoChange = Take(&PsoNoChange);
		LONG runtimePsoCalls = Take(&RuntimePsoCalls);
		LONG runtimePsoChanged = Take(&RuntimePsoChanged);
		LONG runtimePsoNoChange = Take(&RuntimePsoNoChange);
		if (!psoCalls && !psoChanged && !psoNoChange &&
			!runtimePsoCalls && !runtimePsoChanged && !runtimePsoNoChange)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"pso\",\"trackerCalls\":%ld,\"trackerChanged\":%ld,\"trackerNoChange\":%ld,\"runtimeCalls\":%ld,\"runtimeChanged\":%ld,\"runtimeNoChange\":%ld",
			psoCalls, psoChanged, psoNoChange,
			runtimePsoCalls, runtimePsoChanged, runtimePsoNoChange);
	}

	static void LogRootState()
	{
		LONG graphicsRootSignatureCalls = Take(&GraphicsRootSignatureCalls);
		LONG graphicsRootSignatureChanged = Take(&GraphicsRootSignatureChanged);
		LONG graphicsRootSignatureNoChange = Take(&GraphicsRootSignatureNoChange);
		LONG computeRootSignatureCalls = Take(&ComputeRootSignatureCalls);
		LONG computeRootSignatureChanged = Take(&ComputeRootSignatureChanged);
		LONG computeRootSignatureNoChange = Take(&ComputeRootSignatureNoChange);
		LONG graphicsRootTableCalls = Take(&GraphicsRootTableCalls);
		LONG graphicsRootTableTlsHits = Take(&GraphicsRootTableTlsHits);
		LONG graphicsRootTableChanged = Take(&GraphicsRootTableChanged);
		LONG graphicsRootTableNoChange = Take(&GraphicsRootTableNoChange);
		LONG computeRootTableCalls = Take(&ComputeRootTableCalls);
		LONG computeRootTableTlsHits = Take(&ComputeRootTableTlsHits);
		LONG computeRootTableChanged = Take(&ComputeRootTableChanged);
		LONG computeRootTableNoChange = Take(&ComputeRootTableNoChange);
		if (!graphicsRootSignatureCalls && !computeRootSignatureCalls &&
			!graphicsRootTableCalls && !computeRootTableCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"root\",\"gfxSigCalls\":%ld,\"gfxSigChanged\":%ld,\"gfxSigNoChange\":%ld,\"compSigCalls\":%ld,\"compSigChanged\":%ld,\"compSigNoChange\":%ld,\"gfxTableCalls\":%ld,\"gfxTableTlsHits\":%ld,\"gfxTableChanged\":%ld,\"gfxTableNoChange\":%ld,\"compTableCalls\":%ld,\"compTableTlsHits\":%ld,\"compTableChanged\":%ld,\"compTableNoChange\":%ld",
			graphicsRootSignatureCalls, graphicsRootSignatureChanged, graphicsRootSignatureNoChange,
			computeRootSignatureCalls, computeRootSignatureChanged, computeRootSignatureNoChange,
			graphicsRootTableCalls, graphicsRootTableTlsHits, graphicsRootTableChanged, graphicsRootTableNoChange,
			computeRootTableCalls, computeRootTableTlsHits, computeRootTableChanged, computeRootTableNoChange);
	}

	static void LogDescriptorState()
	{
		LONG descriptorHeapResolveCalls = Take(&DescriptorHeapResolveCalls);
		LONG64 descriptorHeapResolveSlots = Take64(&DescriptorHeapResolveSlots);
		LONG descriptorHeapCalls = Take(&DescriptorHeapCalls);
		LONG descriptorHeapChanged = Take(&DescriptorHeapChanged);
		LONG descriptorHeapNoChange = Take(&DescriptorHeapNoChange);
		LONG64 descriptorHeapInvalidatedGraphicsTables = Take64(&DescriptorHeapInvalidatedGraphicsTables);
		LONG64 descriptorHeapInvalidatedComputeTables = Take64(&DescriptorHeapInvalidatedComputeTables);
		LONG graphicsRootDescriptorCalls = Take(&GraphicsRootDescriptorCalls);
		LONG graphicsRootDescriptorTlsHits = Take(&GraphicsRootDescriptorTlsHits);
		LONG graphicsRootDescriptorChanged = Take(&GraphicsRootDescriptorChanged);
		LONG graphicsRootDescriptorNoChange = Take(&GraphicsRootDescriptorNoChange);
		LONG computeRootDescriptorCalls = Take(&ComputeRootDescriptorCalls);
		LONG computeRootDescriptorTlsHits = Take(&ComputeRootDescriptorTlsHits);
		LONG computeRootDescriptorChanged = Take(&ComputeRootDescriptorChanged);
		LONG computeRootDescriptorNoChange = Take(&ComputeRootDescriptorNoChange);
		LONG graphicsRootConstantsCalls = Take(&GraphicsRootConstantsCalls);
		LONG64 graphicsRootConstantsValues = Take64(&GraphicsRootConstantsValues);
		LONG computeRootConstantsCalls = Take(&ComputeRootConstantsCalls);
		LONG64 computeRootConstantsValues = Take64(&ComputeRootConstantsValues);
		if (!descriptorHeapResolveCalls && !descriptorHeapCalls &&
			!graphicsRootDescriptorCalls && !computeRootDescriptorCalls &&
			!graphicsRootConstantsCalls && !computeRootConstantsCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"descriptor\",\"heapResolveCalls\":%ld,\"heapResolveSlots\":%lld,\"heapCalls\":%ld,\"heapChanged\":%ld,\"heapNoChange\":%ld,\"heapInvalidatedGfxTables\":%lld,\"heapInvalidatedCompTables\":%lld,\"gfxDescCalls\":%ld,\"gfxDescTlsHits\":%ld,\"gfxDescChanged\":%ld,\"gfxDescNoChange\":%ld,\"compDescCalls\":%ld,\"compDescTlsHits\":%ld,\"compDescChanged\":%ld,\"compDescNoChange\":%ld,\"gfxConstCalls\":%ld,\"gfxConstValues\":%lld,\"compConstCalls\":%ld,\"compConstValues\":%lld",
			descriptorHeapResolveCalls, static_cast<long long>(descriptorHeapResolveSlots),
			descriptorHeapCalls, descriptorHeapChanged, descriptorHeapNoChange,
			static_cast<long long>(descriptorHeapInvalidatedGraphicsTables),
			static_cast<long long>(descriptorHeapInvalidatedComputeTables),
			graphicsRootDescriptorCalls, graphicsRootDescriptorTlsHits,
			graphicsRootDescriptorChanged, graphicsRootDescriptorNoChange,
			computeRootDescriptorCalls, computeRootDescriptorTlsHits,
			computeRootDescriptorChanged, computeRootDescriptorNoChange,
			graphicsRootConstantsCalls, static_cast<long long>(graphicsRootConstantsValues),
			computeRootConstantsCalls, static_cast<long long>(computeRootConstantsValues));
	}

	static void LogIaState()
	{
		LONG primitiveTopologyCalls = Take(&PrimitiveTopologyCalls);
		LONG primitiveTopologyChanged = Take(&PrimitiveTopologyChanged);
		LONG primitiveTopologyNoChange = Take(&PrimitiveTopologyNoChange);
		LONG runtimePrimitiveTopologyCalls = Take(&RuntimePrimitiveTopologyCalls);
		LONG runtimePrimitiveTopologyChanged = Take(&RuntimePrimitiveTopologyChanged);
		LONG runtimePrimitiveTopologyNoChange = Take(&RuntimePrimitiveTopologyNoChange);
		LONG indexBufferCalls = Take(&IndexBufferCalls);
		LONG indexBufferChanged = Take(&IndexBufferChanged);
		LONG indexBufferNoChange = Take(&IndexBufferNoChange);
		LONG indexBufferResolvedReleased = Take(&IndexBufferResolvedReleased);
		LONG runtimeIndexBufferCalls = Take(&RuntimeIndexBufferCalls);
		LONG runtimeIndexBufferChanged = Take(&RuntimeIndexBufferChanged);
		LONG runtimeIndexBufferNoChange = Take(&RuntimeIndexBufferNoChange);
		LONG vertexBufferCalls = Take(&VertexBufferCalls);
		LONG64 vertexBufferSlots = Take64(&VertexBufferSlots);
		LONG64 vertexBufferChangedSlots = Take64(&VertexBufferChangedSlots);
		LONG64 vertexBufferNoChangeSlots = Take64(&VertexBufferNoChangeSlots);
		LONG vertexBufferResolvedReleased = Take(&VertexBufferResolvedReleased);
		LONG runtimeVertexBufferCalls = Take(&RuntimeVertexBufferCalls);
		LONG64 runtimeVertexBufferSlots = Take64(&RuntimeVertexBufferSlots);
		LONG64 runtimeVertexBufferChangedSlots = Take64(&RuntimeVertexBufferChangedSlots);
		LONG64 runtimeVertexBufferNoChangeSlots = Take64(&RuntimeVertexBufferNoChangeSlots);
		if (!primitiveTopologyCalls && !runtimePrimitiveTopologyCalls &&
			!indexBufferCalls && !runtimeIndexBufferCalls &&
			!vertexBufferCalls && !runtimeVertexBufferCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"ia\",\"topologyCalls\":%ld,\"topologyChanged\":%ld,\"topologyNoChange\":%ld,\"runtimeTopologyCalls\":%ld,\"runtimeTopologyChanged\":%ld,\"runtimeTopologyNoChange\":%ld,\"ibCalls\":%ld,\"ibChanged\":%ld,\"ibNoChange\":%ld,\"ibResolvedReleased\":%ld,\"runtimeIbCalls\":%ld,\"runtimeIbChanged\":%ld,\"runtimeIbNoChange\":%ld,\"vbCalls\":%ld,\"vbSlots\":%lld,\"vbChangedSlots\":%lld,\"vbNoChangeSlots\":%lld,\"vbResolvedReleased\":%ld,\"runtimeVbCalls\":%ld,\"runtimeVbSlots\":%lld,\"runtimeVbChangedSlots\":%lld,\"runtimeVbNoChangeSlots\":%lld",
			primitiveTopologyCalls, primitiveTopologyChanged, primitiveTopologyNoChange,
			runtimePrimitiveTopologyCalls, runtimePrimitiveTopologyChanged, runtimePrimitiveTopologyNoChange,
			indexBufferCalls, indexBufferChanged, indexBufferNoChange, indexBufferResolvedReleased,
			runtimeIndexBufferCalls, runtimeIndexBufferChanged, runtimeIndexBufferNoChange,
			vertexBufferCalls, static_cast<long long>(vertexBufferSlots),
			static_cast<long long>(vertexBufferChangedSlots),
			static_cast<long long>(vertexBufferNoChangeSlots),
			vertexBufferResolvedReleased,
			runtimeVertexBufferCalls, static_cast<long long>(runtimeVertexBufferSlots),
			static_cast<long long>(runtimeVertexBufferChangedSlots),
			static_cast<long long>(runtimeVertexBufferNoChangeSlots));
	}

	static void LogBarrierState()
	{
		LONG barrierCaptureCalls = Take(&BarrierCaptureCalls);
		LONG64 barrierCaptureItems = Take64(&BarrierCaptureItems);
		LONG barrierCaptureSkippedInternalReplay = Take(&BarrierCaptureSkippedInternalReplay);
		LONG barrierCaptureSkippedHotPath = Take(&BarrierCaptureSkippedHotPath);
		LONG barrierCaptureSkippedStatePolicy = Take(&BarrierCaptureSkippedStatePolicy);
		LONG barrierRecordedCalls = Take(&BarrierRecordedCalls);
		LONG64 barrierRecordedItems = Take64(&BarrierRecordedItems);
		LONG barrierUnwrapCalls = Take(&BarrierUnwrapCalls);
		LONG64 barrierUnwrapItems = Take64(&BarrierUnwrapItems);
		if (!barrierCaptureCalls && !barrierRecordedCalls && !barrierUnwrapCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"barrier\",\"captureCalls\":%ld,\"captureItems\":%lld,\"captureSkippedInternalReplay\":%ld,\"captureSkippedHotPath\":%ld,\"captureSkippedStatePolicy\":%ld,\"recordedCalls\":%ld,\"recordedItems\":%lld,\"unwrapCalls\":%ld,\"unwrapItems\":%lld",
			barrierCaptureCalls, static_cast<long long>(barrierCaptureItems),
			barrierCaptureSkippedInternalReplay, barrierCaptureSkippedHotPath,
			barrierCaptureSkippedStatePolicy,
			barrierRecordedCalls, static_cast<long long>(barrierRecordedItems),
			barrierUnwrapCalls, static_cast<long long>(barrierUnwrapItems));
	}

	static void LogPreSkinBindingState()
	{
		LONG preSkinBindingCacheCalls = Take(&PreSkinBindingCacheCalls);
		LONG preSkinBindingCacheHits = Take(&PreSkinBindingCacheHits);
		LONG preSkinBindingCacheMisses = Take(&PreSkinBindingCacheMisses);
		LONG preSkinBindingCacheStores = Take(&PreSkinBindingCacheStores);
		LONG64 preSkinBindingRootTables = Take64(&PreSkinBindingRootTables);
		LONG64 preSkinBindingRanges = Take64(&PreSkinBindingRanges);
		LONG64 preSkinBindingLightDescriptorLookups = Take64(&PreSkinBindingLightDescriptorLookups);
		LONG64 preSkinBindingFullDescriptorLookups = Take64(&PreSkinBindingFullDescriptorLookups);
		LONG64 preSkinBindingConstants = Take64(&PreSkinBindingConstants);
		LONG64 preSkinBindingRootDescriptors = Take64(&PreSkinBindingRootDescriptors);
		LONG64 preSkinBindingRuntimeResolved = Take64(&PreSkinBindingRuntimeResolved);
		LONG64 preSkinBindingRuntimeUnresolved = Take64(&PreSkinBindingRuntimeUnresolved);
		LONG64 preSkinBindingDescriptorHits = Take64(&PreSkinBindingDescriptorHits);
		LONG64 preSkinBindingDescriptorMisses = Take64(&PreSkinBindingDescriptorMisses);
		LONG64 preSkinBindingDescriptorPending = Take64(&PreSkinBindingDescriptorPending);
		LONG64 preSkinBindingWaitAvoided = Take64(&PreSkinBindingWaitAvoided);
		LONG64 runtimeDescriptorWrites = Take64(&RuntimeDescriptorWrites);
		LONG64 runtimeDescriptorCopyCalls = Take64(&RuntimeDescriptorCopyCalls);
		LONG64 runtimeDescriptorCopied = Take64(&RuntimeDescriptorCopied);
		LONG64 runtimeDescriptorCopyMisses = Take64(&RuntimeDescriptorCopyMisses);
		if (!preSkinBindingCacheCalls && !preSkinBindingCacheHits &&
			!preSkinBindingCacheMisses && !preSkinBindingCacheStores &&
			!preSkinBindingRuntimeResolved && !preSkinBindingRuntimeUnresolved &&
			!preSkinBindingWaitAvoided && !runtimeDescriptorWrites &&
			!runtimeDescriptorCopyCalls)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"preskin_bind_cache\",\"calls\":%ld,\"hits\":%ld,\"misses\":%ld,\"stores\":%ld,\"rootTables\":%lld,\"ranges\":%lld,\"lightDescriptorLookups\":%lld,\"fullDescriptorLookups\":%lld,\"constants\":%lld,\"rootDescriptors\":%lld,\"runtimeResolved\":%lld,\"runtimeUnresolved\":%lld,\"descriptorHits\":%lld,\"descriptorMisses\":%lld,\"descriptorPending\":%lld,\"waitAvoided\":%lld,\"runtimeDescriptorWrites\":%lld,\"runtimeDescriptorCopyCalls\":%lld,\"runtimeDescriptorCopied\":%lld,\"runtimeDescriptorCopyMisses\":%lld",
			preSkinBindingCacheCalls, preSkinBindingCacheHits,
			preSkinBindingCacheMisses, preSkinBindingCacheStores,
			static_cast<long long>(preSkinBindingRootTables),
			static_cast<long long>(preSkinBindingRanges),
			static_cast<long long>(preSkinBindingLightDescriptorLookups),
			static_cast<long long>(preSkinBindingFullDescriptorLookups),
			static_cast<long long>(preSkinBindingConstants),
			static_cast<long long>(preSkinBindingRootDescriptors),
			static_cast<long long>(preSkinBindingRuntimeResolved),
			static_cast<long long>(preSkinBindingRuntimeUnresolved),
			static_cast<long long>(preSkinBindingDescriptorHits),
			static_cast<long long>(preSkinBindingDescriptorMisses),
			static_cast<long long>(preSkinBindingDescriptorPending),
			static_cast<long long>(preSkinBindingWaitAvoided),
			static_cast<long long>(runtimeDescriptorWrites),
			static_cast<long long>(runtimeDescriptorCopyCalls),
			static_cast<long long>(runtimeDescriptorCopied),
			static_cast<long long>(runtimeDescriptorCopyMisses));
	}

	static void LogRuntimeLookup()
	{
		LONG tlsHits = Take(&RuntimeLookupTlsHits);
		LONG directHits = Take(&RuntimeLookupDirectHits);
		LONG shardHits = Take(&RuntimeLookupShardHits);
		LONG misses = Take(&RuntimeLookupMisses);
		if (!tlsHits && !directHits && !shardHits && !misses)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"runtime_lookup\",\"tlsHits\":%ld,\"directHits\":%ld,\"shardHits\":%ld,\"misses\":%ld",
			tlsHits, directHits, shardHits, misses);
	}

	static void LogDispatchDecisionState()
	{
		LONG cacheHits = Take(&DispatchDecisionCacheHits);
		LONG cacheMisses = Take(&DispatchDecisionCacheMisses);
		LONG psoCacheHits = Take(&DispatchDecisionPsoCacheHits);
		LONG psoCacheMisses = Take(&DispatchDecisionPsoCacheMisses);
		LONG builds = Take(&DispatchDecisionBuilds);
		LONG noCompute = Take(&DispatchDecisionNoCompute);
		LONG noPreSkinCandidate = Take(&DispatchDecisionNoPreSkinCandidate);
		LONG preSkinCandidate = Take(&DispatchDecisionPreSkinCandidate);
		LONG preSkinCandidateDispatches = Take(&DispatchDecisionPreSkinCandidateDispatches);
		LONG shaderOverrideWork = Take(&DispatchDecisionShaderOverrideWork);
		LONG fastRejects = Take(&DispatchDecisionFastRejects);
		if (!cacheHits && !cacheMisses && !psoCacheHits && !psoCacheMisses &&
			!builds && !noCompute &&
			!noPreSkinCandidate && !preSkinCandidate && !preSkinCandidateDispatches &&
			!shaderOverrideWork && !fastRejects)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"dispatch_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noCompute\":%ld,\"noPreSkinCandidate\":%ld,\"preSkinCandidateBuilds\":%ld,\"preSkinCandidateDispatches\":%ld,\"shaderOverrideWork\":%ld,\"fastRejects\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noCompute,
			noPreSkinCandidate, preSkinCandidate, preSkinCandidateDispatches,
			shaderOverrideWork, fastRejects);
	}

	static void LogPreSkinPlanState()
	{
		LONG lookups = Take(&PreSkinPlanCacheLookups);
		LONG hits = Take(&PreSkinPlanCacheHits);
		LONG negativeHits = Take(&PreSkinPlanCacheNegativeHits);
		LONG positiveHits = Take(&PreSkinPlanCachePositiveHits);
		LONG misses = Take(&PreSkinPlanCacheMisses);
		LONG stores = Take(&PreSkinPlanCacheStores);
		LONG fingerprints = Take(&PreSkinPlanFingerprintBuilds);
		if (!lookups && !hits && !negativeHits && !positiveHits &&
			!misses && !stores && !fingerprints)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"preskin_plan\",\"lookups\":%ld,\"hits\":%ld,\"negativeHits\":%ld,\"positiveHits\":%ld,\"misses\":%ld,\"stores\":%ld,\"fingerprintBuilds\":%ld",
			lookups, hits, negativeHits, positiveHits,
			misses, stores, fingerprints);
	}

	static void LogDrawDecisionState()
	{
		LONG cacheHits = Take(&DrawDecisionCacheHits);
		LONG cacheMisses = Take(&DrawDecisionCacheMisses);
		LONG psoCacheHits = Take(&DrawDecisionPsoCacheHits);
		LONG psoCacheMisses = Take(&DrawDecisionPsoCacheMisses);
		LONG builds = Take(&DrawDecisionBuilds);
		LONG noPso = Take(&DrawDecisionNoPso);
		LONG noShaderOverride = Take(&DrawDecisionNoShaderOverride);
		LONG noDrawOverride = Take(&DrawDecisionNoDrawOverride);
		LONG needsIaHash = Take(&DrawDecisionNeedsIaHash);
		LONG needsDescriptorMatch = Take(&DrawDecisionNeedsDescriptorMatch);
		LONG needsReplacement = Take(&DrawDecisionNeedsReplacement);
		LONG skipOriginal = Take(&DrawDecisionSkipOriginal);
		LONG iaHashDraws = Take(&DrawDecisionIaHashDraws);
		LONG iaHashCacheLookups = Take(&DrawDecisionIaHashCacheLookups);
		LONG iaHashCacheHits = Take(&DrawDecisionIaHashCacheHits);
		LONG iaHashCacheMisses = Take(&DrawDecisionIaHashCacheMisses);
		LONG iaHashCacheStores = Take(&DrawDecisionIaHashCacheStores);
		LONG iaHashCacheEvictions = Take(&DrawDecisionIaHashCacheEvictions);
		LONG iaHashCacheNegativeHits = Take(&DrawDecisionIaHashCacheNegativeHits);
		LONG descriptorDraws = Take(&DrawDecisionDescriptorDraws);
		LONG directProbeCacheLookups = Take(&DrawDecisionDirectProbeCacheLookups);
		LONG directProbeCacheHits = Take(&DrawDecisionDirectProbeCacheHits);
		LONG directProbeCacheMisses = Take(&DrawDecisionDirectProbeCacheMisses);
		LONG directProbeCachePositiveHits = Take(&DrawDecisionDirectProbeCachePositiveHits);
		LONG directProbeCacheNegativeHits = Take(&DrawDecisionDirectProbeCacheNegativeHits);
		LONG directProbeCacheStores = Take(&DrawDecisionDirectProbeCacheStores);
		LONG directProbeCacheEvictions = Take(&DrawDecisionDirectProbeCacheEvictions);
		if (!cacheHits && !cacheMisses && !psoCacheHits && !psoCacheMisses &&
			!builds && !noPso &&
			!noShaderOverride && !noDrawOverride && !needsIaHash &&
			!needsDescriptorMatch && !needsReplacement && !skipOriginal &&
			!iaHashDraws && !iaHashCacheLookups &&
			!iaHashCacheHits && !iaHashCacheMisses && !iaHashCacheStores &&
			!iaHashCacheEvictions &&
			!iaHashCacheNegativeHits && !descriptorDraws &&
			!directProbeCacheLookups && !directProbeCacheHits &&
			!directProbeCacheMisses && !directProbeCachePositiveHits &&
			!directProbeCacheNegativeHits && !directProbeCacheStores &&
			!directProbeCacheEvictions)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"draw_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noPso\":%ld,\"noShaderOverride\":%ld,\"noDrawOverride\":%ld,\"needsIaHash\":%ld,\"needsDescriptorMatch\":%ld,\"needsReplacement\":%ld,\"skipOriginal\":%ld,\"iaHashDraws\":%ld,\"iaHashCacheLookups\":%ld,\"iaHashCacheHits\":%ld,\"iaHashCacheMisses\":%ld,\"iaHashCacheStores\":%ld,\"iaHashCacheEvictions\":%ld,\"iaHashCacheNegativeHits\":%ld,\"descriptorDraws\":%ld,\"directProbeCacheLookups\":%ld,\"directProbeCacheHits\":%ld,\"directProbeCacheMisses\":%ld,\"directProbeCachePositiveHits\":%ld,\"directProbeCacheNegativeHits\":%ld,\"directProbeCacheStores\":%ld,\"directProbeCacheEvictions\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noPso,
			noShaderOverride, noDrawOverride, needsIaHash,
			needsDescriptorMatch, needsReplacement, skipOriginal,
			iaHashDraws, iaHashCacheLookups,
			iaHashCacheHits, iaHashCacheMisses, iaHashCacheStores, iaHashCacheEvictions,
			iaHashCacheNegativeHits, descriptorDraws,
			directProbeCacheLookups, directProbeCacheHits, directProbeCacheMisses,
			directProbeCachePositiveHits, directProbeCacheNegativeHits,
			directProbeCacheStores, directProbeCacheEvictions);
	}

	static void LogDrawNoopFastPathState()
	{
		LONG attempts = Take(&DrawNoopFastPathAttempts);
		LONG forwarded = Take(&DrawNoopFastPathForwarded);
		LONG huntBlocked = Take(&DrawNoopFastPathHuntBlocked);
		LONG captureBlocked = Take(&DrawNoopFastPathCaptureBlocked);
		LONG shaderDumpBlocked = Take(&DrawNoopFastPathShaderDumpBlocked);
		LONG noRuntime = Take(&DrawNoopFastPathNoRuntime);
		LONG noDecision = Take(&DrawNoopFastPathNoDecision);
		LONG nonNoopDecision = Take(&DrawNoopFastPathNonNoopDecision);
		if (!attempts && !forwarded && !huntBlocked && !captureBlocked &&
			!shaderDumpBlocked && !noRuntime && !noDecision && !nonNoopDecision)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"draw_noop_fast_path\",\"attempts\":%ld,\"forwarded\":%ld,\"huntBlocked\":%ld,\"captureBlocked\":%ld,\"shaderDumpBlocked\":%ld,\"noRuntime\":%ld,\"noDecision\":%ld,\"nonNoopDecision\":%ld",
			attempts, forwarded, huntBlocked, captureBlocked, shaderDumpBlocked,
			noRuntime, noDecision, nonNoopDecision);
	}

	static void RecordBindingEventRecorded()
	{
		Inc(&BindingEventsRecorded);
	}

	static void RecordDescriptorEpochWrite()
	{
		Inc(&DescriptorEpochWrites);
	}

	static void RecordHuntVisitedWrite()
	{
		Inc(&HuntVisitedWrites);
	}

	static void RecordDescriptorRecordFastSkip()
	{
		Inc(&DescriptorRecordFastSkips);
	}

	static void LogZeroWorkState()
	{
		LONG bindingEvents = Take(&BindingEventsRecorded);
		LONG epochWrites = Take(&DescriptorEpochWrites);
		LONG visitedWrites = Take(&HuntVisitedWrites);
		LONG descriptorFastSkips = Take(&DescriptorRecordFastSkips);
		if (!bindingEvents && !epochWrites && !visitedWrites && !descriptorFastSkips)
			return;
		DX12LogManager::Fields("DX12ResidentMaintenance",
			"\"category\":\"zero_work\",\"bindingEventsRecorded\":%ld,\"descriptorEpochWrites\":%ld,\"huntVisitedWrites\":%ld,\"descriptorRecordFastSkips\":%ld",
			bindingEvents, epochWrites, visitedWrites, descriptorFastSkips);
	}

	static void LogAndReset()
	{
		LogRuntimeLookup();
		LogPso();
		LogRootState();
		LogDescriptorState();
		LogIaState();
		LogBarrierState();
		LogPreSkinBindingState();
		LogDispatchDecisionState();
		LogPreSkinPlanState();
		LogDrawDecisionState();
		LogDrawNoopFastPathState();
		LogZeroWorkState();
	}
};

inline void DX12RecordDescriptorRecordFastSkip()
{
	DX12ResidentMaintenanceTelemetryUtils::RecordDescriptorRecordFastSkip();
}



class DX12CommandListStateFlow
{
public:
	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePipelineState(
			commandList, runtimeState, pipelineState);
	}

	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		ID3D12PipelineState *pipelineState)
	{
		// Same-PSO fast path: when the identical PSO is set again, skip all
		// tracking.  Resolve runtimeState if the caller didn't provide one.
		DX12SimulateContext *rs = runtimeState;
		if (!rs) {
			rs = DX12CommandListRuntime::GetStatePtrFast(commandList);
		}
		if (rs && (rs->pipelineState == pipelineState ||
				rs->cachedReplacementPsoKey == pipelineState)) {
			DX12_PROFILE_FAST_FORWARD();
			return rs->cachedReplacementPso
				? rs->cachedReplacementPso : pipelineState;
		}

		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::skipBindings &&
		    DX12HotPathState::NeedsHuntOnlyPipelineObservation(flags)) {
			DX12_PROFILE_FAST_FORWARD();
			if (runtimeState)
				DX12CommandListRuntime::RememberPipelineState(runtimeState, pipelineState);
			else
				DX12CommandListRuntime::RememberPipelineState(commandList, pipelineState);
			DX12HuntObservePipelineState(pipelineState);
			return pipelineState;
		}

		ID3D12PipelineState *activePipelineState =
			DX12CommandListStateCapture::PipelineState(commandList, runtimeState, pipelineState);
		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetPipelineState", commandList,
			" pso=%p active=%p", pipelineState, activePipelineState);
		return activePipelineState;
	}

	static bool CaptureResourceBarrier(
		ID3D12GraphicsCommandList *commandList,
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers)
	{
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureCall(numBarriers);
		if (DX12InternalReplayState::IsActive()) {
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedInternalReplay();
			return false;
		}
		if (DX12HotPathState::skipBindings) {
			DX12_PROFILE_FAST_FORWARD();
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedHotPath();
			return false;
		}
		if (!DX12CommandListStateCapture::ShouldTrackResourceStates()) {
			DX12_PROFILE_FAST_FORWARD();
			DX12ResidentMaintenanceTelemetryUtils::RecordBarrierCaptureSkippedStatePolicy();
			return false;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::ResourceBarrier", commandList,
			" count=%u", numBarriers);
		DX12CommandListCommandRecorder::RecordResourceBarrier(numBarriers, barriers);
		DX12ResidentMaintenanceTelemetryUtils::RecordBarrierRecorded(numBarriers);
		return true;
	}

	static bool CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, runtimeState, topology);
	}

	static bool CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
				" topology=%u", static_cast<UINT>(topology));
			DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
			return true;
		}

		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				return DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
			return DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
		}
		return true;
	}

	static bool CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CaptureIndexBuffer(commandList, runtimeState, view);
	}

	static bool CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
				" gpu=0x%llx size=%u format=%u",
				view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
				view ? view->SizeInBytes : 0,
				view ? static_cast<UINT>(view->Format) : 0);
			DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
			return true;
		}

		DX12_PROFILE_FAST_FORWARD();
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::HasAnyFlag(flags, DX12HotPathState::FLAG_HUNT_ENABLED))
			DX12Profiling::RecordIaHuntIaUpdate();
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				return DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
			return DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
		}
		return true;
	}

	static bool CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		DX12SimulateContext *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, views);
	}

	static bool CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12SimulateContext *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (DX12InternalReplayState::IsActive())
			return true;
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
				" start=%u count=%u", startSlot, count);
			DX12CommandListStateCapture::VertexBuffers(
				commandList, runtimeState, startSlot, count, views);
			return true;
		}

		DX12_PROFILE_FAST_FORWARD();
		const UINT64 flags = DX12HotPathState::HookWorkFlags;
		if (DX12HotPathState::HasAnyFlag(flags, DX12HotPathState::FLAG_HUNT_ENABLED))
			DX12Profiling::RecordIaHuntIaUpdate();
		if (DX12HotPathState::NeedsResidentIaState(flags)) {
			if (runtimeState)
				return DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
			return DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
		}
		return true;
	}
};


namespace Bunny {
class TextureOverrideConfig;
}

bool DX12ModRuntimeLoad(
	const wchar_t *configPath,
	std::wstring *statusMessage = nullptr,
	bool *hasWarnings = nullptr,
	bool *hasErrors = nullptr);
bool DX12ModRuntimeReload();
void DX12ModRefreshRuntimeStateAfterReload();
void DX12ModSetSafeMode(bool safeMode);
uint64_t DX12ModHashShaderBytecode(const void *data, size_t size);
bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage);
bool DX12ModHasShaderOverride(uint64_t hash);
bool DX12ModHasActiveShaderOverrides();
bool DX12ModHasShaderOverridesForPso(ID3D12PipelineState *pipelineState);
bool DX12ModCollectShaderOverrideIaHashTargets(
	ID3D12PipelineState *pipelineState,
	DX12IaHashTargetPlan *targetPlan);
bool DX12ModCompileDrawDecision(
	ID3D12PipelineState *pipelineState,
	DX12CompiledDrawDecision *decision);
bool DX12ModNeedsShaderDescriptorTracking();
UINT64 DX12ModGetReloadGeneration();
UINT64 DX12ModGetReloadGenerationFast();
UINT64 DX12ModGetPreSkinActiveGenerationFast();
bool DX12ModHasActiveTextureOverrides();
bool DX12ModHasConfiguredTextureOverrides();
bool DX12ModHasAnyActiveOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsCommandListSubmissionNotifications();
void DX12ModRunPresentMaintenance();
bool DX12ModHasActivePreSkinTextureOverrides();
bool DX12ModMayNeedPreSkinIaViewExpansion(
	const D3D12_VERTEX_BUFFER_VIEW *views, const bool *hasViews, UINT count);
bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash);
bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash);
bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs);
class DX12PreSkinDispatchOverride;
bool DX12ModHasCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool *matched);
void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	UINT64 bindingDependencyFingerprint,
	bool matched,
	const DX12PreSkinDispatchOverride *dispatchOverride = nullptr);

class DX12ModPreSkinProducerSummary
{
public:
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	bool rootDescriptor = false;
	UINT64 gpuVirtualAddress = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
	UINT64 outputByteWidth = 0;
	UINT outputStride = 0;
};
bool DX12ModFindActivePreSkinProducerForOutput(
	ID3D12Resource *outputResource, UINT64 outputByteWidth, UINT outputStride,
	DX12ModPreSkinProducerSummary *summary);

class DX12ModIaReplacement
{
public:
	DX12ModIaReplacement() = default;
	DX12ModIaReplacement(const DX12ModIaReplacement &other)
		: skip(other.skip),
		  hasIndexBuffer(other.hasIndexBuffer),
		  indexBuffer(other.indexBuffer),
		  vertexBufferStartSlot(other.vertexBufferStartSlot),
		  vertexBuffers(other.vertexBuffers),
		  queuedPostTextureOverrides(other.queuedPostTextureOverrides),
		  postCommandLists(other.postCommandLists),
		  draws(other.draws),
		  dispatches(other.dispatches),
		  drawSrvPatches(other.drawSrvPatches),
		  drawSrvApplyState(other.drawSrvApplyState),
		  retainedDescriptorHeaps(other.retainedDescriptorHeaps)
	{
		for (ID3D12Resource *resource : other.retainedResources)
			RetainResource(resource);
		for (ID3D12DescriptorHeap *heap : retainedDescriptorHeaps) {
			if (heap)
				heap->AddRef();
		}
	}
	DX12ModIaReplacement(DX12ModIaReplacement &&other) noexcept
		: skip(other.skip),
		  hasIndexBuffer(other.hasIndexBuffer),
		  indexBuffer(other.indexBuffer),
		  vertexBufferStartSlot(other.vertexBufferStartSlot),
		  vertexBuffers(std::move(other.vertexBuffers)),
		  queuedPostTextureOverrides(std::move(other.queuedPostTextureOverrides)),
		  postCommandLists(std::move(other.postCommandLists)),
		  draws(std::move(other.draws)),
		  dispatches(std::move(other.dispatches)),
		  drawSrvPatches(std::move(other.drawSrvPatches)),
		  drawSrvApplyState(std::move(other.drawSrvApplyState)),
		  retainedResources(std::move(other.retainedResources)),
		  retainedDescriptorHeaps(std::move(other.retainedDescriptorHeaps))
	{
		other.retainedResources.clear();
	}
	~DX12ModIaReplacement()
	{
		ClearRetainedResources();
	}
	DX12ModIaReplacement &operator=(const DX12ModIaReplacement &other)
	{
		if (this == &other)
			return *this;
		ClearRetainedResources();
		skip = other.skip;
		hasIndexBuffer = other.hasIndexBuffer;
		indexBuffer = other.indexBuffer;
		vertexBufferStartSlot = other.vertexBufferStartSlot;
		vertexBuffers = other.vertexBuffers;
		queuedPostTextureOverrides = other.queuedPostTextureOverrides;
		postCommandLists = other.postCommandLists;
		draws = other.draws;
		dispatches = other.dispatches;
		drawSrvPatches = other.drawSrvPatches;
		drawSrvApplyState = other.drawSrvApplyState;
		retainedDescriptorHeaps = other.retainedDescriptorHeaps;
		for (ID3D12Resource *resource : other.retainedResources)
			RetainResource(resource);
		for (ID3D12DescriptorHeap *heap : retainedDescriptorHeaps) {
			if (heap)
				heap->AddRef();
		}
		return *this;
	}
	DX12ModIaReplacement &operator=(DX12ModIaReplacement &&other) noexcept
	{
		if (this == &other)
			return *this;
		ClearRetainedResources();
		skip = other.skip;
		hasIndexBuffer = other.hasIndexBuffer;
		indexBuffer = other.indexBuffer;
		vertexBufferStartSlot = other.vertexBufferStartSlot;
		vertexBuffers = std::move(other.vertexBuffers);
		queuedPostTextureOverrides = std::move(other.queuedPostTextureOverrides);
		postCommandLists = std::move(other.postCommandLists);
		draws = std::move(other.draws);
		dispatches = std::move(other.dispatches);
		drawSrvPatches = std::move(other.drawSrvPatches);
		drawSrvApplyState = std::move(other.drawSrvApplyState);
		retainedResources = std::move(other.retainedResources);
		retainedDescriptorHeaps = std::move(other.retainedDescriptorHeaps);
		other.retainedResources.clear();
		return *this;
	}
	void RetainResource(ID3D12Resource *resource)
	{
		if (!resource)
			return;
		resource->AddRef();
		retainedResources.push_back(resource);
	}
	void AddPostCommandList(const std::wstring &list)
	{
		if (list.empty())
			return;
		if (std::find(postCommandLists.begin(), postCommandLists.end(), list) !=
		    postCommandLists.end())
			return;
		postCommandLists.push_back(list);
	}
	class QueuedTextureOverridePost
	{
	public:
		const Bunny::TextureOverrideConfig *config = nullptr;
		bool hasThisSrvTarget = false;
		bool compute = false;
		DX12CurrentShaderResourceBinding thisSrvBinding;
		uint32_t matchedHash = 0;
		uint32_t resourceHash = 0;
		uint32_t descriptorHash = 0;
	};
	void AddQueuedPostTextureOverride(const QueuedTextureOverridePost &queued)
	{
		if (!queued.config)
			return;
		for (const QueuedTextureOverridePost &existing : queuedPostTextureOverrides) {
			if (existing.config != queued.config ||
			    existing.hasThisSrvTarget != queued.hasThisSrvTarget ||
			    existing.compute != queued.compute ||
			    existing.matchedHash != queued.matchedHash ||
			    existing.resourceHash != queued.resourceHash ||
			    existing.descriptorHash != queued.descriptorHash) {
				continue;
			}
			if (!existing.hasThisSrvTarget)
				return;
			if (existing.thisSrvBinding.rootParameterIndex ==
			        queued.thisSrvBinding.rootParameterIndex &&
			    existing.thisSrvBinding.shaderRegister ==
			        queued.thisSrvBinding.shaderRegister &&
			    existing.thisSrvBinding.registerSpace ==
			        queued.thisSrvBinding.registerSpace &&
			    existing.thisSrvBinding.descriptorOffset ==
			        queued.thisSrvBinding.descriptorOffset &&
			    existing.thisSrvBinding.tableGpuStart.ptr ==
			        queued.thisSrvBinding.tableGpuStart.ptr) {
				return;
			}
		}
		queuedPostTextureOverrides.push_back(queued);
	}
	void ClearRetainedResources()
	{
		for (ID3D12Resource *resource : retainedResources) {
			if (resource)
				resource->Release();
		}
		retainedResources.clear();
		for (ID3D12DescriptorHeap *heap : retainedDescriptorHeaps) {
			if (heap)
				heap->Release();
		}
		retainedDescriptorHeaps.clear();
	}

	bool skip = false;
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	UINT vertexBufferStartSlot = 0;
	std::vector<D3D12_VERTEX_BUFFER_VIEW> vertexBuffers;
	std::vector<QueuedTextureOverridePost> queuedPostTextureOverrides;
	std::vector<std::wstring> postCommandLists;
	class DrawCall
	{
public:
		bool indexed = false;
		UINT count = 0;
		UINT start = 0;
		INT baseVertex = 0;
		bool fromCaller = false;
		// `drawindexed = auto`: count is recomputed from the replaced index
		// buffer view at execution time instead of using a literal value.
		bool autoCount = false;
	};
	class DispatchCall
	{
	public:
		UINT groupsX = 0;
		UINT groupsY = 0;
		UINT groupsZ = 0;
	};
	std::vector<DrawCall> draws;
	std::vector<DispatchCall> dispatches;
	class DrawSrvTablePatch
	{
	public:
		DX12CurrentShaderResourceBinding binding;
		D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
		DXGI_FORMAT replacementViewFormat = DXGI_FORMAT_UNKNOWN;
		std::wstring resourceName;
		std::string source;
		bool compute = false;
		UINT rootParameterIndex = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE originalTable = {};
		D3D12_CPU_DESCRIPTOR_HANDLE targetCpu = {};
	};
	std::vector<DrawSrvTablePatch> drawSrvPatches;
	class DrawSrvApplyState
	{
	public:
		bool applied = false;
		ID3D12DescriptorHeap *patchHeap = nullptr;
		CommandListBindingState originalBindings;
	};
	DrawSrvApplyState drawSrvApplyState;
	std::vector<ID3D12Resource*> retainedResources;
	std::vector<ID3D12DescriptorHeap*> retainedDescriptorHeaps;
};
bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	bool hasActiveShaderOverrides,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	const DX12DrawMatchUsage &drawMatchUsage,
	DX12ModIaReplacement *replacement,
	bool allowNegativeCache = true,
	ID3D12Device *cachedDevice = nullptr,
	bool skipDirectDescriptorTargets = false,
	std::shared_ptr<const DX12ModIaReplacement> *generatedStaticReplacement = nullptr);
bool DX12ModTryLoadStaticReplacementForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration,
	DX12ModIaReplacement *replacement);
std::shared_ptr<const DX12ModIaReplacement> DX12ModFindStaticReplacementSnapshotForPso(
	ID3D12PipelineState *pipelineState,
	UINT64 reloadGeneration);
bool DX12ModApplyDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement,
	bool executeOriginalDraw);
bool DX12ModHasAnyDirectIaTextureOverrideCandidate(
	const DX12IaHashState &iaState,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);
bool DX12ModHasAnyDirectDescriptorTextureOverrideCandidate(
	ID3D12GraphicsCommandList *commandList,
	const DX12IaHashTargetPlan &targetPlan,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);
bool DX12ModHasAnyPotentialDirectIaTextureOverrideCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed,
	uint32_t vertexCount,
	uint32_t indexCount,
	uint32_t instanceCount,
	uint32_t firstVertex,
	uint32_t firstIndex,
	uint32_t firstInstance);
// DX11-style IA-state-level negative check (draw-arg independent; only valid
// when the compiled prefilter tables are not count-sensitive).
bool DX12ModIaStateDefinitelyHasNoCandidate(
	const DX12ActiveIaState &ia,
	const DX12IaHashTargetPlan &targetPlan,
	bool indexed);
bool DX12ModIaPrefilterCountSensitive();
void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement);
void DX12ModRestoreDrawSrvReplacement(
	ID3D12GraphicsCommandList *commandList,
	DX12ModIaReplacement *replacement);
void DX12ModClearIaReplacementRuntimeCaches();
void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc);
void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc);
ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState);
bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch);
void DX12ModRecordComputeUavs(
	ID3D12GraphicsCommandList *commandList, const std::vector<DX12CurrentComputeUavBinding> &uavs);
class DX12PreSkinDispatchOverride
{
public:
	bool handlingSkip = false;
	bool hasDispatch = false;
	UINT groupsX = 0;
	UINT groupsY = 0;
	UINT groupsZ = 0;
};
class DX12PreSkinProbeCacheEntry
{
public:
	bool applied = false;
	DX12PreSkinDispatchOverride dispatchOverride;
};
bool DX12ModGetCachedPreSkinProbeResult(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 bindingHash,
	UINT64 bindingDependencyFingerprint,
	DX12PreSkinProbeCacheEntry *result);
bool DX12ModApplyPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	UINT *originalVertexCount = nullptr,
	UINT *overrideVertexCount = nullptr,
	DX12PreSkinDispatchOverride *dispatchOverride = nullptr,
	ID3D12Device *cachedDevice = nullptr,
	UINT64 bindingFingerprint = 0,
	UINT64 bindingDependencyFingerprint = 0);
bool DX12ModApplyKnownPreSkinningUavPatches(ID3D12GraphicsCommandList *commandList);
bool DX12ModCanDeferPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList);
bool DX12ModHasActivePreSkinningUavReplacement();
void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList);
void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);
void DX12ModBeginFrame(IDXGISwapChain *swapChain);
bool DX12ModPresentReplacementPendingForCurrentPresent();
bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement);
bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source);
bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source);
bool DX12ModResolveExpandedPreSkinBufferWidth(
	ID3D12Resource *resource, UINT vertexStride, UINT64 *outputByteWidth);

// PreSkin DynamicUAV lifetime contract:
// - the GPU-VA publish map is only a fast, frame-local producer handoff;
// - the resize cache is the durable, fence-tracked owner across Present/FG;
// - every returned binding retains its vertex resource until Clear().
// A consumer command list may be recorded after Present has cleared the publish
// map, so callers must preserve the cache fallback below.
class DX12PreSkinOutputResizeBinding
{
public:
	DX12PreSkinOutputResizeBinding() = default;
	DX12PreSkinOutputResizeBinding(const DX12PreSkinOutputResizeBinding &) = delete;
	DX12PreSkinOutputResizeBinding &operator=(const DX12PreSkinOutputResizeBinding &) = delete;
	~DX12PreSkinOutputResizeBinding()
	{
		Clear();
	}

	void Clear()
	{
		if (retainedResource && resizedResource)
			resizedResource->Release();
		resizedResource = nullptr;
		retainedResource = false;
		originalGpuVa = 0;
		originalByteWidth = 0;
		resizedGpuVa = 0;
		resizedByteWidth = 0;
		sourceOffset = 0;
		stride = 0;
		uavBarrierPending = false;
		transitionToVertex = false;
		stateBefore = D3D12_RESOURCE_STATE_COMMON;
	}

	ID3D12Resource *resizedResource = nullptr;
	bool retainedResource = false;
	UINT64 originalGpuVa = 0;
	UINT64 originalByteWidth = 0;
	UINT64 resizedGpuVa = 0;
	UINT64 resizedByteWidth = 0;
	UINT64 sourceOffset = 0;
	UINT stride = 0;
	bool uavBarrierPending = false;
	bool transitionToVertex = false;
	D3D12_RESOURCE_STATES stateBefore = D3D12_RESOURCE_STATE_COMMON;
};
bool DX12ModResolvePreSkinOutputResizeCacheByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
void DX12ModRegisterPreSkinOutputResize(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride);
// Publish the resize mapping before the skinning dispatch executes instead of
// at restore time. Shadow/draw lists recorded concurrently (or before this
// dispatch on another list) resolve IA views during recording; if the mapping
// is only registered after the dispatch, those draws miss it and render the
// un-skinned original buffer - the shadow-mismatch flicker. Registering early
// is safe: the mapping is a VA alias (contents are written by the dispatch
// before any dependent draw on the same list), and re-registration with equal
// content is a no-op that does not bump the generation.
void DX12ModPublishPreSkinOutputResizeEarly(
	ID3D12GraphicsCommandList *commandList);
bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	ID3D12GraphicsCommandList *commandList,
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
bool DX12ModHasPreSkinOutputResizeBindings();
UINT64 DX12ModGetPreSkinOutputResizeGeneration();
void DX12ModForgetPreSkinOutputResizesForCommandList(
	ID3D12GraphicsCommandList *commandList);
void DX12ModAbandonPreSkinOutputResizeCacheUses(
	ID3D12GraphicsCommandList *commandList);
void DX12ModTrackPreSkinOutputResizeCacheUse(
	ID3D12GraphicsCommandList *commandList,
	ID3D12Resource *vertexResource);
void DX12ModClearPreSkinOutputResizeMap();




class DX12PreSkinPlanUtils
{
public:
	static uint64_t HashCombine(uint64_t hash, uint64_t value)
	{
		hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		return hash;
	}

	static uint64_t HashString(uint64_t hash, const std::string &value)
	{
		for (size_t index = 0; index < value.size(); ++index) {
			hash = HashCombine(hash, static_cast<unsigned char>(value[index]));
		}
		return hash;
	}

	static uint64_t AppendDescriptor(uint64_t hash, const DX12DescriptorSummary &descriptor)
	{
		hash = HashString(hash, descriptor.kind);
		hash = HashCombine(hash, descriptor.hasDesc ? 1 : 0);
		hash = HashCombine(hash, reinterpret_cast<uint64_t>(descriptor.resource));
		hash = HashCombine(hash, reinterpret_cast<uint64_t>(descriptor.counterResource));
		hash = HashCombine(hash, descriptor.gpuVirtualAddress);
		hash = HashCombine(hash, descriptor.resourceOffset);
		hash = HashCombine(hash, descriptor.viewSize);
		hash = HashCombine(hash, descriptor.hasResourceHeapType ? 1 : 0);
		hash = HashCombine(hash, descriptor.hasResourceHeapType ?
			descriptor.resourceHeapType : 0);
		hash = HashCombine(hash, descriptor.viewFormat);
		hash = HashCombine(hash, descriptor.viewDimension);
		hash = HashCombine(hash, descriptor.cbvSize);
		hash = HashCombine(hash, descriptor.firstElement);
		hash = HashCombine(hash, descriptor.numElements);
		hash = HashCombine(hash, descriptor.structureByteStride);
		hash = HashCombine(hash, descriptor.bufferViewOffset);
		hash = HashCombine(hash, descriptor.bufferViewBytes);
		if (descriptor.hasResourceDesc) {
			hash = HashCombine(hash, descriptor.resourceDesc.Dimension);
			hash = HashCombine(hash, descriptor.resourceDesc.Alignment);
			hash = HashCombine(hash, descriptor.resourceDesc.Width);
			hash = HashCombine(hash, descriptor.resourceDesc.Height);
			hash = HashCombine(hash, descriptor.resourceDesc.DepthOrArraySize);
			hash = HashCombine(hash, descriptor.resourceDesc.MipLevels);
			hash = HashCombine(hash, descriptor.resourceDesc.Format);
			hash = HashCombine(hash, descriptor.resourceDesc.Layout);
			hash = HashCombine(hash, descriptor.resourceDesc.Flags);
		}
		return hash;
	}

	static bool BindingLess(
		const DX12CurrentComputeUavBinding &left,
		const DX12CurrentComputeUavBinding &right)
	{
		if (left.rootParameterIndex != right.rootParameterIndex)
			return left.rootParameterIndex < right.rootParameterIndex;
		if (left.registerSpace != right.registerSpace)
			return left.registerSpace < right.registerSpace;
		if (left.shaderRegister != right.shaderRegister)
			return left.shaderRegister < right.shaderRegister;
		if (left.rangeIndex != right.rangeIndex)
			return left.rangeIndex < right.rangeIndex;
		if (left.descriptorOffset != right.descriptorOffset)
			return left.descriptorOffset < right.descriptorOffset;
		if (left.rootDescriptor != right.rootDescriptor)
			return left.rootDescriptor < right.rootDescriptor;
		return left.gpuVirtualAddress < right.gpuVirtualAddress;
	}

	static uint64_t AppendBinding(
		uint64_t hash,
		const DX12CurrentComputeUavBinding &binding,
		uint64_t listSalt)
	{
		hash = HashCombine(hash, listSalt);
		hash = HashCombine(hash, binding.rootParameterIndex);
		hash = HashCombine(hash, binding.rangeIndex);
		hash = HashCombine(hash, binding.shaderVisibility);
		hash = HashCombine(hash, binding.shaderRegister);
		hash = HashCombine(hash, binding.registerSpace);
		hash = HashCombine(hash, binding.descriptorOffset);
		hash = HashCombine(hash, binding.rootDescriptor ? 1 : 0);
		hash = HashCombine(hash, binding.gpuVirtualAddress);
		hash = HashCombine(hash, binding.hasDescriptor ? 1 : 0);
		if (binding.hasDescriptor) {
			hash = AppendDescriptor(hash, binding.descriptor);
		} else {
			hash = HashCombine(hash, binding.rootDescriptor ? 0x726f6f7464657363ull : 0x756e7265736f6c76ull);
		}
		return hash;
	}

	static uint64_t AppendBindingList(
		uint64_t hash,
		const std::vector<DX12CurrentComputeUavBinding> &bindings,
		uint64_t listSalt)
	{
		std::vector<DX12CurrentComputeUavBinding> sortedBindings = bindings;
		std::sort(
			sortedBindings.begin(),
			sortedBindings.end(),
			DX12PreSkinPlanUtils::BindingLess);
		hash = HashCombine(hash, sortedBindings.size());
		for (size_t index = 0; index < sortedBindings.size(); ++index) {
			hash = AppendBinding(hash, sortedBindings[index], listSalt);
		}
		return hash;
	}

	static bool RootConstantsLess(
		const DX12CurrentRootConstants &left,
		const DX12CurrentRootConstants &right)
	{
		return left.rootParameterIndex < right.rootParameterIndex;
	}

	static uint64_t AppendRootConstants(
		uint64_t hash,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		std::vector<DX12CurrentRootConstants> sortedConstants = rootConstants;
		std::sort(
			sortedConstants.begin(),
			sortedConstants.end(),
			DX12PreSkinPlanUtils::RootConstantsLess);
		hash = HashCombine(hash, sortedConstants.size());
		for (size_t rootIndex = 0; rootIndex < sortedConstants.size(); ++rootIndex) {
			const DX12CurrentRootConstants &root = sortedConstants[rootIndex];
			hash = HashCombine(hash, root.rootParameterIndex);
			hash = HashCombine(hash, root.maxSet);
			for (UINT valueIndex = 0; valueIndex < 64; ++valueIndex) {
				if (!root.valueValid[valueIndex])
					continue;
				hash = HashCombine(hash, valueIndex);
				hash = HashCombine(hash, root.values[valueIndex]);
			}
		}
		return hash;
	}

	static uint64_t BuildStableBindingFingerprint(
		const std::vector<DX12CurrentComputeUavBinding> &uavs,
		const std::vector<DX12CurrentComputeUavBinding> &srvs,
		const std::vector<DX12CurrentComputeUavBinding> &cbvs,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		uint64_t hash = 0x7a4d17e6b90f8c35ull;
		hash = AppendBindingList(hash, uavs, 0x5541565full);
		hash = AppendBindingList(hash, srvs, 0x5352565full);
		hash = AppendBindingList(hash, cbvs, 0x4342565full);
		hash = AppendRootConstants(hash, rootConstants);
		if (hash == 0)
			hash = 1;
		return hash;
	}

	// Cross-frame stable fingerprint: uses only fields that survive across frames.
	// Strips GPU VA, resource pointer, and resourceOffset, but keeps the complete
	// descriptor shape. Different SRV/UAV/CBV view shapes can select different
	// PreSkin replacements, so they must not collide in the probe cache key.
	static uint64_t AppendStableDescriptor(uint64_t hash, const DX12DescriptorSummary &descriptor)
	{
		hash = HashString(hash, descriptor.kind);
		hash = HashCombine(hash, descriptor.hasDesc ? 1 : 0);
		hash = HashCombine(hash, descriptor.viewFormat);
		hash = HashCombine(hash, descriptor.viewDimension);
		hash = HashCombine(hash, descriptor.viewSize);
		hash = HashCombine(hash, descriptor.cbvSize);
		hash = HashCombine(hash, descriptor.firstElement);
		hash = HashCombine(hash, descriptor.numElements);
		hash = HashCombine(hash, descriptor.structureByteStride);
		hash = HashCombine(hash, descriptor.bufferViewOffset);
		hash = HashCombine(hash, descriptor.bufferViewBytes);
		if (descriptor.hasResourceDesc) {
			hash = HashCombine(hash, descriptor.resourceDesc.Dimension);
			hash = HashCombine(hash, descriptor.resourceDesc.Width);
			hash = HashCombine(hash, descriptor.resourceDesc.Height);
			hash = HashCombine(hash, descriptor.resourceDesc.DepthOrArraySize);
			hash = HashCombine(hash, descriptor.resourceDesc.MipLevels);
			hash = HashCombine(hash, descriptor.resourceDesc.Format);
			hash = HashCombine(hash, descriptor.resourceDesc.Layout);
			hash = HashCombine(hash, descriptor.resourceDesc.Flags);
		}
		return hash;
	}

	static uint64_t AppendStableBinding(
		uint64_t hash,
		const DX12CurrentComputeUavBinding &binding,
		uint64_t listSalt)
	{
		hash = HashCombine(hash, listSalt);
		hash = HashCombine(hash, binding.rootParameterIndex);
		hash = HashCombine(hash, binding.rangeIndex);
		hash = HashCombine(hash, binding.shaderVisibility);
		hash = HashCombine(hash, binding.shaderRegister);
		hash = HashCombine(hash, binding.registerSpace);
		hash = HashCombine(hash, binding.rootDescriptor ? 1 : 0);
		if (binding.hasDescriptor) {
			hash = AppendStableDescriptor(hash, binding.descriptor);
		}
		return hash;
	}

	static uint64_t AppendStableBindingList(
		uint64_t hash,
		const std::vector<DX12CurrentComputeUavBinding> &bindings,
		uint64_t listSalt)
	{
		static thread_local std::vector<uint64_t> bindingHashes;
		bindingHashes.clear();
		if (bindingHashes.capacity() < bindings.size())
			bindingHashes.reserve(bindings.size());
		for (const DX12CurrentComputeUavBinding &binding : bindings) {
			bindingHashes.push_back(AppendStableBinding(
				0xc3a5c85c97cb3127ull,
				binding,
				listSalt));
		}
		std::sort(bindingHashes.begin(), bindingHashes.end());
		hash = HashCombine(hash, bindingHashes.size());
		for (uint64_t bindingHash : bindingHashes)
			hash = HashCombine(hash, bindingHash);
		return hash;
	}

	static uint64_t BuildCrossFrameStableBindingFingerprint(
		const std::vector<DX12CurrentComputeUavBinding> &uavs,
		const std::vector<DX12CurrentComputeUavBinding> &srvs,
		const std::vector<DX12CurrentComputeUavBinding> &cbvs,
		const std::vector<DX12CurrentRootConstants> &rootConstants)
	{
		uint64_t hash = 0xc5f3e8a17d24906bull;
		hash = AppendStableBindingList(hash, uavs, 0x5541565full);
		hash = AppendStableBindingList(hash, srvs, 0x5352565full);
		hash = AppendStableBindingList(hash, cbvs, 0x4342565full);
		hash = AppendRootConstants(hash, rootConstants);
		if (hash == 0)
			hash = 1;
		return hash;
	}
};




void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
bool DX12ResolveBufferResourceByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
// Fast path: try GPU VA → ResourceWrapper lookup (sorted vector) before
// falling back to the full gBufferResourceByGpuVa std::map search.
bool DX12TryResolveFromGpuVaWrapper(
	UINT64 gpuVirtualAddress, UINT64 size, DX12BufferResourceSummary *summary);
const char *DX12DiagnoseBufferResolveByGpuVa(
	UINT64 gpuVirtualAddress, UINT64 size,
	UINT64 *nearestBegin, UINT64 *nearestEnd, UINT64 *nearestResource);
uint32_t DX12HashBufferResourceView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
uint32_t DX12HashBufferView(
	const DX12BufferResourceSummary *summary, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
uint32_t DX12HashIaBufferContentView(
	UINT64 gpuVirtualAddress, UINT64 size, UINT stride, UINT format, UINT slot);
bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize);
bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize);
bool DX12CopyBufferContentRange(
	ID3D12Resource *dstResource,
	UINT64 dstOffset,
	ID3D12Resource *srcResource,
	UINT64 srcOffset,
	UINT64 numBytes);
bool DX12CopyBufferContent(
	ID3D12Resource *dstResource,
	ID3D12Resource *srcResource);
bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size, const uint8_t **data, size_t *dataSize);
UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *resource);
void DX12InvalidateBufferContentRange(
	ID3D12Resource *resource, UINT64 offset, UINT64 size);
void DX12InvalidateBufferContent(ID3D12Resource *resource);
bool DX12CacheTextureHash(
	ID3D12Resource *resource, uint32_t hash, const D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12TryGetTextureHash(ID3D12Resource *resource, uint32_t *hash);
UINT64 DX12GetTextureHashGeneration();
void DX12InvalidateTextureHash(ID3D12Resource *resource);

// ---------------------------------------------------------------------------
// Async content capture (copy-queue readback pipeline).
//
// DX11 kept CPU-side buffer content by memcpy'ing at Map/Unmap time on the
// recording thread; the DX12 port mirrored that with synchronous cache writes
// and global-epoch bumps on every Unmap/Copy call. This pipeline replaces the
// synchronous writes: recording threads only enqueue a tiny capture event
// (no memcpy, no cache lock, no epoch bump). A dedicated copy queue reads the
// buffer/texture content back into a readback ring after the game's submit,
// and a worker thread publishes the bytes into the existing content caches
// (DX12CacheBufferContentRange / DX12CacheTextureHash) when the fence lands.
// Draw-time consumers therefore see content one frame late, which is exact
// for static resources and self-corrects for streaming ones.
// Implemented in the tracking TU (DX12ResourceTracking.cpp).
bool DX12AsyncReadbackQueueBufferCapture(
	ID3D12Resource *resource, UINT64 offset, UINT64 size);
bool DX12AsyncReadbackQueueTextureCapture(
	ID3D12Resource *texture, const D3D12_RESOURCE_DESC *desc);
// Cheap volatile gate: true when at least one capture event is pending a
// submit. Called on the game's ExecuteCommandLists hot path.
bool DX12AsyncReadbackHasPendingWork();
// Submit all pending capture events on the copy queue, ordered after the
// given (game) queue via Signal/Wait. Called after the original
// ExecuteCommandLists call; pure queue operations on the mod-owned channel.
void DX12AsyncReadbackSubmitPending(ID3D12CommandQueue *gameQueue);
void DX12AsyncReadbackShutdown();

// ---------------------------------------------------------------------------
// Async upload channel (ring of allocators, no per-upload CPU wait).
// The game's queue waits on the upload fence at its next ExecuteCommandLists
// instead of the caller blocking on WaitForSingleObject per upload.
bool DX12UploadChannelHasPendingWork();
void DX12UploadChannelWaitOnQueue(ID3D12CommandQueue *queue);

// Resource state query for the readback pipeline: returns the tracked current
// state of a resource (from creation initialState and recorded transitions).
// knownOut=false when no state is tracked; barrierTrackedOut=true when the
// state was observed from an explicit ResourceBarrier transition.
bool DX12GetResourceCurrentState(
	ID3D12Resource *resource,
	D3D12_RESOURCE_STATES *stateOut,
	bool *knownOut,
	bool *barrierTrackedOut);

// Write-time static texture replacement (plan step 3.1). Implemented in the
// mod TU (DX12ModRuntime.cpp) and consumed by the tracking TU descriptor
// hooks; this TU (resource queries) provides the texture hash cache the
// resolution is keyed on.
struct DescriptorRecord;
// Resolves the write-time replacement SRV descriptor for an original texture
// hash. With sourceRecord the SRV is made view-compatible with the
// descriptor the game originally wrote; otherwise the DDS default SRV is
// used. Returns false when no unconditional static replacement exists.
bool DX12ResolveStaticTextureReplacementSrv(
	ID3D12Device *device,
	uint32_t originalHash,
	const DescriptorRecord *sourceRecord,
	D3D12_CPU_DESCRIPTOR_HANDLE *outSrv,
	ID3D12Resource **outResource,
	UINT64 *outTableGeneration = nullptr);
// True when every descriptor-relevant hashed texture override is an
// unconditional static replacement; gates the draw-path descriptor skip.
bool DX12StaticTextureReplacementAllDescriptorOverridesStatic();
// True when this original texture hash has a static write-time replacement.
bool DX12StaticTextureReplacementContainsHash(uint32_t originalHash);
// Cheap volatile gate so descriptor-copy hot paths can skip all patch work
// when no static replacement table is active.
bool DX12HasStaticTextureReplacements();
UINT64 DX12StaticTextureReplacementGeneration();
// Bind-time static IA replacement (plan step 3.2). Same gating idea for the
// IASet bind hot path.
bool DX12HasStaticIaReplacements();
// Resolves the static geometry replacement for a bound IB/VB content hash.
// Fills the replacement view the same way the draw-time path does; returns
// false when there is no static replacement so the caller binds the
// original view unchanged.
bool DX12ResolveStaticIaReplacement(
	ID3D12Device *device,
	bool indexBuffer,
	UINT vertexSlot,
	uint32_t contentHash,
	const D3D12_INDEX_BUFFER_VIEW *originalIndexView,
	D3D12_INDEX_BUFFER_VIEW *outIndexView,
	const D3D12_VERTEX_BUFFER_VIEW *originalVertexView,
	D3D12_VERTEX_BUFFER_VIEW *outVertexView);
bool DX12TrackTextureHashFromCopyTextureRegion(
	const D3D12_TEXTURE_COPY_LOCATION *dst, UINT dstX, UINT dstY, UINT dstZ,
	const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *srcBox);
UINT64 DX12GetIaHashContentGeneration();
UINT64 DX12GetIaHashResolveGeneration();
class DX12BufferResolveValidationUtils
{
public:
	static bool IsRangeStillValid(
		UINT64 observedEpoch, UINT64 gpuVirtualAddress, UINT64 size);
};
uint32_t DX12HashDescriptorBufferView(
	const DX12DescriptorSummary *descriptor, UINT64 fallbackGpuVirtualAddress,
	UINT64 fallbackSize);
uint32_t DX12ComputeDescriptorHuntHash(
	ID3D12Device *device, const DescriptorRecord &record);
bool DX12GetRootSignatureSummary(
	ID3D12RootSignature *rootSignature, DX12RootSignatureSummary *summary);
bool DX12GetPsoRootSignature(UINT64 psoIndex, ID3D12RootSignature **rootSignature);
void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots);
void DX12GetResourceMetadataSnapshot(
	std::vector<DX12RootSignatureSummary> *rootSignatures,
	std::vector<DX12DescriptorSummary> *descriptors,
	std::vector<DX12PsoRootSummary> *psoRoots,
	std::vector<DX12DescriptorHeapSummary> *descriptorHeaps);
void DX12GetTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
void DX12GetRetainedTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
void DX12ReleaseTrackedResourceSnapshot(std::vector<DX12ResourceSummary> *resources);
bool DX12RetainResource(ID3D12Resource *resource);
void DX12ReleaseRetainedResource(ID3D12Resource *resource);
bool DX12FindDescriptorSummaryByCpuHandle(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
bool DX12FindDescriptorSummaryByCpuHandleLight(
	SIZE_T cpuHandle, DX12DescriptorSummary *summary);
void DX12FindDescriptorSummariesByCpuHandle(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	bool completeResourceInfo = true);
void DX12FindDescriptorSummariesByCpuHandleNoWait(
	const SIZE_T *cpuHandles, size_t count,
	DX12DescriptorSummary *summaries, uint8_t *found,
	uint8_t *pending,
	bool completeResourceInfo = true);
void DX12FindDescriptorSummariesByHeapSlot(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	DX12DescriptorSummary *summaries,
	uint8_t *found,
	bool completeResourceInfo = true);
UINT64 CurrentDescriptorEpoch();
UINT64 CurrentDescriptorQuerySerial();
UINT64 DX12GetDescriptorHeapVersion(ID3D12DescriptorHeap *heap);
bool DX12GetDescriptorSlotVersions(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions);
bool DX12GetDescriptorSlotIdentities(
	ID3D12DescriptorHeap *heap,
	const UINT *slots,
	size_t count,
	UINT64 *versions,
	UINT64 *valueIds);
bool FindDescriptorSummaryByCpuHandleAtEpoch(
	SIZE_T cpuHandle, UINT64 epoch, DX12DescriptorSummary *summary);
bool FindDescriptorSummaryByCpuHandleAtEpochAndKind(
	SIZE_T cpuHandle, UINT64 epoch, const char *kind, DX12DescriptorSummary *summary);
void DX12PrimeDescriptorHistoryForHunting();
bool DX12FindDescriptorHeapByPtr(
	ID3D12DescriptorHeap *heap, DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByGpuHandle(
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
bool DX12FindDescriptorHeapByCpuHandle(
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type,
	DX12DescriptorHeapSummary *summary);
void DX12BeginFrameAnalysisMetadataCache();
void DX12EndFrameAnalysisMetadataCache();



void DX12RecordGraphicsPipelineState(
	ID3D12PipelineState *pipelineState, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc);
void DX12RecordComputePipelineState(
	ID3D12PipelineState *pipelineState, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc);
void DX12RecordPipelineStateStream(
	ID3D12PipelineState *pipelineState, const D3D12_PIPELINE_STATE_STREAM_DESC *desc);
bool DX12GetPipelineStateShaderInfo(ID3D12PipelineState *pipelineState, DX12PsoShaderInfo *info);
bool DX12GetPsoShaderSummary(UINT64 psoIndex, DX12PsoShaderSummary *summary);
bool DX12GetPsoInputLayout(UINT64 psoIndex, DX12PsoInputLayoutInfo *layout);
void DX12DumpFrameAnalysis();
void DX12DumpCachedShaders();
void DX12RequestShaderDump();
bool DX12ShaderDumpIsCaptureRequested();
void DX12ShaderDumpBeginCapture();
bool DX12ShaderDumpEndCapture();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsBusy();
void DX12DumpCapturedFrameShaders();



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

#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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

	class CpuStageBucket
	{
	public:
		char category[48] = {};
		char stage[64] = {};
		UINT kind = 0;
		LONG64 calls = 0;
		LONG64 totalUs = 0;
		LONG64 maxUs = 0;
		LONG64 items = 0;
	};

	class CpuStageLookupEntry
	{
	public:
		const char *category = nullptr;
		const char *stage = nullptr;
		UINT kind = 0;
		LONG index = -1;
		// Combined content hash; 0 marks an empty slot (index < 0 also checked).
		UINT64 hash = 0;
	};

	class CpuStageLookupCache
	{
	public:
		// Power of two so lookups use a mask instead of a modulo.
		CpuStageLookupEntry entries[128] = {};
		LONG count = 0;
	};

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
	inline static volatile LONG64 ApiWallCalls[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallTotalUs[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiWallMaxUs[ApiWallBucketCount] = {};
	inline static volatile LONG64 ApiScopeActiveDepth = 0;
	inline static volatile LONG64 ApiScopeCalls = 0;
	inline static volatile LONG64 ApiScopeTotalUs = 0;
	inline static volatile LONG64 ApiScopeNestedCalls = 0;
	inline static volatile LONG64 ApiScopeFirstBeginQpc = 0;
	inline static volatile LONG64 ApiScopeLastEndQpc = 0;
	inline static volatile LONG64 ApiScopeMaxGapUs = 0;
	inline static volatile LONG64 ApiScopeMaxGapBeginQpc = 0;
	inline static volatile LONG64 ApiScopeMaxGapEndQpc = 0;
	inline static volatile LONG64 ApiScopeLastEndThreadKernelUs = 0;
	inline static volatile LONG64 ApiScopeLastEndThreadUserUs = 0;
	// Set by LogApiScopeBegin when a gap is worth recording; the matching End
	// then samples thread CPU times once. Keeps the GetThreadTimes syscall
	// (hundreds of ns each) off the per-API-call path.
	inline static volatile LONG ApiScopeEndCpuSamplePending = 0;
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

	inline static SRWLOCK CpuStageLock = SRWLOCK_INIT;
	inline static CpuStageBucket CpuStageBuckets[160] = {};
	inline static LONG CpuStageBucketCount = 0;
	inline static thread_local CpuStageLookupCache CpuStageCache = {};

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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
		const ULONG_PTR rawHandle = reinterpret_cast<ULONG_PTR>(handle);
		const ULONG_PTR rawFenceHandle = static_cast<ULONG_PTR>(
			InterlockedCompareExchange64(&LastFenceEventHandle, 0, 0));
		const ULONG_PTR rawFrameLatencyHandle = static_cast<ULONG_PTR>(
			InterlockedCompareExchange64(&LastFrameLatencyHandle, 0, 0));
		const bool fenceMatch = rawHandle != 0 && rawHandle == rawFenceHandle;
		const bool frameLatencyMatch = rawHandle != 0 && rawHandle == rawFrameLatencyHandle;
		if (matchesFenceEvent)
			*matchesFenceEvent = fenceMatch;
		if (matchesFrameLatency)
			*matchesFrameLatency = frameLatencyMatch;
		return fenceMatch || frameLatencyMatch;
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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

	static bool StageStartsWith(const char *stage, const char *prefix)
	{
		if (!stage || !prefix)
			return false;
		const size_t prefixLength = strlen(prefix);
		return strncmp(stage, prefix, prefixLength) == 0;
	}

	static bool LabelEquals(const char *left, const char *right)
	{
		if (!left)
			left = "";
		if (!right)
			right = "";
		return strcmp(left, right) == 0;
	}

	static void CopyLabel(char *target, size_t targetSize, const char *source)
	{
		if (!target || targetSize == 0)
			return;
		target[0] = '\0';
		if (!source)
			source = "";
		strncpy_s(target, targetSize, source, _TRUNCATE);
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

	static void MaxQpc(volatile LONG64 *target, LONGLONG value)
	{
		LONG64 oldValue = InterlockedCompareExchange64(target, 0, 0);
		while (value > oldValue) {
			const LONG64 previous =
				InterlockedCompareExchange64(target, static_cast<LONG64>(value), oldValue);
			if (previous == oldValue)
				break;
			oldValue = previous;
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

		local.outerBeginQpc = beginQpc;
		InterlockedIncrement64(&slot->outerCalls);
		InterlockedCompareExchange64(
			&slot->firstBeginQpc, static_cast<LONG64>(beginQpc), 0);
		const LONG64 previousEndQpc = InterlockedCompareExchange64(
			&slot->lastEndQpc, 0, 0);
		if (previousEndQpc > 0 && beginQpc > previousEndQpc)
			MaxUs(&slot->maxGapUs, DeltaUs(previousEndQpc, beginQpc));
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
		AddUs(&slot->outerTotalUs, elapsedUs);
		LONG64 observed = InterlockedCompareExchange64(&slot->maxScopeUs, 0, 0);
		while (elapsedUs > observed) {
			const LONG64 previous = InterlockedCompareExchange64(
				&slot->maxScopeUs, elapsedUs, observed);
			if (previous == observed) {
				InterlockedExchange(&slot->maxScopeBucket, bucket);
				break;
			}
			observed = previous;
		}
		MaxQpc(&slot->lastEndQpc, endQpc);
	}

	static void LogApiScopeBegin(int bucket, LONGLONG beginQpc)
	{
		RecordApiThreadScopeBegin(beginQpc);
		const LONG64 activeDepth = InterlockedIncrement64(&ApiScopeActiveDepth);
		if (activeDepth > 1) {
			InterlockedIncrement64(&ApiScopeNestedCalls);
			return;
		}
		InterlockedIncrement64(&ApiScopeCalls);
		const LONG64 firstBegin = InterlockedCompareExchange64(
			&ApiScopeFirstBeginQpc,
			static_cast<LONG64>(beginQpc),
			0);
		if (firstBegin == 0)
			InterlockedExchange(&ApiScopeFirstBucket, bucket);
		const LONG64 previousEndQpc =
			InterlockedCompareExchange64(&ApiScopeLastEndQpc, 0, 0);
		if (previousEndQpc > 0 && beginQpc > previousEndQpc) {
			const LONG64 gapUs = DeltaUs(previousEndQpc, beginQpc);
			const LONG beforeBucket = InterlockedCompareExchange(&ApiScopeLastBucket, 0, 0);
			const LONG beforeThreadId = InterlockedCompareExchange(&ApiScopeLastThreadId, 0, 0);
			const LONG64 beforeKernelUs =
				InterlockedCompareExchange64(&ApiScopeLastEndThreadKernelUs, 0, 0);
			const LONG64 beforeUserUs =
				InterlockedCompareExchange64(&ApiScopeLastEndThreadUserUs, 0, 0);
			LONG64 afterKernelUs = 0;
			LONG64 afterUserUs = 0;
			// GetThreadTimes is a syscall (hundreds of ns each); sampling it on
			// every API call dominates the TIMING build cost while the 50ms
			// IsInterestingGap threshold almost never fires. Only sample when
			// this gap can actually be logged or beat the recorded maximum.
			const LONG64 observedMaxGap =
				InterlockedCompareExchange64(&ApiScopeMaxGapUs, 0, 0);
			if (IsInterestingGap(gapUs) || gapUs > observedMaxGap) {
				GetCurrentThreadCpuUs(&afterKernelUs, &afterUserUs);
				InterlockedExchange(&ApiScopeEndCpuSamplePending, 1);
			}
			const LONG64 threadKernelDeltaUs =
				static_cast<LONG>(GetCurrentThreadId()) == beforeThreadId ?
					SafeSubtract(afterKernelUs, beforeKernelUs) : 0;
			const LONG64 threadUserDeltaUs =
				static_cast<LONG>(GetCurrentThreadId()) == beforeThreadId ?
					SafeSubtract(afterUserUs, beforeUserUs) : 0;
			const LONG64 threadCpuDeltaUs = threadKernelDeltaUs + threadUserDeltaUs;
			LONG64 observed = InterlockedCompareExchange64(&ApiScopeMaxGapUs, 0, 0);
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
					InterlockedExchange(&ApiScopeMaxGapAfterThreadId,
						static_cast<LONG>(GetCurrentThreadId()));
					break;
				}
				observed = previous;
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
					static_cast<LONG>(GetCurrentThreadId()),
					static_cast<LONG>(static_cast<LONG>(GetCurrentThreadId()) == beforeThreadId),
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
		const LONG64 activeDepth = InterlockedCompareExchange64(&ApiScopeActiveDepth, 0, 0);
		if (activeDepth <= 1) {
			AddUs(&ApiScopeTotalUs, elapsedUs);
			LONG64 observed = InterlockedCompareExchange64(&ApiScopeLastEndQpc, 0, 0);
			while (endQpc > observed) {
				const LONG64 previous = InterlockedCompareExchange64(
					&ApiScopeLastEndQpc, endQpc, observed);
				if (previous == observed) {
					InterlockedExchange(&ApiScopeLastBucket, bucket);
					InterlockedExchange(&ApiScopeLastThreadId,
						static_cast<LONG>(GetCurrentThreadId()));
					LONG64 kernelUs = 0;
					LONG64 userUs = 0;
					// Only sample when the matching Begin flagged a recordable
					// gap; see ApiScopeEndCpuSamplePending in LogApiScopeBegin.
					if (InterlockedExchange(&ApiScopeEndCpuSamplePending, 0)) {
						GetCurrentThreadCpuUs(&kernelUs, &userUs);
						InterlockedExchange64(&ApiScopeLastEndThreadKernelUs, kernelUs);
						InterlockedExchange64(&ApiScopeLastEndThreadUserUs, userUs);
					}
					break;
				}
				observed = previous;
			}
		}
		LogApiWallSlice(bucket, elapsedUs);
		InterlockedDecrement64(&ApiScopeActiveDepth);
	}

	static void LogApiWallSlice(int bucket, LONG64 us)
	{
		if (bucket < 0 || bucket >= ApiWallBucketCount)
			bucket = ApiOtherCommandList;
		const LONG64 safeUs = ClampUs(us);
		if (safeUs <= 0)
			return;
		InterlockedIncrement64(&ApiWallCalls[bucket]);
		InterlockedAdd64(&ApiWallTotalUs[bucket], safeUs);
		MaxUs(&ApiWallMaxUs[bucket], safeUs);
	}

	static void LogApiWallTimingState(LONG present, LONG64 frameWallUs)
	{
		for (int bucket = 0; bucket < ApiWallBucketCount; ++bucket) {
			const LONG64 calls = TakeLong64(&ApiWallCalls[bucket]);
			const LONG64 totalUs = TakeLong64(&ApiWallTotalUs[bucket]);
			const LONG64 maxUs = TakeLong64(&ApiWallMaxUs[bucket]);
			if (!calls && !totalUs && !maxUs)
				continue;
			DX12LogManager::Fields("DX12ApiWallTiming",
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
		LONG64 frameWallUs)
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
			DX12LogManager::Fields("DX12CpuThreadTimeline",
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
		LONG64 presentWallUs)
	{
		const LONG64 calls = TakeLong64(&ApiScopeCalls);
		const LONG64 totalUs = TakeLong64(&ApiScopeTotalUs);
		const LONG64 nestedCalls = TakeLong64(&ApiScopeNestedCalls);
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

		DX12LogManager::Fields("DX12CpuTimelineCoverage",
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

	static LONG FindCpuStageBucketLocked(const char *category, const char *stage, UINT kind)
	{
		for (LONG index = 0; index < CpuStageBucketCount; ++index) {
			if (CpuStageBuckets[index].kind == kind &&
					LabelEquals(CpuStageBuckets[index].category, category) &&
					LabelEquals(CpuStageBuckets[index].stage, stage)) {
				return index;
			}
		}
		if (CpuStageBucketCount >= static_cast<LONG>(sizeof(CpuStageBuckets) / sizeof(CpuStageBuckets[0])))
			return -1;
		const LONG newIndex = CpuStageBucketCount;
		++CpuStageBucketCount;
		CopyLabel(CpuStageBuckets[newIndex].category, sizeof(CpuStageBuckets[newIndex].category), category);
		CopyLabel(CpuStageBuckets[newIndex].stage, sizeof(CpuStageBuckets[newIndex].stage), stage);
		CpuStageBuckets[newIndex].kind = kind;
		return newIndex;
	}

	static LONG FindCpuStageBucketCached(const char *category, const char *stage, UINT kind)
	{
		// Thread-local open-addressed hash table keyed on label content.
		// The previous linear scan paid up to ~34 strcmp pairs per call;
		// on the draw hot path LogCpuStage runs 2-3 times per draw, which
		// made the measurement itself dominate the numbers it was reporting.
		// Pointer equality short-circuits the common case (the same literal
		// is passed from the same call site every time); the content hash +
		// strcmp fallback only runs on collision or distinct-literal aliases.
		const UINT64 hash = HashCpuStageLabel(category) ^
			(HashCpuStageLabel(stage) * 0x9e3779b97f4a7c15ull) ^
			(static_cast<UINT64>(kind) * 0xbf58476d1ce4e5b9ull);
		const UINT slotMask = static_cast<UINT>(
			ARRAYSIZE(CpuStageCache.entries)) - 1;
		UINT slot = static_cast<UINT>(hash) & slotMask;
		for (UINT probe = 0; probe < ARRAYSIZE(CpuStageCache.entries); ++probe) {
			const CpuStageLookupEntry &entry = CpuStageCache.entries[slot];
			if (entry.hash == 0 && entry.index < 0)
				break; // empty slot: not cached
			if (entry.hash == hash && entry.kind == kind) {
				if (entry.category == category && entry.stage == stage)
					return entry.index;
				if (LabelEquals(entry.category, category) &&
						LabelEquals(entry.stage, stage))
					return entry.index;
			}
			slot = (slot + 1) & slotMask;
		}

		AcquireSRWLockExclusive(&CpuStageLock);
		const LONG bucketIndex = FindCpuStageBucketLocked(category, stage, kind);
		ReleaseSRWLockExclusive(&CpuStageLock);
		if (bucketIndex >= 0) {
			UINT insertSlot = static_cast<UINT>(hash) & slotMask;
			for (UINT probe = 0; probe < ARRAYSIZE(CpuStageCache.entries); ++probe) {
				CpuStageLookupEntry &entry = CpuStageCache.entries[insertSlot];
				if (entry.hash == 0 && entry.index < 0) {
					entry.hash = hash;
					entry.category = category;
					entry.stage = stage;
					entry.kind = kind;
					entry.index = bucketIndex;
					break;
				}
				insertSlot = (insertSlot + 1) & slotMask;
			}
			// A full table simply stops caching; lookups still resolve
			// correctly through the locked path above.
		}
		return bucketIndex;
	}

	static void UpdateCpuStageMax(LONG64 *maximum, LONG64 value)
	{
		LONG64 observed = InterlockedCompareExchange64(maximum, 0, 0);
		while (value > observed) {
			const LONG64 previous = InterlockedCompareExchange64(maximum, value, observed);
			if (previous == observed)
				break;
			observed = previous;
		}
	}

	static void LogCpuStage(
		const char *category,
		const char *stage,
		UINT kind,
		size_t itemCount,
		LONG64 us)
	{
#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
		const LONG64 safeUs = ClampUs(us);
		const LONG index = FindCpuStageBucketCached(category, stage, kind);
		if (index >= 0) {
			CpuStageBucket &bucket = CpuStageBuckets[index];
			InterlockedIncrement64(&bucket.calls);
			InterlockedExchangeAdd64(&bucket.totalUs, safeUs);
			UpdateCpuStageMax(&bucket.maxUs, safeUs);
			InterlockedExchangeAdd64(&bucket.items, static_cast<LONG64>(itemCount));
		}
#else
		(void)category;
		(void)stage;
		(void)kind;
		(void)itemCount;
		(void)us;
#endif
	}

	static void LogCpuStageTimingState(LONG present, LONG64 frameWallUs)
	{
		CpuStageBucket snapshot[160] = {};
		LONG snapshotCount = 0;
		AcquireSRWLockExclusive(&CpuStageLock);
		snapshotCount = CpuStageBucketCount;
		for (LONG index = 0; index < snapshotCount; ++index) {
			CopyLabel(snapshot[index].category, sizeof(snapshot[index].category), CpuStageBuckets[index].category);
			CopyLabel(snapshot[index].stage, sizeof(snapshot[index].stage), CpuStageBuckets[index].stage);
			snapshot[index].kind = CpuStageBuckets[index].kind;
			snapshot[index].calls = InterlockedExchange64(&CpuStageBuckets[index].calls, 0);
			snapshot[index].totalUs = InterlockedExchange64(&CpuStageBuckets[index].totalUs, 0);
			snapshot[index].maxUs = InterlockedExchange64(&CpuStageBuckets[index].maxUs, 0);
			snapshot[index].items = InterlockedExchange64(&CpuStageBuckets[index].items, 0);
		}
		ReleaseSRWLockExclusive(&CpuStageLock);

		for (LONG index = 0; index < snapshotCount; ++index) {
			if (!snapshot[index].calls && !snapshot[index].totalUs)
				continue;
			if (snapshot[index].totalUs < 40 && snapshot[index].maxUs < 10)
				continue;
			DX12LogManager::Fields("DX12CpuStageTiming",
				"\"present\":%ld,\"category\":\"%s\",\"stage\":\"%s\",\"kind\":%u,"
				"\"calls\":%lld,\"items\":%lld,\"totalUs\":%lld,\"maxUs\":%lld,"
				"\"frameWallUs\":%lld,\"framePctX100\":%lld",
				present,
				snapshot[index].category,
				snapshot[index].stage,
				snapshot[index].kind,
				static_cast<long long>(snapshot[index].calls),
				static_cast<long long>(snapshot[index].items),
				static_cast<long long>(snapshot[index].totalUs),
				static_cast<long long>(snapshot[index].maxUs),
				static_cast<long long>(frameWallUs),
				static_cast<long long>(
					frameWallUs > 0 ? (snapshot[index].totalUs * 10000 / frameWallUs) : 0));
		}
	}

	static void LogLogCostState(LONG present, LONG64 frameWallUs)
	{
		DX12LogManager::DX12LogCostSnapshot snapshot = {};
		DX12LogManager::TakeLogCostSnapshot(&snapshot);
		if (!snapshot.fieldCalls && !snapshot.textCalls && !snapshot.debugFieldCalls &&
				!snapshot.enqueueCalls && !snapshot.formatUs && !snapshot.enqueueUs &&
				!snapshot.droppedLines) {
			return;
		}
		const LONG64 totalUs = snapshot.formatUs + snapshot.enqueueUs;
		if (totalUs < 40 && !snapshot.droppedLines)
			return;
		DX12LogManager::Fields("DX12LogCost",
			"\"present\":%ld,\"fieldCalls\":%lld,\"textCalls\":%lld,"
			"\"debugFieldCalls\":%lld,\"enqueueCalls\":%lld,\"formatUs\":%lld,"
			"\"enqueueUs\":%lld,\"totalUs\":%lld,\"droppedLines\":%lld,"
			"\"frameWallUs\":%lld,\"framePctX100\":%lld",
			present,
			static_cast<long long>(snapshot.fieldCalls),
			static_cast<long long>(snapshot.textCalls),
			static_cast<long long>(snapshot.debugFieldCalls),
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
	#if defined(DX12_ENABLE_DETAILED_TIMING) && (defined(_DEBUG) || defined(DX12_RELEASE_TIMING))
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
			presentWallUs);
		LogApiThreadTimelineState(
			present,
			frameStartQpc,
			frameEndQpc,
			frameWallUs);
		LogApiWallTimingState(present, frameWallUs);
		LogCpuStageTimingState(present, frameWallUs);

		DX12LogManager::Fields("DX12FrameTimeline",
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
		LogLogCostState(present, frameWallUs);
	}
};



