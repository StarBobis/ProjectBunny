#pragma once

#include "Core/DX12Platform.hpp"

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
