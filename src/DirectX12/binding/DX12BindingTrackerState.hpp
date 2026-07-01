#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "DX12BindingTrackerTypes.hpp"
#include "types/DX12PsoTypes.hpp"
#include "types/DX12ResourceTypes.hpp"


constexpr UINT MaxRootParameters = 64;
constexpr UINT MaxVertexBufferSlots = 32;
constexpr UINT MaxRenderTargetSlots = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
constexpr UINT MaxTrackedEvents = 20000;
constexpr UINT MaxTrackedOutputEvents = 4096;
constexpr UINT MaxDescriptorsPerRangeDump = 256;

class RootTableState
{
public:
	bool valid = false;
	UINT rootParameterIndex = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor = {};
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
	UINT64 computeBindingSerial = 0;
	UINT64 bindingQuerySerial = 1;
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
	std::string kind;
	ID3D12GraphicsCommandList *commandList = nullptr;
	ID3D12PipelineState *pipelineState = nullptr;
	DX12PsoShaderInfo shaderInfo = {};
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;
	UINT slot = 0;
	SIZE_T cpuHandle = 0;
};

constexpr size_t kBindingShardCount = 64;

class BindingShard
{
public:
	SRWLOCK lock = SRWLOCK_INIT;
	std::unordered_map<ID3D12GraphicsCommandList*, CommandListBindingState> states;
};

// Per-thread read cache for binding tracker: avoids SRWLOCK on repeated identical calls.
// Key insight: D3D12 command lists are single-threaded, so TLS cache is safe.
class BindingTlsTableCache
{
public:
	ID3D12GraphicsCommandList *cl = nullptr;
	UINT64 gpuHandle[MaxRootParameters] = {};
};

class BindingTlsDescriptorCache
{
public:
	ID3D12GraphicsCommandList *cl = nullptr;
	UINT64 address[MaxRootParameters] = {};
};

class BindingTlsDescriptorQueryCache
{
public:
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
	UINT64 bindingQuerySerial = 0;
	ID3D12DescriptorHeap *cbvSrvUavHeap = nullptr;
	UINT64 cbvSrvUavHeapVersion = 0;
	std::vector<DX12CurrentComputeUavBinding> uavs;
	std::vector<DX12CurrentComputeUavBinding> srvs;
	std::vector<DX12CurrentComputeUavBinding> cbvs;
	std::vector<DX12CurrentRootConstants> constants;
};



class DX12BindingTrackerState
{
public:
	static BindingShard Shards[kBindingShardCount];
	static SRWLOCK EventsLock;
	static std::vector<BindingEvent> Events;
	static std::vector<OutputBindingEvent> OutputEvents;
	static UINT64 EventSerial;
	static UINT64 OutputEventSerial;
	static volatile LONG64 GlobalDrawSerial;
	static volatile LONG64 GlobalDispatchSerial;
	static UINT64 DroppedEvents;
	static UINT64 DroppedOutputEvents;

	static BindingShard &ShardFor(ID3D12GraphicsCommandList *commandList)
	{
		uintptr_t key = reinterpret_cast<uintptr_t>(commandList);
		key ^= key >> 7;
		key *= 0x9e3779b97f4a7c15ull;
		return Shards[(key >> 17) & (kBindingShardCount - 1)];
	}
};



