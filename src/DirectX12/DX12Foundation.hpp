#pragma once

// Consolidated DirectX 12 interface layer.
// Sections retain their original file names in #line diagnostics.

// ---- DX12PsoTypes.hpp ----
#line 1 "DX12PsoTypes.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <string>
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

// ---- DX12ResourceTypes.hpp ----
#line 1 "DX12ResourceTypes.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

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

class DX12BufferResourceSummary
{
public:
	ID3D12Resource *resource = nullptr;
	// Monotonic Resource Identity generation. Unlike a COM pointer, this token
	// changes when an address is reused or the tracked GPUVA range changes.
	UINT64 resourceGeneration = 0;
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

// ---- DX12BindingTrackerTypes.hpp ----
#line 1 "DX12BindingTrackerTypes.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <stdint.h>

#include <string>
#include <vector>


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

// ---- DX12BindingTrackerState.hpp ----
#line 1 "DX12BindingTrackerState.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>



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

// ---- DX12FrameAnalysisTypes.hpp ----
#line 1 "DX12FrameAnalysisTypes.hpp"
#include <stddef.h>

class DX12FrameAnalysisDumpResult
{
public:
	bool ok = false;
	size_t written = 0;
	size_t skipped = 0;
	size_t failed = 0;
	const char *reason = "";
};

// ---- DX12IaRegion.hpp ----
#line 1 "DX12IaRegion.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

uint32_t DX12HashIaBufferView(
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
		hash = DX12HashIaBufferView(gpuVa, size, 0, format, 0);
		return hash;
	}

	uint32_t HashVertexBuffer(UINT stride, UINT slot)
	{
		hash = DX12HashIaBufferView(gpuVa, size, stride, 0, slot);
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

// ---- DX12BindingTracker.hpp ----
#line 1 "DX12BindingTracker.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <stdio.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


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
	static void SetPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void RecordStateEvent(ID3D12GraphicsCommandList *commandList, const char *kind);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static bool SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void SetComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void SetGraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void SetComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
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
	static bool GetCurrentComputeRootConstants(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentRootConstants> *constants);
	static bool GetCurrentComputeAll(ID3D12GraphicsCommandList *commandList, std::vector<DX12CurrentComputeUavBinding> *uavs, std::vector<DX12CurrentComputeUavBinding> *srvs, std::vector<DX12CurrentComputeUavBinding> *cbvs, std::vector<DX12CurrentRootConstants> *constants);
	static bool CollectPreSkinBindings(
		ID3D12GraphicsCommandList *commandList,
		std::vector<DX12CurrentComputeUavBinding> *uavs,
		std::vector<DX12CurrentComputeUavBinding> *srvs,
		std::vector<DX12CurrentComputeUavBinding> *cbvs,
		std::vector<DX12CurrentRootConstants> *constants,
		UINT64 *dependencyFingerprint = nullptr);
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

// ---- DX12LogUtils.hpp ----
#line 1 "DX12LogUtils.hpp"
#include <Windows.h>

#include <stdarg.h>
#include <stdio.h>

// ── DX11-aligned global log utilities ──────────────────────────────
//
//   DX12LogInfo  / DX12LogDebug   — direct fprintf, zero overhead, no timestamp
//   DX12LogInfoTS / DX12LogDebugTS — direct fprintf + GetTickCount64() prefix
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

// ── No-timestamp macros — pure fprintf, DX11 speed ──────────────────
// Prefixed DX12 to avoid collision with DX12FrameAnalysis::LogInfo etc.

#if defined(_DEBUG)
#define DX12LogDebug(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)
#else
#define DX12LogDebug(fmt, ...) do {} while (0)
#endif

#define DX12LogInfo(fmt, ...) \
	do { if (DX12LogUtils::LogFile) fprintf(DX12LogUtils::LogFile, fmt, __VA_ARGS__); } while (0)

// ── Timestamped macros — GetTickCount64() prefix + direct fprintf ────
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

// ---- DX12RuntimeState.hpp ----
#line 1 "DX12RuntimeState.hpp"
#include <Windows.h>

enum class DX12RuntimeMode : LONG
{
	Mod = 0,
	Hunting = 1,
};

class DX12RuntimeState
{
public:
	inline static HINSTANCE module = nullptr;
	inline static volatile LONG presentCount = 0;
	inline static volatile LONG runtimeMode = static_cast<LONG>(DX12RuntimeMode::Mod);
	inline static volatile LONG perFrameTotalDraws = 0;
	inline static volatile LONG perFrameTotalDispatches = 0;

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
		return InterlockedIncrement(&presentCount);
	}

	static LONG GetPresentCount()
	{
		return InterlockedCompareExchange(&presentCount, 0, 0);
	}

	static DX12RuntimeMode GetRuntimeMode()
	{
		const LONG mode = InterlockedCompareExchange(&runtimeMode, 0, 0);
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

	static void IncrementFrameDraws()
	{
		InterlockedIncrement(&perFrameTotalDraws);
	}

	static void IncrementFrameDispatches()
	{
		InterlockedIncrement(&perFrameTotalDispatches);
	}

	static LONG TakeFrameDraws()
	{
		return InterlockedExchange(&perFrameTotalDraws, 0);
	}

	static LONG TakeFrameDispatches()
	{
		return InterlockedExchange(&perFrameTotalDispatches, 0);
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

// ---- DX12LogManager.hpp ----
#line 1 "DX12LogManager.hpp"
#include <Windows.h>
#include <Shlwapi.h>

#include <atomic>
#include <deque>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>


class DX12LogManager
{
public:
	class DX12LogCostSnapshot
	{
	public:
		LONG64 jsonCalls = 0;
		LONG64 textCalls = 0;
		LONG64 debugJsonCalls = 0;
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
	inline static volatile LONG64 LogCostJsonCalls = 0;
	inline static volatile LONG64 LogCostTextCalls = 0;
	inline static volatile LONG64 LogCostDebugJsonCalls = 0;
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
		snapshot->jsonCalls = TakeLong64(&LogCostJsonCalls);
		snapshot->textCalls = TakeLong64(&LogCostTextCalls);
		snapshot->debugJsonCalls = TakeLong64(&LogCostDebugJsonCalls);
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
						"%s\tDX12LogQueue\tcount=%ld\n", timePlain, dropped);
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
		PathAppendW(path, L"d3d12_log.jsonl");
		state.log = _wfsopen(path, L"w", _SH_DENYNO);
		DX12LogUtils::Open(path); // DX11-style direct fprintf file
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

	static bool DiagnosticsLoggingEnabled()
	{
#if defined(_DEBUG)
		return true;
#else
		return false;
#endif
	}

	static bool HookCallLoggingEnabled()
	{
#if defined(_DEBUG)
		static LONG sEnabled = -1;
		LONG enabled = InterlockedCompareExchange(&sEnabled, 0, -1);
		if (enabled == -1) {
			char value[16] = {};
			const DWORD len = GetEnvironmentVariableA(
				"ZZMIDX12_VERBOSE_DIAGNOSTICS", value, ARRAYSIZE(value));
			const bool requested = len > 0 &&
				(!_stricmp(value, "1") || !_stricmp(value, "true") || !_stricmp(value, "yes"));
			enabled = requested ? 1 : 0;
			InterlockedExchange(&sEnabled, enabled);
		}
		return enabled != 0;
#else
		return false;
#endif
	}

	static bool VerboseDiagnosticsEnabled()
	{
		return HookCallLoggingEnabled();
	}

	static bool ShouldLogHookCall(const char *api)
	{
#if defined(_DEBUG)
		if (!api || !api[0] || DX12InternalReplayState::IsActive() || !HookCallLoggingEnabled())
			return false;
		return true;
#else
		(void)api;
		return false;
#endif
	}

	static bool ReleaseShouldLogFunction(const char *func)
	{
#if !defined(_DEBUG)
		if (Get().releaseStartupLogging.load(std::memory_order_relaxed))
			return true;
		if (!func)
			return false;
		if (!strcmp(func, "Log"))
			return true;
		if (!strcmp(func, "HookInstall"))
			return true;
		if (!strcmp(func, "DeviceHooksReady"))
			return true;
		if (!strcmp(func, "D3D12CreateDevice"))
			return true;
		if (!strncmp(func, "CreateDXGIFactory", 17))
			return true;
		if (!strncmp(func, "IDXGIFactory", 12))
			return true;
		if (!strncmp(func, "IDXGISwapChain", 14))
			return true;
		if (!strncmp(func, "ID3D12CommandQueue", 18))
			return true;
		if (!strcmp(func, "DX12CommandQueueState"))
			return true;
		if (!strcmp(func, "DX12ExecuteCommandListsFirst"))
			return true;
		if (!strcmp(func, "DX12PresentFirst"))
			return true;
		if (!strcmp(func, "DX12ResidentMaintenance"))
			return true;
		if (!strcmp(func, "DX12HotPathHookGate"))
			return true;
		if (!strcmp(func, "DX12HotPathState"))
			return true;
		if (!strcmp(func, "InputHotkey"))
			return true;
		if (!strcmp(func, "DX12InputThread"))
			return true;
		if (!strcmp(func, "DX12HuntSelect"))
			return true;
		if (!strcmp(func, "DX12HuntIaStats"))
			return true;
		if (!strcmp(func, "DX12HuntToggle"))
			return true;
		if (!strncmp(func, "OverlayWindow", 13))
			return true;
		return false;
#else
		(void)func;
		return true;
#endif
	}

	static void EnqueueJsonLogLine(const char *line)
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

	// ── TSV log format (DX11-aligned, no JSON overhead) ──────────────
	// Format: HH:MM:SS.mmm<TAB>FuncName<TAB>k1=v1<TAB>k2=v2<NEWLINE>
	// No JSON escaping — plain fprintf to async queue. Direct sprintf for
	// max speed, no intermediate field quoting or brace wrapping.

	static void WriteLogLine(
		const char *func, const char *extra, bool flush)
	{
		if (!Get().log)
			return;

		const LARGE_INTEGER formatBegin = LogNow();
		char timePlain[32] = {};
		DX12LogUtils::MakeLogTimePlain(timePlain, sizeof(timePlain));
		char line[8192];
		if (extra && extra[0]) {
			snprintf(line, sizeof(line), "%s\t%s\t%s\n", timePlain, func, extra);
		} else {
			snprintf(line, sizeof(line), "%s\t%s\n", timePlain, func);
		}
		AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		EnqueueJsonLogLine(line);
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

		WriteLogLine("Log", message, false);
	}

	static void JsonFuncV(const char *func, const char *fmt, va_list args, bool flush, bool debugCall)
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
			InterlockedIncrement64(&LogCostDebugJsonCalls);
		else
			InterlockedIncrement64(&LogCostJsonCalls);

		if (fmt && fmt[0]) {
			const LARGE_INTEGER formatBegin = LogNow();
			vsnprintf(extra, sizeof(extra), fmt, args);
			AddLogCost(&LogCostFormatUs, LogDeltaUs(formatBegin, LogNow()));
		}

		WriteLogLine(safeFunc, extra, flush);
	}

	static void JsonFunc(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, false, false);
		va_end(args);
	}

	static void JsonFuncFlush(const char *func, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, true, false);
		va_end(args);
	}

#if defined(_DEBUG)
	static void DebugJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, false, true);
		va_end(args);
	}

	static void DebugJsonFuncFlush(const char *func, const char *fmt, ...)
	{
		if (!Get().log || !DiagnosticsLoggingEnabled())
			return;
		va_list args;
		va_start(args, fmt);
		JsonFuncV(func, fmt, args, true, true);
		va_end(args);
	}
#else
	static void DebugJsonFunc(const char *func, const char *fmt, ...)
	{
		(void)func;
		(void)fmt;
	}

	static void DebugJsonFuncFlush(const char *func, const char *fmt, ...)
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

// ---- DX12JsonUtils.hpp ----
#line 1 "DX12JsonUtils.hpp"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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

// ---- DX12CommandListFlowDiagnostics.hpp ----
#line 1 "DX12CommandListFlowDiagnostics.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


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
		DX12LogManager::DebugJsonFunc("DX12HookCall",
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
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && api)
			return;
		DX12LogManager::DebugJsonFunc("DX12QueueStage",
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

// ---- DX12ActiveIaState.hpp ----
#line 1 "DX12ActiveIaState.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <cstdint>


class DX12ActiveIaState
{
public:
	bool hasIndexBuffer = false;
	D3D12_INDEX_BUFFER_VIEW indexBuffer = {};
	bool indexBufferResolved = false;
	DX12BufferResourceSummary indexBufferResource = {};
	bool hasVertexBuffer[32] = {};
	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[32] = {};
	bool vertexBufferResolved[32] = {};
	DX12BufferResourceSummary vertexBufferResources[32] = {};
	bool preSkinRedirectNegativeValid[32] = {};
	UINT64 preSkinRedirectNegativeGeneration[32] = {};
	D3D12_VERTEX_BUFFER_VIEW preSkinRedirectNegativeSource[32] = {};
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

// ---- DX12CompiledDispatchDecision.hpp ----
#line 1 "DX12CompiledDispatchDecision.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

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

// ---- DX12IaHashTargetPlan.hpp ----
#line 1 "DX12IaHashTargetPlan.hpp"
#include <Windows.h>

#include <stdint.h>

class DX12IaHashTargetPlan
{
public:
	bool requireAllIaBuffers = true;
	bool requireIndexBuffer = false;
	bool requireIndexBufferView = false;
	bool requireVertexBuffers[32] = {};
	bool hasDescriptorTargets = false;
	bool hasTextureOverrideChecks = false;

	void ResetKnown()
	{
		requireAllIaBuffers = false;
		requireIndexBuffer = false;
		requireIndexBufferView = false;
		hasDescriptorTargets = false;
		hasTextureOverrideChecks = false;
		for (UINT slot = 0; slot < 32; ++slot)
			requireVertexBuffers[slot] = false;
	}

	void RequireAll()
	{
		requireAllIaBuffers = true;
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		for (UINT slot = 0; slot < 32; ++slot)
			requireVertexBuffers[slot] = true;
	}

	void RequireIndexBuffer()
	{
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		hasTextureOverrideChecks = true;
	}

	void RequireIndexBufferView()
	{
		requireIndexBufferView = true;
	}

	void RequireVertexBuffer(UINT slot)
	{
		hasTextureOverrideChecks = true;
		if (slot >= 32) {
			RequireAll();
			return;
		}
		requireVertexBuffers[slot] = true;
	}

	void RequireDescriptorTarget()
	{
		hasDescriptorTargets = true;
		hasTextureOverrideChecks = true;
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
		return requireAllIaBuffers || (slot < 32 && requireVertexBuffers[slot]);
	}

	bool HasIaTargets() const
	{
		if (requireAllIaBuffers || requireIndexBuffer || requireIndexBufferView)
			return true;
		for (UINT slot = 0; slot < 32; ++slot) {
			if (requireVertexBuffers[slot])
				return true;
		}
		return false;
	}

	bool HasSameIaHashTargets(const DX12IaHashTargetPlan &other) const
	{
		if (requireAllIaBuffers != other.requireAllIaBuffers ||
			requireIndexBuffer != other.requireIndexBuffer ||
			requireIndexBufferView != other.requireIndexBufferView)
			return false;
		for (UINT slot = 0; slot < 32; ++slot) {
			if (requireVertexBuffers[slot] != other.requireVertexBuffers[slot])
				return false;
		}
		return true;
	}

	UINT64 Fingerprint() const
	{
		UINT64 key = requireAllIaBuffers ? 0xf17a11b7a11b7a11ull : 0x27a7b125f00d1234ull;
		key ^= requireIndexBuffer ? 0x9e3779b97f4a7c15ull : 0ull;
		key ^= requireIndexBufferView ? 0xb7e151628aed2a6bull : 0ull;
		key ^= hasDescriptorTargets ? 0x51ed1234cafe9876ull : 0ull;
		for (UINT slot = 0; slot < 32; ++slot) {
			if (!requireVertexBuffers[slot])
				continue;
			key ^= static_cast<UINT64>(slot + 1) * 0x100000001b3ull;
			key ^= key >> 33;
			key *= 0xff51afd7ed558ccdull;
		}
		return key ? key : 1;
	}
};

// ---- DX12CompiledDrawDecision.hpp ----
#line 1 "DX12CompiledDrawDecision.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>


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
	bool negativeCacheSafe = false;
	bool shaderOverrideNegativeCacheAllowed = false;
	DX12IaHashTargetPlan targetPlan;
	DX12IaHashTargetPlan directTargetPlan;

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
		negativeCacheSafe = false;
		shaderOverrideNegativeCacheAllowed = false;
		targetPlan = DX12IaHashTargetPlan();
		directTargetPlan = DX12IaHashTargetPlan();
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
		targetPlanResolved = true;
		directTargetPlanResolved = true;
		targetPlanHasIaTargets = targetPlan.HasIaTargets();
		targetPlanHasDescriptorTargets = targetPlan.hasDescriptorTargets;
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

		if (!hasShaderOverrides && !hasShaderOverrideSkip) {
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

	bool CanFastReject() const
	{
		return resolved &&
			kind == DX12CompiledDrawDecisionKind::NoopNeverMatches &&
			!hasShaderOverrideSkip;
	}

	bool NeedsIaHash() const
	{
		return targetPlanHasIaTargets;
	}

	bool NeedsDescriptorMatch() const
	{
		return targetPlanHasDescriptorTargets;
	}

	bool NeedsDrawOverrideWork() const
	{
		return !CanFastReject();
	}

	bool MatchesGlobals(bool activeShaderOverrides) const
	{
		return hasActiveShaderOverrides == activeShaderOverrides;
	}
};

// ---- DX12CommandListTrackingState.hpp ----
#line 1 "DX12CommandListTrackingState.hpp"
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

// ---- DX12CommandListTrackingCache.hpp ----
#line 1 "DX12CommandListTrackingCache.hpp"
#include <Windows.h>


class DX12CommandListTrackingCache : public DX12CommandListTrackingState
{
public:
	UINT64 generation = 0;
};

// ---- DX12PreSkinCacheIdentity.hpp ----
#line 1 "DX12PreSkinCacheIdentity.hpp"
#include <cstdint>

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

// ---- DX12IaBufferHash.hpp ----
#line 1 "DX12IaBufferHash.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

class DX12IaBufferHash
{
public:
	UINT slot = 0;
	uint32_t hash = 0;
	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
};

// ---- DX12IaHashState.hpp ----
#line 1 "DX12IaHashState.hpp"
#include <d3d12.h>

#include <vector>


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

// ---- DX12IaDrawInvocation.hpp ----
#line 1 "DX12IaDrawInvocation.hpp"
#include <Windows.h>
#include <stdint.h>

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

// ---- DX12DirectIaProbeCacheUtils.hpp ----
#line 1 "DX12DirectIaProbeCacheUtils.hpp"
#include <array>
#include <list>
#include <unordered_map>


class DX12DirectIaProbeCacheKey
{
public:
	UINT64 reloadGeneration = 0;
	UINT64 targetPlanFingerprint = 0;
	bool indexed = false;
	bool hasIndexBuffer = false;
	uint32_t indexHash = 0;
	uint32_t vertexSlotMask = 0;
	std::array<uint32_t, 32> vertexHashes = {};
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t instanceCount = 0;
	uint32_t firstVertex = 0;
	uint32_t firstIndex = 0;
	INT baseVertex = 0;
	uint32_t firstInstance = 0;

	bool operator==(const DX12DirectIaProbeCacheKey &other) const
	{
		return reloadGeneration == other.reloadGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			indexed == other.indexed &&
			hasIndexBuffer == other.hasIndexBuffer &&
			indexHash == other.indexHash &&
			vertexSlotMask == other.vertexSlotMask &&
			vertexHashes == other.vertexHashes &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex &&
			firstInstance == other.firstInstance;
	}
};

class DX12DirectIaProbeCacheKeyHash
{
public:
	static size_t Combine(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}

	size_t operator()(const DX12DirectIaProbeCacheKey &key) const
	{
		size_t hash = 0x84222325a9582d75ull;
		hash = Combine(hash, key.reloadGeneration);
		hash = Combine(hash, key.targetPlanFingerprint);
		hash = Combine(hash, key.indexed ? 1 : 0);
		hash = Combine(hash, key.hasIndexBuffer ? 1 : 0);
		hash = Combine(hash, key.indexHash);
		hash = Combine(hash, key.vertexSlotMask);
		uint32_t slots = key.vertexSlotMask;
		for (UINT slot = 0; slot < key.vertexHashes.size(); ++slot) {
			if (slots & (1u << slot))
				hash = Combine(hash, key.vertexHashes[slot]);
		}
		hash = Combine(hash, key.vertexCount);
		hash = Combine(hash, key.indexCount);
		hash = Combine(hash, key.instanceCount);
		hash = Combine(hash, key.firstVertex);
		hash = Combine(hash, key.firstIndex);
		hash = Combine(hash, static_cast<UINT64>(static_cast<INT64>(key.baseVertex)));
		hash = Combine(hash, key.firstInstance);
		return hash;
	}
};

class DX12DirectIaProbeCacheEntry
{
public:
	bool hasDirectCandidate = false;
	std::list<DX12DirectIaProbeCacheKey>::iterator lruPosition;
};

class DX12DirectIaProbeCache
{
public:
	DX12DirectIaProbeCache() = default;

	DX12DirectIaProbeCache(const DX12DirectIaProbeCache &)
	{
	}

	DX12DirectIaProbeCache &operator=(const DX12DirectIaProbeCache &other)
	{
		if (this != &other) {
			entries.clear();
			lruKeys.clear();
			hasScope = false;
		}
		return *this;
	}

	DX12DirectIaProbeCache(DX12DirectIaProbeCache &&) noexcept = default;
	DX12DirectIaProbeCache &operator=(DX12DirectIaProbeCache &&) noexcept = default;

	std::list<DX12DirectIaProbeCacheKey> lruKeys;
	std::unordered_map<
		DX12DirectIaProbeCacheKey,
		DX12DirectIaProbeCacheEntry,
		DX12DirectIaProbeCacheKeyHash> entries;
	bool hasScope = false;
	UINT64 scopeReloadGeneration = 0;
};

class DX12DirectIaProbeCacheUtils
{
public:
	static constexpr size_t MaxEntries = 4096;

	static DX12DirectIaProbeCacheKey MakeKey(
		UINT64 reloadGeneration,
		UINT64 targetPlanFingerprint,
		const DX12IaHashState &state,
		const DX12IaDrawInvocation &draw)
	{
		DX12DirectIaProbeCacheKey key;
		key.reloadGeneration = reloadGeneration;
		key.targetPlanFingerprint = targetPlanFingerprint;
		key.indexed = draw.indexed;
		key.hasIndexBuffer = state.hasIndexBuffer;
		key.indexHash = state.indexHash;
		for (const DX12IaBufferHash &buffer : state.vertexBuffers) {
			if (buffer.slot >= key.vertexHashes.size())
				continue;
			key.vertexSlotMask |= 1u << buffer.slot;
			key.vertexHashes[buffer.slot] = buffer.hash;
		}
		key.vertexCount = draw.vertexCount;
		key.indexCount = draw.indexCount;
		key.instanceCount = draw.instanceCount;
		key.firstVertex = draw.firstVertex;
		key.firstIndex = draw.firstIndex;
		key.baseVertex = draw.baseVertex;
		key.firstInstance = draw.firstInstance;
		return key;
	}

	static bool TryLoad(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key,
		bool *hasDirectCandidate)
	{
		if (!cache || !hasDirectCandidate)
			return false;
		PrepareScope(cache, key);

		auto found = cache->entries.find(key);
		if (found == cache->entries.end())
			return false;

		cache->lruKeys.splice(
			cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
		found->second.lruPosition = cache->lruKeys.begin();
		*hasDirectCandidate = found->second.hasDirectCandidate;
		return true;
	}

	static void Store(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key,
		bool hasDirectCandidate,
		bool *evicted)
	{
		if (evicted)
			*evicted = false;
		if (!cache)
			return;
		PrepareScope(cache, key);

		auto found = cache->entries.find(key);
		if (found != cache->entries.end()) {
			cache->lruKeys.splice(
				cache->lruKeys.begin(), cache->lruKeys, found->second.lruPosition);
			found->second.lruPosition = cache->lruKeys.begin();
			found->second.hasDirectCandidate = hasDirectCandidate;
			return;
		}

		if (cache->entries.size() >= MaxEntries) {
			cache->entries.erase(cache->lruKeys.back());
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}

		cache->lruKeys.push_front(key);
		DX12DirectIaProbeCacheEntry entry;
		entry.hasDirectCandidate = hasDirectCandidate;
		entry.lruPosition = cache->lruKeys.begin();
		cache->entries.emplace(key, std::move(entry));
	}

	static void Clear(DX12DirectIaProbeCache *cache)
	{
		if (!cache)
			return;
		cache->entries.clear();
		cache->lruKeys.clear();
		cache->hasScope = false;
	}

private:
	static void PrepareScope(
		DX12DirectIaProbeCache *cache,
		const DX12DirectIaProbeCacheKey &key)
	{
		if (!cache)
			return;
		const bool sameScope = cache->hasScope &&
			cache->scopeReloadGeneration == key.reloadGeneration;
		if (sameScope)
			return;
		cache->entries.clear();
		cache->lruKeys.clear();
		cache->hasScope = true;
		cache->scopeReloadGeneration = key.reloadGeneration;
	}
};

// ---- DX12DirectIaFastRejectCacheUtils.hpp ----
#line 1 "DX12DirectIaFastRejectCacheUtils.hpp"
#include <Windows.h>

#include <array>
#include <unordered_set>


class DX12DirectIaFastRejectCacheKey
{
public:
	UINT64 reloadGeneration = 0;
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
		return reloadGeneration == other.reloadGeneration &&
			targetPlanFingerprint == other.targetPlanFingerprint &&
			iaViewFingerprint == other.iaViewFingerprint &&
			contentGeneration == other.contentGeneration &&
			resolveGeneration == other.resolveGeneration &&
			requiresIndexBuffer == other.requiresIndexBuffer &&
			requiredVertexMask == other.requiredVertexMask &&
			hasIndexBuffer == other.hasIndexBuffer &&
			indexBufferLocation == other.indexBufferLocation &&
			indexBufferSize == other.indexBufferSize &&
			indexBufferFormat == other.indexBufferFormat &&
			vertexBufferMask == other.vertexBufferMask &&
			vertexBufferLocations == other.vertexBufferLocations &&
			vertexBufferSizes == other.vertexBufferSizes &&
			vertexBufferStrides == other.vertexBufferStrides &&
			indexed == other.indexed &&
			vertexCount == other.vertexCount &&
			indexCount == other.indexCount &&
			instanceCount == other.instanceCount &&
			firstVertex == other.firstVertex &&
			firstIndex == other.firstIndex &&
			baseVertex == other.baseVertex &&
			firstInstance == other.firstInstance;
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
	DX12DirectIaFastRejectCache() = default;

	DX12DirectIaFastRejectCache(const DX12DirectIaFastRejectCache &)
	{
	}

	DX12DirectIaFastRejectCache &operator=(const DX12DirectIaFastRejectCache &other)
	{
		if (this != &other)
			entries.clear();
		return *this;
	}

	DX12DirectIaFastRejectCache(DX12DirectIaFastRejectCache &&) noexcept = default;
	DX12DirectIaFastRejectCache &operator=(DX12DirectIaFastRejectCache &&) noexcept = default;

	std::unordered_set<
		DX12DirectIaFastRejectCacheKey,
		DX12DirectIaFastRejectCacheKeyHash> entries;
};

class DX12DirectIaFastRejectCacheUtils
{
public:
	static constexpr size_t MaxEntries = 4096;

	static UINT64 MixViewValue(UINT64 fingerprint, UINT64 value)
	{
		return fingerprint ^ (value + 0x9e3779b97f4a7c15ull +
			(fingerprint << 6) + (fingerprint >> 2));
	}

	static UINT64 BuildViewFingerprint(
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		bool indexed)
	{
		UINT64 fingerprint = 0x84222325a9582d75ull;
		if (targetPlan.RequiresIndexBuffer(indexed)) {
			fingerprint = MixViewValue(fingerprint, ia.hasIndexBuffer ? 1 : 0);
			if (ia.hasIndexBuffer) {
				fingerprint = MixViewValue(fingerprint, ia.indexBuffer.BufferLocation);
				fingerprint = MixViewValue(fingerprint, ia.indexBuffer.SizeInBytes);
				fingerprint = MixViewValue(fingerprint, ia.indexBuffer.Format);
			}
		}
		for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
			if (!targetPlan.RequiresVertexBuffer(slot))
				continue;
			fingerprint = MixViewValue(fingerprint, slot + 1);
			fingerprint = MixViewValue(
				fingerprint, ia.hasVertexBuffer[slot] ? 1 : 0);
			if (!ia.hasVertexBuffer[slot])
				continue;
			const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
			fingerprint = MixViewValue(fingerprint, view.BufferLocation);
			fingerprint = MixViewValue(fingerprint, view.SizeInBytes);
			fingerprint = MixViewValue(fingerprint, view.StrideInBytes);
		}
		return fingerprint;
	}

	static DX12DirectIaFastRejectCacheKey MakeKey(
		UINT64 reloadGeneration,
		UINT64 contentGeneration,
		UINT64 resolveGeneration,
		const DX12ActiveIaState &ia,
		const DX12IaHashTargetPlan &targetPlan,
		const DX12IaDrawInvocation &draw)
	{
		DX12DirectIaFastRejectCacheKey key;
		key.reloadGeneration = reloadGeneration;
		key.targetPlanFingerprint = targetPlan.Fingerprint();
		key.iaViewFingerprint = BuildViewFingerprint(ia, targetPlan, draw.indexed);
		key.contentGeneration = contentGeneration;
		key.resolveGeneration = resolveGeneration;
		key.requiresIndexBuffer = targetPlan.RequiresIndexBuffer(draw.indexed);
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
			if (!ia.hasVertexBuffer[slot])
				continue;
			key.vertexBufferMask |= 1u << slot;
			key.vertexBufferLocations[slot] = ia.vertexBuffers[slot].BufferLocation;
			key.vertexBufferSizes[slot] = ia.vertexBuffers[slot].SizeInBytes;
			key.vertexBufferStrides[slot] = ia.vertexBuffers[slot].StrideInBytes;
		}
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
		const DX12DirectIaFastRejectCache &cache,
		const DX12DirectIaFastRejectCacheKey &key)
	{
		return cache.entries.find(key) != cache.entries.end();
	}

	static bool Store(
		DX12DirectIaFastRejectCache *cache,
		const DX12DirectIaFastRejectCacheKey &key)
	{
		if (!cache)
			return false;
		if (cache->entries.size() >= MaxEntries)
			cache->entries.erase(cache->entries.begin());
		return cache->entries.insert(key).second;
	}

	static void Clear(DX12DirectIaFastRejectCache *cache)
	{
		if (cache)
			cache->entries.clear();
	}
};

// ---- DX12IaHashStateCacheUtils.hpp ----
#line 1 "DX12IaHashStateCacheUtils.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <list>
#include <unordered_map>
#include <utility>


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
};

class DX12IaHashStateCacheUtils
{
public:
	// The measured title cycles through roughly 1,450 IA draw identities per
	// frame. A 1,024-entry LRU evicts the front of that working set immediately
	// before the next frame reuses it, turning a resident cache into a churn loop.
	static constexpr size_t MaxEntries = 4096;

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

		if (cache->entries.size() >= MaxEntries) {
			cache->entries.erase(cache->lruKeys.back());
			cache->lruKeys.pop_back();
			if (evicted)
				*evicted = true;
		}

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

// ---- DX12CommandListRuntimeState.hpp ----
#line 1 "DX12CommandListRuntimeState.hpp"
#include <d3d12.h>

#include <cstdint>
#include <array>
#include <vector>


// Exact, command-list-local negative result. Unlike the legacy unordered cache,
// this key uses the IA mutation epoch as the identity of all bound views. The
// global generations cover writes/resource reuse, so no content-hash result is
// reused after any dependency can have changed.
class DX12ResidentDrawNegativeEntry
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
	UINT64 reloadGeneration = 0;
	UINT64 contentGeneration = 0;
	UINT64 resolveGeneration = 0;
	UINT64 iaEpoch = 0;
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

class DX12ResolvedDrawRuntimeState
{
public:
	class CachedIaHashBuildEntry
	{
	public:
		bool valid = false;
		bool hasState = false;
		DX12IaHashStateCacheKey key;
		DX12IaHashState state;
	};

	DX12IaHashStateCache iaHashStateCache;
	// Direct-target and full-target key prefixes. Resolved IA resources carry
	// their own content/lifetime generations; global epochs are only consulted
	// for unresolved fallback keys.
	std::array<DX12IaHashKeyPrefixCacheEntry, 2> iaHashKeyPrefixes;
	CachedIaHashBuildEntry lastDirectIaHashBuild;
	CachedIaHashBuildEntry lastFullIaHashBuild;
	DX12DirectIaProbeCache directIaProbeCache;
	DX12DirectIaFastRejectCache directIaFastRejectCache;
	// Small direct-mapped resident cache: the common repeated negative draw is a
	// handful of integer comparisons, with no 32-slot scan or unordered lookup.
	std::array<DX12ResidentDrawNegativeEntry, 16> residentDrawNegativeCache;
};

class DX12ResolvedComputeRuntimeState
{
public:
	bool bindingsValid = false;
	bool bindingsFoundAny = false;
	UINT64 bindingsSerial = 0;
	UINT64 bindingsDependencyFingerprint = 0;
	ID3D12DescriptorHeap *bindingsHeap = nullptr;
	std::vector<UINT> observedSlots;
	std::vector<UINT64> observedSlotVersions;
	std::vector<UINT64> observedSlotValueIds;
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> rootConstants;

	UINT64 bindingFingerprintDependency = 0;
	UINT64 bindingFingerprint = 0;
	UINT64 probeFingerprint = 0;
	uint64_t probeCsHash = 0;
	UINT64 probeReloadGeneration = 0;
	UINT64 probeBindingSerial = 0;
	UINT64 probeIaContentEpoch = 0;
	bool probeResultValid = false;
	bool probeApplied = false;
	bool probeDispatchHandlingSkip = false;
	bool probeDispatchHasDispatch = false;
	UINT probeGroupsX = 0;
	UINT probeGroupsY = 0;
	UINT probeGroupsZ = 0;
};

class DX12CommandListRuntimeState
{
public:
	ID3D12PipelineState *pipelineState = nullptr;
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
	DX12ResolvedDrawRuntimeState resolvedDraw;
	DX12CommandListTrackingCache trackingCache;

	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *samplerHeap = nullptr;
	bool runtimeCbvSrvUavHeapResolved = false;
	DX12DescriptorHeapSummary runtimeCbvSrvUavHeap = {};
	bool runtimeSamplerHeapResolved = false;
	DX12DescriptorHeapSummary runtimeSamplerHeap = {};
	UINT64 bindingQuerySerial = 1;
	UINT graphicsRootTableValidCount = 0;
	UINT computeRootTableValidCount = 0;
	RootTableState graphicsRootTables[MaxRootParameters] = {};
	RootTableState computeRootTables[MaxRootParameters] = {};

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
	DX12CompiledDispatchDecision dispatchDecision;

	// Compiled per-PSO draw policy. Draw consumes this instead of rediscovering
	// PSO shader override metadata on every command.
	DX12CompiledDrawDecision drawDecision;

	// Cached device pointer to eliminate per-draw AcquireModDevice QI overhead.
	// Set during Reset from the command list; device must outlive the command list
	// per DX12 spec (device -> command list is a parent-child relationship).
	ID3D12Device *device = nullptr;

	DX12ResolvedComputeRuntimeState resolvedCompute;
};

// ---- DX12CommandListRuntimeShard.hpp ----
#line 1 "DX12CommandListRuntimeShard.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>


class DX12CommandListRuntimeShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState*> directStates;
	std::unordered_map<ID3D12GraphicsCommandList*, DX12CommandListRuntimeState> states;
};

// ---- DX12CommandListRuntimeTlsCache.hpp ----
#line 1 "DX12CommandListRuntimeTlsCache.hpp"
#include <d3d12.h>


class DX12CommandListRuntimeTlsCache
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	DX12CommandListRuntimeShard *shard = nullptr;
	bool stateIsDirect = false;
	UINT64 trackingGeneration = 0;
	DX12CommandListTrackingCache tracking;
	DX12CommandListRuntimeState *statePtr = nullptr;
};

// ---- DX12CommandListRuntime.hpp ----
#line 1 "DX12CommandListRuntime.hpp"
#include <Windows.h>
#include <d3d12.h>

#include <unordered_map>


class DX12CommandListRuntime
{
public:
	static constexpr size_t kRuntimeShardCount = 64;

	static DX12CommandListRuntimeShard Shards[kRuntimeShardCount];
	static volatile UINT64 TrackingGenerationValue;
	static thread_local DX12CommandListRuntimeTlsCache Tls;

	static void RegisterDirectState(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *state);
	static void UnregisterDirectState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListRuntimeState *GetDirectStatePtr(ID3D12GraphicsCommandList *commandList);

	static UINT64 TrackingGeneration();
	static void BumpTrackingGeneration();
	static DX12CommandListRuntimeShard &ShardFor(ID3D12GraphicsCommandList *commandList);
	static void RefreshTrackingCache(DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState MakeTrackingState(const DX12CommandListTrackingCache &cache);
	static DX12CommandListTrackingState GetTrackingState(DX12CommandListRuntimeState *state);
	static void TlsRefreshFromState(
		DX12CommandListRuntimeState *state,
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeShard *shard,
		bool stateIsDirect);
	static void TlsRefresh(DX12CommandListRuntimeShard &shard, ID3D12GraphicsCommandList *commandList);
	static void GetTrackingCacheFast(ID3D12GraphicsCommandList *commandList, DX12CommandListTrackingCache &out);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListRuntimeState *GetStatePtrFast(ID3D12GraphicsCommandList *commandList);
	static void TlsInvalidateIfMatch(ID3D12GraphicsCommandList *commandList);

	static void Register(ID3D12GraphicsCommandList *commandList);
	static bool Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(DX12CommandListRuntimeState *state, ID3D12PipelineState *initialState);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static DX12CompiledDispatchDecision BuildDispatchDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static DX12CompiledDrawDecision BuildDrawDecision(
		ID3D12PipelineState *pipelineState,
		bool hasShaderOverrides);
	static const DX12CompiledDispatchDecision *EnsureDispatchDecision(
		DX12CommandListRuntimeState *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static const DX12CompiledDrawDecision *EnsureDrawDecision(
		DX12CommandListRuntimeState *state,
		bool hasShaderOverrides);
	static void CompileDispatchDecision(
		DX12CommandListRuntimeState *state,
		bool hasShaderOverrides,
		bool needsPreSkinProbe);
	static void CompileDrawDecision(
		DX12CommandListRuntimeState *state,
		bool hasShaderOverrides);
	static void RememberPipelineState(DX12CommandListRuntimeState *state, ID3D12PipelineState *pipelineState);
	static void RememberPipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void ResetIa(DX12CommandListRuntimeState *state);
	static void ResetIa(ID3D12GraphicsCommandList *commandList);
	static void RememberIndexBuffer(DX12CommandListRuntimeState *state, const D3D12_INDEX_BUFFER_VIEW *view);
	static void RememberIndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void RememberVertexBuffers(DX12CommandListRuntimeState *state, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void RememberVertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void RememberPrimitiveTopology(DX12CommandListRuntimeState *state, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void RememberPrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void BumpIaEpoch(DX12CommandListRuntimeState *state);
	static void BumpIaContentEpoch(DX12CommandListRuntimeState *state);
	static void BumpIaContentEpoch(ID3D12GraphicsCommandList *commandList);
	static bool BuildIaHashState(const DX12ActiveIaState &ia, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static bool BuildIaHashStateForTargets(const DX12ActiveIaState &ia, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex, DX12IaHashState *state);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargets(const DX12ActiveIaState &ia, UINT64 iaContentEpoch, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);
	static DX12IaHashStateCacheKey BuildIaHashStateCacheKeyForTargetsCached(DX12CommandListRuntimeState *state, UINT cacheSlot, const DX12IaHashTargetPlan &targetPlan, bool indexed, UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex, INT baseVertex);
	static ID3D12PipelineState *GetPipelineState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListRuntimeState GetState(ID3D12GraphicsCommandList *commandList);
	static const DX12CommandListRuntimeState *GetStatePtr(ID3D12GraphicsCommandList *commandList);
	static void SetDescriptorHeaps(DX12CommandListRuntimeState *state, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void SetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void RememberRootDescriptorTable(DX12CommandListRuntimeState *state, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void RememberRootDescriptorTable(ID3D12GraphicsCommandList *commandList, bool graphics, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static bool GetDescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap **cbvSrvUavHeap, ID3D12DescriptorHeap **samplerHeap);
};

// ---- DX12CommandListStateCapture.hpp ----
#line 1 "DX12CommandListStateCapture.hpp"
#include <d3d12.h>


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
	static DX12CommandListTrackingState GetTrackingState(DX12CommandListRuntimeState *runtimeState);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList);
	static DX12CommandListTrackingState GetTrackingState(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState);

	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, D3D12_PRIMITIVE_TOPOLOGY topology);
	static void PrimitiveTopology(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, D3D12_PRIMITIVE_TOPOLOGY topology);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static ID3D12PipelineState *PipelineState(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, ID3D12PipelineState *pipelineState);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT count, ID3D12DescriptorHeap *const *heaps);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void DescriptorHeaps(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, ID3D12DescriptorHeap *cbvSrvUavHeap, ID3D12DescriptorHeap *samplerHeap);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void ComputeRootSignature(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, ID3D12RootSignature *rootSignature);
	static void GraphicsRootSignature(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, ID3D12RootSignature *rootSignature);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void GraphicsRootDescriptorTable(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void GraphicsRoot32BitConstant(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, UINT srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void GraphicsRoot32BitConstants(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, UINT num32BitValuesToSet, const void *srcData, UINT destOffset);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void ComputeRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void GraphicsRootDescriptor(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT rootParameterIndex, D3D12_ROOT_PARAMETER_TYPE type, D3D12_GPU_VIRTUAL_ADDRESS address);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, const D3D12_INDEX_BUFFER_VIEW *view);
	static void IndexBuffer(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, const D3D12_INDEX_BUFFER_VIEW *view);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
	static void VertexBuffers(ID3D12GraphicsCommandList *commandList, DX12CommandListRuntimeState *runtimeState, UINT startSlot, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views);
};

// ---- DX12FrameAnalysisState.hpp ----
#line 1 "DX12FrameAnalysisState.hpp"
#include <Windows.h>
#include <Shlwapi.h>
#include <share.h>
#include <stdio.h>
#include <time.h>


class DX12FrameAnalysisState
{
public:
	inline static SRWLOCK lock = SRWLOCK_INIT;
	inline static bool active = false;
	inline static bool captureRequested = false;
	inline static bool capturing = false;
	inline static bool dumpPending = false;
	inline static UINT captureFramesRemaining = 0;
	inline static volatile LONG activeFast = 0;
	inline static volatile LONG captureRequestedFast = 0;
	inline static volatile LONG capturingFast = 0;
	inline static wchar_t path[MAX_PATH] = {};
	inline static FILE *log = nullptr;
	inline static unsigned long long lineNo = 0;
	inline static constexpr UINT CaptureFramesPerDump = 1;

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
		InterlockedExchange(&activeFast, 1);
		InterlockedExchange(&captureRequestedFast, 0);
		InterlockedExchange(&capturingFast, 0);
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
		InterlockedExchange(&activeFast, 0);
		InterlockedExchange(&captureRequestedFast, 0);
		InterlockedExchange(&capturingFast, 0);
		CloseLogLocked();
		ReleaseSRWLockExclusive(&lock);
	}

	static void RequestCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && !capturing && !dumpPending) {
			captureRequested = true;
			InterlockedExchange(&captureRequestedFast, 1);
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static void BeginCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && captureRequested && !capturing && !dumpPending) {
			captureRequested = false;
			capturing = true;
			InterlockedExchange(&captureRequestedFast, 0);
			InterlockedExchange(&capturingFast, 1);
			captureFramesRemaining = CaptureFramesPerDump;
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
			InterlockedExchange(&capturingFast, 0);
			ReleaseSRWLockExclusive(&lock);
			return true;
		}
		ReleaseSRWLockExclusive(&lock);
		return false;
	}

	static bool IsActive()
	{
		return InterlockedCompareExchange(&activeFast, 0, 0) != 0;
	}

	static bool IsCaptureRequested()
	{
		return InterlockedCompareExchange(&captureRequestedFast, 0, 0) != 0;
	}

	static bool IsCapturing()
	{
		return InterlockedCompareExchange(&capturingFast, 0, 0) != 0;
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

// ---- DX12HotPathState.hpp ----
#line 1 "DX12HotPathState.hpp"
#include <Windows.h>


bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsBusy();
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsShaderDescriptorTracking();

class DX12HotPathState
{
public:
	inline static volatile LONG skipAll = 1;
	inline static volatile LONG skipBindings = 1;
	inline static volatile LONG skipGraphicsBindings = 1;
	inline static volatile LONG trackResourceMetadata = 0;
	inline static volatile LONG stateMask = -1;

	// ── 单一 flags word：替代 9 个独立函数调用，一条 if 判断全部 ──
	// 在 Update() 和 RebuildHookWorkFlags() 中更新，
	// ExecuteCommandLists 入口处也会刷新确保帧级别一致性。
	inline static volatile UINT64 HookWorkFlags = 0;

	// bit 定义
	static constexpr UINT64 FLAG_NEEDS_MOD_WORK          = 1ull << 0;
	static constexpr UINT64 FLAG_HUNT_ENABLED            = 1ull << 1;
	static constexpr UINT64 FLAG_FRAME_ANALYSIS_ACTIVE   = 1ull << 2;
	static constexpr UINT64 FLAG_SHADER_DUMP_ACTIVE      = 1ull << 3;
	static constexpr UINT64 FLAG_PRE_SKIN_PROBE          = 1ull << 4;
	static constexpr UINT64 FLAG_PRE_SKIN_BINDING_TRACK  = 1ull << 5;
	static constexpr UINT64 FLAG_SHADER_OVERRIDES         = 1ull << 6;
	static constexpr UINT64 FLAG_PRESENT_REPLACEMENT      = 1ull << 7;

	// 汇总 mask：一条 if 即可判断是否有 draw / dispatch 级 hook 工作
	static constexpr UINT64 MASK_DRAW_WORK =
		FLAG_NEEDS_MOD_WORK | FLAG_HUNT_ENABLED |
		FLAG_FRAME_ANALYSIS_ACTIVE | FLAG_SHADER_DUMP_ACTIVE;
	static constexpr UINT64 MASK_DISPATCH_WORK =
		MASK_DRAW_WORK | FLAG_PRE_SKIN_PROBE;

	static void RebuildHookWorkFlags()
	{
		UINT64 flags = 0;
		const bool shaderOverrides = DX12ModHasActiveShaderOverrides();
		const bool presentReplacement = DX12ModNeedsPresentReplacement();
		if (shaderOverrides || presentReplacement)
			flags |= FLAG_NEEDS_MOD_WORK;
		if (shaderOverrides)
			flags |= FLAG_SHADER_OVERRIDES;
		if (presentReplacement)
			flags |= FLAG_PRESENT_REPLACEMENT;
		if (DX12HuntIsEnabled())
			flags |= FLAG_HUNT_ENABLED;
		if (DX12FrameAnalysisState::IsActive() || DX12FrameAnalysisState::IsCapturing() || DX12FrameAnalysisState::IsCaptureRequested())
			flags |= FLAG_FRAME_ANALYSIS_ACTIVE;
		if (DX12ShaderDumpIsCapturingFrame() || DX12ShaderDumpIsCaptureRequested() || DX12ShaderDumpIsBusy())
			flags |= FLAG_SHADER_DUMP_ACTIVE;
		if (DX12ModNeedsPreSkinningUavProbe())
			flags |= FLAG_PRE_SKIN_PROBE;
		if (DX12ModNeedsPreSkinningBindingTracking())
			flags |= FLAG_PRE_SKIN_BINDING_TRACK;

		InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&HookWorkFlags), static_cast<LONG64>(flags));
	}

	static void Update()
	{
		if (skipAll &&
			!DX12FrameAnalysisState::IsActive() &&
			!DX12FrameAnalysisState::IsCapturing() &&
			!DX12FrameAnalysisState::IsCaptureRequested() &&
			!DX12ShaderDumpIsCapturingFrame() &&
			!DX12ShaderDumpIsCaptureRequested() &&
			!DX12ShaderDumpIsBusy() &&
			!DX12HuntNeedsHotPath() &&
			!DX12ModHasActiveShaderOverrides() &&
			!DX12ModNeedsPresentReplacement() &&
			!DX12ModNeedsPreSkinningUavProbe() &&
			!DX12ModNeedsPreSkinningBindingTracking())
			return;

		const bool huntingMode = DX12RuntimeState::IsHuntingMode();
		const bool needsHeavyTracking =
			huntingMode ||
			DX12ShaderDumpIsCapturingFrame() ||
			DX12ShaderDumpIsBusy();

		// DX11 风格: skipAll 只控制 draw/state/copy 级 hook。
		// PreSkin 仅需 dispatch hook + compute binding 跟踪，
		// 通过 skipBindings + dispatch hook 中的独立检查保证。
		const bool needsRecordWork =
			huntingMode ||
			DX12ModHasActiveShaderOverrides() ||
			DX12ModNeedsPresentReplacement();
		const bool needsBindingState =
			needsHeavyTracking ||
			DX12ModNeedsPreSkinningBindingTracking();
		const bool needsGraphicsBindingState =
			needsHeavyTracking ||
			DX12ModNeedsShaderDescriptorTracking();

		const LONG skipAll = (needsHeavyTracking || needsRecordWork) ? 0 : 1;
		const LONG skipBindings = needsBindingState ? 0 : 1;
		const LONG skipGraphicsBindings = needsGraphicsBindingState ? 0 : 1;
		const LONG trackResourceMetadata = needsHeavyTracking ? 1 : 0;

		InterlockedExchange(&DX12HotPathState::skipAll, skipAll);
		InterlockedExchange(&DX12HotPathState::skipBindings, skipBindings);
		InterlockedExchange(&DX12HotPathState::skipGraphicsBindings, skipGraphicsBindings);
		InterlockedExchange(&DX12HotPathState::trackResourceMetadata, trackResourceMetadata);

		const LONG mask =
			(skipAll ? 1 : 0) |
			(skipBindings ? 2 : 0) |
			(skipGraphicsBindings ? 4 : 0) |
			(trackResourceMetadata ? 8 : 0);
		if (InterlockedExchange(&stateMask, mask) != mask) {
			// Command lists cache binding-capture decisions. Mode or capture-state
			// changes must invalidate that cache, otherwise Hunting can keep using
			// a Mod-era "do not track bindings" decision and lose SRV texture tables.
			DX12CommandListRuntime::BumpTrackingGeneration();
			DX12LogManager::JsonFunc("DX12HotPathState",
				"\"mode\":\"%s\",\"skipAll\":%ld,\"skipBindings\":%ld,\"skipGraphicsBindings\":%ld,\"trackResourceMetadata\":%ld",
				huntingMode ? "Hunting" : "Mod",
				skipAll, skipBindings, skipGraphicsBindings, trackResourceMetadata);
		}

		// 同步更新 flags word：一条 if 替代 9 个函数调用
		RebuildHookWorkFlags();
	}

	static bool HooksNeeded()
	{
		return InterlockedCompareExchange(&skipAll, 0, 0) == 0 ||
			DX12HuntNeedsHotPath();
	}
};

// ---- DX12Profiling.hpp ----
#line 1 "DX12Profiling.hpp"
#include <Windows.h>
#include <stdio.h>
#include <wchar.h>


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

	class ScopedTimer
	{
	public:
		Counter &counter;
		LARGE_INTEGER start = {};

		explicit ScopedTimer(Counter &counter);
		~ScopedTimer();
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

#define DX12_PROFILE_SCOPE(counter) \
	DX12Profiling::ScopedTimer _profile_timer(DX12Profiling::Get().counter)

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

inline DX12Profiling::ScopedTimer::ScopedTimer(Counter &counter) :
	counter(counter)
{
	if (!DX12Profiling::ShouldCollectCounters()) {
		start.QuadPart = 0;
		return;
	}
	counter.IncrementCalls();
	start.QuadPart = 0;
	if (DX12Profiling::Get().collectTiming)
		QueryPerformanceCounter(&start);
}

inline DX12Profiling::ScopedTimer::~ScopedTimer()
{
	if (start.QuadPart == 0)
		return;
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);
	InterlockedAdd64(&counter.cpuTicks, endTime.QuadPart - start.QuadPart);
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
	InterlockedExchange(&collectCounters, 1);
#if defined(_DEBUG)
	InterlockedExchange(&collectTiming, 1);
#else
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
	} else if (DX12LogManager::VerboseDiagnosticsEnabled()) {
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
		DX12LogManager::JsonFunc("DX12Profiling", "\"status\":\"enabled\",\"mode\":\"SUMMARY\"");
	} else {
		if (mode == Mode::SUMMARY)
			FlushFrameState();
		mode = Mode::NONE;
		InterlockedExchange(&collectCounters, 0);
		if (!ShouldCollectCounters())
			ResetFrame();
		DX12LogManager::Text("DX12 profiling disabled\n");
		DX12LogManager::JsonFunc("DX12Profiling", "\"status\":\"disabled\"");
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

	DX12LogManager::DebugJsonFunc("DX12FrameStats",
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
	bindings.AccumulateFrom(SetComputeRootUnorderedAccessView);
	bindings.AccumulateFrom(IASetIndexBuffer);
	bindings.AccumulateFrom(IASetVertexBuffers);
	bindings.AccumulateFrom(IASetPrimitiveTopology);

	Counter all = {};
	all.AccumulateFrom(draws);
	all.AccumulateFrom(Dispatch);
	all.AccumulateFrom(SetPipelineState);
	all.AccumulateFrom(bindings);
	all.AccumulateFrom(ResourceBarrier);

	DX12LogManager::DebugJsonFunc("DX12ProfilingFrame",
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

// ---- DX12CommandListComputeBindingFlow.hpp ----
#line 1 "DX12CommandListComputeBindingFlow.hpp"
#include <d3d12.h>


class DX12CommandListComputeBindingFlow
{
public:
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureDescriptorHeaps(
			commandList, runtimeState, count, heaps);
	}

	static void CaptureDescriptorHeaps(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT count,
		ID3D12DescriptorHeap *const *heaps)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		if (DX12HotPathState::skipAll) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		ID3D12DescriptorHeap *cbvSrvUav = nullptr;
		ID3D12DescriptorHeap *sampler = nullptr;
		ResolveDescriptorHeaps(count, heaps, &cbvSrvUav, &sampler);
		if (runtimeState)
			DX12CommandListRuntime::SetDescriptorHeaps(runtimeState, cbvSrvUav, sampler);
		else
			DX12CommandListRuntime::SetDescriptorHeaps(commandList, cbvSrvUav, sampler);

		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
		const bool skipTrackedCapture =
			DX12HotPathState::skipBindings && !tracking.trackBindings;
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
		}

		DX12CommandListFlowDiagnostics::LogDX12Call(
			"ID3D12GraphicsCommandList::SetDescriptorHeaps", commandList,
			" count=%u", count);
		DX12CommandListStateCapture::DescriptorHeaps(
			commandList, runtimeState, cbvSrvUav, sampler);
	}

	static void UpdateComputeBindingHash(
		DX12CommandListRuntimeState *runtimeState,
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(runtimeState, hashComponent);
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootSignature(
			commandList, runtimeState, rootSignature, graphics);
	}

	static void CaptureRootSignature(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12RootSignature *rootSignature,
		bool graphics)
	{
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
		const bool skipTrackedCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			(DX12HotPathState::skipBindings && !tracking.trackBindings);
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
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
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState, reinterpret_cast<uint64_t>(rootSignature));
		}
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptorTable(
			commandList, runtimeState, rootParameterIndex, baseDescriptor, graphics);
	}

	static void CaptureRootDescriptorTable(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor,
		bool graphics)
	{
		if (DX12InternalReplayState::IsActive())
			return;
		if (runtimeState) {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				runtimeState, graphics, rootParameterIndex, baseDescriptor);
		} else {
			DX12CommandListRuntime::RememberRootDescriptorTable(
				commandList, graphics, rootParameterIndex, baseDescriptor);
		}
		const DX12CommandListTrackingState tracking =
			DX12CommandListStateCapture::GetTrackingState(commandList, runtimeState);
		const bool skipTrackedCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			(DX12HotPathState::skipBindings && !tracking.trackBindings);
		if (skipTrackedCapture) {
			DX12_PROFILE_FAST_FORWARD();
			return;
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
			DX12CommandListComputeBindingFlow::UpdateComputeBindingHash(
				runtimeState,
				static_cast<uint64_t>(rootParameterIndex) ^ 0x9e3779b97f4a7c15ull);
		}
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstant(
			commandList, runtimeState, rootParameterIndex, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstant(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		UINT srcData,
		UINT destOffset,
		bool graphics)
	{
		const bool skipBindingCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRoot32BitConstants(
			commandList, runtimeState, rootParameterIndex,
			num32BitValuesToSet, srcData, destOffset, graphics);
	}

	static void CaptureRoot32BitConstants(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		UINT num32BitValuesToSet,
		const void *srcData,
		UINT destOffset,
		bool graphics)
	{
		const bool skipBindingCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
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
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListComputeBindingFlow::CaptureRootDescriptor(
			commandList, runtimeState, rootParameterIndex, type, address, graphics, api);
	}

	static void CaptureRootDescriptor(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT rootParameterIndex,
		D3D12_ROOT_PARAMETER_TYPE type,
		D3D12_GPU_VIRTUAL_ADDRESS address,
		bool graphics,
		const char *api)
	{
		const bool skipBindingCapture = graphics ?
			DX12HotPathState::skipGraphicsBindings :
			DX12HotPathState::skipBindings;
		if (skipBindingCapture) {
			DX12_PROFILE_FAST_FORWARD();
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

// ---- DX12CommandListLifecycle.hpp ----
#line 1 "DX12CommandListLifecycle.hpp"
#include <d3d12.h>

class DX12CommandListLifecycle
{
public:
	static void Register(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void Unregister(ID3D12GraphicsCommandList *commandList);
	static void Reset(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *initialState);
	static void ClearState(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState);
	static void InvalidateAfterExecuteBundle(ID3D12GraphicsCommandList *commandList);
};

// ---- DX12CommandListCommandRecorder.hpp ----
#line 1 "DX12CommandListCommandRecorder.hpp"
#include <d3d12.h>

class DX12CommandListCommandRecorder
{
public:
	static void RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
	static void RecordExecuteBundle(ID3D12GraphicsCommandList *bundle, ID3D12PipelineState *initialState);
	static void RecordExecuteIndirect(ID3D12GraphicsCommandList *commandList);
};

// ---- DX12ShaderHunt.hpp ----
#line 1 "DX12ShaderHunt.hpp"
#include <d3d12.h>
#include <dxgi.h>
#include <stdint.h>
#include <stddef.h>


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

// ---- DX12ResidentMaintenanceTelemetryUtils.hpp ----
#line 1 "DX12ResidentMaintenanceTelemetryUtils.hpp"
#include <Windows.h>


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
	inline static volatile LONG DrawDecisionFastRejects = 0;
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

	static void Inc(volatile LONG *value)
	{
		InterlockedIncrement(value);
	}

	static void Add64(volatile LONG64 *value, LONG64 amount)
	{
		if (amount)
			InterlockedExchangeAdd64(value, amount);
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

	static void RecordDrawDecisionFastReject()
	{
		Inc(&DrawDecisionFastRejects);
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
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
		LONG fastRejects = Take(&DrawDecisionFastRejects);
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
			!fastRejects && !iaHashDraws && !iaHashCacheLookups &&
			!iaHashCacheHits && !iaHashCacheMisses && !iaHashCacheStores &&
			!iaHashCacheEvictions &&
			!iaHashCacheNegativeHits && !descriptorDraws &&
			!directProbeCacheLookups && !directProbeCacheHits &&
			!directProbeCacheMisses && !directProbeCachePositiveHits &&
			!directProbeCacheNegativeHits && !directProbeCacheStores &&
			!directProbeCacheEvictions)
			return;
		DX12LogManager::JsonFunc("DX12ResidentMaintenance",
			"\"category\":\"draw_decision\",\"cacheHits\":%ld,\"cacheMisses\":%ld,\"psoCacheHits\":%ld,\"psoCacheMisses\":%ld,\"builds\":%ld,\"noPso\":%ld,\"noShaderOverride\":%ld,\"noDrawOverride\":%ld,\"needsIaHash\":%ld,\"needsDescriptorMatch\":%ld,\"needsReplacement\":%ld,\"skipOriginal\":%ld,\"fastRejects\":%ld,\"iaHashDraws\":%ld,\"iaHashCacheLookups\":%ld,\"iaHashCacheHits\":%ld,\"iaHashCacheMisses\":%ld,\"iaHashCacheStores\":%ld,\"iaHashCacheEvictions\":%ld,\"iaHashCacheNegativeHits\":%ld,\"descriptorDraws\":%ld,\"directProbeCacheLookups\":%ld,\"directProbeCacheHits\":%ld,\"directProbeCacheMisses\":%ld,\"directProbeCachePositiveHits\":%ld,\"directProbeCacheNegativeHits\":%ld,\"directProbeCacheStores\":%ld,\"directProbeCacheEvictions\":%ld",
			cacheHits, cacheMisses, psoCacheHits, psoCacheMisses, builds, noPso,
			noShaderOverride, noDrawOverride, needsIaHash,
			needsDescriptorMatch, needsReplacement, skipOriginal,
			fastRejects, iaHashDraws, iaHashCacheLookups,
			iaHashCacheHits, iaHashCacheMisses, iaHashCacheStores, iaHashCacheEvictions,
			iaHashCacheNegativeHits, descriptorDraws,
			directProbeCacheLookups, directProbeCacheHits, directProbeCacheMisses,
			directProbeCachePositiveHits, directProbeCacheNegativeHits,
			directProbeCacheStores, directProbeCacheEvictions);
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
	}
};

// ---- DX12CommandListStateFlow.hpp ----
#line 1 "DX12CommandListStateFlow.hpp"
#include <d3d12.h>


class DX12CommandListStateFlow
{
public:
	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		ID3D12PipelineState *pipelineState)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		return DX12CommandListStateFlow::CapturePipelineState(
			commandList, runtimeState, pipelineState);
	}

	static ID3D12PipelineState *CapturePipelineState(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		ID3D12PipelineState *pipelineState)
	{
		// Same-PSO fast path: when the identical PSO is set again, skip all
		// tracking.  Resolve runtimeState if the caller didn't provide one.
		DX12CommandListRuntimeState *rs = runtimeState;
		if (!rs) {
			rs = DX12CommandListRuntime::GetStatePtrFast(commandList);
		}
		if (rs && (rs->pipelineState == pipelineState ||
				rs->cachedReplacementPsoKey == pipelineState)) {
			DX12_PROFILE_FAST_FORWARD();
			return rs->cachedReplacementPso
				? rs->cachedReplacementPso : pipelineState;
		}

		if (DX12HotPathState::skipBindings && DX12HuntIsEnabled() &&
		    !DX12ModHasActiveShaderOverrides()) {
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

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CapturePrimitiveTopology(commandList, runtimeState, topology);
	}

	static void CapturePrimitiveTopology(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetPrimitiveTopology", commandList,
				" topology=%u", static_cast<UINT>(topology));
			DX12CommandListStateCapture::PrimitiveTopology(commandList, runtimeState, topology);
			return;
		}

		const bool huntEnabled = DX12HuntIsEnabled();
		const bool modEnabled = DX12ModHasActiveShaderOverrides();
		if (huntEnabled || modEnabled) {
			if (runtimeState)
				DX12CommandListRuntime::RememberPrimitiveTopology(runtimeState, topology);
			else
				DX12CommandListRuntime::RememberPrimitiveTopology(commandList, topology);
		}
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CaptureIndexBuffer(commandList, runtimeState, view);
	}

	static void CaptureIndexBuffer(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		const D3D12_INDEX_BUFFER_VIEW *view)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetIndexBuffer", commandList,
				" gpu=0x%llx size=%u format=%u",
				view ? static_cast<unsigned long long>(view->BufferLocation) : 0ull,
				view ? view->SizeInBytes : 0,
				view ? static_cast<UINT>(view->Format) : 0);
			DX12CommandListStateCapture::IndexBuffer(commandList, runtimeState, view);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		const bool huntEnabled = DX12HuntIsEnabled();
		const bool modEnabled = DX12ModHasActiveShaderOverrides();
		if (huntEnabled)
			DX12Profiling::RecordIaHuntIaUpdate();
		if (huntEnabled || modEnabled) {
			if (runtimeState)
				DX12CommandListRuntime::RememberIndexBuffer(runtimeState, view);
			else
				DX12CommandListRuntime::RememberIndexBuffer(commandList, view);
		}
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		DX12CommandListRuntimeState *runtimeState =
			DX12CommandListRuntime::GetStatePtrFast(commandList);
		DX12CommandListStateFlow::CaptureVertexBuffers(
			commandList, runtimeState, startSlot, count, views);
	}

	static void CaptureVertexBuffers(
		ID3D12GraphicsCommandList *commandList,
		DX12CommandListRuntimeState *runtimeState,
		UINT startSlot,
		UINT count,
		const D3D12_VERTEX_BUFFER_VIEW *views)
	{
		if (!DX12HotPathState::skipBindings) {
			DX12CommandListFlowDiagnostics::LogDX12Call(
				"ID3D12GraphicsCommandList::IASetVertexBuffers", commandList,
				" start=%u count=%u", startSlot, count);
			DX12CommandListStateCapture::VertexBuffers(
				commandList, runtimeState, startSlot, count, views);
			return;
		}

		DX12_PROFILE_FAST_FORWARD();
		const bool huntEnabled = DX12HuntIsEnabled();
		const bool modEnabled = DX12ModHasActiveShaderOverrides();
		if (huntEnabled)
			DX12Profiling::RecordIaHuntIaUpdate();
		if (huntEnabled || modEnabled) {
			if (runtimeState)
				DX12CommandListRuntime::RememberVertexBuffers(runtimeState, startSlot, count, views);
			else
				DX12CommandListRuntime::RememberVertexBuffers(commandList, startSlot, count, views);
		}
	}
};

// ---- DX12ModRuntime.hpp ----
#line 1 "DX12ModRuntime.hpp"
#include <d3d12.h>
#include <dxgi.h>
#include <stdint.h>
#include <wchar.h>

#include <algorithm>
#include <string>
#include <vector>


bool DX12ModRuntimeLoad(
	const wchar_t *configPath,
	std::wstring *statusMessage = nullptr,
	bool *hasWarnings = nullptr,
	bool *hasErrors = nullptr);
bool DX12ModRuntimeReload();
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
bool DX12ModHasAnyActiveOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModHasActivePreSkinTextureOverrides();
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
	std::vector<std::wstring> postCommandLists;
	class DrawCall
	{
	public:
		bool indexed = false;
		UINT count = 0;
		UINT start = 0;
		INT baseVertex = 0;
		bool fromCaller = false;
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
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement,
	bool allowNegativeCache = true,
	ID3D12Device *cachedDevice = nullptr);
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
	UINT64 computeBindingSerial,
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
	DX12PreSkinDispatchOverride *dispatchOverride = nullptr);
bool DX12ModApplyKnownPreSkinningUavPatches(ID3D12GraphicsCommandList *commandList);
void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList);
void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists);
void DX12ModBeginFrame(IDXGISwapChain *swapChain);
bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement);
bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source);
bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source);

// PreSkin DynamicUAV: register a resized output UAV so IA redirects can bind
// the larger buffer when vertex count exceeds original buffer capacity.
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
void DX12ModRegisterPreSkinOutputResize(
	ID3D12Resource *originalResource, ID3D12Resource *resizedResource,
	UINT64 originalByteWidth, UINT64 resizedByteWidth, UINT stride);
bool DX12ModResolvePreSkinOutputResizeByGpuVa(
	UINT64 viewGpuVa, DX12PreSkinOutputResizeBinding *binding);
UINT64 DX12ModGetPreSkinOutputResizeGeneration();
void DX12ModClearPreSkinOutputResizeMap();

// ---- DX12PreSkinPlan.hpp ----
#line 1 "DX12PreSkinPlan.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>


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
	// Strips GPU VA, resource pointer, resourceOffset, viewSize, and other per-
	// frame-volatile fields. The result is safe because different resources with
	// the same shader binding pattern produce identical PreSkin replacement results.
	static uint64_t AppendStableDescriptor(uint64_t hash, const DX12DescriptorSummary &descriptor)
	{
		hash = HashString(hash, descriptor.kind);
		hash = HashCombine(hash, descriptor.hasDesc ? 1 : 0);
		hash = HashCombine(hash, descriptor.viewFormat);
		hash = HashCombine(hash, descriptor.viewDimension);
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
		std::vector<DX12CurrentComputeUavBinding> sortedBindings = bindings;
		std::sort(
			sortedBindings.begin(),
			sortedBindings.end(),
			DX12PreSkinPlanUtils::BindingLess);
		hash = HashCombine(hash, sortedBindings.size());
		for (size_t index = 0; index < sortedBindings.size(); ++index) {
			hash = AppendStableBinding(hash, sortedBindings[index], listSalt);
		}
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

// ---- DX12ResourceTracker.hpp ----
#line 1 "DX12ResourceTracker.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>

#include <vector>


void DX12RecordPsoRootSignature(
	UINT64 psoIndex, const char *kind, ID3D12PipelineState *pipelineState,
	ID3D12RootSignature *rootSignature);
void DX12RecordResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER *barriers);
bool DX12ResolveBufferResourceByGpuVa(
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
bool DX12CacheBufferContent(ID3D12Resource *resource, const void *data, size_t dataSize);
bool DX12CacheBufferContentRange(
	ID3D12Resource *resource, UINT64 resourceOffset, const void *data, size_t dataSize);
bool DX12TryGetBufferContent(UINT64 gpuVirtualAddress, UINT64 size, const uint8_t **data, size_t *dataSize);
UINT64 DX12GetBufferContentGenerationForResource(ID3D12Resource *resource);
void DX12InvalidateBufferContent(ID3D12Resource *resource);
bool DX12CacheTextureHash(
	ID3D12Resource *resource, uint32_t hash, const D3D12_RESOURCE_DESC *desc, const char *source);
bool DX12TryGetTextureHash(ID3D12Resource *resource, uint32_t *hash);
void DX12InvalidateTextureHash(ID3D12Resource *resource);
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

// ---- DX12ShaderDump.hpp ----
#line 1 "DX12ShaderDump.hpp"
#include <d3d12.h>


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

// ---- DX12OuterTimingProbeUtils.hpp ----
#line 1 "DX12OuterTimingProbeUtils.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>


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
		ApiResourceMap,
		ApiResourceUnmap,
		ApiResourceReadWrite,
		ApiResourceQuery,
		ApiDeviceQuery,
		ApiObjectLifetime,
		ApiWallBucketCount
	};

#if defined(_DEBUG)
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
	inline static volatile LONG ApiScopeFirstBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeLastBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeLastThreadId = 0;
	inline static volatile LONG ApiScopeMaxGapBeforeBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeMaxGapAfterBucket = ApiOtherCommandList;
	inline static volatile LONG ApiScopeMaxGapBeforeThreadId = 0;
	inline static volatile LONG ApiScopeMaxGapAfterThreadId = 0;

	inline static SRWLOCK CpuStageLock = SRWLOCK_INIT;
	inline static CpuStageBucket CpuStageBuckets[160] = {};
	inline static LONG CpuStageBucketCount = 0;

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
		QueryPerformanceCounter(&value);
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
		const LARGE_INTEGER end = Now();
		return DeltaUs(begin.QuadPart, end.QuadPart);
	}

	static UINT64 FenceCompletedValue(ID3D12Fence *fence)
	{
		if (!fence)
			return 0;
		return fence->GetCompletedValue();
	}

	static LONG64 ClampUs(LONG64 us)
	{
		return us > 0 ? us : 0;
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
		case ApiResourceMap: return "ResourceMap";
		case ApiResourceUnmap: return "ResourceUnmap";
		case ApiResourceReadWrite: return "ResourceReadWrite";
		case ApiResourceQuery: return "ResourceQuery";
		case ApiDeviceQuery: return "DeviceQuery";
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

	static void LogApiScopeBegin(int bucket, LONGLONG beginQpc)
	{
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
			LONG64 observed = InterlockedCompareExchange64(&ApiScopeMaxGapUs, 0, 0);
			while (gapUs > observed) {
				const LONG64 previous = InterlockedCompareExchange64(
					&ApiScopeMaxGapUs, gapUs, observed);
				if (previous == observed) {
					InterlockedExchange(&ApiScopeMaxGapBeforeBucket,
						InterlockedCompareExchange(&ApiScopeLastBucket, 0, 0));
					InterlockedExchange(&ApiScopeMaxGapAfterBucket, bucket);
					InterlockedExchange(&ApiScopeMaxGapBeforeThreadId,
						InterlockedCompareExchange(&ApiScopeLastThreadId, 0, 0));
					InterlockedExchange(&ApiScopeMaxGapAfterThreadId,
						static_cast<LONG>(GetCurrentThreadId()));
					break;
				}
				observed = previous;
			}
		}
	}

	static void LogApiScopeEnd(int bucket, LONGLONG beginQpc, LONGLONG endQpc)
	{
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
			DX12LogManager::JsonFunc("DX12ApiWallTiming",
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
		const LONG firstBucket = InterlockedExchange(&ApiScopeFirstBucket, ApiOtherCommandList);
		const LONG lastBucket = InterlockedExchange(&ApiScopeLastBucket, ApiOtherCommandList);
		const LONG maxGapBeforeBucket = InterlockedExchange(
			&ApiScopeMaxGapBeforeBucket, ApiOtherCommandList);
		const LONG maxGapAfterBucket = InterlockedExchange(
			&ApiScopeMaxGapAfterBucket, ApiOtherCommandList);
		const LONG maxGapBeforeThreadId = TakeLong(&ApiScopeMaxGapBeforeThreadId);
		const LONG maxGapAfterThreadId = TakeLong(&ApiScopeMaxGapAfterThreadId);
		TakeLong(&ApiScopeLastThreadId);

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

		DX12LogManager::JsonFunc("DX12CpuTimelineCoverage",
			"\"present\":%ld,\"apiScopeCalls\":%lld,\"apiScopeNestedCalls\":%lld,"
			"\"apiScopeTotalUs\":%lld,\"apiScopeSpanUs\":%lld,\"apiScopeMaxGapUs\":%lld,"
			"\"apiScopeFirstBucket\":\"%s\",\"apiScopeLastBucket\":\"%s\","
			"\"apiScopeMaxGapBeforeBucket\":\"%s\",\"apiScopeMaxGapAfterBucket\":\"%s\","
			"\"apiScopeMaxGapBeforeThreadId\":%ld,\"apiScopeMaxGapAfterThreadId\":%ld,"
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

	static void LogCpuStage(
		const char *category,
		const char *stage,
		UINT kind,
		size_t itemCount,
		LONG64 us)
	{
#if defined(_DEBUG)
		const LONG64 safeUs = ClampUs(us);
		AcquireSRWLockExclusive(&CpuStageLock);
		const LONG index = FindCpuStageBucketLocked(category, stage, kind);
		if (index >= 0) {
			++CpuStageBuckets[index].calls;
			CpuStageBuckets[index].totalUs += safeUs;
			if (safeUs > CpuStageBuckets[index].maxUs)
				CpuStageBuckets[index].maxUs = safeUs;
			CpuStageBuckets[index].items += static_cast<LONG64>(itemCount);
		}
		ReleaseSRWLockExclusive(&CpuStageLock);
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
		for (LONG index = 0; index < snapshotCount; ++index)
			snapshot[index] = CpuStageBuckets[index];
		memset(CpuStageBuckets, 0, sizeof(CpuStageBuckets));
		CpuStageBucketCount = 0;
		ReleaseSRWLockExclusive(&CpuStageLock);

		for (LONG index = 0; index < snapshotCount; ++index) {
			if (!snapshot[index].calls && !snapshot[index].totalUs)
				continue;
			DX12LogManager::JsonFunc("DX12CpuStageTiming",
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
		if (!snapshot.jsonCalls && !snapshot.textCalls && !snapshot.debugJsonCalls &&
				!snapshot.enqueueCalls && !snapshot.formatUs && !snapshot.enqueueUs &&
				!snapshot.droppedLines) {
			return;
		}
		const LONG64 totalUs = snapshot.formatUs + snapshot.enqueueUs;
		DX12LogManager::JsonFunc("DX12LogCost",
			"\"present\":%ld,\"jsonCalls\":%lld,\"textCalls\":%lld,"
			"\"debugJsonCalls\":%lld,\"enqueueCalls\":%lld,\"formatUs\":%lld,"
			"\"enqueueUs\":%lld,\"totalUs\":%lld,\"droppedLines\":%lld,"
			"\"frameWallUs\":%lld,\"framePctX100\":%lld",
			present,
			static_cast<long long>(snapshot.jsonCalls),
			static_cast<long long>(snapshot.textCalls),
			static_cast<long long>(snapshot.debugJsonCalls),
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
	}

	static void LogPresentLeave(const char *api, IDXGISwapChain *swapChain, HRESULT hr)
	{
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
	}

	static void LogPresentSlice(const char *api, const char *stage, IDXGISwapChain *swapChain, HRESULT hr, LONG64 us)
	{
		(void)api;
		(void)swapChain;
		(void)hr;

		const LONG64 safeUs = ClampUs(us);
		AddUs(&PresentSliceKnownUs, safeUs);
		if (StageEquals(stage, "original_present") || StageEquals(stage, "original_present_skip")) {
			AddUs(&PresentOriginalUs, safeUs);
		} else if (StageEquals(stage, "post_original_runtime") || StageEquals(stage, "post_original_skip")) {
			AddUs(&PresentPostOriginalRuntimeUs, safeUs);
		} else if (StageEquals(stage, "mod_begin_frame")) {
			AddUs(&PresentModBeginFrameUs, safeUs);
		} else if (StageEquals(stage, "profiling_and_hotpath_flush")) {
			AddUs(&PresentProfilingFlushUs, safeUs);
		} else if (StageEquals(stage, "consume_ui_actions")) {
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
	}

	static void LogQueueExecuteSlice(const char *stage, ID3D12CommandQueue *queue, UINT numCommandLists, LONG64 us)
	{
#if defined(_DEBUG)
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
		(void)api;
		(void)stage;
		(void)fence;
		(void)value;
		(void)completedBefore;
		(void)completedAfter;
		(void)hr;

		InterlockedIncrement64(&FenceCalls);
		AddUs(&FenceTotalUs, us);
	}

	static void LogEventWaitSlice(const char *api, const char *stage, HANDLE eventHandle, DWORD waitMs, DWORD waitResult, DWORD lastError, LONG64 us)
	{
		(void)api;
		(void)stage;
		(void)eventHandle;
		(void)waitMs;
		(void)waitResult;
		(void)lastError;

		InterlockedIncrement64(&EventWaitCalls);
		AddUs(&EventWaitTotalUs, us);
	}

	static void LogWorkerTask(const char *queueName, const char *stage, UINT taskKind, UINT64 sequence, size_t batchSize, size_t pending, DWORD enqueueThreadId, LONG64 us)
	{
		(void)taskKind;
		(void)sequence;
		(void)batchSize;
		(void)pending;
		(void)enqueueThreadId;

		InterlockedIncrement64(&WorkerTaskCalls);
		AddUs(&WorkerTaskUs, us);
		LogCpuStage(queueName, stage, taskKind, batchSize, us);
	}

	static void LogWorkerWait(const char *queueName, const char *reason, const char *mode, UINT64 targetSequence, UINT64 completedSequence, size_t pending, LONG64 us)
	{
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
		LogApiWallTimingState(present, frameWallUs);
		LogCpuStageTimingState(present, frameWallUs);

		DX12LogManager::JsonFunc("DX12FrameTimeline",
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
			"\"eventWaitTotalUs\":%lld,\"workerTaskCalls\":%lld,\"workerTaskUs\":%lld,"
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
