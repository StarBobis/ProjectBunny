#pragma once

// Resource summaries, frame/current binding state, and binding event models.
// Split out of Core/DX12CoreModels.h; include that umbrella header from existing code.
#include "../DX12Platform.h"

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

	// Per-recording reset without the placement-new memset: the root-constant
	// payloads (~41KB of values/valueValid arrays) are only read while
	// valid==true, and the retained IA resources were already released and
	// cleared by ReleaseRetainedIaResources, so clearing the validity and
	// identity fields is semantically identical to reconstructing the state.
	void ResetForRecording()
	{
		pipelineState = nullptr;
		pipelineShaderInfo = DX12PsoShaderInfo();
		pipelineShaderInfoValid = false;
		graphicsRootSignature = nullptr;
		computeRootSignature = nullptr;
		cbvSrvUavHeap = nullptr;
		samplerHeap = nullptr;
		runtimeCbvSrvUavHeapResolved = false;
		runtimeCbvSrvUavHeap = DX12DescriptorHeapSummary();
		runtimeSamplerHeapResolved = false;
		runtimeSamplerHeap = DX12DescriptorHeapSummary();
		computeBindingSerial = 0;
		bindingQuerySerial = 1;
		graphicsTableValidCount = 0;
		computeTableValidCount = 0;
		for (UINT root = 0; root < MaxRootParameters; ++root) {
			graphicsTables[root] = RootTableState();
			computeTables[root] = RootTableState();
			graphicsRootDescriptors[root] = RootDescriptorState();
			computeRootDescriptors[root] = RootDescriptorState();
			graphicsRootConstants[root].valid = false;
			graphicsRootConstants[root].rootParameterIndex = 0;
			graphicsRootConstants[root].maxSet = 0;
			computeRootConstants[root].valid = false;
			computeRootConstants[root].rootParameterIndex = 0;
			computeRootConstants[root].maxSet = 0;
		}
		for (UINT slot = 0; slot < MaxVertexBufferSlots; ++slot) {
			vertexBuffers[slot] = D3D12_VERTEX_BUFFER_VIEW();
			vertexBufferValid[slot] = false;
			vertexBufferResolved[slot] = false;
		}
		indexBuffer = D3D12_INDEX_BUFFER_VIEW();
		indexBufferValid = false;
		indexBufferResolved = false;
		for (UINT slot = 0; slot < MaxRenderTargetSlots; ++slot) {
			renderTargets[slot] = D3D12_CPU_DESCRIPTOR_HANDLE();
			renderTargetValid[slot] = false;
		}
		depthStencil = D3D12_CPU_DESCRIPTOR_HANDLE();
		depthStencilValid = false;
		primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		drawSerial = 0;
		dispatchSerial = 0;
	}
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
