#pragma once

#include "Core/DX12FeatureCore.hpp"

enum class DX12ResourceCollectTaskKind
{
	Resource,
	ResourceBarrier,
	RootSignature,
	PsoRootSignature
};

class DX12ResourceCollectTask
{
public:
	DX12ResourceCollectTaskKind kind = DX12ResourceCollectTaskKind::Resource;
	ID3D12Resource *resource = nullptr;
	bool retainedResource = false;
	D3D12_RESOURCE_DESC desc = {};
	bool hasDesc = false;
	D3D12_HEAP_PROPERTIES heapProperties = {};
	bool hasHeapProperties = false;
	PlacedResourceInfo placedInfo = {};
	bool hasPlacedInfo = false;
	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	const char *source = nullptr;
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	ID3D12RootSignature *rootSignature = nullptr;
	std::vector<uint8_t> rootSignatureBlob;
	UINT rootSignatureNodeMask = 0;
	UINT64 psoIndex = 0;
	std::string psoKind;
	ID3D12PipelineState *pipelineState = nullptr;
	uint64_t sequence = 0;
	DWORD enqueueThreadId = 0;
};

class DX12ResourceCollectQueue
{
public:
	inline static std::mutex mutex;
	inline static std::condition_variable wake;
	inline static std::condition_variable idle;
	inline static std::deque<DX12ResourceCollectTask> tasks;
	inline static std::thread worker;
	inline static std::atomic<bool> started = false;
	inline static std::atomic<bool> stopRequested = false;
	inline static std::atomic<uint64_t> nextSequence = 1;
	inline static std::atomic<uint64_t> enqueued = 0;
	inline static std::atomic<uint64_t> dequeued = 0;
	inline static std::atomic<uint64_t> completed = 0;
	inline static std::atomic<uint64_t> completedSequence = 0;
	inline static std::atomic<uint64_t> dropped = 0;
	inline static std::atomic<uint64_t> addRefFailed = 0;
	inline static std::atomic<uint64_t> maxDepth = 0;
	inline static std::atomic<uint64_t> resourceTasksCompleted = 0;
	inline static std::atomic<uint64_t> resourceTasksElapsedUs = 0;
	inline static std::atomic<uint64_t> resourceTasksMaxElapsedUs = 0;
	inline static std::atomic<uint64_t> resourceTaskStatsSerial = 0;
	inline static uint64_t activeTasks = 0;

	static void Start();
	static void Stop();
	static bool Enqueue(
		ID3D12Resource *resource,
		const D3D12_RESOURCE_DESC *desc,
		const D3D12_HEAP_PROPERTIES *heapProperties,
		const PlacedResourceInfo *placedInfo,
		D3D12_RESOURCE_STATES initialState);
	static bool EnqueueResourceBarriers(
		UINT numBarriers,
		const D3D12_RESOURCE_BARRIER *barriers,
		const char *source);
	static bool EnqueueRootSignature(
		ID3D12RootSignature *rootSignature,
		UINT nodeMask,
		const void *blob,
		SIZE_T blobLength,
		const char *source);
	static bool EnqueuePsoRootSignature(
		UINT64 psoIndex,
		const char *kind,
		ID3D12PipelineState *pipelineState,
		ID3D12RootSignature *rootSignature,
		const char *source);
	static void DrainForSnapshot(const char *reason);
	static size_t PendingCount();
	static UINT64 LastSubmittedSequence();

	static void WorkerLoop();
	static void DrainPending(const char *reason);
	static void WaitForSubmittedSequence(UINT64 targetSequence, const char *reason);
	static void WaitForIdle(const char *reason);
	static void ProcessTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessResourceTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessResourceBarrierTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	static void ProcessPsoRootSignatureTask(DX12ResourceCollectTask *task, const char *source);
	static bool TryAddRefBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	static void ReleaseBarrierResources(D3D12_RESOURCE_BARRIER *barrier);
	static void ReleaseTaskRootSignature(DX12ResourceCollectTask *task);
	static void ReleaseTaskPsoRootSignature(DX12ResourceCollectTask *task);
	static void UpdateMaxDepth(size_t depth);
	static void LogStats(const char *event, uint64_t sequence, size_t depth);
	static void RecordResourceTaskStats(DX12ResourceCollectTask *task, const char *source, int64_t elapsedUs);
};




// Resource dump tasks intentionally keep failure notes per item so a bad
// resource never aborts the whole frame dump batch.
enum class DumpTaskSource
{
	Descriptor,
	InputAssembler,
	Output,
	Resource,
};

class DumpTask
{
public:
	DumpTaskSource sourceKind = DumpTaskSource::Descriptor;
	DX12FrameResourceBinding binding;
	DX12FrameIaBufferBinding iaBuffer;
	DX12FrameOutputBinding outputBinding;
	ID3D12Resource *source = nullptr;
	ID3D12Resource *readback = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> textureLayouts;
	std::vector<UINT> textureRows;
	std::vector<UINT64> textureRowSizes;
	UINT64 sourceOffset = 0;
		UINT64 copyBytes = 0;
		UINT64 readbackOffset = 0;
		uint32_t contentHash = 0;
		D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON;
		bool needsBarrier = false;
		bool isTexture = false;
		bool hasContentHash = false;
		bool ready = false;
	bool copied = false;
	bool stateKnown = false;
	bool directMap = false;
	bool keepReadbackAlive = false;
	bool sourceRetained = false;
	std::wstring fileName;
	std::string dedupeKey;
	const char *skipNote = nullptr;
};



class DX12ResourceDumpSafetyUtils
{
public:
	static bool IsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension)
	{
		return dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	}

	static UINT64 MaxSingleReadbackBytes()
	{
		return UINT64_MAX;
	}

	static UINT MaxTextureSubresourcesForDump()
	{
		return static_cast<UINT>(~0u);
	}

	static bool TextureSubresourceCountForDump(
		const D3D12_RESOURCE_DESC &desc, UINT *subresourceCount)
	{
		if (!subresourceCount)
			return false;
		*subresourceCount = 0;
		if (!IsTextureResourceDimension(desc.Dimension))
			return false;
		if (desc.Width == 0 || desc.Height == 0 || desc.DepthOrArraySize == 0)
			return false;
		if (desc.SampleDesc.Count == 0 || desc.SampleDesc.Count > 64)
			return false;
		const UINT mipLevels = desc.MipLevels ? desc.MipLevels : 1;
		const UINT64 arrayOrDepth = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
			1 : desc.DepthOrArraySize;
		const UINT64 count = static_cast<UINT64>(mipLevels) * arrayOrDepth;
		if (count == 0 || count > static_cast<UINT64>(MaxTextureSubresourcesForDump()))
			return false;
		*subresourceCount = static_cast<UINT>(count);
		return true;
	}

	static bool ResourceDescLooksDumpable(const D3D12_RESOURCE_DESC &desc)
	{
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			return desc.Width > 0;
		if (IsTextureResourceDimension(desc.Dimension)) {
			UINT subresources = 0;
			return TextureSubresourceCountForDump(desc, &subresources);
		}
		return false;
	}

	static bool DescriptorResourceDescLooksDumpable(const DX12DescriptorSummary &descriptor)
	{
		return descriptor.resource &&
			descriptor.hasResourceDesc &&
			ResourceDescLooksDumpable(descriptor.resourceDesc);
	}

	static bool HeapTypeAllowsSafeDiscoveryCopy(UINT heapType, bool hasHeapType)
	{
		if (!hasHeapType)
			return false;
		return heapType == D3D12_HEAP_TYPE_UPLOAD ||
			heapType == D3D12_HEAP_TYPE_READBACK;
	}

	static bool ResourceStateAllowsLiveDiscoveryCopy(const DX12ResourceSummary &resource)
	{
		if (HeapTypeAllowsSafeDiscoveryCopy(
				resource.resourceHeapType, resource.hasResourceHeapType))
			return true;

		return resource.hasCurrentState;
	}

	static bool DescriptorStateAllowsLiveDiscoveryCopy(const DX12DescriptorSummary &descriptor)
	{
		if (HeapTypeAllowsSafeDiscoveryCopy(
				descriptor.resourceHeapType, descriptor.hasResourceHeapType))
			return true;

		return descriptor.hasCurrentState;
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12ResourceSummary &resource)
	{
		if (!resource.hasCurrentState)
			return "resource_state_unknown";
		if (!resource.stateTrackedByBarrier)
			return "resource_state_create_only";
		return "resource_discovery_copy_unsafe";
	}

	static const char *LiveDiscoveryCopySkipReason(const DX12DescriptorSummary &descriptor)
	{
		if (!descriptor.hasCurrentState)
			return "descriptor_state_unknown";
		if (!descriptor.stateTrackedByBarrier)
			return "descriptor_state_create_only";
		return "descriptor_discovery_copy_unsafe";
	}

	static bool DiscoveryBudgetAllows(
		UINT queuedTasks, UINT64 queuedBytes, UINT64 nextBytes,
		UINT taskBudget, UINT64 byteBudget)
	{
		if (taskBudget > 0 && queuedTasks >= taskBudget)
			return false;
		if (byteBudget > 0 &&
			(queuedBytes > byteBudget || nextBytes > byteBudget - queuedBytes))
			return false;
		return true;
	}
};




class DX12ResourceDumpCore
{
public:
static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

static bool EnsureDirectory(const wchar_t *path)
{
	return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static const char *DumpTaskSourceName(DumpTaskSource sourceKind)
{
	switch (sourceKind) {
	case DumpTaskSource::Descriptor:
		return "descriptor";
	case DumpTaskSource::InputAssembler:
		return "ia";
	case DumpTaskSource::Output:
		return "output";
	case DumpTaskSource::Resource:
		return "resource";
	default:
		return "unknown";
	}
}

static uint32_t HashBytes(uint32_t seed, const void *data, size_t size)
{
	if (!data || size == 0)
		return seed;
	__try {
		return crc32c_append(seed, static_cast<const uint8_t*>(data), size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12FrameAnalysis::LogJsonFunc("Crc32cFailed",
			"\"exception\":\"0x%lx\",\"size\":%zu", GetExceptionCode(), size);
		return 0;
	}
}

static void GetDedupedDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
{
	swprintf_s(path, pathCount, L"%s\\deduped", dir);
	DX12ResourceDumpCore::EnsureDirectory(path);
}
static UINT64 FrameReadbackBudgetBytes()
{
	return 0;
}

static UINT64 AlignUp(UINT64 value, UINT64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static const char *RootDescriptorKindName(UINT rangeType)
{
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
		return DescriptorKind::UAV.data();
	return "";
}

static const char *DescriptorRangeKindName(UINT rangeType)
{
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
		return DescriptorKind::CBV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
		return DescriptorKind::SRV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
		return DescriptorKind::UAV.data();
	if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
		return DescriptorKind::Sampler.data();
	return "";
}

static const char *BindingKindName(const DX12FrameResourceBinding &binding)
{
	if (!binding.usageClass.empty())
		return binding.usageClass.c_str();
	if (!binding.descriptor.kind.empty())
		return binding.descriptor.kind.c_str();
	if (binding.rootDescriptor)
		return DX12ResourceDumpCore::RootDescriptorKindName(binding.rangeType);
	return DescriptorRangeKindName(binding.rangeType);
}

static DX12FrameResourceBinding ResourceBindingFromOutputBinding(
	const DX12FrameOutputBinding &output)
{
	DX12FrameResourceBinding binding;
	binding.eventSerial = output.eventSerial;
	binding.drawId = output.drawId ? output.drawId : output.eventSerial;
	binding.dispatchId = output.dispatchId;
	binding.psoIndex = output.psoIndex;
	binding.shaderInfo = output.shaderInfo;
	binding.bindSpace = "output";
	binding.rootParameterIndex = output.slot;
	binding.rangeIndex = 0;
	binding.rangeType = output.kind == "DSV" ?
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	binding.shaderRegister = output.slot;
	binding.registerSpace = 0;
	binding.descriptorOffset = output.slot;
	binding.cpuHandle = output.cpuHandle;
	binding.descriptor = output.descriptor;
	if (binding.descriptor.kind.empty())
		binding.descriptor.kind = output.kind;
	binding.hasDescriptor = output.hasDescriptor;
	return binding;
}


static DX12FrameResourceBinding ResourceBindingFromTrackedResource(
	const DX12ResourceSummary &resource, UINT64 index)
{
	DX12FrameResourceBinding binding;
	binding.bindSpace = "hunting_resource";
	binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	binding.descriptorIndex = index;
	binding.descriptor.kind = DescriptorKind::SRV;
	binding.descriptor.resource = resource.resource;
	binding.descriptor.resourceDesc = resource.resourceDesc;
	binding.descriptor.hasResourceDesc = resource.hasResourceDesc;
	binding.descriptor.resourceHeapType = resource.resourceHeapType;
	binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
	binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
	binding.descriptor.viewSize = resource.size;
	binding.descriptor.currentState = resource.currentState;
	binding.descriptor.hasCurrentState = resource.hasCurrentState;
	binding.descriptor.stateTrackedByBarrier = resource.stateTrackedByBarrier;
	binding.hasDescriptor = resource.resource != nullptr;
	return binding;
}


};




class DX12ResourceDumpBufferTextUtils
{
public:
	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		switch (topology) {
		case D3D_PRIMITIVE_TOPOLOGY_UNDEFINED:
			return "undefined";
		case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
			return "pointlist";
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			return "linelist";
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
			return "linestrip";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return "trianglelist";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			return "trianglestrip";
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:
			return "linelist_adj";
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:
			return "linestrip_adj";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:
			return "trianglelist_adj";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ:
			return "trianglestrip_adj";
		case D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST:
			return "1_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST:
			return "2_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:
			return "3_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST:
			return "4_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST:
			return "5_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST:
			return "6_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST:
			return "7_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST:
			return "8_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST:
			return "9_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST:
			return "10_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST:
			return "11_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST:
			return "12_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST:
			return "13_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST:
			return "14_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST:
			return "15_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST:
			return "16_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_17_CONTROL_POINT_PATCHLIST:
			return "17_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_18_CONTROL_POINT_PATCHLIST:
			return "18_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_19_CONTROL_POINT_PATCHLIST:
			return "19_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_20_CONTROL_POINT_PATCHLIST:
			return "20_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_21_CONTROL_POINT_PATCHLIST:
			return "21_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_22_CONTROL_POINT_PATCHLIST:
			return "22_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_23_CONTROL_POINT_PATCHLIST:
			return "23_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_24_CONTROL_POINT_PATCHLIST:
			return "24_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_25_CONTROL_POINT_PATCHLIST:
			return "25_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_26_CONTROL_POINT_PATCHLIST:
			return "26_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_27_CONTROL_POINT_PATCHLIST:
			return "27_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_28_CONTROL_POINT_PATCHLIST:
			return "28_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_29_CONTROL_POINT_PATCHLIST:
			return "29_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_30_CONTROL_POINT_PATCHLIST:
			return "30_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_31_CONTROL_POINT_PATCHLIST:
			return "31_control_point_patchlist";
		case D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST:
			return "32_control_point_patchlist";
		default:
			return "invalid";
		}
	}

	static int TopologyIndexGrouping(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		switch (topology) {
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			return 2;
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return 3;
		default:
			return 1;
		}
	}

	static const char *IndexFormatName(UINT format)
	{
		switch (format) {
		case DXGI_FORMAT_R16_UINT:
			return "DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R32_UINT:
			return "DXGI_FORMAT_R32_UINT";
		default:
			return "UNKNOWN";
		}
	}

	static bool BindingIsConstantBuffer(const DumpTask &task)
	{
		if (task.sourceKind != DumpTaskSource::Descriptor)
			return false;
		if (task.binding.rootDescriptor)
			return task.binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV;
		return task.binding.descriptor.kind == DescriptorKind::CBV ||
			task.binding.descriptor.kind == DescriptorKind::CONSTANT_BUFFER_VIEW ||
			task.binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	}

	static char GenericBufferType(const DumpTask &task)
	{
		return BindingIsConstantBuffer(task) ? 'c' : '?';
	}

	static int GenericBufferIndex(const DumpTask &task)
	{
		if (task.sourceKind != DumpTaskSource::Descriptor)
			return -1;
		if (task.binding.shaderRegister == UINT_MAX)
			return -1;
		return static_cast<int>(task.binding.shaderRegister);
	}

	static UINT GenericBufferStride(const DumpTask &task)
	{
		if (task.sourceKind == DumpTaskSource::InputAssembler)
			return task.iaBuffer.stride;
		return task.binding.descriptor.structureByteStride;
	}

	static bool WriteGenericBufferText(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		if (!file || !data)
			return false;

		const UINT64 offset = task.sourceOffset;
		const UINT stride = GenericBufferStride(task);
		if (offset)
			fprintf(file, "offset: %llu\n", static_cast<unsigned long long>(offset));
		if (stride)
			fprintf(file, "stride: %u\n", stride);

		const char components[] = "xyzw";
		const char type = GenericBufferType(task);
		const int index = GenericBufferIndex(task);
		const UINT64 registerBase = offset / 16;
		for (UINT64 row = 0; row + 16 <= task.copyBytes; row += 16) {
			const float *values = reinterpret_cast<const float*>(data + row);
			const UINT64 registerIndex = registerBase + row / 16;
			UINT componentStart = 0;
			if (row == 0)
				componentStart = static_cast<UINT>((offset % 16) / 4);
			for (UINT component = componentStart; component < 4; ++component) {
				if (index == -1) {
					fprintf(file, "buf[%llu].%c: %.9g\n",
						static_cast<unsigned long long>(registerIndex),
						components[component], values[component]);
				} else {
					fprintf(file, "%cb%i[%llu].%c: %.9g\n",
						type, index,
						static_cast<unsigned long long>(registerIndex),
						components[component], values[component]);
				}
			}
		}
		return true;
	}

	static bool WriteIndexBufferText(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		if (!file || !data)
			return false;

		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
		if (buffer.drawStartIndex || buffer.drawIndexCount) {
			fprintf(file, "first index: %u\n", buffer.drawStartIndex);
			fprintf(file, "index count: %u\n", buffer.drawIndexCount);
		}

		const D3D12_PRIMITIVE_TOPOLOGY topology = buffer.topology;
		if (topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
			fprintf(file, "topology: %s\n", TopologyName(topology));
		const int grouping = TopologyIndexGrouping(topology);
		const UINT format = buffer.format;

		if (format == DXGI_FORMAT_R16_UINT) {
			fprintf(file, "format: DXGI_FORMAT_R16_UINT\n");
			const UINT elementCount = static_cast<UINT>(task.copyBytes / sizeof(uint16_t));
			UINT start = buffer.drawStartIndex;
			if (start > elementCount)
				start = elementCount;
			UINT end = elementCount;
			if (buffer.drawIndexCount)
				end = (std::min<UINT>)(end, start + buffer.drawIndexCount);
			const uint16_t *indices = reinterpret_cast<const uint16_t*>(data);
			for (UINT i = start; i < end; ++i) {
				if ((i - start) % grouping == 0)
					fprintf(file, "\n");
				else
					fprintf(file, " ");
				fprintf(file, "%u", indices[i]);
			}
			fprintf(file, "\n");
			return true;
		}

		if (format == DXGI_FORMAT_R32_UINT) {
			fprintf(file, "format: DXGI_FORMAT_R32_UINT\n");
			const UINT elementCount = static_cast<UINT>(task.copyBytes / sizeof(uint32_t));
			UINT start = buffer.drawStartIndex;
			if (start > elementCount)
				start = elementCount;
			UINT end = elementCount;
			if (buffer.drawIndexCount)
				end = (std::min<UINT>)(end, start + buffer.drawIndexCount);
			const uint32_t *indices = reinterpret_cast<const uint32_t*>(data);
			for (UINT i = start; i < end; ++i) {
				if ((i - start) % grouping == 0)
					fprintf(file, "\n");
				else
					fprintf(file, " ");
				fprintf(file, "%u", indices[i]);
			}
			fprintf(file, "\n");
			return true;
		}

		fprintf(file, "format %u is illegal\n", format);
		return true;
	}

	static const char *InputSlotClassName(D3D12_INPUT_CLASSIFICATION value)
	{
		switch (value) {
		case D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:
			return "per-vertex";
		case D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:
			return "per-instance";
		default:
			return nullptr;
		}
	}

	static void WriteInputLayout(
		FILE *file, const DX12PsoInputLayoutInfo &layout, UINT slot,
		bool *perVertex, bool *perInstance)
	{
		if (!file)
			return;
		if (perVertex)
			*perVertex = false;
		if (perInstance)
			*perInstance = false;

		for (UINT i = 0; i < layout.elements.size(); ++i) {
			const DX12PsoInputElementInfo &element = layout.elements[i];
			fprintf(file, "element[%u]:\n", i);
			fprintf(file, "  SemanticName: %s\n", element.semanticName.c_str());
			fprintf(file, "  SemanticIndex: %u\n", element.semanticIndex);
			fprintf(file, "  Format: %s\n", DX12PsoInputLayoutUtils::FormatName(element.format));
			fprintf(file, "  InputSlot: %u\n", element.inputSlot);
			if (element.appendAlignedElement)
				fprintf(file, "  AlignedByteOffset: append\n");
			else
				fprintf(file, "  AlignedByteOffset: %u\n", element.alignedByteOffset);
			const char *slotClass = InputSlotClassName(element.inputSlotClass);
			if (slotClass) {
				fprintf(file, "  InputSlotClass: %s\n", slotClass);
				if (element.inputSlot == slot &&
						element.inputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA &&
						perVertex)
					*perVertex = true;
				if (element.inputSlot == slot &&
						element.inputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA &&
						perInstance)
					*perInstance = true;
			} else {
				fprintf(file, "  InputSlotClass: %u\n", static_cast<UINT>(element.inputSlotClass));
			}
			fprintf(file, "  InstanceDataStepRate: %u\n", element.instanceDataStepRate);
		}
	}

	static void WriteVertexElement(
		FILE *file, const unsigned char *vertexData, const DX12PsoInputElementInfo &element,
		UINT slot, UINT vertexIndex, UINT stride)
	{
		if (!file || !vertexData || element.inputSlot != slot)
			return;

		const UINT offset = element.resolvedByteOffset;
		fprintf(file, "vb%u[%u]+%03u %s", slot, vertexIndex, offset, element.semanticName.c_str());
		if (element.semanticIndex)
			fprintf(file, "%u", element.semanticIndex);
		fprintf(file, ": ");

		const UINT size = DX12PsoInputLayoutUtils::FormatSize(element.format);
		if (size && offset + size <= stride) {
			DX12PsoInputLayoutUtils::WriteFormatValue(file, element.format, vertexData + offset);
			fprintf(file, "\n");
		} else {
			fprintf(file, "\n");
			if (!size)
				fprintf(file, "# WARNING: Unknown format size, vertex buffer may be decoded incorrectly\n");
			if (offset + size > stride)
				fprintf(file, "# WARNING: Offset exceeded stride, vertex buffer may be decoded incorrectly\n");
		}
	}

	static void WriteKnownVertexData(
		FILE *file, const DumpTask &task, const unsigned char *data, UINT start, UINT end)
	{
		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		for (UINT vertex = start; vertex < end; ++vertex) {
			fprintf(file, "\n");
			const unsigned char *vertexData = data + static_cast<size_t>(buffer.stride) * vertex;
			for (UINT elementIndex = 0; elementIndex < buffer.inputLayout.elements.size(); ++elementIndex) {
				const DX12PsoInputElementInfo &element = buffer.inputLayout.elements[elementIndex];
				if (element.inputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA)
					continue;
				WriteVertexElement(file, vertexData, element, buffer.slot, vertex - start, buffer.stride);
			}
		}
	}

	static void WriteKnownInstanceData(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		if (!buffer.stride || !buffer.drawInstanceCount)
			return;

		const UINT elementCount = static_cast<UINT>(task.copyBytes / buffer.stride);
		const UINT start = buffer.drawStartInstance;
		UINT end = elementCount;
		if (start < end)
			end = (std::min<UINT>)(end, start + buffer.drawInstanceCount);
		else
			return;

		for (UINT instance = start; instance < end; ++instance) {
			fprintf(file, "\n");
			for (UINT elementIndex = 0; elementIndex < buffer.inputLayout.elements.size(); ++elementIndex) {
				const DX12PsoInputElementInfo &element = buffer.inputLayout.elements[elementIndex];
				if (element.inputSlotClass != D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA)
					continue;
				UINT index = instance;
				if (element.instanceDataStepRate)
					index = (instance - start) / element.instanceDataStepRate + start;
				if (index >= elementCount)
					continue;
				const unsigned char *instanceData = data + static_cast<size_t>(buffer.stride) * index;
				WriteVertexElement(file, instanceData, element, buffer.slot, index - start, buffer.stride);
			}
		}
	}

	static void WriteUnknownVertexData(
		FILE *file, const DX12FrameIaBufferBinding &buffer, const unsigned char *data,
		UINT start, UINT end)
	{
		const float *floats = reinterpret_cast<const float*>(data);
		const uint32_t *words = reinterpret_cast<const uint32_t*>(data);
		const uint8_t *bytes = reinterpret_cast<const uint8_t*>(data);
		for (UINT vertex = start; vertex < end; ++vertex) {
			fprintf(file, "\n");
			UINT j = 0;
			for (; j < buffer.stride / 4; ++j) {
				const UINT wordIndex = vertex * buffer.stride / 4 + j;
				fprintf(file, "vb%u[%u]+%03u: 0x%08x %.9g\n",
					buffer.slot, vertex - start, j * 4,
					words[wordIndex], floats[wordIndex]);
			}
			for (j = j * 4; j < buffer.stride; ++j) {
				const UINT byteIndex = vertex * buffer.stride + j;
				fprintf(file, "vb%u[%u]+%03u: 0x%02x\n",
					buffer.slot, vertex - start, j, bytes[byteIndex]);
			}
		}
	}

	static bool WriteVertexBufferText(FILE *file, const DumpTask &task, const unsigned char *data)
	{
		if (!file || !data)
			return false;

		const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
		const UINT stride = buffer.stride;
		if (task.sourceOffset)
			fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
		fprintf(file, "stride: %u\n", stride);
		if (buffer.drawStartIndex || buffer.drawIndexCount) {
			fprintf(file, "first vertex: %u\n", buffer.drawStartIndex);
			fprintf(file, "vertex count: %u\n", buffer.drawIndexCount);
		}
		if (buffer.drawStartInstance || buffer.drawInstanceCount) {
			fprintf(file, "first instance: %u\n", buffer.drawStartInstance);
			fprintf(file, "instance count: %u\n", buffer.drawInstanceCount);
		}
		if (buffer.topology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED)
			fprintf(file, "topology: %s\n", TopologyName(buffer.topology));

		bool perVertex = false;
		bool perInstance = false;
		if (!buffer.inputLayout.elements.empty())
			WriteInputLayout(file, buffer.inputLayout, buffer.slot, &perVertex, &perInstance);
		if (!stride)
			return true;

		const UINT vertexCount = static_cast<UINT>(task.copyBytes / stride);
		UINT start = buffer.drawStartIndex;
		if (start > vertexCount)
			start = vertexCount;
		UINT end = vertexCount;
		if (buffer.drawIndexCount)
			end = (std::min<UINT>)(end, start + buffer.drawIndexCount);

		if (!buffer.inputLayout.elements.empty()) {
			if (perVertex) {
				fprintf(file, "\nvertex-data:\n");
				WriteKnownVertexData(file, task, data, start, end);
			}
			if (perInstance) {
				fprintf(file, "\ninstance-data:\n");
				WriteKnownInstanceData(file, task, data);
			}
		} else {
			WriteUnknownVertexData(file, buffer, data, start, end);
		}
		return true;
	}

	static bool WriteBufferTextFile(FILE *file, const DumpTask &task, const void *mappedData)
	{
		if (!file || !mappedData || task.isTexture)
			return false;
		const unsigned char *data =
			static_cast<const unsigned char*>(mappedData) + task.readbackOffset;
		if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == IaBufferRole::IB)
			return WriteIndexBufferText(file, task, data);
		if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == IaBufferRole::VB)
			return WriteVertexBufferText(file, task, data);
		return WriteGenericBufferText(file, task, data);
	}
};



class DX12ResourceDumpDedupedNamingUtils
{
public:
	static const wchar_t *ExtensionForTask(const DumpTask &task)
	{
		return task.isTexture ? L"dds" : L"buf";
	}

	static DXGI_FORMAT TextureFormatForTask(const DumpTask &task)
	{
		UINT viewFormat = 0;
		if (task.sourceKind == DumpTaskSource::Output &&
				task.outputBinding.hasDescriptor &&
				task.outputBinding.descriptor.viewFormat) {
			viewFormat = task.outputBinding.descriptor.viewFormat;
		} else if (task.binding.hasDescriptor && task.binding.descriptor.viewFormat) {
			viewFormat = task.binding.descriptor.viewFormat;
		}
		return viewFormat ? static_cast<DXGI_FORMAT>(viewFormat) : task.desc.Format;
	}

	static const wchar_t *TextureFormatName(DXGI_FORMAT format)
	{
		switch (format) {
		case DXGI_FORMAT_UNKNOWN: return L"DXGI_FORMAT_UNKNOWN";
		case DXGI_FORMAT_R32G32B32A32_TYPELESS: return L"DXGI_FORMAT_R32G32B32A32_TYPELESS";
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return L"DXGI_FORMAT_R32G32B32A32_FLOAT";
		case DXGI_FORMAT_R32G32B32A32_UINT: return L"DXGI_FORMAT_R32G32B32A32_UINT";
		case DXGI_FORMAT_R32G32B32A32_SINT: return L"DXGI_FORMAT_R32G32B32A32_SINT";
		case DXGI_FORMAT_R32G32B32_TYPELESS: return L"DXGI_FORMAT_R32G32B32_TYPELESS";
		case DXGI_FORMAT_R32G32B32_FLOAT: return L"DXGI_FORMAT_R32G32B32_FLOAT";
		case DXGI_FORMAT_R32G32B32_UINT: return L"DXGI_FORMAT_R32G32B32_UINT";
		case DXGI_FORMAT_R32G32B32_SINT: return L"DXGI_FORMAT_R32G32B32_SINT";
		case DXGI_FORMAT_R16G16B16A16_TYPELESS: return L"DXGI_FORMAT_R16G16B16A16_TYPELESS";
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return L"DXGI_FORMAT_R16G16B16A16_FLOAT";
		case DXGI_FORMAT_R16G16B16A16_UNORM: return L"DXGI_FORMAT_R16G16B16A16_UNORM";
		case DXGI_FORMAT_R16G16B16A16_UINT: return L"DXGI_FORMAT_R16G16B16A16_UINT";
		case DXGI_FORMAT_R16G16B16A16_SNORM: return L"DXGI_FORMAT_R16G16B16A16_SNORM";
		case DXGI_FORMAT_R16G16B16A16_SINT: return L"DXGI_FORMAT_R16G16B16A16_SINT";
		case DXGI_FORMAT_R32G32_TYPELESS: return L"DXGI_FORMAT_R32G32_TYPELESS";
		case DXGI_FORMAT_R32G32_FLOAT: return L"DXGI_FORMAT_R32G32_FLOAT";
		case DXGI_FORMAT_R32G32_UINT: return L"DXGI_FORMAT_R32G32_UINT";
		case DXGI_FORMAT_R32G32_SINT: return L"DXGI_FORMAT_R32G32_SINT";
		case DXGI_FORMAT_R32G8X24_TYPELESS: return L"DXGI_FORMAT_R32G8X24_TYPELESS";
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return L"DXGI_FORMAT_D32_FLOAT_S8X24_UINT";
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return L"DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS";
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return L"DXGI_FORMAT_X32_TYPELESS_G8X24_UINT";
		case DXGI_FORMAT_R10G10B10A2_TYPELESS: return L"DXGI_FORMAT_R10G10B10A2_TYPELESS";
		case DXGI_FORMAT_R10G10B10A2_UNORM: return L"DXGI_FORMAT_R10G10B10A2_UNORM";
		case DXGI_FORMAT_R10G10B10A2_UINT: return L"DXGI_FORMAT_R10G10B10A2_UINT";
		case DXGI_FORMAT_R11G11B10_FLOAT: return L"DXGI_FORMAT_R11G11B10_FLOAT";
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return L"DXGI_FORMAT_R8G8B8A8_TYPELESS";
		case DXGI_FORMAT_R8G8B8A8_UNORM: return L"DXGI_FORMAT_R8G8B8A8_UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return L"DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
		case DXGI_FORMAT_R8G8B8A8_UINT: return L"DXGI_FORMAT_R8G8B8A8_UINT";
		case DXGI_FORMAT_R8G8B8A8_SNORM: return L"DXGI_FORMAT_R8G8B8A8_SNORM";
		case DXGI_FORMAT_R8G8B8A8_SINT: return L"DXGI_FORMAT_R8G8B8A8_SINT";
		case DXGI_FORMAT_R16G16_TYPELESS: return L"DXGI_FORMAT_R16G16_TYPELESS";
		case DXGI_FORMAT_R16G16_FLOAT: return L"DXGI_FORMAT_R16G16_FLOAT";
		case DXGI_FORMAT_R16G16_UNORM: return L"DXGI_FORMAT_R16G16_UNORM";
		case DXGI_FORMAT_R16G16_UINT: return L"DXGI_FORMAT_R16G16_UINT";
		case DXGI_FORMAT_R16G16_SNORM: return L"DXGI_FORMAT_R16G16_SNORM";
		case DXGI_FORMAT_R16G16_SINT: return L"DXGI_FORMAT_R16G16_SINT";
		case DXGI_FORMAT_R32_TYPELESS: return L"DXGI_FORMAT_R32_TYPELESS";
		case DXGI_FORMAT_D32_FLOAT: return L"DXGI_FORMAT_D32_FLOAT";
		case DXGI_FORMAT_R32_FLOAT: return L"DXGI_FORMAT_R32_FLOAT";
		case DXGI_FORMAT_R32_UINT: return L"DXGI_FORMAT_R32_UINT";
		case DXGI_FORMAT_R32_SINT: return L"DXGI_FORMAT_R32_SINT";
		case DXGI_FORMAT_R24G8_TYPELESS: return L"DXGI_FORMAT_R24G8_TYPELESS";
		case DXGI_FORMAT_D24_UNORM_S8_UINT: return L"DXGI_FORMAT_D24_UNORM_S8_UINT";
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return L"DXGI_FORMAT_R24_UNORM_X8_TYPELESS";
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return L"DXGI_FORMAT_X24_TYPELESS_G8_UINT";
		case DXGI_FORMAT_R8G8_TYPELESS: return L"DXGI_FORMAT_R8G8_TYPELESS";
		case DXGI_FORMAT_R8G8_UNORM: return L"DXGI_FORMAT_R8G8_UNORM";
		case DXGI_FORMAT_R8G8_UINT: return L"DXGI_FORMAT_R8G8_UINT";
		case DXGI_FORMAT_R8G8_SNORM: return L"DXGI_FORMAT_R8G8_SNORM";
		case DXGI_FORMAT_R8G8_SINT: return L"DXGI_FORMAT_R8G8_SINT";
		case DXGI_FORMAT_R16_TYPELESS: return L"DXGI_FORMAT_R16_TYPELESS";
		case DXGI_FORMAT_R16_FLOAT: return L"DXGI_FORMAT_R16_FLOAT";
		case DXGI_FORMAT_D16_UNORM: return L"DXGI_FORMAT_D16_UNORM";
		case DXGI_FORMAT_R16_UNORM: return L"DXGI_FORMAT_R16_UNORM";
		case DXGI_FORMAT_R16_UINT: return L"DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R16_SNORM: return L"DXGI_FORMAT_R16_SNORM";
		case DXGI_FORMAT_R16_SINT: return L"DXGI_FORMAT_R16_SINT";
		case DXGI_FORMAT_R8_TYPELESS: return L"DXGI_FORMAT_R8_TYPELESS";
		case DXGI_FORMAT_R8_UNORM: return L"DXGI_FORMAT_R8_UNORM";
		case DXGI_FORMAT_R8_UINT: return L"DXGI_FORMAT_R8_UINT";
		case DXGI_FORMAT_R8_SNORM: return L"DXGI_FORMAT_R8_SNORM";
		case DXGI_FORMAT_R8_SINT: return L"DXGI_FORMAT_R8_SINT";
		case DXGI_FORMAT_A8_UNORM: return L"DXGI_FORMAT_A8_UNORM";
		case DXGI_FORMAT_R1_UNORM: return L"DXGI_FORMAT_R1_UNORM";
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return L"DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
		case DXGI_FORMAT_R8G8_B8G8_UNORM: return L"DXGI_FORMAT_R8G8_B8G8_UNORM";
		case DXGI_FORMAT_G8R8_G8B8_UNORM: return L"DXGI_FORMAT_G8R8_G8B8_UNORM";
		case DXGI_FORMAT_BC1_TYPELESS: return L"DXGI_FORMAT_BC1_TYPELESS";
		case DXGI_FORMAT_BC1_UNORM: return L"DXGI_FORMAT_BC1_UNORM";
		case DXGI_FORMAT_BC1_UNORM_SRGB: return L"DXGI_FORMAT_BC1_UNORM_SRGB";
		case DXGI_FORMAT_BC2_TYPELESS: return L"DXGI_FORMAT_BC2_TYPELESS";
		case DXGI_FORMAT_BC2_UNORM: return L"DXGI_FORMAT_BC2_UNORM";
		case DXGI_FORMAT_BC2_UNORM_SRGB: return L"DXGI_FORMAT_BC2_UNORM_SRGB";
		case DXGI_FORMAT_BC3_TYPELESS: return L"DXGI_FORMAT_BC3_TYPELESS";
		case DXGI_FORMAT_BC3_UNORM: return L"DXGI_FORMAT_BC3_UNORM";
		case DXGI_FORMAT_BC3_UNORM_SRGB: return L"DXGI_FORMAT_BC3_UNORM_SRGB";
		case DXGI_FORMAT_BC4_TYPELESS: return L"DXGI_FORMAT_BC4_TYPELESS";
		case DXGI_FORMAT_BC4_UNORM: return L"DXGI_FORMAT_BC4_UNORM";
		case DXGI_FORMAT_BC4_SNORM: return L"DXGI_FORMAT_BC4_SNORM";
		case DXGI_FORMAT_BC5_TYPELESS: return L"DXGI_FORMAT_BC5_TYPELESS";
		case DXGI_FORMAT_BC5_UNORM: return L"DXGI_FORMAT_BC5_UNORM";
		case DXGI_FORMAT_BC5_SNORM: return L"DXGI_FORMAT_BC5_SNORM";
		case DXGI_FORMAT_B5G6R5_UNORM: return L"DXGI_FORMAT_B5G6R5_UNORM";
		case DXGI_FORMAT_B5G5R5A1_UNORM: return L"DXGI_FORMAT_B5G5R5A1_UNORM";
		case DXGI_FORMAT_B8G8R8A8_UNORM: return L"DXGI_FORMAT_B8G8R8A8_UNORM";
		case DXGI_FORMAT_B8G8R8X8_UNORM: return L"DXGI_FORMAT_B8G8R8X8_UNORM";
		case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return L"DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM";
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: return L"DXGI_FORMAT_B8G8R8A8_TYPELESS";
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return L"DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
		case DXGI_FORMAT_B8G8R8X8_TYPELESS: return L"DXGI_FORMAT_B8G8R8X8_TYPELESS";
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return L"DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
		case DXGI_FORMAT_BC6H_TYPELESS: return L"DXGI_FORMAT_BC6H_TYPELESS";
		case DXGI_FORMAT_BC6H_UF16: return L"DXGI_FORMAT_BC6H_UF16";
		case DXGI_FORMAT_BC6H_SF16: return L"DXGI_FORMAT_BC6H_SF16";
		case DXGI_FORMAT_BC7_TYPELESS: return L"DXGI_FORMAT_BC7_TYPELESS";
		case DXGI_FORMAT_BC7_UNORM: return L"DXGI_FORMAT_BC7_UNORM";
		case DXGI_FORMAT_BC7_UNORM_SRGB: return L"DXGI_FORMAT_BC7_UNORM_SRGB";
		case DXGI_FORMAT_AYUV: return L"DXGI_FORMAT_AYUV";
		case DXGI_FORMAT_Y410: return L"DXGI_FORMAT_Y410";
		case DXGI_FORMAT_Y416: return L"DXGI_FORMAT_Y416";
		case DXGI_FORMAT_NV12: return L"DXGI_FORMAT_NV12";
		case DXGI_FORMAT_P010: return L"DXGI_FORMAT_P010";
		case DXGI_FORMAT_P016: return L"DXGI_FORMAT_P016";
		case DXGI_FORMAT_420_OPAQUE: return L"DXGI_FORMAT_420_OPAQUE";
		case DXGI_FORMAT_YUY2: return L"DXGI_FORMAT_YUY2";
		case DXGI_FORMAT_Y210: return L"DXGI_FORMAT_Y210";
		case DXGI_FORMAT_Y216: return L"DXGI_FORMAT_Y216";
		case DXGI_FORMAT_NV11: return L"DXGI_FORMAT_NV11";
		case DXGI_FORMAT_AI44: return L"DXGI_FORMAT_AI44";
		case DXGI_FORMAT_IA44: return L"DXGI_FORMAT_IA44";
		case DXGI_FORMAT_P8: return L"DXGI_FORMAT_P8";
		case DXGI_FORMAT_A8P8: return L"DXGI_FORMAT_A8P8";
		case DXGI_FORMAT_B4G4R4A4_UNORM: return L"DXGI_FORMAT_B4G4R4A4_UNORM";
		default:
			return nullptr;
		}
	}

	static void BuildTextureFormatName(DXGI_FORMAT format, wchar_t *name, size_t nameCount)
	{
		if (!name || nameCount == 0)
			return;
		const wchar_t *formatName = TextureFormatName(format);
		if (formatName)
			wcsncpy_s(name, nameCount, formatName, _TRUNCATE);
		else
			swprintf_s(name, nameCount, L"DXGI_FORMAT_%u", static_cast<UINT>(format));
	}

	static void BuildDedupedFileName(
		const DumpTask &task, uint32_t huntHash, uint32_t fileHash,
		wchar_t *fileName, size_t fileNameCount)
	{
		if (!fileName || fileNameCount == 0)
			return;
		fileName[0] = L'\0';

		const wchar_t *extension = ExtensionForTask(task);
		if (task.isTexture) {
			wchar_t formatName[96];
			BuildTextureFormatName(TextureFormatForTask(task), formatName, ARRAYSIZE(formatName));
			swprintf_s(fileName, fileNameCount, L"%08x_%08x-%s.%s",
				static_cast<UINT>(huntHash),
				static_cast<UINT>(fileHash),
				formatName,
				extension);
			return;
		}
		swprintf_s(fileName, fileNameCount, L"%08x.%s", static_cast<UINT>(fileHash), extension);
	}
};
