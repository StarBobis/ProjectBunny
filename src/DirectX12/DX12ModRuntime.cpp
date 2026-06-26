#include "DX12ModRuntime.h"

#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DX12State.h"
#include "DX12CommandListRuntime.h"
#include "DX12DeviceHooks.h"
#include "DX12Profiling.h"
#include "DX12ShaderDump.h"
#include "util.h"
#include "IniDocument.h"
#include "MigotoCommandList.h"
#include "MigotoIniLoader.h"
#include "MigotoResource.h"
#include "MigotoShaderOverride.h"
#include "MigotoTextureOverride.h"

static DXGI_FORMAT ParseDx12ResourceFormat(const std::wstring &format)
{
	std::wstring value = Bunny::ToLower(Bunny::Trim(format));
	if (value.empty())
		return DXGI_FORMAT_UNKNOWN;
	if (value.rfind(L"dxgi_format_", 0) == 0)
		value = value.substr(12);
	if (value == L"r16_uint")
		return DXGI_FORMAT_R16_UINT;
	if (value == L"r32_uint")
		return DXGI_FORMAT_R32_UINT;

	wchar_t *end = nullptr;
	unsigned long numeric = wcstoul(value.c_str(), &end, 10);
	if (end && *end == L'\0')
		return static_cast<DXGI_FORMAT>(numeric);
	return DXGI_FORMAT_UNKNOWN;
}

static SRWLOCK gModLock = SRWLOCK_INIT;
static bool gLoaded = false;
static std::wstring gConfigPath;
static std::wstring gBaseDir;
static std::wstring gShaderFixesDir;
static Bunny::ShaderOverrideMap gShaderOverrides;
static Bunny::TextureOverrideMap gTextureOverrides;
static Bunny::ResourceMap gResources;
static Bunny::CommandListMap gCommandLists;
static std::vector<std::wstring> gVlrResourceCandidates;
static std::unordered_map<std::wstring, bool> gCommandListRuntimeEffect;
static std::unordered_map<std::wstring, bool> gTextureOverridePreRuntimeEffect;
static std::unordered_map<std::wstring, bool> gTextureOverridePostRuntimeEffect;
static std::unordered_map<std::wstring, uint32_t> gTextureOverrideSectionIds;
struct DX12RelatedTextureOverrideCandidate
{
	Bunny::TextureOverrideConfig config;
	std::set<std::wstring> tokens;
	uint32_t sectionId = 0;
};
static std::vector<DX12RelatedTextureOverrideCandidate> gRelatedTextureOverrides;
static std::unordered_map<std::wstring, std::vector<size_t>> gRelatedTextureOverrideTokenIndex;
struct DX12IaTextureOverrideCandidate
{
	Bunny::TextureOverrideConfig config;
	UINT64 order = 0;
	uint32_t sectionId = 0;
	bool preRuntimeEffect = true;
	bool postRuntimeEffect = true;
};
static std::vector<DX12IaTextureOverrideCandidate> gIaTextureOverrides;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaIndexTextureOverrideIndex;
static std::unordered_map<UINT64, std::vector<size_t>> gIaVertexTextureOverrideIndex;
static std::unordered_map<uint32_t, std::vector<size_t>> gIaAnyVertexTextureOverrideIndex;
static std::unordered_map<UINT64, std::vector<size_t>> gIaMergedVertexTextureOverrideIndex;
struct DX12TriggeredTextureOverride
{
	const Bunny::TextureOverrideConfig *config = nullptr;
	Bunny::TextureOverrideConfig fallbackConfig;
	uint32_t sectionId = 0;
	bool postRuntimeEffect = true;
	bool indexBuffer = false;
	uint32_t vertexSlot = 0;
	bool executeCommands = false;
	bool executeDrawActions = true;
	bool directIaAnchor = false;
	bool relatedMesh = false;
	bool preSkinAnchor = false;
	ID3D12Resource *preSkinResource = nullptr;
	UINT64 preSkinByteWidth = 0;
	UINT preSkinStride = 0;
};

static const Bunny::TextureOverrideConfig &TriggeredTextureOverrideConfig(
	const DX12TriggeredTextureOverride &entry)
{
	return entry.config ? *entry.config : entry.fallbackConfig;
}

static uint64_t HashCombine64(uint64_t hash, uint64_t value)
{
	hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
	return hash;
}

struct DX12CompiledVertexResourceBinding
{
	uint32_t slot = 0;
	std::wstring resource;
};

struct DX12CompiledTextureOverridePlan
{
	bool hasActions = false;
	bool hasUnsupportedCommandActions = false;
	bool handlingSkip = false;
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
	std::vector<DX12ModIaReplacement::DrawCall> draws;
	std::vector<DX12ModIaReplacement::DispatchCall> dispatches;
};
static std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> gCompiledTextureOverridePlans;

struct DX12CompiledCommandListPlan
{
	bool unsupported = false;
	bool handlingSkip = false;
	std::wstring indexResource;
	std::vector<DX12CompiledVertexResourceBinding> vertexResources;
	std::vector<DX12ModIaReplacement::DrawCall> draws;
	std::vector<DX12ModIaReplacement::DispatchCall> dispatches;
};
static std::unordered_map<std::wstring, DX12CompiledCommandListPlan> gCompiledCommandListPlans;

struct DX12VertexLimitRaiseConfig
{
	std::wstring section;
	bool hasMatchCs = false;
	uint64_t matchCs = 0;
	uint32_t overrideByteStride = 0;
	uint32_t overrideVertexCount = 0;
	UINT64 overrideByteWidth = 0;
	uint32_t uavByteStride = 0;
	UINT64 overrideNumElements = 0;
};
static std::vector<DX12VertexLimitRaiseConfig> gVertexLimitRaiseConfigs;
static UINT64 gReloadGeneration = 1;
static volatile LONG gHasShaderOverrides = 0;
static volatile LONG gHasTextureOverrides = 0;
static volatile LONG gHasUavHashTextureOverrides = 0;
static volatile LONG gHasExplicitPreSkinMatchCsUavBytes = 0;
static volatile LONG gHasPresentRuntimeEffect = 0;
static bool gHasPreSkinVlrWithoutMatchCs = false;
static std::unordered_set<uint64_t> gPreSkinMatchCsHashes;
static std::unordered_map<uint64_t, std::unordered_set<UINT64>> gPreSkinMatchCsUavByteFilters;
struct DX12PreSkinUavMatchCacheEntry
{
	uint64_t computeShaderHash = 0;
	UINT64 computeBindingSerial = 0;
	bool valid = false;
	bool matched = false;
};
static std::unordered_map<ID3D12GraphicsCommandList*, DX12PreSkinUavMatchCacheEntry> gPreSkinUavMatchCache;
static volatile LONG gDx12SafeMode = 0;
static std::unordered_map<uint64_t, bool> gIaSkipCache;
static std::unordered_map<uint64_t, bool> gIaTextureCandidateCache;
static std::unordered_map<uint64_t, std::vector<DX12TriggeredTextureOverride>> gIaReplacementMatchCache;
static std::unordered_map<uint64_t, DX12ModIaReplacement> gIaReplacementPreparedFrameCache;
static LONG gIaReplacementPrepareCachePresent = -1;
static volatile LONG gPresentCommandListExecutedPresent = -1;
static UINT64 gPreSkinActiveGeneration = 1;
static UINT64 gPreSkinSrvCacheGeneration = 1;
struct DX12ShaderOverridePsoMatchCache
{
	UINT64 generation = 0;
	std::vector<const Bunny::ShaderOverrideConfig*> drawConfigs;
	std::vector<const Bunny::ShaderOverrideConfig*> dispatchConfigs;
};
static std::unordered_map<ID3D12PipelineState*, DX12ShaderOverridePsoMatchCache> gShaderOverridePsoMatchCache;

struct DX12ComputeUavProducer
{
	UINT64 serial = 0;
	UINT rootParameterIndex = 0;
	UINT rangeIndex = UINT_MAX;
	UINT shaderRegister = UINT_MAX;
	UINT registerSpace = 0;
	UINT descriptorOffset = 0;
	bool rootDescriptor = false;
	UINT64 gpuVirtualAddress = 0;
	DX12DescriptorSummary descriptor;
	bool hasDescriptor = false;
};

static SRWLOCK gPreSkinLock = SRWLOCK_INIT;
static UINT64 gComputeUavSerial = 0;
static std::vector<DX12ComputeUavProducer> gRecentComputeUavs;
static constexpr size_t MaxRecentComputeUavs = 512;
static std::set<SIZE_T> gPatchedPreSkinDescriptors;
static std::map<SIZE_T, std::wstring> gKnownPreSkinDescriptorPatches;
struct DX12ActivePreSkinTextureOverride
{
	Bunny::TextureOverrideConfig config;
	DX12ComputeUavProducer producer;
	ID3D12Resource *outputResource = nullptr;
	UINT64 outputByteWidth = 0;
	UINT outputStride = 0;
};

static std::map<std::wstring, DX12ActivePreSkinTextureOverride> gActivePreSkinTextureOverrides;
static void ReleaseActivePreSkinTextureOverride(DX12ActivePreSkinTextureOverride *active)
{
	if (!active)
		return;
	if (active->outputResource) {
		active->outputResource->Release();
		active->outputResource = nullptr;
	}
	active->outputByteWidth = 0;
	active->outputStride = 0;
}

static void ClearActivePreSkinTextureOverridesLocked()
{
	for (auto &item : gActivePreSkinTextureOverrides)
		ReleaseActivePreSkinTextureOverride(&item.second);
	gActivePreSkinTextureOverrides.clear();
}

static void StoreActivePreSkinTextureOverrideLocked(
	const std::wstring &section, const DX12ActivePreSkinTextureOverride &active)
{
	auto existing = gActivePreSkinTextureOverrides.find(section);
	if (existing != gActivePreSkinTextureOverrides.end())
		ReleaseActivePreSkinTextureOverride(&existing->second);
	DX12ActivePreSkinTextureOverride stored = active;
	if (stored.outputResource)
		stored.outputResource->AddRef();
	gActivePreSkinTextureOverrides[section] = stored;
}
static std::map<std::wstring, LONG> gPreSkinSectionAppliedPresent;

struct DX12PreSkinCbvReadCacheKey
{
	ID3D12Resource *resource = nullptr;
	UINT64 offset = 0;
	UINT size = 0;
	LONG present = -1;

	bool operator<(const DX12PreSkinCbvReadCacheKey &rhs) const
	{
		if (resource != rhs.resource)
			return resource < rhs.resource;
		if (offset != rhs.offset)
			return offset < rhs.offset;
		if (size != rhs.size)
			return size < rhs.size;
		return present < rhs.present;
	}
};

struct DX12PreSkinCbvReadCacheValue
{
	HRESULT hr = E_FAIL;
	std::vector<unsigned char> bytes;
};

struct DX12LoadedResource;

struct DX12PreSkinSrvPositiveCacheValue
{
	DX12LoadedResource *resource = nullptr;
	UINT elementStride = 0;
	UINT64 srvByteWidth = 0;
	uint32_t vbSlot = UINT_MAX;
};

static std::map<DX12PreSkinCbvReadCacheKey, DX12PreSkinCbvReadCacheValue> gPreSkinCbvReadCache;
static std::set<UINT64> gPreSkinSrvNegativeCache;
static std::set<UINT64> gPreSkinSrvStableNegativeCache;
static std::map<UINT64, DX12PreSkinSrvPositiveCacheValue> gPreSkinSrvPositiveCache;
static std::map<UINT64, DX12PreSkinSrvPositiveCacheValue> gPreSkinSrvStablePositiveCache;

static UINT64 MakePreSkinSrvStableProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth)
{
	UINT64 key = 14695981039346656037ull;
	auto append = [&key](UINT64 value) {
		for (size_t i = 0; i < sizeof(value); ++i) {
			key ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
			key *= 1099511628211ull;
		}
	};
	append(static_cast<UINT64>(gPreSkinSrvCacheGeneration));
	append(static_cast<UINT64>(srv.rootParameterIndex));
	append(static_cast<UINT64>(srv.rangeIndex));
	append(static_cast<UINT64>(srv.shaderRegister));
	append(static_cast<UINT64>(srv.registerSpace));
	append(static_cast<UINT64>(srv.descriptorOffset));
	append(static_cast<UINT64>(srv.descriptor.viewDimension));
	append(static_cast<UINT64>(srv.descriptor.structureByteStride));
	append(static_cast<UINT64>(srv.descriptor.gpuVirtualAddress));
	append(srvByteWidth);
	append(reinterpret_cast<UINT64>(srv.descriptor.resource));
	for (wchar_t ch : config.section) {
		key ^= static_cast<unsigned char>(ch & 0xff);
		key *= 1099511628211ull;
		key ^= static_cast<unsigned char>((ch >> 8) & 0xff);
		key *= 1099511628211ull;
	}
	return key;
}

static UINT64 MakePreSkinSrvProbeCacheKey(
	const DX12CurrentComputeUavBinding &srv,
	const Bunny::TextureOverrideConfig &config,
	UINT64 srvByteWidth)
{
	UINT64 key = 14695981039346656037ull;
	auto append = [&key](UINT64 value) {
		for (size_t i = 0; i < sizeof(value); ++i) {
			key ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
			key *= 1099511628211ull;
		}
	};
	append(static_cast<UINT64>(DX12GetPresentCount()));
	append(static_cast<UINT64>(gPreSkinSrvCacheGeneration));
	append(static_cast<UINT64>(srv.rootParameterIndex));
	append(static_cast<UINT64>(srv.rangeIndex));
	append(static_cast<UINT64>(srv.shaderRegister));
	append(static_cast<UINT64>(srv.registerSpace));
	append(static_cast<UINT64>(srv.descriptorOffset));
	append(static_cast<UINT64>(srv.descriptor.viewDimension));
	append(static_cast<UINT64>(srv.descriptor.structureByteStride));
	append(static_cast<UINT64>(srv.descriptor.gpuVirtualAddress));
	append(srvByteWidth);
	append(reinterpret_cast<UINT64>(srv.descriptor.resource));
	for (wchar_t ch : config.section) {
		key ^= static_cast<unsigned char>(ch & 0xff);
		key *= 1099511628211ull;
		key ^= static_cast<unsigned char>((ch >> 8) & 0xff);
		key *= 1099511628211ull;
	}
	return key;
}

struct DX12UavHashTextureOverrideMatch
{
	Bunny::TextureOverrideConfig config;
	uint32_t hash = 0;
	std::wstring resourceName;
};

struct DX12PreSkinTextureOverrideCandidate
{
	Bunny::TextureOverrideConfig config;
	uint32_t hash = 0;
	std::wstring resourceName;
	UINT64 order = 0;
};

static std::vector<DX12PreSkinTextureOverrideCandidate> gPreSkinTextureOverrides;
static std::unordered_map<uint32_t, std::vector<size_t>> gPreSkinUavHashTextureOverrideIndex;
static std::unordered_map<uint64_t, std::vector<size_t>> gPreSkinMatchCsTextureOverrideIndex;
static std::unordered_map<std::wstring, std::vector<std::wstring>> gExplicitMatchCsResourceSections;

struct DX12LoadedResource
{
	ID3D12Resource *resource = nullptr;
	ID3D12Resource *uavResource = nullptr;
	ID3D12Resource *srvResource = nullptr;
	ID3D12DescriptorHeap *uavHeap = nullptr;
	ID3D12DescriptorHeap *srvHeap = nullptr;
	bool failed = false;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
	bool uavInitialized = false;
	bool uavWritten = false;
	bool uavValid = false;
	bool uavBarrierPending = false;
	UINT64 uavByteWidth = 0;
	UINT64 srvByteWidth = 0;
	UINT srvStride = 0;
	D3D12_RESOURCE_STATES uavState = D3D12_RESOURCE_STATE_COPY_DEST;
	std::wstring name;
	std::wstring path;
};

static std::unordered_map<std::wstring, DX12LoadedResource> gLoadedResources;

struct DX12RetiredLoadedResource
{
	DX12LoadedResource resource;
	LONG retirePresent = 0;
};
static std::vector<DX12RetiredLoadedResource> gRetiredLoadedResources;
static constexpr LONG RetiredLoadedResourcePresentDelay = 8;

struct DX12PreSkinReplacementState
{
	bool active = false;
	UINT rootParameterIndex = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE originalTable = {};
	D3D12_GPU_DESCRIPTOR_HANDLE replacementTable = {};
	ID3D12DescriptorHeap *descriptorHeap = nullptr;
	ID3D12DescriptorHeap *originalHeap = nullptr;
	ID3D12Resource *resource = nullptr;
	UINT64 byteWidth = 0;
	UINT stride = 0;
	UINT64 producerSerial = 0;
	struct DescriptorRestore
	{
		SIZE_T targetCpu = 0;
		ID3D12DescriptorHeap *heap = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
	};
	std::vector<DescriptorRestore> descriptorRestores;
	struct TableRestore
	{
		UINT rootParameterIndex = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE originalTable = {};
	};
	std::vector<TableRestore> tableRestores;
	ID3D12DescriptorHeap *patchHeap = nullptr;
	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	std::vector<ID3D12Resource*> temporaryResources;
};

static std::unordered_map<ID3D12GraphicsCommandList*, DX12PreSkinReplacementState> gActivePreSkinReplacements;
struct DX12RetiredPreSkinResource
{
	ID3D12Resource *resource = nullptr;
	ID3D12Fence *fence = nullptr;
	UINT64 fenceValue = 0;
};
static std::vector<DX12RetiredPreSkinResource> gRetiredPreSkinResources;

struct DX12PreSkinDescriptorRing
{
	ID3D12Device *device = nullptr;
	ID3D12DescriptorHeap *heap = nullptr;
	UINT capacity = 0;
	UINT offset = 0;
	UINT increment = 0;
	LONG present = -1;
};

struct DX12RetiredPreSkinDescriptorHeap
{
	ID3D12DescriptorHeap *heap = nullptr;
	ID3D12Fence *fence = nullptr;
	UINT64 fenceValue = 0;
};

static SRWLOCK gPreSkinDescriptorRingLock = SRWLOCK_INIT;
static DX12PreSkinDescriptorRing gPreSkinDescriptorRing;
static std::vector<DX12RetiredPreSkinDescriptorHeap> gRetiredPreSkinDescriptorHeaps;
static constexpr UINT PreSkinDescriptorRingInitialCapacity = 4096;

struct DX12PreSkinPendingSubmission
{
	std::vector<ID3D12Resource*> resources;
	std::vector<ID3D12DescriptorHeap*> descriptorHeaps;
};

static SRWLOCK gPreSkinRetireLock = SRWLOCK_INIT;
static ID3D12Fence *gPreSkinRetireFence = nullptr;
static ID3D12Device *gPreSkinRetireFenceDevice = nullptr;
static ID3D12CommandQueue *gPreSkinRetireFenceQueue = nullptr;
static UINT64 gPreSkinRetireFenceNextValue = 0;
static std::unordered_map<IUnknown*, DX12PreSkinPendingSubmission> gPendingPreSkinSubmissions;

static void ReleasePreSkinDescriptorRestores(
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *restores)
{
	if (!restores)
		return;
	for (auto &restore : *restores) {
		if (restore.heap) {
			restore.heap->Release();
			restore.heap = nullptr;
		}
	}
	restores->clear();
}

static bool EnsurePreSkinRetireFenceLocked(ID3D12CommandQueue *queue)
{
	if (!queue)
		return false;
	D3D12_COMMAND_QUEUE_DESC queueDesc = queue->GetDesc();
	if (queueDesc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
		return false;
	ID3D12Device *device = nullptr;
	HRESULT hr = queue->GetDevice(IID_PPV_ARGS(&device));
	if (FAILED(hr) || !device)
		return false;

	if (gPreSkinRetireFence &&
	    gPreSkinRetireFenceDevice == device &&
	    gPreSkinRetireFenceQueue == queue) {
		device->Release();
		return true;
	}

	if (gPreSkinRetireFence)
		gPreSkinRetireFence->Release();
	if (gPreSkinRetireFenceDevice)
		gPreSkinRetireFenceDevice->Release();
	if (gPreSkinRetireFenceQueue)
		gPreSkinRetireFenceQueue->Release();
	gPreSkinRetireFence = nullptr;
	gPreSkinRetireFenceDevice = nullptr;
	gPreSkinRetireFenceQueue = nullptr;
	gPreSkinRetireFenceNextValue = 0;

	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gPreSkinRetireFence));
	if (FAILED(hr) || !gPreSkinRetireFence) {
		device->Release();
		return false;
	}
	gPreSkinRetireFenceDevice = device;
	queue->AddRef();
	gPreSkinRetireFenceQueue = queue;
	return true;
}

static void CleanupRetiredPreSkinObjectsLocked()
{
	auto resourceIt = gRetiredPreSkinResources.begin();
	while (resourceIt != gRetiredPreSkinResources.end()) {
		if (resourceIt->fence &&
		    resourceIt->fence->GetCompletedValue() < resourceIt->fenceValue) {
			++resourceIt;
			continue;
		}
		if (resourceIt->resource)
			resourceIt->resource->Release();
		if (resourceIt->fence)
			resourceIt->fence->Release();
		resourceIt = gRetiredPreSkinResources.erase(resourceIt);
	}

	auto heapIt = gRetiredPreSkinDescriptorHeaps.begin();
	while (heapIt != gRetiredPreSkinDescriptorHeaps.end()) {
		if (heapIt->fence &&
		    heapIt->fence->GetCompletedValue() < heapIt->fenceValue) {
			++heapIt;
			continue;
		}
		if (heapIt->heap)
			heapIt->heap->Release();
		if (heapIt->fence)
			heapIt->fence->Release();
		heapIt = gRetiredPreSkinDescriptorHeaps.erase(heapIt);
	}
}

static IUnknown *PreSkinCommandListIdentity(IUnknown *commandList)
{
	if (!commandList)
		return nullptr;
	IUnknown *identity = nullptr;
	if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&identity))) || !identity)
		return commandList;
	identity->Release();
	return identity;
}

static void ReleasePendingPreSkinSubmission(DX12PreSkinPendingSubmission *pending)
{
	if (!pending)
		return;
	for (ID3D12Resource *resource : pending->resources) {
		if (resource)
			resource->Release();
	}
	for (ID3D12DescriptorHeap *heap : pending->descriptorHeaps) {
		if (heap)
			heap->Release();
	}
	pending->resources.clear();
	pending->descriptorHeaps.clear();
}

static void RequeuePendingPreSkinSubmissionLocked(DX12PreSkinPendingSubmission *pending)
{
	if (!pending)
		return;
	DX12PreSkinPendingSubmission &globalPending = gPendingPreSkinSubmissions[nullptr];
	globalPending.resources.insert(globalPending.resources.end(),
		pending->resources.begin(), pending->resources.end());
	globalPending.descriptorHeaps.insert(globalPending.descriptorHeaps.end(),
		pending->descriptorHeaps.begin(), pending->descriptorHeaps.end());
	pending->resources.clear();
	pending->descriptorHeaps.clear();
}
static void RetirePreSkinResource(ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[nullptr].resources.push_back(resource);
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetirePreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource)
{
	if (!resource)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[PreSkinCommandListIdentity(commandList)].resources.push_back(resource);
	CleanupRetiredPreSkinObjectsLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}

static void RetainPreSkinResourceForCommandList(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource)
{
	if (!resource)
		return;
	resource->AddRef();
	RetirePreSkinResourceForCommandList(commandList, resource);
}

static void RetirePreSkinDescriptorHeapLocked(ID3D12DescriptorHeap *heap)
{
	if (!heap)
		return;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	gPendingPreSkinSubmissions[nullptr].descriptorHeaps.push_back(heap);
	CleanupRetiredPreSkinObjectsLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}
void DX12ModNotifyCommandListsSubmitted(
	ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists)
{
	if (!queue || !commandLists || !numCommandLists)
		return;

	DX12PreSkinPendingSubmission pending;
	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	CleanupRetiredPreSkinObjectsLocked();

	auto globalIt = gPendingPreSkinSubmissions.find(nullptr);
	if (globalIt != gPendingPreSkinSubmissions.end()) {
		pending.resources.insert(pending.resources.end(),
			globalIt->second.resources.begin(), globalIt->second.resources.end());
		pending.descriptorHeaps.insert(pending.descriptorHeaps.end(),
			globalIt->second.descriptorHeaps.begin(), globalIt->second.descriptorHeaps.end());
		gPendingPreSkinSubmissions.erase(globalIt);
	}

	for (UINT i = 0; i < numCommandLists; ++i) {
		auto it = gPendingPreSkinSubmissions.find(PreSkinCommandListIdentity(commandLists[i]));
		if (it == gPendingPreSkinSubmissions.end())
			continue;
		pending.resources.insert(pending.resources.end(),
			it->second.resources.begin(), it->second.resources.end());
		pending.descriptorHeaps.insert(pending.descriptorHeaps.end(),
			it->second.descriptorHeaps.begin(), it->second.descriptorHeaps.end());
		gPendingPreSkinSubmissions.erase(it);
	}

	if (pending.resources.empty() && pending.descriptorHeaps.empty()) {
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		return;
	}

	if (!EnsurePreSkinRetireFenceLocked(queue)) {
		RequeuePendingPreSkinSubmissionLocked(&pending);
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		return;
	}

	const UINT64 fenceValue = ++gPreSkinRetireFenceNextValue;
	gPreSkinRetireFence->AddRef();
	ID3D12Fence *fence = gPreSkinRetireFence;
	HRESULT hr = queue->Signal(fence, fenceValue);
	if (FAILED(hr)) {
		fence->Release();
		RequeuePendingPreSkinSubmissionLocked(&pending);
		ReleaseSRWLockExclusive(&gPreSkinRetireLock);
		return;
	}

	for (ID3D12Resource *resource : pending.resources) {
		if (!resource)
			continue;
		fence->AddRef();
		gRetiredPreSkinResources.push_back({ resource, fence, fenceValue });
	}
	for (ID3D12DescriptorHeap *heap : pending.descriptorHeaps) {
		if (!heap)
			continue;
		fence->AddRef();
		gRetiredPreSkinDescriptorHeaps.push_back({ heap, fence, fenceValue });
	}
	fence->Release();
	CleanupRetiredPreSkinObjectsLocked();
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);
}
static void ReleasePreSkinDescriptorRingLocked()
{
	if (gPreSkinDescriptorRing.heap) {
		gPreSkinDescriptorRing.heap->Release();
		gPreSkinDescriptorRing.heap = nullptr;
	}
	if (gPreSkinDescriptorRing.device) {
		gPreSkinDescriptorRing.device->Release();
		gPreSkinDescriptorRing.device = nullptr;
	}
	gPreSkinDescriptorRing.capacity = 0;
	gPreSkinDescriptorRing.offset = 0;
	gPreSkinDescriptorRing.increment = 0;
	gPreSkinDescriptorRing.present = -1;
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			retired.heap->Release();
	}
	gRetiredPreSkinDescriptorHeaps.clear();
}

static bool EnsurePreSkinDescriptorRingLocked(
	ID3D12Device *device, UINT requiredDescriptors,
	D3D12_CPU_DESCRIPTOR_HANDLE *cpuBase,
	D3D12_GPU_DESCRIPTOR_HANDLE *gpuBase,
	UINT *increment)
{
	if (!device || !requiredDescriptors || !cpuBase || !gpuBase || !increment)
		return false;

	AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
	const LONG present = DX12GetPresentCount();
	if (gPreSkinDescriptorRing.device != device ||
	    !gPreSkinDescriptorRing.heap ||
	    requiredDescriptors > gPreSkinDescriptorRing.capacity ||
	    gPreSkinDescriptorRing.offset + requiredDescriptors > gPreSkinDescriptorRing.capacity) {
		const UINT capacity = (std::max)(
			PreSkinDescriptorRingInitialCapacity, requiredDescriptors);
		if (gPreSkinDescriptorRing.heap) {
			// Do not overwrite descriptor slots that may still be referenced by
			// already-recorded command lists. Allocate a fresh shader-visible
			// heap page and retire the old one after several Presents.
			RetirePreSkinDescriptorHeapLocked(gPreSkinDescriptorRing.heap);
			gPreSkinDescriptorRing.heap = nullptr;
		}
		if (gPreSkinDescriptorRing.device && gPreSkinDescriptorRing.device != device) {
			gPreSkinDescriptorRing.device->Release();
			gPreSkinDescriptorRing.device = nullptr;
		}

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = capacity;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ID3D12DescriptorHeap *heap = nullptr;
		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
		if (FAILED(hr) || !heap) {
			ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
			return false;
		}

		if (!gPreSkinDescriptorRing.device) {
			device->AddRef();
			gPreSkinDescriptorRing.device = device;
		}
		gPreSkinDescriptorRing.heap = heap;
		gPreSkinDescriptorRing.capacity = capacity;
		gPreSkinDescriptorRing.offset = 0;
		gPreSkinDescriptorRing.increment =
			device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gPreSkinDescriptorRing.present = present;

#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinDescriptorRing",
			"\"status\":\"created\",\"capacity\":%u,\"required\":%u",
			capacity, requiredDescriptors);
#endif
	}

	D3D12_CPU_DESCRIPTOR_HANDLE cpu = gPreSkinDescriptorRing.heap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = gPreSkinDescriptorRing.heap->GetGPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(gPreSkinDescriptorRing.offset) *
		gPreSkinDescriptorRing.increment;
	gpu.ptr += static_cast<UINT64>(gPreSkinDescriptorRing.offset) *
		gPreSkinDescriptorRing.increment;
	gPreSkinDescriptorRing.offset += requiredDescriptors;
	*cpuBase = cpu;
	*gpuBase = gpu;
	*increment = gPreSkinDescriptorRing.increment;
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
	return true;
}

enum class DX12PsoKind
{
	Graphics,
	Compute
};

struct DX12StoredPso
{
	DX12PsoKind kind = DX12PsoKind::Graphics;
	ID3D12Device *device = nullptr;
	ID3D12RootSignature *graphicsRootSignature = nullptr;
	ID3D12RootSignature *computeRootSignature = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc = {};
	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	std::vector<unsigned char> vsBytecode;
	std::vector<unsigned char> psBytecode;
	std::vector<unsigned char> dsBytecode;
	std::vector<unsigned char> hsBytecode;
	std::vector<unsigned char> gsBytecode;
	std::vector<unsigned char> csBytecode;
	ID3D12PipelineState *replacement = nullptr;
	UINT64 replacementGeneration = 0;
	bool skipDraw = false;
	bool skipDispatch = false;
	UINT64 skipGeneration = 0;
};

static std::unordered_map<ID3D12PipelineState*, DX12StoredPso> gPsoRecords;

static void ReleaseLoadedResourcesLocked();
static bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config);

static std::vector<DX12VertexLimitRaiseConfig> CollectVertexLimitRaiseConfigs(
	const Bunny::TextureOverrideMap &textureOverrides)
{
	std::vector<DX12VertexLimitRaiseConfig> configs;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hasVertexLimitRaise || !config.overrideByteWidth)
				continue;
			DX12VertexLimitRaiseConfig out;
			out.section = config.originalSection.empty() ? config.section : config.originalSection;
			out.hasMatchCs = config.hasMatchCs;
			out.matchCs = config.matchCs;
			out.overrideByteStride = config.overrideByteStride;
			out.overrideVertexCount = config.overrideVertexCount;
			out.overrideByteWidth = config.overrideByteWidth;
			out.uavByteStride = config.uavByteStride;
			out.overrideNumElements = config.overrideNumElements;
			configs.push_back(out);
		}
	}
	return configs;
}

static void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex,
	std::unordered_map<uint64_t, std::unordered_set<UINT64>> *matchCsUavByteFilters)
{
	if (!preSkinTextureOverrides || !uavHashTextureOverrideIndex ||
	    !matchCsTextureOverrideIndex || !matchCsUavByteFilters)
		return;

	preSkinTextureOverrides->clear();
	uavHashTextureOverrideIndex->clear();
	matchCsTextureOverrideIndex->clear();
	matchCsUavByteFilters->clear();
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (config.vertexBufferResources.empty())
				continue;

			DX12PreSkinTextureOverrideCandidate candidate;
			candidate.config = config;
			candidate.hash = bucket.first;
			candidate.resourceName = config.vertexBufferResources.begin()->second;
			candidate.order = order++;
			const size_t index = preSkinTextureOverrides->size();
			preSkinTextureOverrides->push_back(std::move(candidate));
			if (config.hasMatchCs) {
				(*matchCsTextureOverrideIndex)[config.matchCs].push_back(index);
				if (config.hasMatchUavBytes)
					(*matchCsUavByteFilters)[config.matchCs].insert(config.matchUavBytes);
			}
		}
	}
}

static void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections)
{
	if (!resourceSections)
		return;

	resourceSections->clear();
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (!config.hasMatchCs)
				continue;
			for (const auto &item : config.vertexBufferResources) {
				if (item.second.empty())
					continue;
				// Preindex explicit match_cs ownership by lower-cased resource
				// name so resource load/patch paths avoid scanning every
				// TextureOverride while command lists are being recorded.
				(*resourceSections)[Bunny::ToLower(item.second)].push_back(config.section);
			}
		}
	}
}

static bool HasPreSkinTextureOverrideCandidates(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_set<uint64_t> *matchCsHashes,
	bool *hasVlrWithoutMatchCs,
	bool *hasExplicitMatchCsUavBytes)
{
	if (matchCsHashes)
		matchCsHashes->clear();
	if (hasVlrWithoutMatchCs)
		*hasVlrWithoutMatchCs = false;
	if (hasExplicitMatchCsUavBytes)
		*hasExplicitMatchCsUavBytes = false;

	bool found = false;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (config.hasMatchCs) {
				if (!config.vertexBufferResources.empty() || config.hasVertexLimitRaise)
					found = true;
				if (matchCsHashes)
					matchCsHashes->insert(config.matchCs);
				if (hasExplicitMatchCsUavBytes && config.hasMatchUavBytes)
					*hasExplicitMatchCsUavBytes = true;
				continue;
			}
			if (config.hasVertexLimitRaise && config.overrideByteWidth) {
				found = true;
				if (hasVlrWithoutMatchCs)
					*hasVlrWithoutMatchCs = true;
			}
		}
	}
	return found;
}

static bool ResourceUsedByExplicitMatchCsLocked(const std::wstring &resourceName)
{
	if (resourceName.empty())
		return false;
	return gExplicitMatchCsResourceSections.find(Bunny::ToLower(resourceName)) !=
		gExplicitMatchCsResourceSections.end();
}

static bool ResourceBlockedByInactiveExplicitMatchCsLocked(const std::wstring &resourceName)
{
	if (resourceName.empty())
		return false;

	auto indexed = gExplicitMatchCsResourceSections.find(Bunny::ToLower(resourceName));
	if (indexed == gExplicitMatchCsResourceSections.end())
		return false;

	for (const std::wstring &section : indexed->second) {
		AcquireSRWLockShared(&gPreSkinLock);
		const bool activeExplicitMatchCs =
			gActivePreSkinTextureOverrides.find(section) !=
			gActivePreSkinTextureOverrides.end();
		ReleaseSRWLockShared(&gPreSkinLock);
		if (activeExplicitMatchCs)
			return false;
	}
	return true;
}

static void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates)
{
	if (!resourceCandidates)
		return;

	resourceCandidates->clear();
	std::unordered_set<std::wstring> seen;
	for (const auto &item : resources) {
		if (!ResourceConfigLooksLikeBuffer(item.second))
			continue;
		if (explicitMatchCsResourceSections.find(Bunny::ToLower(item.first)) !=
		    explicitMatchCsResourceSections.end())
			continue;
		if (!seen.insert(item.first).second)
			continue;
		// VLR fallback scans are rare but can trigger resource loading. Keep a
		// prefiltered resource-name list so dispatch-time fallback avoids
		// walking all parsed Resource sections.
		resourceCandidates->push_back(item.first);
	}
}

static void ClearPreSkinRuntimeState()
{
	AcquireSRWLockExclusive(&gPreSkinLock);
	gRecentComputeUavs.clear();
	gComputeUavSerial = 0;
	gPatchedPreSkinDescriptors.clear();
	gKnownPreSkinDescriptorPatches.clear();
	ClearActivePreSkinTextureOverridesLocked();
	gPreSkinCbvReadCache.clear();
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvStableNegativeCache.clear();
	gPreSkinSrvPositiveCache.clear();
	gPreSkinSrvStablePositiveCache.clear();
	++gPreSkinSrvCacheGeneration;
	ReleaseSRWLockExclusive(&gPreSkinLock);

	AcquireSRWLockExclusive(&gPreSkinRetireLock);
	for (auto &pending : gPendingPreSkinSubmissions)
		ReleasePendingPreSkinSubmission(&pending.second);
	gPendingPreSkinSubmissions.clear();
	for (auto &retired : gRetiredPreSkinResources) {
		if (retired.resource)
			retired.resource->Release();
		if (retired.fence)
			retired.fence->Release();
	}
	gRetiredPreSkinResources.clear();
	for (auto &retired : gRetiredPreSkinDescriptorHeaps) {
		if (retired.heap)
			retired.heap->Release();
		if (retired.fence)
			retired.fence->Release();
	}
	gRetiredPreSkinDescriptorHeaps.clear();
	if (gPreSkinRetireFence) {
		gPreSkinRetireFence->Release();
		gPreSkinRetireFence = nullptr;
	}
	if (gPreSkinRetireFenceDevice) {
		gPreSkinRetireFenceDevice->Release();
		gPreSkinRetireFenceDevice = nullptr;
	}
	if (gPreSkinRetireFenceQueue) {
		gPreSkinRetireFenceQueue->Release();
		gPreSkinRetireFenceQueue = nullptr;
	}
	gPreSkinRetireFenceNextValue = 0;
	ReleaseSRWLockExclusive(&gPreSkinRetireLock);

	AcquireSRWLockExclusive(&gPreSkinDescriptorRingLock);
	ReleasePreSkinDescriptorRingLocked();
	ReleaseSRWLockExclusive(&gPreSkinDescriptorRingLock);
}

static bool DescriptorOverlapsView(
	const DX12DescriptorSummary &descriptor, UINT64 gpuVa, UINT64 size)
{
	if (!descriptor.resource || gpuVa == 0 || size == 0)
		return false;

	UINT64 descriptorBegin = descriptor.gpuVirtualAddress + descriptor.resourceOffset;
	UINT64 descriptorSize = descriptor.viewSize;
	if (!descriptorSize && descriptor.hasResourceDesc &&
		descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		descriptorSize = descriptor.resourceDesc.Width;
	if (!descriptorSize)
		return false;

	const UINT64 descriptorEnd = descriptorBegin + descriptorSize;
	const UINT64 viewEnd = gpuVa + size;
	return gpuVa < descriptorEnd && descriptorBegin < viewEnd;
}

static bool ProducerMatchesIaBuffer(
	const DX12ComputeUavProducer &producer, const DX12IaBufferHash &buffer)
{
	if (!producer.hasDescriptor)
		return false;
	const D3D12_VERTEX_BUFFER_VIEW &view = buffer.vertexView;
	return DescriptorOverlapsView(producer.descriptor, view.BufferLocation, view.SizeInBytes);
}

static bool ProducerMatchesReplacementTarget(
	const DX12ComputeUavProducer &producer, const DX12VertexLimitRaiseConfig &vlr)
{
	if (!producer.hasDescriptor)
		return false;
	const DX12DescriptorSummary &descriptor = producer.descriptor;
	if (descriptor.kind != "UAV" || descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	if (vlr.uavByteStride && descriptor.structureByteStride &&
		descriptor.structureByteStride != vlr.uavByteStride)
		return false;
	if (vlr.overrideByteWidth && descriptor.viewSize &&
		descriptor.viewSize + static_cast<UINT64>(vlr.overrideByteStride) * 4096 < vlr.overrideByteWidth)
		return false;
	return true;
}

static void SetBasePaths(const wchar_t *configPath)
{
	gConfigPath = configPath ? configPath : L"";
	gBaseDir = gConfigPath;
	if (!gBaseDir.empty()) {
		wchar_t path[MAX_PATH];
		wcsncpy_s(path, gBaseDir.c_str(), _TRUNCATE);
		PathRemoveFileSpecW(path);
		gBaseDir = path;
	}
	if (gBaseDir.empty())
		gBaseDir = L".";

	wchar_t shaderFixes[MAX_PATH];
	wcsncpy_s(shaderFixes, gBaseDir.c_str(), _TRUNCATE);
	PathAppendW(shaderFixes, L"ShaderFixes");
	gShaderFixesDir = shaderFixes;
}

static bool ReadFileBytes(const wchar_t *path, std::vector<unsigned char> *data)
{
	if (!path || !data)
		return false;

	FILE *file = _wfsopen(path, L"rb", _SH_DENYNO);
	if (!file)
		return false;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return false;
	}
	long size = ftell(file);
	if (size <= 0) {
		fclose(file);
		return false;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return false;
	}

	data->resize(static_cast<size_t>(size));
	size_t read = fread(data->data(), 1, data->size(), file);
	fclose(file);
	if (read != data->size()) {
		data->clear();
		return false;
	}
	return true;
}

static void ReleaseStoredPso(DX12StoredPso *record)
{
	if (!record)
		return;
	if (record->replacement) {
		record->replacement->Release();
		record->replacement = nullptr;
	}
	if (record->device) {
		record->device->Release();
		record->device = nullptr;
	}
	if (record->graphicsRootSignature) {
		record->graphicsRootSignature->Release();
		record->graphicsRootSignature = nullptr;
	}
	if (record->computeRootSignature) {
		record->computeRootSignature->Release();
		record->computeRootSignature = nullptr;
	}
}

static void StoreShaderBytecode(
	const D3D12_SHADER_BYTECODE &source, std::vector<unsigned char> *storage,
	D3D12_SHADER_BYTECODE *target)
{
	if (!storage || !target)
		return;

	*target = {};
	storage->clear();
	if (!source.pShaderBytecode || source.BytecodeLength == 0)
		return;

	storage->resize(source.BytecodeLength);
	memcpy(storage->data(), source.pShaderBytecode, source.BytecodeLength);
	target->pShaderBytecode = storage->data();
	target->BytecodeLength = storage->size();
}

static void DeepCopyGraphicsDesc(
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *source, DX12StoredPso *record)
{
	record->graphicsDesc = *source;
	record->graphicsRootSignature = source->pRootSignature;
	if (record->graphicsRootSignature)
		record->graphicsRootSignature->AddRef();
	StoreShaderBytecode(source->VS, &record->vsBytecode, &record->graphicsDesc.VS);
	StoreShaderBytecode(source->PS, &record->psBytecode, &record->graphicsDesc.PS);
	StoreShaderBytecode(source->DS, &record->dsBytecode, &record->graphicsDesc.DS);
	StoreShaderBytecode(source->HS, &record->hsBytecode, &record->graphicsDesc.HS);
	StoreShaderBytecode(source->GS, &record->gsBytecode, &record->graphicsDesc.GS);
}

static void DeepCopyComputeDesc(
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *source, DX12StoredPso *record)
{
	record->computeDesc = *source;
	record->computeRootSignature = source->pRootSignature;
	if (record->computeRootSignature)
		record->computeRootSignature->AddRef();
	StoreShaderBytecode(source->CS, &record->csBytecode, &record->computeDesc.CS);
}

uint64_t DX12ModHashShaderBytecode(const void *data, size_t size)
{
	const unsigned char *bytes = static_cast<const unsigned char*>(data);
	uint64_t hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

void DX12ModSetSafeMode(bool safeMode)
{
	InterlockedExchange(&gDx12SafeMode, safeMode ? 1 : 0);
}

static bool IsVlrBufferResizeCandidate(
	const D3D12_RESOURCE_DESC &desc, const DX12VertexLimitRaiseConfig &vlr)
{
	if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width == 0)
		return false;
	if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
		return false;
	if (desc.Width >= vlr.overrideByteWidth)
		return false;

	const UINT64 tolerance = std::max<UINT64>(
		static_cast<UINT64>(vlr.overrideByteStride) * 4096,
		vlr.overrideByteWidth / 4);
	if (desc.Width + tolerance < vlr.overrideByteWidth)
		return false;

	return true;
}

static bool AdjustBufferResourceDescCommon(D3D12_RESOURCE_DESC *desc, const char *source)
{
	if (!desc)
		return false;

	DX12VertexLimitRaiseConfig match;
	bool found = false;
	if (!TryAcquireSRWLockShared(&gModLock))
		return false;
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (!IsVlrBufferResizeCandidate(*desc, vlr))
			continue;
		if (!found || vlr.overrideByteWidth > match.overrideByteWidth) {
			match = vlr;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!found)
		return false;

#if defined(_DEBUG)
	const UINT64 oldWidth = desc->Width;
#endif
	desc->Width = match.overrideByteWidth;
#if defined(_DEBUG)
	DX12LogDebugJsonFunc("DX12VertexLimitRaiseResize",
		"\"source\":\"%s\",\"section\":\"%S\",\"oldWidth\":%llu,\"newWidth\":%llu,\"overrideByteStride\":%u,\"overrideVertexCount\":%u,\"flags\":%u",
		source ? source : "", match.section.c_str(),
		static_cast<unsigned long long>(oldWidth),
		static_cast<unsigned long long>(desc->Width),
		match.overrideByteStride, match.overrideVertexCount,
		static_cast<unsigned>(desc->Flags));
#endif
	return true;
}

bool DX12ModAdjustBufferResourceDesc(D3D12_RESOURCE_DESC *desc, const char *source)
{
	return AdjustBufferResourceDescCommon(desc, source);
}

bool DX12ModAdjustBufferResourceDesc1(D3D12_RESOURCE_DESC1 *desc, const char *source)
{
	if (!desc)
		return false;
	D3D12_RESOURCE_DESC desc0 = {};
	desc0.Dimension = desc->Dimension;
	desc0.Alignment = desc->Alignment;
	desc0.Width = desc->Width;
	desc0.Height = desc->Height;
	desc0.DepthOrArraySize = desc->DepthOrArraySize;
	desc0.MipLevels = desc->MipLevels;
	desc0.Format = desc->Format;
	desc0.SampleDesc = desc->SampleDesc;
	desc0.Layout = desc->Layout;
	desc0.Flags = desc->Flags;
	if (!AdjustBufferResourceDescCommon(&desc0, source))
		return false;
	desc->Width = desc0.Width;
	return true;
}

bool DX12ModAdjustUavDesc(
	ID3D12Resource *resource, D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, const char *source)
{
	if (!resource || !desc || desc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;

	UINT bytesPerElement = desc->Buffer.StructureByteStride;
	if (!bytesPerElement && (desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW))
		bytesPerElement = 4;
	if (!bytesPerElement)
		return false;

	D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
	if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
		return false;

	DX12VertexLimitRaiseConfig match;
	bool found = false;
	if (!TryAcquireSRWLockShared(&gModLock))
		return false;
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (!vlr.overrideNumElements || !vlr.uavByteStride)
			continue;
		if (bytesPerElement != vlr.uavByteStride)
			continue;
		if (desc->Buffer.NumElements >= vlr.overrideNumElements)
			continue;
		const UINT64 requiredBytes =
			(desc->Buffer.FirstElement + vlr.overrideNumElements) * bytesPerElement;
		if (resourceDesc.Width < requiredBytes)
			continue;
		if (!found || vlr.overrideNumElements > match.overrideNumElements) {
			match = vlr;
			found = true;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!found)
		return false;

#if defined(_DEBUG)
	const UINT oldNumElements = desc->Buffer.NumElements;
#endif
	desc->Buffer.NumElements = static_cast<UINT>(match.overrideNumElements);
#if defined(_DEBUG)
	DX12LogDebugJsonFunc("DX12VertexLimitRaiseUav",
		"\"source\":\"%s\",\"section\":\"%S\",\"oldNumElements\":%u,\"newNumElements\":%u,\"bytesPerElement\":%u,\"resourceWidth\":%llu",
		source ? source : "", match.section.c_str(), oldNumElements,
		desc->Buffer.NumElements, bytesPerElement,
		static_cast<unsigned long long>(resourceDesc.Width));
#endif
	return true;
}

static bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting);
static bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot);
static void BuildDx12RelatedTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	std::vector<DX12RelatedTextureOverrideCandidate> *relatedTextureOverrides,
	std::unordered_map<std::wstring, std::vector<size_t>> *relatedTokenIndex);
static void BuildDx12IaTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::vector<DX12IaTextureOverrideCandidate> *iaTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverrideIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex);
static void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans);
static void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans);
static void BuildDx12PreSkinTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::vector<DX12PreSkinTextureOverrideCandidate> *preSkinTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *uavHashTextureOverrideIndex,
	std::unordered_map<uint64_t, std::vector<size_t>> *matchCsTextureOverrideIndex);
static void BuildDx12ExplicitMatchCsResourceIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, std::vector<std::wstring>> *resourceSections);
static void BuildDx12VlrResourceCandidates(
	const Bunny::ResourceMap &resources,
	const std::unordered_map<std::wstring, std::vector<std::wstring>> &explicitMatchCsResourceSections,
	std::vector<std::wstring> *resourceCandidates);

static bool CommandListHasRuntimeEffect(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting)
{
	if (!effectCache || !visiting)
		return true;

	auto cached = effectCache->find(name);
	if (cached != effectCache->end())
		return cached->second;
	if (!visiting->insert(name).second)
		return true;

	bool effect = false;
	auto it = commandLists.find(name);
	if (it != commandLists.end()) {
		for (const Bunny::CommandListAction &action : it->second.actions) {
			if (CommandListActionHasRuntimeEffect(
				    action, commandLists, effectCache, visiting)) {
				effect = true;
				break;
			}
		}
	}
	visiting->erase(name);
	(*effectCache)[name] = effect;
	return effect;
}

static bool CommandListActionHasRuntimeEffect(
	const Bunny::CommandListAction &action,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache,
	std::set<std::wstring> *visiting)
{
	switch (action.kind) {
	case Bunny::CommandListActionKind::Run:
		return CommandListHasRuntimeEffect(
			action.commandList, commandLists, effectCache, visiting);
	case Bunny::CommandListActionKind::CheckTextureOverride:
	case Bunny::CommandListActionKind::HandlingSkip:
	case Bunny::CommandListActionKind::SetIndexBuffer:
	case Bunny::CommandListActionKind::SetVertexBuffer:
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller:
	case Bunny::CommandListActionKind::Dispatch:
		return true;
	}
	return true;
}

static bool CommandListLinksHaveRuntimeEffect(
	const std::vector<std::wstring> &links,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache)
{
	std::set<std::wstring> visiting;
	for (const std::wstring &link : links) {
		if (CommandListHasRuntimeEffect(link, commandLists, effectCache, &visiting))
			return true;
	}
	return false;
}

static bool TextureOverrideActionsHaveRuntimeEffect(
	const Bunny::TextureOverrideConfig &config,
	bool executeDrawActions,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *effectCache)
{
	if (config.actions.empty())
		return !config.indexBufferResource.empty() ||
			!config.vertexBufferResources.empty() ||
			(executeDrawActions && config.handlingSkip);

	std::set<std::wstring> visiting;
	for (const Bunny::CommandListAction &action : config.actions) {
		switch (action.kind) {
		case Bunny::CommandListActionKind::Run:
			if (CommandListHasRuntimeEffect(
				    action.commandList, commandLists, effectCache, &visiting))
				return true;
			break;
		case Bunny::CommandListActionKind::CheckTextureOverride:
		case Bunny::CommandListActionKind::SetIndexBuffer:
		case Bunny::CommandListActionKind::SetVertexBuffer:
		case Bunny::CommandListActionKind::Dispatch:
			return true;
		case Bunny::CommandListActionKind::HandlingSkip:
		case Bunny::CommandListActionKind::Draw:
		case Bunny::CommandListActionKind::DrawIndexed:
		case Bunny::CommandListActionKind::DrawFromCaller:
		case Bunny::CommandListActionKind::DrawIndexedFromCaller:
			if (executeDrawActions)
				return true;
			break;
		}
	}
	return false;
}

static void BuildDx12RuntimeEffectIndexes(
	const Bunny::TextureOverrideMap &textureOverrides,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, bool> *commandListEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePreEffect,
	std::unordered_map<std::wstring, bool> *textureOverridePostEffect)
{
	if (!commandListEffect || !textureOverridePreEffect || !textureOverridePostEffect)
		return;

	commandListEffect->clear();
	textureOverridePreEffect->clear();
	textureOverridePostEffect->clear();

	for (const auto &item : commandLists) {
		std::set<std::wstring> visiting;
		CommandListHasRuntimeEffect(item.first, commandLists, commandListEffect, &visiting);
	}

	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			const bool preEffect =
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.pre, commandLists, commandListEffect) ||
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.main, commandLists, commandListEffect) ||
				TextureOverrideActionsHaveRuntimeEffect(
					config, true, commandLists, commandListEffect);
			const bool postEffect =
				CommandListLinksHaveRuntimeEffect(
					config.commandLists.post, commandLists, commandListEffect);
			(*textureOverridePreEffect)[config.section] = preEffect;
			(*textureOverridePostEffect)[config.section] = postEffect;
		}
	}
}

static bool LookupRuntimeEffect(
	const std::unordered_map<std::wstring, bool> &effects,
	const std::wstring &section)
{
	auto effect = effects.find(section);
	return effect == effects.end() || effect->second;
}

static void BuildDx12TextureOverrideSectionIds(
	const Bunny::TextureOverrideMap &textureOverrides,
	std::unordered_map<std::wstring, uint32_t> *sectionIds)
{
	if (!sectionIds)
		return;

	sectionIds->clear();
	uint32_t nextId = 1;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			if (sectionIds->find(config.section) != sectionIds->end())
				continue;
			(*sectionIds)[config.section] = nextId++;
		}
	}
}

bool DX12ModRuntimeLoad(
	const wchar_t *configPath,
	std::wstring *statusMessage,
	bool *hasWarnings,
	bool *hasErrors)
{
	Bunny::MigotoIniLoadResult iniLoad;
	Bunny::ShaderOverrideMap shaderOverrides;
	Bunny::TextureOverrideMap textureOverrides;
	Bunny::ResourceMap resources;
	Bunny::CommandListMap commandLists;
	std::unordered_map<std::wstring, bool> commandListRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePreRuntimeEffect;
	std::unordered_map<std::wstring, bool> textureOverridePostRuntimeEffect;
	std::unordered_map<std::wstring, uint32_t> textureOverrideSectionIds;
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> compiledTextureOverridePlans;
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> compiledCommandListPlans;
	bool presentRuntimeEffect = false;
	std::vector<DX12RelatedTextureOverrideCandidate> relatedTextureOverrides;
	std::unordered_map<std::wstring, std::vector<size_t>> relatedTextureOverrideTokenIndex;
	std::vector<DX12IaTextureOverrideCandidate> iaTextureOverrides;
	std::unordered_map<uint32_t, std::vector<size_t>> iaIndexTextureOverrideIndex;
	std::unordered_map<UINT64, std::vector<size_t>> iaVertexTextureOverrideIndex;
	std::unordered_map<uint32_t, std::vector<size_t>> iaAnyVertexTextureOverrideIndex;
	std::unordered_map<UINT64, std::vector<size_t>> iaMergedVertexTextureOverrideIndex;
	std::vector<DX12PreSkinTextureOverrideCandidate> preSkinTextureOverrides;
	std::unordered_map<uint32_t, std::vector<size_t>> preSkinUavHashTextureOverrideIndex;
	std::unordered_map<uint64_t, std::vector<size_t>> preSkinMatchCsTextureOverrideIndex;
	std::unordered_map<uint64_t, std::unordered_set<UINT64>> preSkinMatchCsUavByteFilters;
	std::unordered_map<std::wstring, std::vector<std::wstring>> explicitMatchCsResourceSections;
	std::vector<std::wstring> vlrResourceCandidates;
	std::unordered_set<uint64_t> preSkinMatchCsHashes;
	bool hasUavHashTextureOverridesWithoutMatchCs = false;
	bool hasExplicitPreSkinMatchCsUavBytes = false;

	SetBasePaths(configPath);
	if (!Bunny::LoadMigotoIniWithIncludes(configPath, &iniLoad)) {
		DX12LogJsonFunc("DX12ModRuntime",
			"\"status\":\"config_load_failed\",\"path\":\"%S\",\"error\":\"%S\"",
			configPath ? configPath : L"", iniLoad.document.Error().c_str());
		if (statusMessage)
			*statusMessage = L"DX12 mod config load failed: " + iniLoad.document.Error();
		if (hasWarnings)
			*hasWarnings = true;
		if (hasErrors)
			*hasErrors = true;
		return false;
	}

	const bool hasIniErrors = !iniLoad.errors.empty();
	Bunny::ParseShaderOverrideSections(iniLoad.document, &shaderOverrides);
	Bunny::ParseTextureOverrideSections(iniLoad.document, &textureOverrides);
	Bunny::ParseResourceSections(iniLoad.document, &resources);
	Bunny::ParseCommandListSections(iniLoad.document, &commandLists);
	std::vector<DX12VertexLimitRaiseConfig> vertexLimitRaiseConfigs =
		CollectVertexLimitRaiseConfigs(textureOverrides);
	bool hasUavHashTextureOverrides =
		HasPreSkinTextureOverrideCandidates(
			textureOverrides, &preSkinMatchCsHashes,
			&gHasPreSkinVlrWithoutMatchCs,
			&hasExplicitPreSkinMatchCsUavBytes);
	BuildDx12RuntimeEffectIndexes(
		textureOverrides, commandLists,
		&commandListRuntimeEffect,
		&textureOverridePreRuntimeEffect,
		&textureOverridePostRuntimeEffect);
	{
		std::set<std::wstring> visiting;
		presentRuntimeEffect =
			CommandListHasRuntimeEffect(
				L"Present", commandLists, &commandListRuntimeEffect, &visiting);
	}
	BuildDx12TextureOverrideSectionIds(
		textureOverrides, &textureOverrideSectionIds);
	BuildDx12RelatedTextureOverrideIndex(
		textureOverrides, textureOverridePreRuntimeEffect, textureOverrideSectionIds,
		&relatedTextureOverrides, &relatedTextureOverrideTokenIndex);
	BuildDx12IaTextureOverrideIndex(
		textureOverrides,
		textureOverrideSectionIds,
		textureOverridePreRuntimeEffect,
		textureOverridePostRuntimeEffect,
		&iaTextureOverrides,
		&iaIndexTextureOverrideIndex,
		&iaVertexTextureOverrideIndex,
		&iaAnyVertexTextureOverrideIndex,
		&iaMergedVertexTextureOverrideIndex);
	BuildDx12CompiledCommandListPlans(
		commandLists, &compiledCommandListPlans);
	BuildDx12CompiledTextureOverridePlans(
		textureOverrides, textureOverrideSectionIds, compiledCommandListPlans,
		&compiledTextureOverridePlans);
	BuildDx12PreSkinTextureOverrideIndex(
		textureOverrides,
		&preSkinTextureOverrides,
		&preSkinUavHashTextureOverrideIndex,
		&preSkinMatchCsTextureOverrideIndex,
		&preSkinMatchCsUavByteFilters);
	BuildDx12ExplicitMatchCsResourceIndex(
		textureOverrides, &explicitMatchCsResourceSections);
	BuildDx12VlrResourceCandidates(
		resources, explicitMatchCsResourceSections, &vlrResourceCandidates);

	AcquireSRWLockExclusive(&gModLock);
	gShaderOverrides.swap(shaderOverrides);
	gTextureOverrides.swap(textureOverrides);
	gResources.swap(resources);
	gCommandLists.swap(commandLists);
	gVlrResourceCandidates.swap(vlrResourceCandidates);
	gCommandListRuntimeEffect.swap(commandListRuntimeEffect);
	gTextureOverridePreRuntimeEffect.swap(textureOverridePreRuntimeEffect);
	gTextureOverridePostRuntimeEffect.swap(textureOverridePostRuntimeEffect);
	gTextureOverrideSectionIds.swap(textureOverrideSectionIds);
	gRelatedTextureOverrides.swap(relatedTextureOverrides);
	gRelatedTextureOverrideTokenIndex.swap(relatedTextureOverrideTokenIndex);
	gIaTextureOverrides.swap(iaTextureOverrides);
	gIaIndexTextureOverrideIndex.swap(iaIndexTextureOverrideIndex);
	gIaVertexTextureOverrideIndex.swap(iaVertexTextureOverrideIndex);
	gIaAnyVertexTextureOverrideIndex.swap(iaAnyVertexTextureOverrideIndex);
	gIaMergedVertexTextureOverrideIndex.swap(iaMergedVertexTextureOverrideIndex);
	gCompiledCommandListPlans.swap(compiledCommandListPlans);
	gCompiledTextureOverridePlans.swap(compiledTextureOverridePlans);
	gPreSkinTextureOverrides.swap(preSkinTextureOverrides);
	gPreSkinUavHashTextureOverrideIndex.swap(preSkinUavHashTextureOverrideIndex);
	gPreSkinMatchCsTextureOverrideIndex.swap(preSkinMatchCsTextureOverrideIndex);
	gPreSkinMatchCsUavByteFilters.swap(preSkinMatchCsUavByteFilters);
	gExplicitMatchCsResourceSections.swap(explicitMatchCsResourceSections);
	gVertexLimitRaiseConfigs.swap(vertexLimitRaiseConfigs);
	gPreSkinMatchCsHashes.swap(preSkinMatchCsHashes);
	ReleaseLoadedResourcesLocked();
	gIaSkipCache.clear();
	gIaTextureCandidateCache.clear();
	gIaReplacementMatchCache.clear();
	gIaReplacementPreparedFrameCache.clear();
	gShaderOverridePsoMatchCache.clear();
	gIaReplacementPrepareCachePresent = -1;
	gPresentCommandListExecutedPresent = -1;
	gPreSkinSectionAppliedPresent.clear();
	gPreSkinCbvReadCache.clear();
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvStableNegativeCache.clear();
	gPreSkinSrvPositiveCache.clear();
	gPreSkinSrvStablePositiveCache.clear();
	gPreSkinUavMatchCache.clear();
	++gPreSkinActiveGeneration;
	++gPreSkinSrvCacheGeneration;
	InterlockedExchange(&gHasShaderOverrides, gShaderOverrides.empty() ? 0 : 1);
	InterlockedExchange(&gHasTextureOverrides, gTextureOverrides.empty() ? 0 : 1);
	InterlockedExchange(&gHasUavHashTextureOverrides, hasUavHashTextureOverrides ? 1 : 0);
	InterlockedExchange(&gHasExplicitPreSkinMatchCsUavBytes,
		hasExplicitPreSkinMatchCsUavBytes ? 1 : 0);
	InterlockedExchange(&gHasPresentRuntimeEffect, presentRuntimeEffect ? 1 : 0);
	gLoaded = true;
	++gReloadGeneration;
	for (auto &item : gPsoRecords) {
		if (item.second.replacement) {
			item.second.replacement->Release();
			item.second.replacement = nullptr;
		}
		item.second.replacementGeneration = 0;
		item.second.skipGeneration = 0;
	}
	ReleaseSRWLockExclusive(&gModLock);
	ClearPreSkinRuntimeState();

	DX12LogJsonFunc("DX12ModRuntime",
		"\"status\":\"loaded\",\"path\":\"%S\",\"iniFiles\":%zu,\"errors\":%zu,\"warnings\":%zu,\"shaderOverrides\":%zu,\"textureOverrides\":%zu,\"resources\":%zu,\"commandLists\":%zu,\"shaderFixes\":\"%S\",\"generation\":%llu",
		configPath ? configPath : L"", iniLoad.loadedFiles.size(), iniLoad.errors.size(), iniLoad.warnings.size(),
		gShaderOverrides.size(), gTextureOverrides.size(), gResources.size(), gCommandLists.size(),
		gShaderFixesDir.c_str(),
		static_cast<unsigned long long>(gReloadGeneration));
	for (const std::wstring &loadedFile : iniLoad.loadedFiles) {
		DX12LogJsonFunc("DX12ModRuntimeIni",
			"\"status\":\"loaded\",\"path\":\"%S\"", loadedFile.c_str());
	}
	for (const std::wstring &error : iniLoad.errors) {
		DX12LogJsonFunc("DX12ModRuntimeIni",
			"\"status\":\"error\",\"message\":\"%S\"", error.c_str());
	}
	for (const std::wstring &warning : iniLoad.warnings) {
		DX12LogJsonFunc("DX12ModRuntimeIni",
			"\"status\":\"warning\",\"message\":\"%S\"", warning.c_str());
	}
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		DX12LogJsonFunc("DX12VertexLimitRaiseConfig",
			"\"section\":\"%S\",\"overrideByteStride\":%u,\"overrideVertexCount\":%u,\"overrideByteWidth\":%llu,\"uavByteStride\":%u,\"overrideNumElements\":%llu",
			vlr.section.c_str(), vlr.overrideByteStride, vlr.overrideVertexCount,
			static_cast<unsigned long long>(vlr.overrideByteWidth),
			vlr.uavByteStride, static_cast<unsigned long long>(vlr.overrideNumElements));
	}
	if (statusMessage) {
		wchar_t text[256];
		if (hasIniErrors) {
			swprintf_s(text, L"F10 DX12 mod config reloaded with %zu error(s)",
				iniLoad.errors.size());
		} else if (iniLoad.warnings.empty()) {
			swprintf_s(text, L"F10 DX12 mod config reloaded: %zu ini, %zu TextureOverride, %zu Resource",
				iniLoad.loadedFiles.size(), gTextureOverrides.size(), gResources.size());
		} else {
			swprintf_s(text, L"F10 DX12 mod config reloaded with %zu warning(s)",
				iniLoad.warnings.size());
		}
		*statusMessage = text;
	}
	if (hasWarnings)
		*hasWarnings = hasIniErrors || !iniLoad.warnings.empty();
	if (hasErrors)
		*hasErrors = hasIniErrors;
	return true;
}

bool DX12ModRuntimeReload()
{
	std::wstring configPath;
	AcquireSRWLockShared(&gModLock);
	configPath = gConfigPath;
	ReleaseSRWLockShared(&gModLock);
	if (configPath.empty()) {
		DX12LogJsonFunc("DX12ModRuntimeReload", "\"status\":\"skipped\",\"reason\":\"empty_config_path\"");
		return false;
	}
	DX12LogJsonFunc("DX12ModRuntimeReload", "\"status\":\"begin\",\"path\":\"%S\"", configPath.c_str());
	std::wstring status;
	bool hasWarnings = false;
	bool hasErrors = false;
	if (!DX12ModRuntimeLoad(configPath.c_str(), &status, &hasWarnings, &hasErrors)) {
		DX12SetOverlayError(status.c_str());
		return false;
	}
	if (hasErrors)
		DX12SetOverlayError(status.c_str());
	else if (hasWarnings)
		DX12SetOverlayWarning(status.c_str());
	else
		DX12SetOverlayStatus(status.c_str());
	return true;
}

static std::wstring ResolveResourcePathLocked(
	const Bunny::ResourceConfig &config, bool rootFallback)
{
	if (config.filename.empty())
		return L"";

	wchar_t path[MAX_PATH];
	wcsncpy_s(path, config.filename.c_str(), _TRUNCATE);
	if (PathIsRelativeW(path)) {
		wchar_t combined[MAX_PATH];
		const std::wstring &base = rootFallback || config.sourceDir.empty() ? gBaseDir : config.sourceDir;
		wcsncpy_s(combined, base.c_str(), _TRUNCATE);
		PathAppendW(combined, config.filename.c_str());
		return combined;
	}
	return path;
}

static bool ResourceConfigLooksLikeBuffer(const Bunny::ResourceConfig &config)
{
	std::wstring type = Bunny::ToLower(Bunny::Trim(config.type));
	if (type.empty())
		return true;
	return type == L"buffer" ||
		type == L"structuredbuffer" ||
		type == L"appendstructuredbuffer" ||
		type == L"consumestructuredbuffer" ||
		type == L"byteaddressbuffer" ||
		type == L"rwbuffer" ||
		type == L"rwstructuredbuffer" ||
		type == L"rwbyteaddressbuffer";
}

static bool LoadResourceBytesLocked(
	const Bunny::ResourceConfig &config, std::vector<unsigned char> *bytes, std::wstring *path)
{
	if (!bytes)
		return false;
	bytes->clear();

	std::wstring resolvedPath = ResolveResourcePathLocked(config, false);
	if (!resolvedPath.empty()) {
		if (path)
			*path = resolvedPath;
		if (ReadFileBytes(resolvedPath.c_str(), bytes))
			return true;

		if (!config.sourceDir.empty()) {
			std::wstring fallbackPath = ResolveResourcePathLocked(config, true);
			if (fallbackPath != resolvedPath) {
				if (path)
					*path = fallbackPath;
				return ReadFileBytes(fallbackPath.c_str(), bytes);
			}
		}
		return false;
	}

	if (config.data.empty())
		return false;

	std::wstring data = Bunny::Trim(config.data);
	if (data.rfind(L"0x", 0) == 0 || data.rfind(L"0X", 0) == 0)
		data = data.substr(2);
	data.erase(std::remove_if(data.begin(), data.end(), iswspace), data.end());
	if (data.empty() || (data.size() % 2) != 0)
		return false;

	for (size_t i = 0; i < data.size(); i += 2) {
		wchar_t text[3] = {data[i], data[i + 1], 0};
		wchar_t *end = nullptr;
		unsigned long parsed = wcstoul(text, &end, 16);
		if (!end || *end)
			return false;
		bytes->push_back(static_cast<unsigned char>(parsed));
	}
	return !bytes->empty();
}

static ID3D12Device *AcquireModDevice(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return nullptr;

	ID3D12Device *device = nullptr;
	if (SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&device))) && device)
		return device;
	return nullptr;
}

static DX12LoadedResource *EnsureLoadedResourceLocked(
	ID3D12Device *device, const std::wstring &name)
{
	if (!device || name.empty())
		return nullptr;

	auto loaded = gLoadedResources.find(name);
	if (loaded != gLoadedResources.end())
		return loaded->second.resource && !loaded->second.failed ? &loaded->second : nullptr;

	auto configIt = gResources.find(name);
	if (configIt == gResources.end())
		return nullptr;

	const Bunny::ResourceConfig &config = configIt->second;
	if (!ResourceConfigLooksLikeBuffer(config))
		return nullptr;

	std::vector<unsigned char> bytes;
	std::wstring path;
	if (!LoadResourceBytesLocked(config, &bytes, &path)) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		gLoadedResources[name] = failedResource;
		DX12LogJsonFunc("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"read_failed\",\"path\":\"%S\"",
			config.section.c_str(), path.c_str());
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod warning: failed to read resource\n%ls\n%ls",
			config.section.c_str(), path.c_str());
		DX12SetOverlayWarning(status);
		return nullptr;
	}

	UINT64 byteWidth = config.hasByteWidth ? config.byteWidth : bytes.size();
	if (byteWidth < bytes.size())
		byteWidth = bytes.size();
	if (byteWidth == 0)
		return nullptr;

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap.CreationNodeMask = 1;
	heap.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = byteWidth;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr) || !resource) {
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		gLoadedResources[name] = failedResource;
		DX12LogJsonFunc("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"create_failed\",\"hr\":\"0x%lx\",\"bytes\":%llu",
			config.section.c_str(), hr, static_cast<unsigned long long>(byteWidth));
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to create resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12SetOverlayError(status);
		return nullptr;
	}

	void *mapped = nullptr;
	D3D12_RANGE readRange = {};
	hr = resource->Map(0, &readRange, &mapped);
	if (SUCCEEDED(hr) && mapped) {
		memcpy(mapped, bytes.data(), bytes.size());
		if (byteWidth > bytes.size())
			memset(static_cast<unsigned char*>(mapped) + bytes.size(), 0, byteWidth - bytes.size());
		resource->Unmap(0, nullptr);
	} else {
		resource->Release();
		DX12LoadedResource failedResource;
		failedResource.failed = true;
		failedResource.name = name;
		failedResource.path = path;
		gLoadedResources[name] = failedResource;
		DX12LogJsonFunc("DX12ResourceLoad",
			"\"status\":\"failed\",\"section\":\"%S\",\"reason\":\"map_failed\",\"hr\":\"0x%lx\"",
			config.section.c_str(), hr);
		wchar_t status[512];
		swprintf_s(status, L"DX12 mod error: failed to map resource\n%ls hr=0x%lx",
			config.section.c_str(), hr);
		DX12SetOverlayError(status);
		return nullptr;
	}

	DX12LoadedResource loadedResource;
	loadedResource.resource = resource;
	loadedResource.byteWidth = byteWidth;
	loadedResource.stride = config.hasStride ? config.stride : 0;
	loadedResource.format = ParseDx12ResourceFormat(config.format);
	loadedResource.name = name;
	loadedResource.path = path;
	auto inserted = gLoadedResources.emplace(name, loadedResource);
	DX12LogJsonFunc("DX12ResourceLoad",
		"\"status\":\"loaded\",\"section\":\"%S\",\"name\":\"%S\",\"path\":\"%S\",\"bytes\":%llu,\"stride\":%u,\"format\":%u",
		config.section.c_str(), name.c_str(), path.c_str(),
		static_cast<unsigned long long>(byteWidth), loadedResource.stride,
		static_cast<UINT>(loadedResource.format));
	return &inserted.first->second;
}

static bool EnsureResourceUavLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	DX12LoadedResource *resource, UINT elementStride, UINT64 minByteWidth = 0,
	bool allowInactiveExplicitMatchCs = false, bool forceNewResource = false)
{
	if (!device || !commandList || !resource || !resource->resource)
		return false;
	if (!allowInactiveExplicitMatchCs &&
	    ResourceBlockedByInactiveExplicitMatchCsLocked(resource->name))
		return false;
	const UINT64 uavByteWidth = (std::max)(resource->byteWidth, minByteWidth);
	if (!forceNewResource && resource->uavHeap && resource->uavResource && resource->uavByteWidth >= uavByteWidth)
		return true;

	if (!elementStride)
		elementStride = resource->stride ? resource->stride : 4;
	if (!elementStride)
		return false;

	if (resource->uavResource) {
		RetirePreSkinResourceForCommandList(commandList, resource->uavResource);
		resource->uavResource = nullptr;
	}
	if (resource->uavHeap) {
		RetirePreSkinDescriptorHeapLocked(resource->uavHeap);
		resource->uavHeap = nullptr;
	}
	resource->uavCpu = {};
	resource->uavGpu = {};
	resource->uavInitialized = false;
	resource->uavWritten = false;
	resource->uavValid = false;
	resource->uavByteWidth = 0;
	resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;

	ID3D12DescriptorHeap *heap = nullptr;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
	if (FAILED(hr) || !heap) {
		DX12LogDebugJsonFunc("DX12PreSkinningUav",
			"\"status\":\"failed\",\"reason\":\"create_heap_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\"",
			hr, resource->name.c_str());
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = uavByteWidth;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ID3D12Resource *uavResource = nullptr;
	hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr, IID_PPV_ARGS(&uavResource));
	if (FAILED(hr) || !uavResource) {
		DX12LogDebugJsonFunc("DX12PreSkinningUav",
			"\"status\":\"failed\",\"reason\":\"create_resource_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\",\"bytes\":%llu",
			hr, resource->name.c_str(), static_cast<unsigned long long>(uavByteWidth));
		heap->Release();
		return false;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.StructureByteStride = elementStride;
	uavDesc.Buffer.NumElements = static_cast<UINT>(
		(std::min)(uavByteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
	uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	resource->uavCpu = heap->GetCPUDescriptorHandleForHeapStart();
	resource->uavGpu = heap->GetGPUDescriptorHandleForHeapStart();
	device->CreateUnorderedAccessView(uavResource, nullptr, &uavDesc, resource->uavCpu);
	resource->uavHeap = heap;
	resource->uavResource = uavResource;
	resource->uavByteWidth = uavByteWidth;
	resource->uavInitialized = true;
	resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	DX12LogDebugJsonFunc("DX12PreSkinningUav",
		"\"status\":\"created\",\"resource\":\"%S\",\"bytes\":%llu,\"uavBytes\":%llu,\"stride\":%u,\"elements\":%u",
		resource->name.c_str(),
		static_cast<unsigned long long>(resource->byteWidth),
		static_cast<unsigned long long>(uavByteWidth),
		elementStride, uavDesc.Buffer.NumElements);
	return true;
}

static bool EnsureResourceSrvLocked(
	ID3D12Device *device, DX12LoadedResource *resource, UINT elementStride,
	UINT64 minByteWidth = 0)
{
	if (!device || !resource || !resource->resource)
		return false;
	if (!elementStride)
		elementStride = resource->stride ? resource->stride : 4;
	if (!elementStride)
		return false;
	const UINT64 srvByteWidth = (std::max)(resource->byteWidth, minByteWidth);
	if (resource->srvHeap && resource->srvCpu.ptr &&
	    resource->srvStride == elementStride && resource->srvByteWidth >= srvByteWidth)
		return true;

	if (resource->srvHeap) {
		resource->srvHeap->Release();
		resource->srvHeap = nullptr;
	}
	if (resource->srvResource) {
		resource->srvResource->Release();
		resource->srvResource = nullptr;
	}
	resource->srvCpu = {};
	resource->srvStride = 0;
	resource->srvByteWidth = 0;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heapDesc.NodeMask = 0;

	ID3D12DescriptorHeap *heap = nullptr;
	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap));
	if (FAILED(hr) || !heap) {
		DX12LogDebugJsonFunc("DX12PreSkinningSrv",
			"\"status\":\"failed\",\"reason\":\"create_heap_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\"",
			hr, resource->name.c_str());
		return false;
	}

	ID3D12Resource *srvBacking = resource->resource;
	if (srvByteWidth > resource->byteWidth) {
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProps.CreationNodeMask = 1;
		heapProps.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = srvByteWidth;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		hr = device->CreateCommittedResource(
			&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr, IID_PPV_ARGS(&srvBacking));
		if (FAILED(hr) || !srvBacking) {
			heap->Release();
			DX12LogDebugJsonFunc("DX12PreSkinningSrv",
				"\"status\":\"failed\",\"reason\":\"create_padded_resource_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\",\"bytes\":%llu",
				hr, resource->name.c_str(), static_cast<unsigned long long>(srvByteWidth));
			return false;
		}

		void *dst = nullptr;
		D3D12_RANGE dstReadRange = {};
		hr = srvBacking->Map(0, &dstReadRange, &dst);
		if (FAILED(hr) || !dst) {
			srvBacking->Release();
			heap->Release();
			return false;
		}
		memset(dst, 0, static_cast<size_t>(srvByteWidth));
		void *src = nullptr;
		D3D12_RANGE srcReadRange = { 0, static_cast<SIZE_T>(resource->byteWidth) };
		hr = resource->resource->Map(0, &srcReadRange, &src);
		if (SUCCEEDED(hr) && src) {
			memcpy(dst, src, static_cast<size_t>(resource->byteWidth));
			D3D12_RANGE srcWrittenRange = {};
			resource->resource->Unmap(0, &srcWrittenRange);
		}
		srvBacking->Unmap(0, nullptr);
		if (FAILED(hr) || !src) {
			srvBacking->Release();
			heap->Release();
			DX12LogDebugJsonFunc("DX12PreSkinningSrv",
				"\"status\":\"failed\",\"reason\":\"copy_padded_resource_failed\",\"hr\":\"0x%lx\",\"resource\":\"%S\"",
				hr, resource->name.c_str());
			return false;
		}
		resource->srvResource = srvBacking;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = static_cast<UINT>(
		(std::min)(srvByteWidth / elementStride, static_cast<UINT64>(UINT_MAX)));
	srvDesc.Buffer.StructureByteStride = elementStride;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	resource->srvCpu = heap->GetCPUDescriptorHandleForHeapStart();
	device->CreateShaderResourceView(srvBacking, &srvDesc, resource->srvCpu);
	resource->srvHeap = heap;
	resource->srvStride = elementStride;
	resource->srvByteWidth = srvByteWidth;
	DX12LogDebugJsonFunc("DX12PreSkinningSrv",
		"\"status\":\"created\",\"resource\":\"%S\",\"bytes\":%llu,\"srvBytes\":%llu,\"stride\":%u,\"elements\":%u",
		resource->name.c_str(),
		static_cast<unsigned long long>(resource->byteWidth),
		static_cast<unsigned long long>(srvByteWidth),
		elementStride, srvDesc.Buffer.NumElements);
	return true;
}

static DX12LoadedResource *FindReplacementResourceForVlrLocked(
	ID3D12Device *device, const DX12VertexLimitRaiseConfig &vlr)
{
	DX12LoadedResource *best = nullptr;
	for (const std::wstring &name : gVlrResourceCandidates) {
		auto configIt = gResources.find(name);
		if (configIt == gResources.end())
			continue;
		const Bunny::ResourceConfig &config = configIt->second;
		if (config.hasStride && vlr.overrideByteStride &&
		    config.stride != vlr.overrideByteStride)
			continue;
		DX12LoadedResource *resource = EnsureLoadedResourceLocked(device, name);
		if (!resource || !resource->resource)
			continue;
		if (resource->byteWidth < vlr.overrideByteWidth)
			continue;
		if (!best || resource->byteWidth < best->byteWidth)
			best = resource;
	}
	return best;
}

static bool ProducerMatchesAnyVlr(
	const DX12ComputeUavProducer &producer, uint64_t computeShaderHash,
	DX12VertexLimitRaiseConfig *match)
{
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (vlr.hasMatchCs && vlr.matchCs != computeShaderHash)
			continue;
		if (!ProducerMatchesReplacementTarget(producer, vlr))
			continue;
		if (match)
			*match = vlr;
		return true;
	}
	return false;
}

static bool ResourceMatchesVlrTarget(
	const DX12LoadedResource &resource, const DX12VertexLimitRaiseConfig &vlr)
{
	if (!vlr.overrideByteWidth)
		return false;
	if (resource.byteWidth < vlr.overrideByteWidth)
		return false;
	if (vlr.overrideByteStride && resource.stride &&
	    resource.stride != vlr.overrideByteStride)
		return false;
	return true;
}

static UINT64 DescriptorViewSize(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.viewSize)
		return descriptor.viewSize;
	if (descriptor.hasResourceDesc &&
	    descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
	    descriptor.resourceDesc.Width > descriptor.resourceOffset)
		return descriptor.resourceDesc.Width - descriptor.resourceOffset;
	return 0;
}

static void LogPreSkinSrvProbe(
	const char *status, const DX12CurrentComputeUavBinding *binding,
	const Bunny::TextureOverrideConfig *config, uint32_t vbSlot,
	UINT64 viewBytes, UINT elementStride)
{
#if defined(_DEBUG)
	if (!binding) {
		DX12LogDebugJsonFunc("DX12PreSkinSrvProbe",
			"\"status\":\"%s\"", status ? status : "");
		return;
	}

	DX12LogDebugJsonFunc("DX12PreSkinSrvProbe",
		"\"status\":\"%s\",\"section\":\"%S\",\"root\":%u,\"range\":%u,\"tRegister\":%u,\"space\":%u,\"offset\":%u,\"rootDescriptor\":%s,\"hasDescriptor\":%s,\"kind\":\"%s\",\"viewDimension\":%u,\"resource\":\"%p\",\"gpuVa\":\"0x%llx\",\"viewBytes\":%llu,\"stride\":%u,\"vbSlot\":%u,\"hasMappedResource\":%s",
		status ? status : "",
		config ? config->section.c_str() : L"",
		binding->rootParameterIndex, binding->rangeIndex,
		binding->shaderRegister, binding->registerSpace,
		binding->descriptorOffset,
		binding->rootDescriptor ? "true" : "false",
		binding->hasDescriptor ? "true" : "false",
		binding->descriptor.kind.c_str(),
		binding->descriptor.viewDimension,
		binding->descriptor.resource,
		static_cast<unsigned long long>(binding->descriptor.gpuVirtualAddress),
		static_cast<unsigned long long>(viewBytes),
		elementStride, vbSlot,
		(config && vbSlot != UINT_MAX &&
		 config->vertexBufferResources.find(vbSlot) != config->vertexBufferResources.end()) ? "true" : "false");
#else
	(void)status;
	(void)binding;
	(void)config;
	(void)vbSlot;
	(void)viewBytes;
	(void)elementStride;
#endif
}

static void LogPreSkinCbvProbe(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &cbv,
	const Bunny::TextureOverrideConfig &config)
{
#if defined(_DEBUG)
	(void)device;
	(void)commandList;

	UINT64 gpuVa = cbv.gpuVirtualAddress;
	UINT64 viewBytes = 0;
	ID3D12Resource *resource = nullptr;
	UINT64 resourceOffset = 0;
	UINT heapType = 0;
	bool hasHeapType = false;
	if (cbv.hasDescriptor && cbv.descriptor.kind == "CBV") {
		gpuVa = cbv.descriptor.gpuVirtualAddress;
		viewBytes = cbv.descriptor.viewSize;
		resource = cbv.descriptor.resource;
		resourceOffset = cbv.descriptor.resourceOffset;
		heapType = cbv.descriptor.resourceHeapType;
		hasHeapType = cbv.descriptor.hasResourceHeapType;
	} else if (gpuVa) {
		DX12BufferResourceSummary summary = {};
		if (DX12ResolveBufferResourceByGpuVa(gpuVa, 256, &summary)) {
			resource = summary.resource;
			resourceOffset = summary.resourceOffset;
			viewBytes = summary.viewSize;
			heapType = summary.resourceHeapType;
			hasHeapType = summary.hasResourceHeapType;
		}
	}

	UINT32 words[16] = {};
	bool mapped = false;
	HRESULT mapHr = E_FAIL;
	if (resource) {
		const UINT64 bytesToRead = (std::min<UINT64>)(sizeof(words),
			viewBytes ? viewBytes : sizeof(words));
		D3D12_RANGE readRange = {
			static_cast<SIZE_T>(resourceOffset),
			static_cast<SIZE_T>(resourceOffset + bytesToRead)
		};
		void *mappedPtr = nullptr;
		mapHr = resource->Map(0, &readRange, &mappedPtr);
		if (SUCCEEDED(mapHr) && mappedPtr) {
			memcpy(words, static_cast<const unsigned char*>(mappedPtr) + resourceOffset,
				static_cast<size_t>(bytesToRead));
			D3D12_RANGE writtenRange = {};
			resource->Unmap(0, &writtenRange);
			mapped = true;
		}
	}

	DX12LogDebugJsonFunc("DX12PreSkinCbvProbe",
		"\"section\":\"%S\",\"root\":%u,\"range\":%u,\"bRegister\":%u,\"space\":%u,\"offset\":%u,\"rootDescriptor\":%s,\"hasDescriptor\":%s,\"resource\":\"%p\",\"gpuVa\":\"0x%llx\",\"resourceOffset\":%llu,\"viewBytes\":%llu,\"heapType\":%u,\"hasHeapType\":%s,\"mapped\":%s,\"mapHr\":\"0x%lx\",\"u32\":\"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\",\"u64_0\":\"0x%016llx\",\"u64_1\":\"0x%016llx\",\"u64_2\":\"0x%016llx\",\"u64_3\":\"0x%016llx\"",
		config.section.c_str(),
		cbv.rootParameterIndex, cbv.rangeIndex,
		cbv.shaderRegister, cbv.registerSpace, cbv.descriptorOffset,
		cbv.rootDescriptor ? "true" : "false",
		cbv.hasDescriptor ? "true" : "false",
		resource,
		static_cast<unsigned long long>(gpuVa),
		static_cast<unsigned long long>(resourceOffset),
		static_cast<unsigned long long>(viewBytes),
		heapType,
		hasHeapType ? "true" : "false",
		mapped ? "true" : "false",
		mapHr,
		words[0], words[1], words[2], words[3],
		words[4], words[5], words[6], words[7],
		words[8], words[9], words[10], words[11],
		words[12], words[13], words[14], words[15],
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[1]) << 32) | words[0]),
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[3]) << 32) | words[2]),
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[5]) << 32) | words[4]),
		static_cast<unsigned long long>(
			(static_cast<UINT64>(words[7]) << 32) | words[6]));
#else
	(void)device;
	(void)commandList;
	(void)cbv;
	(void)config;
#endif
}

static UINT AlignCbvSize(UINT size)
{
	return (size + 255u) & ~255u;
}

static bool ReadPreSkinCbvBytesLocked(
	ID3D12Resource *resource, UINT64 sourceOffset, UINT viewBytes,
	std::vector<unsigned char> *bytes, HRESULT *hrOut)
{
	if (hrOut)
		*hrOut = E_FAIL;
	if (!resource || !viewBytes || !bytes)
		return false;

	const DX12PreSkinCbvReadCacheKey key = {
		resource, sourceOffset, viewBytes, DX12GetPresentCount()
	};
	auto cached = gPreSkinCbvReadCache.find(key);
	if (cached != gPreSkinCbvReadCache.end()) {
		if (hrOut)
			*hrOut = cached->second.hr;
		if (FAILED(cached->second.hr))
			return false;
		*bytes = cached->second.bytes;
		return true;
	}

	DX12PreSkinCbvReadCacheValue value;
	value.bytes.assign(viewBytes, 0);
	D3D12_RANGE readRange = {
		static_cast<SIZE_T>(sourceOffset),
		static_cast<SIZE_T>(sourceOffset + viewBytes)
	};
	void *sourceMapped = nullptr;
	value.hr = resource->Map(0, &readRange, &sourceMapped);
	if (SUCCEEDED(value.hr) && sourceMapped) {
		memcpy(value.bytes.data(),
			static_cast<const unsigned char*>(sourceMapped) + sourceOffset,
			viewBytes);
		D3D12_RANGE writtenRange = {};
		resource->Unmap(0, &writtenRange);
	}

	if (gPreSkinCbvReadCache.size() > 128)
		gPreSkinCbvReadCache.clear();
	gPreSkinCbvReadCache[key] = value;
	if (hrOut)
		*hrOut = value.hr;
	if (FAILED(value.hr) || !sourceMapped)
		return false;
	*bytes = value.bytes;
	return true;
}

static bool PatchPreSkinVertexCountCbvLocked(
	ID3D12Device *device, const DX12CurrentComputeUavBinding &cbv,
	const Bunny::TextureOverrideConfig &config, UINT overrideVertexCount,
	D3D12_CPU_DESCRIPTOR_HANDLE dst,
	std::vector<ID3D12Resource*> *temporaryResources,
	UINT *originalVertexCountOut,
	UINT *overrideVertexCountOut)
{
	if (!device || !overrideVertexCount || !dst.ptr || !temporaryResources)
		return false;
	if (!cbv.hasDescriptor || cbv.rootDescriptor || cbv.descriptor.kind != "CBV" ||
	    !cbv.descriptor.resource)
		return false;

	const UINT64 sourceOffset = cbv.descriptor.resourceOffset;
	UINT viewBytes = cbv.descriptor.cbvSize;
	if (!viewBytes && cbv.descriptor.viewSize)
		viewBytes = static_cast<UINT>((std::min<UINT64>)(cbv.descriptor.viewSize, UINT_MAX));
	if (!viewBytes)
		viewBytes = 256;
	viewBytes = AlignCbvSize(viewBytes);

	std::vector<unsigned char> bytes;
	HRESULT hr = E_FAIL;
	if (!ReadPreSkinCbvBytesLocked(cbv.descriptor.resource, sourceOffset, viewBytes, &bytes, &hr)) {
#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinCbvPatch",
			"\"status\":\"skip_map_failed\",\"section\":\"%S\",\"root\":%u,\"bRegister\":%u,\"hr\":\"0x%lx\"",
			config.section.c_str(), cbv.rootParameterIndex, cbv.shaderRegister, hr);
#endif
		return false;
	}

	UINT32 originalVertexCount = 0;
	memcpy(&originalVertexCount, bytes.data(), sizeof(originalVertexCount));
	if (!originalVertexCount || originalVertexCount >= overrideVertexCount) {
#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinCbvPatch",
			"\"status\":\"skip_no_raise\",\"section\":\"%S\",\"root\":%u,\"bRegister\":%u,\"old\":%u,\"new\":%u",
			config.section.c_str(), cbv.rootParameterIndex, cbv.shaderRegister,
			originalVertexCount, overrideVertexCount);
#endif
		return false;
	}
	memcpy(bytes.data(), &overrideVertexCount, sizeof(overrideVertexCount));

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = viewBytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource *patchedResource = nullptr;
	hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&patchedResource));
	if (FAILED(hr) || !patchedResource)
		return false;

	void *patchedMapped = nullptr;
	D3D12_RANGE patchedReadRange = {};
	hr = patchedResource->Map(0, &patchedReadRange, &patchedMapped);
	if (FAILED(hr) || !patchedMapped) {
		patchedResource->Release();
		return false;
	}
	memcpy(patchedMapped, bytes.data(), viewBytes);
	patchedResource->Unmap(0, nullptr);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = patchedResource->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = viewBytes;
	device->CreateConstantBufferView(&cbvDesc, dst);
	temporaryResources->push_back(patchedResource);
	if (originalVertexCountOut)
		*originalVertexCountOut = originalVertexCount;
	if (overrideVertexCountOut)
		*overrideVertexCountOut = overrideVertexCount;

#if defined(_DEBUG)
	DX12LogDebugJsonFunc("DX12PreSkinCbvPatch",
		"\"status\":\"patched\",\"section\":\"%S\",\"root\":%u,\"bRegister\":%u,\"old\":%u,\"new\":%u,\"bytes\":%u",
		config.section.c_str(), cbv.rootParameterIndex, cbv.shaderRegister,
		originalVertexCount, overrideVertexCount, viewBytes);
#endif
	return true;
}

static uint32_t HashComputeUavBinding(const DX12CurrentComputeUavBinding &uav)
{
	if (!uav.hasDescriptor || !uav.descriptor.resource)
		return 0;
	const DX12DescriptorSummary &descriptor = uav.descriptor;
	if (descriptor.kind != "UAV" || descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return 0;

	UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress + descriptor.resourceOffset;
	if (!fallbackGpuVa)
		fallbackGpuVa = uav.gpuVirtualAddress;
	const UINT64 fallbackSize = DescriptorViewSize(descriptor);
	if (!fallbackGpuVa && !fallbackSize)
		return 0;

	DX12BufferResourceSummary summary = {};
	if (DX12ResolveBufferResourceByGpuVa(fallbackGpuVa, fallbackSize, &summary))
		return DX12HashBufferResourceView(&summary, fallbackGpuVa, fallbackSize);

	DX12BufferResourceSummary descriptorSummary = {};
	descriptorSummary.resource = descriptor.resource;
	descriptorSummary.resourceDesc = descriptor.resourceDesc;
	descriptorSummary.hasResourceDesc = descriptor.hasResourceDesc;
	descriptorSummary.resourceHeapType = descriptor.resourceHeapType;
	descriptorSummary.hasResourceHeapType = descriptor.hasResourceHeapType;
	descriptorSummary.gpuVirtualAddress = descriptor.gpuVirtualAddress;
	descriptorSummary.resourceOffset = descriptor.resourceOffset;
	descriptorSummary.viewSize = fallbackSize;
	descriptorSummary.currentState = descriptor.currentState;
	descriptorSummary.hasCurrentState = descriptor.hasCurrentState;
	return DX12HashBufferResourceView(&descriptorSummary, fallbackGpuVa, fallbackSize);
}

static bool FindUavHashTextureOverrideLocked(
	uint32_t hash, DX12UavHashTextureOverrideMatch *match)
{
	if (!hash)
		return false;
	auto indexed = gPreSkinUavHashTextureOverrideIndex.find(hash);
	if (indexed == gPreSkinUavHashTextureOverrideIndex.end())
		return false;

	for (size_t index : indexed->second) {
		if (index >= gPreSkinTextureOverrides.size())
			continue;
		const DX12PreSkinTextureOverrideCandidate &candidate =
			gPreSkinTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (config.hasMatchCs || config.vertexBufferResources.empty())
			continue;
		if (match) {
			match->config = config;
			match->hash = hash;
			match->resourceName = candidate.resourceName;
		}
		return true;
	}
	return false;
}

static bool FindPreSkinProducerByUavHashLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	DX12ComputeUavProducer *producer,
	DX12CurrentComputeUavBinding *binding,
	DX12UavHashTextureOverrideMatch *match)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it) {
		if (!it->hasDescriptor || it->rootDescriptor || !it->descriptorIncrement ||
		    !it->tableCpuStart || !it->tableGpuStart.ptr)
			continue;
		if (!it->descriptor.resource || it->descriptor.viewSize == 0)
			continue;

		const uint32_t hash = HashComputeUavBinding(*it);
		DX12UavHashTextureOverrideMatch candidateMatch;
		const bool matched = FindUavHashTextureOverrideLocked(hash, &candidateMatch);
#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinUavHashCandidate",
			"\"hash\":\"%08x\",\"matched\":%s,\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"viewBytes\":%llu,\"stride\":%u,\"resource\":\"%p\"",
			hash, matched ? "true" : "false",
			it->rootParameterIndex, it->rangeIndex, it->shaderRegister,
			it->registerSpace,
			static_cast<unsigned long long>(it->descriptor.viewSize),
			it->descriptor.structureByteStride,
			it->descriptor.resource);
#endif
		if (!matched)
			continue;

		DX12ComputeUavProducer candidate = {};
		candidate.rootParameterIndex = it->rootParameterIndex;
		candidate.rangeIndex = it->rangeIndex;
		candidate.shaderRegister = it->shaderRegister;
		candidate.registerSpace = it->registerSpace;
		candidate.descriptorOffset = it->descriptorOffset;
		candidate.rootDescriptor = it->rootDescriptor;
		candidate.gpuVirtualAddress = it->gpuVirtualAddress;
		candidate.hasDescriptor = it->hasDescriptor;
		candidate.descriptor = it->descriptor;
		if (producer)
			*producer = candidate;
		if (binding)
			*binding = *it;
		if (match)
			*match = candidateMatch;
		return true;
	}
	return false;
}

static bool FindMatchCsTextureOverrideLocked(
	uint64_t csHash, UINT64 viewBytes, DX12UavHashTextureOverrideMatch *match)
{
	if (!csHash)
		return false;
	auto indexed = gPreSkinMatchCsTextureOverrideIndex.find(csHash);
	if (indexed == gPreSkinMatchCsTextureOverrideIndex.end())
		return false;

	for (size_t index : indexed->second) {
		if (index >= gPreSkinTextureOverrides.size())
			continue;
		const DX12PreSkinTextureOverrideCandidate &candidate =
			gPreSkinTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (!config.hasMatchCs || config.matchCs != csHash ||
		    config.vertexBufferResources.empty())
			continue;
		// match_uav_bytes depends on the live UAV descriptor, so it remains a
		// runtime filter while the expensive TextureOverride scan is preindexed.
		if (config.hasMatchUavBytes && config.matchUavBytes != viewBytes)
			continue;
		if (match) {
			match->config = config;
			match->hash = candidate.hash;
			match->resourceName = candidate.resourceName;
		}
		return true;
	}
	return false;
}

static bool HasMatchCsTextureOverrideLocked(uint64_t csHash)
{
	if (!csHash)
		return false;
	auto indexed = gPreSkinMatchCsTextureOverrideIndex.find(csHash);
	return indexed != gPreSkinMatchCsTextureOverrideIndex.end() &&
		!indexed->second.empty();
}

static bool IsPreSkinMatchCsUavCandidate(const DX12CurrentComputeUavBinding &binding)
{
	if (!binding.hasDescriptor || binding.rootDescriptor || !binding.descriptorIncrement ||
	    !binding.tableCpuStart || !binding.tableGpuStart.ptr)
		return false;
	if (!binding.descriptor.resource || binding.descriptor.viewSize == 0)
		return false;
	if (binding.descriptor.kind != "UAV" ||
	    binding.descriptor.viewDimension != D3D12_UAV_DIMENSION_BUFFER)
		return false;
	return true;
}

static bool FindPreSkinProducerByMatchCsLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs, uint64_t csHash,
	DX12ComputeUavProducer *producer,
	DX12CurrentComputeUavBinding *binding,
	DX12UavHashTextureOverrideMatch *match)
{
	DX12UavHashTextureOverrideMatch candidateMatch = {};
	DX12UavHashTextureOverrideMatch bestMatch = {};
	const DX12CurrentComputeUavBinding *best = nullptr;
	UINT64 bestBytes = 0;
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it) {
		if (!IsPreSkinMatchCsUavCandidate(*it))
			continue;
		const UINT64 viewBytes = DescriptorViewSize(it->descriptor);
		if (viewBytes < 1024)
			continue;
		const bool matched = FindMatchCsTextureOverrideLocked(
			csHash, viewBytes, &candidateMatch);
		if (matched && (!best || viewBytes > bestBytes)) {
			best = &(*it);
			bestBytes = viewBytes;
			bestMatch = candidateMatch;
		}
#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinCsMatchCandidate",
			"\"cs\":\"%016llx\",\"matched\":%s,\"section\":\"%S\",\"expectedBytes\":%llu,\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"viewBytes\":%llu,\"stride\":%u,\"resource\":\"%p\"",
			static_cast<unsigned long long>(csHash),
			matched ? "true" : "false",
			matched ? candidateMatch.config.originalSection.c_str() : L"",
			matched && candidateMatch.config.hasMatchUavBytes ?
				static_cast<unsigned long long>(candidateMatch.config.matchUavBytes) : 0ull,
			it->rootParameterIndex, it->rangeIndex, it->shaderRegister,
			it->registerSpace,
			static_cast<unsigned long long>(viewBytes),
			it->descriptor.structureByteStride,
			it->descriptor.resource);
#endif
	}
	if (!best)
		return false;

	DX12ComputeUavProducer candidate = {};
	candidate.rootParameterIndex = best->rootParameterIndex;
	candidate.rangeIndex = best->rangeIndex;
	candidate.shaderRegister = best->shaderRegister;
	candidate.registerSpace = best->registerSpace;
	candidate.descriptorOffset = best->descriptorOffset;
	candidate.rootDescriptor = best->rootDescriptor;
	candidate.gpuVirtualAddress = best->gpuVirtualAddress;
	candidate.hasDescriptor = best->hasDescriptor;
	candidate.descriptor = best->descriptor;
	if (producer)
		*producer = candidate;
	if (binding)
		*binding = *best;
	if (match)
		*match = bestMatch;
	return true;
}

static UINT FindPreSkinUavElementStrideLocked(const DX12LoadedResource &resource)
{
	for (const DX12VertexLimitRaiseConfig &vlr : gVertexLimitRaiseConfigs) {
		if (vlr.hasMatchCs &&
		    ResourceBlockedByInactiveExplicitMatchCsLocked(resource.name))
			continue;
		if (!ResourceMatchesVlrTarget(resource, vlr))
			continue;
		if (vlr.uavByteStride)
			return vlr.uavByteStride;
		if (vlr.overrideByteStride)
			return vlr.overrideByteStride;
	}
	return resource.stride ? resource.stride : 4;
}

static bool FindPreSkinProducerLocked(
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	uint64_t computeShaderHash,
	DX12ComputeUavProducer *producer,
	DX12VertexLimitRaiseConfig *vlr)
{
	for (auto it = uavs.rbegin(); it != uavs.rend(); ++it) {
		if (!it->hasDescriptor || it->rootDescriptor || !it->descriptorIncrement ||
		    !it->tableCpuStart || !it->tableGpuStart.ptr)
			continue;
		if (!it->descriptor.resource || it->descriptor.viewSize == 0)
			continue;
		DX12ComputeUavProducer candidate = {};
		candidate.rootParameterIndex = it->rootParameterIndex;
		candidate.rangeIndex = it->rangeIndex;
		candidate.shaderRegister = it->shaderRegister;
		candidate.registerSpace = it->registerSpace;
		candidate.descriptorOffset = it->descriptorOffset;
		candidate.rootDescriptor = it->rootDescriptor;
		candidate.gpuVirtualAddress = it->gpuVirtualAddress;
		candidate.hasDescriptor = it->hasDescriptor;
		candidate.descriptor = it->descriptor;
		DX12VertexLimitRaiseConfig candidateVlr;
		if (!ProducerMatchesAnyVlr(candidate, computeShaderHash, &candidateVlr))
			continue;
		if (producer)
			*producer = candidate;
		if (vlr)
			*vlr = candidateVlr;
		return true;
	}
	return false;
}

static bool ApplyPreSkinDescriptorTablePatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &uavBinding, const DX12ComputeUavProducer &producer,
	DX12LoadedResource *replacement, const Bunny::TextureOverrideConfig &config,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	const wchar_t *section, uint32_t triggerHash,
	UINT *originalVertexCountOut, UINT *overrideVertexCountOut)
{
	if (!device || !commandList || !replacement || !replacement->uavResource ||
	    !replacement->uavCpu.ptr || !uavBinding.hasDescriptor || !uavBinding.tableGpuStart.ptr)
		return false;
	auto logStage = [](const char*, UINT = 0, UINT = 0) {};
	logStage("enter");

	if (replacement->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = replacement->uavResource;
		barrier.Transition.StateBefore = replacement->uavState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
		replacement->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	struct TableCopy
	{
		UINT root = 0;
		SIZE_T cpuStart = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
		ID3D12DescriptorHeap *heap = nullptr;
		UINT increment = 0;
		UINT count = 0;
		UINT tempOffset = 0;
	};
	// Reuse per-thread scratch storage to keep the pre-skin hot path from
	// allocating vectors on every dispatch-driven patch attempt.
	static thread_local std::vector<TableCopy> tlsTables;
	std::vector<TableCopy> &tables = tlsTables;
	tables.clear();
	auto addTable = [&tables](const DX12CurrentComputeUavBinding &binding) {
		if (!binding.hasDescriptor || binding.rootDescriptor || !binding.tableCpuStart ||
		    !binding.tableGpuStart.ptr || !binding.descriptorIncrement || !binding.tableHeap)
			return;
		const UINT needed = (std::max)(
			binding.tableCopyCount ? binding.tableCopyCount : 1,
			binding.descriptorOffset + 1);
		for (TableCopy &table : tables) {
			if (table.root == binding.rootParameterIndex &&
			    table.cpuStart == binding.tableCpuStart &&
			    table.gpuStart.ptr == binding.tableGpuStart.ptr) {
				table.count = (std::max)(table.count, needed);
				return;
			}
		}
		TableCopy table;
		table.root = binding.rootParameterIndex;
		table.cpuStart = binding.tableCpuStart;
		table.gpuStart = binding.tableGpuStart;
		table.heap = binding.tableHeap;
		table.increment = binding.descriptorIncrement;
		table.count = needed;
		tables.push_back(table);
	};

	for (const DX12CurrentComputeUavBinding &cbv : cbvs)
		addTable(cbv);
	for (const DX12CurrentComputeUavBinding &srv : srvs)
		addTable(srv);
	addTable(uavBinding);
	if (tables.empty()) {
		logStage("no_tables");
		return false;
	}

	if (srvs.empty()) {
		LogPreSkinSrvProbe("no_srvs", nullptr, &config, UINT_MAX, 0, 0);
	}
	(void)rootConstants;

	UINT totalDescriptors = 0;
	for (TableCopy &table : tables) {
		table.tempOffset = totalDescriptors;
		totalDescriptors += table.count;
	}
	logStage("tables_built", static_cast<UINT>(tables.size()), totalDescriptors);
	if (!totalDescriptors) {
		logStage("no_descriptors", static_cast<UINT>(tables.size()), totalDescriptors);
		return false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE tempCpuBase = {};
	D3D12_GPU_DESCRIPTOR_HANDLE tempGpuBase = {};
	UINT tempIncrement = 0;
	if (!EnsurePreSkinDescriptorRingLocked(
		    device, totalDescriptors, &tempCpuBase, &tempGpuBase, &tempIncrement)) {
		logStage("descriptor_ring_failed", static_cast<UINT>(tables.size()), totalDescriptors);
		return false;
	}
	logStage("ring_allocated", static_cast<UINT>(tables.size()), totalDescriptors);
	for (const TableCopy &table : tables) {
		D3D12_CPU_DESCRIPTOR_HANDLE src = {};
		src.ptr = table.cpuStart;
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table.tempOffset) * tempIncrement;
		device->CopyDescriptorsSimple(
			table.count, dst, src,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	logStage("tables_copied", static_cast<UINT>(tables.size()), totalDescriptors);

	auto findTable = [&tables](const DX12CurrentComputeUavBinding &binding) -> const TableCopy* {
		for (const TableCopy &table : tables) {
			if (table.root == binding.rootParameterIndex &&
			    table.cpuStart == binding.tableCpuStart &&
			    table.gpuStart.ptr == binding.tableGpuStart.ptr)
				return &table;
		}
		return nullptr;
	};
	static thread_local std::vector<ID3D12Resource*> tlsTemporaryResources;
	std::vector<ID3D12Resource*> &temporaryResources = tlsTemporaryResources;
	temporaryResources.clear();
	(void)originalVertexCountOut;
	(void)overrideVertexCountOut;
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		const UINT64 negativeKey = MakePreSkinSrvProbeCacheKey(srv, config, srvByteWidth);
		const UINT64 stableNegativeKey =
			MakePreSkinSrvStableProbeCacheKey(srv, config, srvByteWidth);
		if (gPreSkinSrvStableNegativeCache.find(stableNegativeKey) !=
		    gPreSkinSrvStableNegativeCache.end())
			continue;
		if (gPreSkinSrvNegativeCache.find(negativeKey) != gPreSkinSrvNegativeCache.end())
			continue;
		if (!srv.hasDescriptor || srv.descriptor.kind != "SRV" ||
		    srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER) {
			if (gPreSkinSrvStableNegativeCache.size() > 1024)
				gPreSkinSrvStableNegativeCache.clear();
			gPreSkinSrvStableNegativeCache.insert(stableNegativeKey);
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_not_buffer_srv", &srv, &config, UINT_MAX, srvByteWidth, 0);
			continue;
		}
		uint32_t vbSlot = UINT_MAX;
		if (srv.shaderRegister == 0)
			vbSlot = 0;
		else if (srv.shaderRegister == 1)
			vbSlot = 2;
		else {
			if (gPreSkinSrvStableNegativeCache.size() > 1024)
				gPreSkinSrvStableNegativeCache.clear();
			gPreSkinSrvStableNegativeCache.insert(stableNegativeKey);
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_unmapped_register", &srv, &config, UINT_MAX, srvByteWidth, 0);
			continue;
		}
		auto resourceIt = config.vertexBufferResources.find(vbSlot);
		if (resourceIt == config.vertexBufferResources.end()) {
			if (gPreSkinSrvStableNegativeCache.size() > 1024)
				gPreSkinSrvStableNegativeCache.clear();
			gPreSkinSrvStableNegativeCache.insert(stableNegativeKey);
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_no_vb_resource", &srv, &config, vbSlot, srvByteWidth, 0);
			continue;
		}
		const TableCopy *table = findTable(srv);
		if (!table) {
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_no_table", &srv, &config, vbSlot, srvByteWidth, 0);
			continue;
		}
		DX12LoadedResource *resource = nullptr;
		UINT elementStride = 0;
		auto stablePositiveIt = gPreSkinSrvStablePositiveCache.find(stableNegativeKey);
		if (stablePositiveIt != gPreSkinSrvStablePositiveCache.end()) {
			// A stable hit skips the DX11-style binding resolution work, but still
			// writes the current descriptor table because heaps are command-list state.
			resource = stablePositiveIt->second.resource;
			elementStride = stablePositiveIt->second.elementStride;
			vbSlot = stablePositiveIt->second.vbSlot;
		} else {
			auto positiveIt = gPreSkinSrvPositiveCache.find(negativeKey);
			if (positiveIt != gPreSkinSrvPositiveCache.end()) {
				resource = positiveIt->second.resource;
				elementStride = positiveIt->second.elementStride;
				vbSlot = positiveIt->second.vbSlot;
			} else {
				resource = EnsureLoadedResourceLocked(device, resourceIt->second);
				elementStride =
					srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride :
					(resource ? resource->stride : 0);
				if (!resource || !EnsureResourceSrvLocked(device, resource, elementStride, srvByteWidth) ||
				    !resource->srvCpu.ptr) {
					gPreSkinSrvNegativeCache.insert(negativeKey);
					LogPreSkinSrvProbe("skip_srv_create_failed", &srv, &config, vbSlot, srvByteWidth, elementStride);
					continue;
				}
				if (gPreSkinSrvPositiveCache.size() > 256)
					gPreSkinSrvPositiveCache.clear();
				if (gPreSkinSrvStablePositiveCache.size() > 512)
					gPreSkinSrvStablePositiveCache.clear();
				// Cache only immutable SRV patch ingredients. The live descriptor
				// destination is intentionally excluded so rewritten tables remain safe.
				const DX12PreSkinSrvPositiveCacheValue cacheValue =
					{ resource, elementStride, srvByteWidth, vbSlot };
				gPreSkinSrvPositiveCache[negativeKey] = cacheValue;
				gPreSkinSrvStablePositiveCache[stableNegativeKey] = cacheValue;
			}
		}
		if (!resource || !resource->srvCpu.ptr) {
			gPreSkinSrvNegativeCache.insert(negativeKey);
			LogPreSkinSrvProbe("skip_srv_create_failed", &srv, &config, vbSlot, srvByteWidth, elementStride);
			continue;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE dst = tempCpuBase;
		dst.ptr += static_cast<SIZE_T>(table->tempOffset + srv.descriptorOffset) * tempIncrement;
		device->CopyDescriptorsSimple(
			1, dst, resource->srvCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		LogPreSkinSrvProbe("patched", &srv, &config, vbSlot, srvByteWidth, elementStride);
#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinningSrvPatch",
			"\"status\":\"table_patched\",\"resource\":\"%S\",\"tRegister\":%u,\"vbSlot\":%u,\"root\":%u,\"offset\":%u,\"bytes\":%llu,\"srvBytes\":%llu,\"stride\":%u",
			resource->name.c_str(), srv.shaderRegister, vbSlot,
			srv.rootParameterIndex, srv.descriptorOffset,
			static_cast<unsigned long long>(resource->byteWidth),
			static_cast<unsigned long long>(resource->srvByteWidth),
			elementStride);
#endif
	}

	const TableCopy *uavTable = findTable(uavBinding);
	if (!uavTable) {
		for (ID3D12Resource *resource : temporaryResources) {
			if (resource)
				resource->Release();
		}
		return false;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE uavDst = tempCpuBase;
	uavDst.ptr += static_cast<SIZE_T>(uavTable->tempOffset + uavBinding.descriptorOffset) * tempIncrement;
	device->CopyDescriptorsSimple(
		1, uavDst, replacement->uavCpu,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	logStage("uav_copied", static_cast<UINT>(tables.size()), totalDescriptors);

	ID3D12DescriptorHeap *restoreCbvSrvUavHeap = nullptr;
	ID3D12DescriptorHeap *restoreSamplerHeap = nullptr;
	DX12BindingGetCurrentDescriptorHeaps(
		commandList, &restoreCbvSrvUavHeap, &restoreSamplerHeap);
	if (restoreCbvSrvUavHeap != gPreSkinDescriptorRing.heap) {
		ID3D12DescriptorHeap *heaps[2] = { gPreSkinDescriptorRing.heap, restoreSamplerHeap };
		logStage("before_set_heaps", static_cast<UINT>(tables.size()), totalDescriptors);
		commandList->SetDescriptorHeaps(restoreSamplerHeap ? 2 : 1, heaps);
		logStage("after_set_heaps", static_cast<UINT>(tables.size()), totalDescriptors);
	}
	DX12PreSkinReplacementState state;
	state.active = true;
	state.rootParameterIndex = uavBinding.rootParameterIndex;
	state.originalTable = uavBinding.tableGpuStart;
	state.originalHeap = uavBinding.tableHeap;
	state.resource = replacement->uavResource;
	state.byteWidth = replacement->byteWidth;
	state.stride = replacement->stride;
	state.patchHeap = gPreSkinDescriptorRing.heap;
	state.restoreCbvSrvUavHeap = restoreCbvSrvUavHeap;
	state.restoreSamplerHeap = restoreSamplerHeap;
	state.temporaryResources = temporaryResources;
	for (const TableCopy &table : tables) {
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = tempGpuBase;
		gpu.ptr += static_cast<UINT64>(table.tempOffset) * tempIncrement;
		logStage("before_set_root_table", static_cast<UINT>(tables.size()), totalDescriptors);
		commandList->SetComputeRootDescriptorTable(table.root, gpu);
		logStage("after_set_root_table", static_cast<UINT>(tables.size()), totalDescriptors);
		DX12PreSkinReplacementState::TableRestore restore;
		restore.rootParameterIndex = table.root;
		restore.originalTable = table.gpuStart;
		state.tableRestores.push_back(restore);
	}
	replacement->uavWritten = true;
	replacement->uavValid = true;
	gActivePreSkinReplacements[commandList] = state;

#if defined(_DEBUG)
	DX12LogDebugJsonFunc("DX12PreSkinningApply",
		"\"status\":\"table_patched\",\"section\":\"%S\",\"resource\":\"%S\",\"triggerHash\":\"%08x\",\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"tables\":%zu,\"descriptors\":%u,\"bytes\":%llu,\"stride\":%u,\"producerResource\":\"%p\",\"producerBytes\":%llu,\"producerStride\":%u",
		section ? section : L"", replacement->name.c_str(), triggerHash,
		uavBinding.rootParameterIndex, uavBinding.rangeIndex,
		uavBinding.shaderRegister, uavBinding.registerSpace,
		tables.size(), totalDescriptors,
		static_cast<unsigned long long>(replacement->byteWidth),
		replacement->stride,
		producer.descriptor.resource,
		static_cast<unsigned long long>(producer.descriptor.viewSize),
		producer.descriptor.structureByteStride);
#endif
	return true;
}

static bool ApplyPreSkinBindingPatchLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
	const DX12CurrentComputeUavBinding &binding, const DX12ComputeUavProducer &producer,
	DX12LoadedResource *replacement, const wchar_t *section, uint32_t triggerHash,
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *descriptorRestores)
{
	if (!device || !commandList || !replacement || !replacement->uavResource ||
	    !replacement->uavCpu.ptr || !binding.hasDescriptor || !binding.tableGpuStart.ptr)
		return false;

	bool alreadyPatched = false;
	AcquireSRWLockExclusive(&gPreSkinLock);
	alreadyPatched = !gPatchedPreSkinDescriptors.insert(
		binding.descriptor.cpuHandle).second;
	if (gPatchedPreSkinDescriptors.size() > 4096)
		gPatchedPreSkinDescriptors.clear();
	gKnownPreSkinDescriptorPatches[binding.descriptor.cpuHandle] =
		replacement->name;
	ReleaseSRWLockExclusive(&gPreSkinLock);
	if (replacement->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = replacement->uavResource;
		barrier.Transition.StateBefore = replacement->uavState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);
		replacement->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE target = {};
	target.ptr = binding.descriptor.cpuHandle;
	if (descriptorRestores) {
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = 1;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ID3D12DescriptorHeap *restoreHeap = nullptr;
		HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&restoreHeap));
		if (FAILED(hr) || !restoreHeap)
			return false;
		DX12PreSkinReplacementState::DescriptorRestore restore;
		restore.targetCpu = target.ptr;
		restore.heap = restoreHeap;
		restore.cpu = restoreHeap->GetCPUDescriptorHandleForHeapStart();
		device->CopyDescriptorsSimple(
			1, restore.cpu, target,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		descriptorRestores->push_back(restore);
	}
	device->CopyDescriptorsSimple(
		1, target, replacement->uavCpu,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	DX12PreSkinReplacementState state;
	state.active = true;
	state.rootParameterIndex = binding.rootParameterIndex;
	state.originalTable = binding.tableGpuStart;
	state.replacementTable = replacement->uavGpu;
	state.descriptorHeap = replacement->uavHeap;
	state.originalHeap = binding.tableHeap;
	state.resource = replacement->uavResource;
	state.byteWidth = replacement->byteWidth;
	state.stride = replacement->stride;
	state.producerSerial = gComputeUavSerial + 1;
	if (descriptorRestores)
		state.descriptorRestores.swap(*descriptorRestores);
	replacement->uavWritten = true;
	replacement->uavValid = true;
	gActivePreSkinReplacements[commandList] = state;

#if defined(_DEBUG)
	if (!alreadyPatched) {
		DX12LogDebugJsonFunc("DX12PreSkinningApply",
			"\"status\":\"patched\",\"section\":\"%S\",\"resource\":\"%S\",\"triggerHash\":\"%08x\",\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"targetCpu\":\"0x%zx\",\"bytes\":%llu,\"stride\":%u,\"producerResource\":\"%p\",\"producerBytes\":%llu,\"producerStride\":%u",
			section ? section : L"", replacement->name.c_str(), triggerHash,
			binding.rootParameterIndex, binding.rangeIndex,
			binding.shaderRegister, binding.registerSpace,
			binding.descriptor.cpuHandle,
			static_cast<unsigned long long>(replacement->byteWidth),
			replacement->stride,
			producer.descriptor.resource,
			static_cast<unsigned long long>(producer.descriptor.viewSize),
			producer.descriptor.structureByteStride);
	}
#else
	(void)alreadyPatched;
#endif
	return true;
}

static bool ApplyPreSkinSrvInputPatchesLocked(
	ID3D12Device *device, const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const Bunny::TextureOverrideConfig &config,
	std::vector<DX12PreSkinReplacementState::DescriptorRestore> *descriptorRestores)
{
	if (!device || srvs.empty())
		return false;

	bool changed = false;
	for (const DX12CurrentComputeUavBinding &srv : srvs) {
		if (!srv.hasDescriptor || !srv.descriptor.cpuHandle ||
		    srv.descriptor.kind != "SRV" ||
		    srv.descriptor.viewDimension != D3D12_SRV_DIMENSION_BUFFER)
			continue;

		uint32_t vbSlot = UINT_MAX;
		if (srv.shaderRegister == 0)
			vbSlot = 0;
		else if (srv.shaderRegister == 1)
			vbSlot = 2;
		else
			continue;

		auto resourceIt = config.vertexBufferResources.find(vbSlot);
		if (resourceIt == config.vertexBufferResources.end())
			continue;

		DX12LoadedResource *resource = EnsureLoadedResourceLocked(device, resourceIt->second);
		const UINT elementStride =
			srv.descriptor.structureByteStride ? srv.descriptor.structureByteStride :
			(resource ? resource->stride : 0);
		const UINT64 srvByteWidth = DescriptorViewSize(srv.descriptor);
		if (!resource || !EnsureResourceSrvLocked(device, resource, elementStride, srvByteWidth) ||
		    !resource->srvCpu.ptr)
			continue;

		D3D12_CPU_DESCRIPTOR_HANDLE target = {};
		target.ptr = srv.descriptor.cpuHandle;
		if (descriptorRestores) {
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors = 1;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			ID3D12DescriptorHeap *restoreHeap = nullptr;
			HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&restoreHeap));
			if (FAILED(hr) || !restoreHeap)
				continue;
			DX12PreSkinReplacementState::DescriptorRestore restore;
			restore.targetCpu = target.ptr;
			restore.heap = restoreHeap;
			restore.cpu = restoreHeap->GetCPUDescriptorHandleForHeapStart();
			device->CopyDescriptorsSimple(
				1, restore.cpu, target,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			descriptorRestores->push_back(restore);
		}
		device->CopyDescriptorsSimple(
			1, target, resource->srvCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		changed = true;

#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12PreSkinningSrvPatch",
			"\"status\":\"patched\",\"resource\":\"%S\",\"tRegister\":%u,\"vbSlot\":%u,\"targetCpu\":\"0x%zx\",\"bytes\":%llu,\"srvBytes\":%llu,\"stride\":%u",
			resource->name.c_str(), srv.shaderRegister, vbSlot,
			srv.descriptor.cpuHandle,
			static_cast<unsigned long long>(resource->byteWidth),
			static_cast<unsigned long long>(resource->srvByteWidth),
			elementStride);
#endif
	}
	return changed;
}

bool DX12ModApplyPreSkinningUavReplacement(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs,
	const std::vector<DX12CurrentComputeUavBinding> &srvs,
	const std::vector<DX12CurrentComputeUavBinding> &cbvs,
	const std::vector<DX12CurrentRootConstants> &rootConstants,
	UINT *originalVertexCount,
	UINT *overrideVertexCount)
{
	if (originalVertexCount)
		*originalVertexCount = 0;
	if (overrideVertexCount)
		*overrideVertexCount = 0;
	if (!commandList || uavs.empty() || gHasTextureOverrides == 0)
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	DX12InternalReplayScope internalReplay;
	bool applied = false;
	DX12ComputeUavProducer producer = {};
	DX12VertexLimitRaiseConfig vlr = {};
	DX12LoadedResource *replacement = nullptr;

	AcquireSRWLockExclusive(&gModLock);
	DX12CurrentComputeUavBinding hashBinding = {};
	DX12UavHashTextureOverrideMatch hashMatch = {};
	bool explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
	bool hashMatched = explicitMatchCs && FindPreSkinProducerByMatchCsLocked(
		uavs, computeShaderHash, &producer, &hashBinding, &hashMatch);
	if (hashMatched) {
		replacement = EnsureLoadedResourceLocked(device, hashMatch.resourceName);
		const UINT elementStride =
			producer.descriptor.structureByteStride ?
			producer.descriptor.structureByteStride : 4;
		if (replacement && EnsureResourceUavLocked(
			device, commandList, replacement, elementStride,
			DescriptorViewSize(producer.descriptor),
			hashMatch.config.hasMatchCs, true)) {
			applied = ApplyPreSkinDescriptorTablePatchLocked(
				device, commandList, hashBinding, producer, replacement,
				hashMatch.config, srvs, cbvs, rootConstants,
				hashMatch.config.originalSection.empty() ?
					hashMatch.config.section.c_str() :
					hashMatch.config.originalSection.c_str(),
				hashMatch.hash, originalVertexCount, overrideVertexCount);
			if (applied) {
				DX12Profiling::RecordPreSkinApplied();
				DX12ActivePreSkinTextureOverride active;
				active.config = hashMatch.config;
				active.producer = producer;
				active.outputResource = replacement->uavResource;
				active.outputByteWidth = replacement->uavByteWidth ? replacement->uavByteWidth : replacement->byteWidth;
				active.outputStride = replacement->stride;
				AcquireSRWLockExclusive(&gPreSkinLock);
				StoreActivePreSkinTextureOverrideLocked(hashMatch.config.section, active);
				ReleaseSRWLockExclusive(&gPreSkinLock);
				++gPreSkinActiveGeneration;
			}
		}
	}
	if (!applied &&
	    FindPreSkinProducerLocked(uavs, computeShaderHash, &producer, &vlr)) {
		replacement = FindReplacementResourceForVlrLocked(device, vlr);
		if (replacement && EnsureResourceUavLocked(
			device, commandList, replacement,
			vlr.uavByteStride ? vlr.uavByteStride : vlr.overrideByteStride,
			0, false, true)) {
			const DX12CurrentComputeUavBinding *binding = nullptr;
			for (const DX12CurrentComputeUavBinding &uav : uavs) {
				if (uav.rootParameterIndex == producer.rootParameterIndex &&
				    uav.rangeIndex == producer.rangeIndex &&
				    uav.descriptorOffset == producer.descriptorOffset &&
				    uav.hasDescriptor && uav.tableGpuStart.ptr) {
					binding = &uav;
					break;
				}
			}
			if (binding) {
				std::vector<DX12PreSkinReplacementState::DescriptorRestore> descriptorRestores;
				applied = ApplyPreSkinBindingPatchLocked(
					device, commandList, *binding, producer, replacement,
					vlr.section.c_str(), 0, &descriptorRestores);
				if (applied)
					DX12Profiling::RecordPreSkinApplied();
				if (!applied)
					ReleasePreSkinDescriptorRestores(&descriptorRestores);
			}
		}
	}
	ReleaseSRWLockExclusive(&gModLock);
	device->Release();
	return applied;
}

bool DX12ModApplyKnownPreSkinningUavPatches(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList || gHasTextureOverrides == 0)
		return false;

	std::vector<std::pair<SIZE_T, std::wstring>> patches;
	AcquireSRWLockShared(&gPreSkinLock);
	patches.assign(gKnownPreSkinDescriptorPatches.begin(), gKnownPreSkinDescriptorPatches.end());
	ReleaseSRWLockShared(&gPreSkinLock);
	if (patches.empty())
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	DX12InternalReplayScope internalReplay;
	bool applied = false;
	AcquireSRWLockExclusive(&gModLock);
	for (const auto &patch : patches) {
		if (ResourceBlockedByInactiveExplicitMatchCsLocked(patch.second))
			continue;
		DX12LoadedResource *resource = EnsureLoadedResourceLocked(device, patch.second);
		if (!resource)
			continue;
		const UINT elementStride = FindPreSkinUavElementStrideLocked(*resource);
		if (!EnsureResourceUavLocked(device, commandList, resource, elementStride))
			continue;
		if (!resource->uavResource || !resource->uavCpu.ptr)
			continue;
		if (resource->uavState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource->uavResource;
			barrier.Transition.StateBefore = resource->uavState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);
			resource->uavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE target = {};
		target.ptr = patch.first;
		device->CopyDescriptorsSimple(
			1, target, resource->uavCpu,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		resource->uavWritten = true;
		resource->uavValid = true;
		applied = true;
	}
	ReleaseSRWLockExclusive(&gModLock);
	device->Release();
	return applied;
}

void DX12ModRestorePreSkinningUavReplacement(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	DX12PreSkinReplacementState state = {};
	bool found = false;
	AcquireSRWLockExclusive(&gModLock);
	auto it = gActivePreSkinReplacements.find(commandList);
	if (it != gActivePreSkinReplacements.end()) {
		state = it->second;
		gActivePreSkinReplacements.erase(it);
		found = state.active;
	}
	ReleaseSRWLockExclusive(&gModLock);

	if (!found)
		return;

#if defined(_DEBUG)
	const size_t restoredDescriptorCount = state.descriptorRestores.size();
	const size_t restoredTableCount = state.tableRestores.size();
#endif
	if (state.patchHeap) {
		ID3D12DescriptorHeap *restoreCbvSrvUavHeap =
			state.restoreCbvSrvUavHeap ? state.restoreCbvSrvUavHeap : state.originalHeap;
		ID3D12DescriptorHeap *heaps[2] = {
			restoreCbvSrvUavHeap, state.restoreSamplerHeap
		};
		UINT heapCount = 0;
		if (heaps[0])
			heapCount++;
		if (heaps[1])
			heapCount++;
		ID3D12DescriptorHeap *currentCbvSrvUavHeap = nullptr;
		ID3D12DescriptorHeap *currentSamplerHeap = nullptr;
		DX12BindingGetCurrentDescriptorHeaps(
			commandList, &currentCbvSrvUavHeap, &currentSamplerHeap);
		if (heapCount &&
		    (currentCbvSrvUavHeap != restoreCbvSrvUavHeap ||
		     currentSamplerHeap != state.restoreSamplerHeap)) {
			// Avoid redundant restore calls because shader-visible heap
			// switches can be expensive on some D3D12 implementations.
			commandList->SetDescriptorHeaps(heapCount, heaps);
		}
		for (const auto &restore : state.tableRestores)
			commandList->SetComputeRootDescriptorTable(
				restore.rootParameterIndex, restore.originalTable);
		state.patchHeap = nullptr;
	}
	for (ID3D12Resource *resource : state.temporaryResources) {
		if (resource)
			RetirePreSkinResourceForCommandList(commandList, resource);
	}
	state.temporaryResources.clear();
	ID3D12Device *device = AcquireModDevice(commandList);
	if (device) {
		for (const auto &restore : state.descriptorRestores) {
			if (!restore.heap || !restore.cpu.ptr || !restore.targetCpu)
				continue;
			D3D12_CPU_DESCRIPTOR_HANDLE target = {};
			target.ptr = restore.targetCpu;
			device->CopyDescriptorsSimple(
				1, target, restore.cpu,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		device->Release();
	}
	ReleasePreSkinDescriptorRestores(&state.descriptorRestores);

#if defined(_DEBUG)
	DX12LogDebugJsonFunc("DX12PreSkinningApply",
		"\"status\":\"post_dispatch\",\"root\":%u,\"bytes\":%llu,\"stride\":%u,\"restoredDescriptors\":%zu,\"restoredTables\":%zu",
		state.rootParameterIndex,
		static_cast<unsigned long long>(state.byteWidth), state.stride,
		restoredDescriptorCount, restoredTableCount);
#endif
}

bool DX12ModReplaceShaderBytecode(
	const char *stage, const D3D12_SHADER_BYTECODE &source,
	D3D12_SHADER_BYTECODE *replacement, std::vector<unsigned char> *storage)
{
	if (!stage || !source.pShaderBytecode || source.BytecodeLength == 0 ||
	    !replacement || !storage)
		return false;

	const uint64_t hash = DX12ModHashShaderBytecode(
		source.pShaderBytecode, source.BytecodeLength);

	std::wstring section;
	std::wstring shaderFixesDir;
	AcquireSRWLockShared(&gModLock);
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end()) {
		ReleaseSRWLockShared(&gModLock);
		return false;
	}
	section = it->second.section;
	shaderFixesDir = gShaderFixesDir;
	ReleaseSRWLockShared(&gModLock);

	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%016llx-%S.bin",
		shaderFixesDir.c_str(), static_cast<unsigned long long>(hash), stage);

	std::vector<unsigned char> bytes;
	if (!ReadFileBytes(path, &bytes)) {
		DX12LogDebugJsonFunc("DX12ShaderOverrideMissingReplacement",
			"\"section\":\"%S\",\"stage\":\"%s\",\"hash\":\"%016llx\",\"path\":\"%S\"",
			section.c_str(), stage, static_cast<unsigned long long>(hash), path);
		return false;
	}

	storage->swap(bytes);
	replacement->pShaderBytecode = storage->data();
	replacement->BytecodeLength = storage->size();
	DX12LogDebugJsonFunc("DX12ShaderOverrideApplied",
		"\"section\":\"%S\",\"stage\":\"%s\",\"hash\":\"%016llx\",\"path\":\"%S\",\"bytes\":%zu",
		section.c_str(), stage, static_cast<unsigned long long>(hash), path, storage->size());
	return true;
}

bool DX12ModHasShaderOverride(uint64_t hash)
{
	AcquireSRWLockShared(&gModLock);
	bool found = gShaderOverrides.find(hash) != gShaderOverrides.end();
	ReleaseSRWLockShared(&gModLock);
	return found;
}

bool DX12ModHasActiveShaderOverrides()
{
	return gHasShaderOverrides != 0;
}

bool DX12ModHasActiveTextureOverrides()
{
	return gHasTextureOverrides != 0;
}

bool DX12ModHasAnyActiveOverrides()
{
	return gHasShaderOverrides != 0 || gHasTextureOverrides != 0;
}

bool DX12ModNeedsPresentReplacement()
{
	return gHasPresentRuntimeEffect != 0 &&
		InterlockedCompareExchange(&gDx12SafeMode, 0, 0) == 0;
}

bool DX12ModNeedsPreSkinningUavProbe()
{
	return gHasTextureOverrides != 0 && gHasUavHashTextureOverrides != 0;
}

bool DX12ModHasActivePreSkinTextureOverrides()
{
	bool active = false;
	AcquireSRWLockShared(&gPreSkinLock);
	active = !gActivePreSkinTextureOverrides.empty();
	ReleaseSRWLockShared(&gPreSkinLock);
	return active;
}

bool DX12ModShouldProbePreSkinningForCs(uint64_t computeShaderHash)
{
	if (gHasTextureOverrides == 0 || gHasUavHashTextureOverrides == 0)
		return false;

	bool shouldProbe = false;
	AcquireSRWLockShared(&gModLock);
	if (gHasPreSkinVlrWithoutMatchCs) {
		shouldProbe = true;
	} else if (computeShaderHash) {
		shouldProbe = gPreSkinMatchCsHashes.find(computeShaderHash) !=
			gPreSkinMatchCsHashes.end();
	}
	ReleaseSRWLockShared(&gModLock);
	DX12Profiling::RecordPreSkinCsTest(shouldProbe);
	return shouldProbe;
}

bool DX12ModShouldTrackPreSkinBindingsForCs(uint64_t computeShaderHash)
{
	if (!DX12ModShouldProbePreSkinningForCs(computeShaderHash))
		return false;
	if (gHasPreSkinVlrWithoutMatchCs)
		return true;
	if (gHasExplicitPreSkinMatchCsUavBytes == 0)
		return false;

	bool shouldTrack = false;
	AcquireSRWLockShared(&gModLock);
	auto byteFilter = gPreSkinMatchCsUavByteFilters.find(computeShaderHash);
	shouldTrack = byteFilter != gPreSkinMatchCsUavByteFilters.end() &&
		!byteFilter->second.empty();
	ReleaseSRWLockShared(&gModLock);
	return shouldTrack;
}

bool DX12ModPreSkinningUavProducerMayMatch(
	uint64_t computeShaderHash,
	const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	if (uavs.empty() || gHasTextureOverrides == 0 || gHasUavHashTextureOverrides == 0)
		return false;

	bool hasCandidateUav = false;
	AcquireSRWLockShared(&gModLock);
	const bool explicitMatchCs = HasMatchCsTextureOverrideLocked(computeShaderHash);
	if (explicitMatchCs) {
		auto byteFilter = gPreSkinMatchCsUavByteFilters.find(computeShaderHash);
		const bool hasByteFilter = byteFilter != gPreSkinMatchCsUavByteFilters.end() &&
			!byteFilter->second.empty();
		if (!hasByteFilter) {
			// Explicit match_cs alone is too broad for the dispatch hot path.
			// Require match_uav_bytes before we keep root-table tracking active,
			// otherwise startup compute traffic can look like our own GPU work.
			ReleaseSRWLockShared(&gModLock);
			DX12Profiling::RecordPreSkinUavTest(false);
			return false;
		}
		for (const DX12CurrentComputeUavBinding &uav : uavs) {
			if (!IsPreSkinMatchCsUavCandidate(uav))
				continue;
			const UINT64 viewBytes = DescriptorViewSize(uav.descriptor);
			if (viewBytes < 1024)
				continue;
			// D3D12 dispatch binding state is expressed through root tables; this
			// cheap byte-size gate avoids a second locked descriptor/hash scan on
			// compute-heavy frames before the real replacement path runs.
			if (!hasByteFilter || byteFilter->second.find(viewBytes) != byteFilter->second.end()) {
				hasCandidateUav = true;
				break;
			}
		}
	}
	if (!hasCandidateUav && !explicitMatchCs && gHasPreSkinVlrWithoutMatchCs) {
		for (const DX12CurrentComputeUavBinding &uav : uavs) {
			if (!uav.hasDescriptor || uav.rootDescriptor || !uav.descriptorIncrement ||
			    !uav.tableCpuStart || !uav.tableGpuStart.ptr)
				continue;
			if (!uav.descriptor.resource || uav.descriptor.viewSize == 0)
				continue;
			hasCandidateUav = true;
			break;
		}
	}
	ReleaseSRWLockShared(&gModLock);
	DX12Profiling::RecordPreSkinUavTest(hasCandidateUav);
	return hasCandidateUav;
}

bool DX12ModHasCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool *matched)
{
	if (!matched || !commandList || !computeShaderHash || computeBindingSerial == 0)
		return false;

	AcquireSRWLockShared(&gModLock);
	auto cached = gPreSkinUavMatchCache.find(commandList);
	if (cached != gPreSkinUavMatchCache.end() &&
	    cached->second.valid &&
	    cached->second.computeShaderHash == computeShaderHash &&
	    cached->second.computeBindingSerial == computeBindingSerial) {
		*matched = cached->second.matched;
		ReleaseSRWLockShared(&gModLock);
		return true;
	}
	ReleaseSRWLockShared(&gModLock);
	return false;
}

static void UpdatePreSkinningUavMatchCacheLocked(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool matched)
{
	DX12PreSkinUavMatchCacheEntry &entry = gPreSkinUavMatchCache[commandList];
	entry.computeShaderHash = computeShaderHash;
	entry.computeBindingSerial = computeBindingSerial;
	entry.valid = true;
	entry.matched = matched;
}

void DX12ModStoreCachedPreSkinningUavMatch(
	ID3D12GraphicsCommandList *commandList,
	uint64_t computeShaderHash,
	UINT64 computeBindingSerial,
	bool matched)
{
	if (!commandList || !computeShaderHash || computeBindingSerial == 0)
		return;

	AcquireSRWLockExclusive(&gModLock);
	UpdatePreSkinningUavMatchCacheLocked(
		commandList, computeShaderHash, computeBindingSerial, matched);
	ReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputeUavs(
	ID3D12GraphicsCommandList *commandList, const std::vector<DX12CurrentComputeUavBinding> &uavs)
{
	if (!commandList || uavs.empty() || gHasTextureOverrides == 0)
		return;

	AcquireSRWLockExclusive(&gPreSkinLock);
	for (const DX12CurrentComputeUavBinding &uav : uavs) {
		DX12ComputeUavProducer producer;
		producer.serial = ++gComputeUavSerial;
		producer.rootParameterIndex = uav.rootParameterIndex;
		producer.rangeIndex = uav.rangeIndex;
		producer.shaderRegister = uav.shaderRegister;
		producer.registerSpace = uav.registerSpace;
		producer.descriptorOffset = uav.descriptorOffset;
		producer.rootDescriptor = uav.rootDescriptor;
		producer.gpuVirtualAddress = uav.gpuVirtualAddress;
		producer.hasDescriptor = uav.hasDescriptor;
		producer.descriptor = uav.descriptor;
		gRecentComputeUavs.push_back(std::move(producer));
	}
	if (gRecentComputeUavs.size() > MaxRecentComputeUavs) {
		gRecentComputeUavs.erase(
			gRecentComputeUavs.begin(),
			gRecentComputeUavs.begin() +
			static_cast<ptrdiff_t>(gRecentComputeUavs.size() - MaxRecentComputeUavs));
	}
	ReleaseSRWLockExclusive(&gPreSkinLock);
}

static bool NumericMatchSatisfied(const Bunny::NumericMatch &match, uint32_t actual)
{
	if (!match.enabled)
		return true;
	switch (match.op) {
	case Bunny::NumericMatchOp::Equal:
		return actual == match.value;
	case Bunny::NumericMatchOp::NotEqual:
		return actual != match.value;
	case Bunny::NumericMatchOp::Less:
		return actual < match.value;
	case Bunny::NumericMatchOp::LessEqual:
		return actual <= match.value;
	case Bunny::NumericMatchOp::Greater:
		return actual > match.value;
	case Bunny::NumericMatchOp::GreaterEqual:
		return actual >= match.value;
	default:
		return false;
	}
}

static bool TextureOverrideMatchesDrawContext(
	const Bunny::TextureOverrideConfig &config,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance)
{
	// DX11 supports simple comparison operators for draw-context filters.
	// Keep the hot path to fixed small comparisons so IA replacement checks
	// stay cheap even when several TextureOverride sections share a hash.
	if (config.hasMatchVertexCount && !NumericMatchSatisfied(config.matchVertexCount, vertexCount))
		return false;
	if (config.hasMatchIndexCount && !NumericMatchSatisfied(config.matchIndexCount, indexCount))
		return false;
	if (config.hasMatchInstanceCount && !NumericMatchSatisfied(config.matchInstanceCount, instanceCount))
		return false;
	if (config.hasMatchFirstVertex && !NumericMatchSatisfied(config.matchFirstVertex, firstVertex))
		return false;
	if (config.hasMatchFirstIndex && !NumericMatchSatisfied(config.matchFirstIndex, firstIndex))
		return false;
	if (config.hasMatchFirstInstance && !NumericMatchSatisfied(config.matchFirstInstance, firstInstance))
		return false;
	return true;
}

static bool ContainsI(const std::wstring &value, const wchar_t *needle)
{
	return Bunny::ToLower(value).find(Bunny::ToLower(needle)) != std::wstring::npos;
}

static bool TextureOverrideHasDrawKind(
	const Bunny::TextureOverrideConfig &config, Bunny::CommandListActionKind kind)
{
	for (const Bunny::CommandListAction &action : config.actions) {
		if (action.kind == kind)
			return true;
	}
	return false;
}

static bool TextureOverrideHasDrawContextMatch(const Bunny::TextureOverrideConfig &config)
{
	return config.hasMatchVertexCount || config.hasMatchIndexCount ||
		config.hasMatchInstanceCount || config.hasMatchFirstVertex ||
		config.hasMatchFirstIndex || config.hasMatchFirstInstance;
}

static void SetCompiledVertexResourceBinding(
	std::vector<DX12CompiledVertexResourceBinding> *bindings,
	uint32_t slot,
	const std::wstring &resource)
{
	if (!bindings)
		return;
	for (DX12CompiledVertexResourceBinding &binding : *bindings) {
		if (binding.slot != slot)
			continue;
		binding.resource = resource;
		return;
	}
	bindings->push_back({ slot, resource });
	std::sort(bindings->begin(), bindings->end(),
		[](const DX12CompiledVertexResourceBinding &lhs,
		   const DX12CompiledVertexResourceBinding &rhs) {
			return lhs.slot < rhs.slot;
		});
}

static std::vector<DX12CompiledVertexResourceBinding> CompileVertexResourceBindings(
	const std::map<uint32_t, std::wstring> &vertexResources)
{
	std::vector<DX12CompiledVertexResourceBinding> bindings;
	bindings.reserve(vertexResources.size());
	for (const auto &item : vertexResources)
		bindings.push_back({ item.first, item.second });
	return bindings;
}

static bool TextureOverrideMatchesIaBinding(
	const Bunny::TextureOverrideConfig &config, bool indexBuffer, uint32_t vertexSlot)
{
	if (config.hash == 0 && TextureOverrideHasDrawContextMatch(config) &&
	    config.indexBufferResource.empty() && config.vertexBufferResources.empty()) {
		if (indexBuffer)
			return !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
		return TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw);
	}

	if (indexBuffer) {
		if (config.indexBufferResource.empty() &&
		    !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::DrawIndexed) &&
		    !ContainsI(config.section, L"TextureOverride_IB") &&
		    !ContainsI(config.section, L"\\_IB_"))
			return false;
		return true;
	}

	if (config.vertexBufferResources.empty())
		return false;
	if (config.vertexBufferResources.find(vertexSlot) == config.vertexBufferResources.end())
		return false;
	return true;
}

static UINT64 MakeIaVertexIndexKey(uint32_t hash, uint32_t vertexSlot)
{
	return (static_cast<UINT64>(hash) << 32) | vertexSlot;
}

static void BuildDx12IaTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, bool> &textureOverridePostEffect,
	std::vector<DX12IaTextureOverrideCandidate> *iaTextureOverrides,
	std::unordered_map<uint32_t, std::vector<size_t>> *indexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *vertexTextureOverrideIndex,
	std::unordered_map<uint32_t, std::vector<size_t>> *anyVertexTextureOverrideIndex,
	std::unordered_map<UINT64, std::vector<size_t>> *mergedVertexTextureOverrideIndex)
{
	if (!iaTextureOverrides || !indexTextureOverrideIndex ||
	    !vertexTextureOverrideIndex || !anyVertexTextureOverrideIndex ||
	    !mergedVertexTextureOverrideIndex)
		return;

	iaTextureOverrides->clear();
	indexTextureOverrideIndex->clear();
	vertexTextureOverrideIndex->clear();
	anyVertexTextureOverrideIndex->clear();
	mergedVertexTextureOverrideIndex->clear();
	UINT64 order = 0;
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			DX12IaTextureOverrideCandidate candidate;
			candidate.config = config;
			candidate.order = order++;
			auto sectionId = textureOverrideSectionIds.find(config.section);
			candidate.sectionId =
				sectionId != textureOverrideSectionIds.end() ? sectionId->second : 0;
			candidate.preRuntimeEffect =
				LookupRuntimeEffect(textureOverridePreEffect, config.section);
			candidate.postRuntimeEffect =
				LookupRuntimeEffect(textureOverridePostEffect, config.section);
			const size_t index = iaTextureOverrides->size();
			iaTextureOverrides->push_back(std::move(candidate));

			if (TextureOverrideMatchesIaBinding(config, true, 0))
				(*indexTextureOverrideIndex)[config.hash].push_back(index);

			// DX11 prefilters TextureOverride work by resource binding before
			// applying draw-context checks. Keep that same shape for DX12 so
			// command-list recording does not rescan unrelated sections.
			bool indexedSpecificVertexSlot = false;
			for (const auto &slotResource : config.vertexBufferResources) {
				if (!TextureOverrideMatchesIaBinding(config, false, slotResource.first))
					continue;
				(*vertexTextureOverrideIndex)[
					MakeIaVertexIndexKey(config.hash, slotResource.first)].push_back(index);
				indexedSpecificVertexSlot = true;
			}
			if (!indexedSpecificVertexSlot &&
			    TextureOverrideMatchesIaBinding(config, false, 0)) {
				(*anyVertexTextureOverrideIndex)[config.hash].push_back(index);
			}
		}
	}

	// Merge exact-slot and any-slot VB candidates once at load time. Draw-time
	// IA matching can then walk an already ordered table without allocating or
	// sorting while the mod lock is held.
	for (const auto &bucket : *vertexTextureOverrideIndex) {
		std::vector<size_t> merged = bucket.second;
		const uint32_t hash = static_cast<uint32_t>(bucket.first >> 32);
		auto anySlot = anyVertexTextureOverrideIndex->find(hash);
		if (anySlot != anyVertexTextureOverrideIndex->end())
			merged.insert(merged.end(), anySlot->second.begin(), anySlot->second.end());
		std::sort(merged.begin(), merged.end(),
			[iaTextureOverrides](size_t lhs, size_t rhs) {
				return (*iaTextureOverrides)[lhs].order < (*iaTextureOverrides)[rhs].order;
			});
		(*mergedVertexTextureOverrideIndex)[bucket.first] = std::move(merged);
	}
}

static const std::vector<size_t> *FindIaTextureOverrideCandidatesLocked(
	uint32_t hash, bool indexBuffer, uint32_t vertexSlot)
{
	if (indexBuffer) {
		auto indexed = gIaIndexTextureOverrideIndex.find(hash);
		return indexed != gIaIndexTextureOverrideIndex.end() ? &indexed->second : nullptr;
	}

	auto merged = gIaMergedVertexTextureOverrideIndex.find(
		MakeIaVertexIndexKey(hash, vertexSlot));
	if (merged != gIaMergedVertexTextureOverrideIndex.end())
		return &merged->second;

	auto anySlot = gIaAnyVertexTextureOverrideIndex.find(hash);
	return anySlot != gIaAnyVertexTextureOverrideIndex.end() ? &anySlot->second : nullptr;
}

bool DX12ModIaHashMayHaveTextureOverrideCandidate(
	uint32_t hash, bool indexBuffer, uint32_t vertexSlot)
{
	if (gHasTextureOverrides == 0 || !hash)
		return false;

	AcquireSRWLockShared(&gModLock);
	const bool mayMatch =
		FindIaTextureOverrideCandidatesLocked(hash, indexBuffer, vertexSlot) != nullptr ||
		FindIaTextureOverrideCandidatesLocked(0, indexBuffer, vertexSlot) != nullptr;
	ReleaseSRWLockShared(&gModLock);
	return mayMatch;
}

static bool AppendActionToCompiledPlan(
	const Bunny::CommandListAction &action,
	DX12CompiledCommandListPlan *plan)
{
	if (!plan)
		return false;

	switch (action.kind) {
	case Bunny::CommandListActionKind::HandlingSkip:
		plan->handlingSkip = true;
		return true;
	case Bunny::CommandListActionKind::SetIndexBuffer:
		plan->indexResource = action.resource;
		return true;
	case Bunny::CommandListActionKind::SetVertexBuffer:
		SetCompiledVertexResourceBinding(
			&plan->vertexResources, action.target.slot, action.resource);
		return true;
	case Bunny::CommandListActionKind::Draw:
	case Bunny::CommandListActionKind::DrawIndexed:
	case Bunny::CommandListActionKind::DrawFromCaller:
	case Bunny::CommandListActionKind::DrawIndexedFromCaller: {
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
		                action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
		                   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
		if (!draw.fromCaller) {
			draw.count = action.args[0];
			draw.start = action.args[1];
			draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		}
		plan->draws.push_back(draw);
		return true;
	}
	case Bunny::CommandListActionKind::Dispatch: {
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		plan->dispatches.push_back(dispatch);
		return true;
	}
	default:
		return false;
	}
}

static void AppendCompiledCommandListPlan(
	DX12CompiledCommandListPlan *dst, const DX12CompiledCommandListPlan &src)
{
	if (!dst)
		return;
	dst->handlingSkip = dst->handlingSkip || src.handlingSkip;
	if (!src.indexResource.empty())
		dst->indexResource = src.indexResource;
	for (const auto &item : src.vertexResources)
		SetCompiledVertexResourceBinding(&dst->vertexResources, item.slot, item.resource);
	dst->draws.insert(dst->draws.end(), src.draws.begin(), src.draws.end());
	dst->dispatches.insert(dst->dispatches.end(), src.dispatches.begin(), src.dispatches.end());
}

static void AppendCompiledCommandListToTexturePlan(
	DX12CompiledTextureOverridePlan *dst, const DX12CompiledCommandListPlan &src)
{
	if (!dst)
		return;
	dst->handlingSkip = dst->handlingSkip || src.handlingSkip;
	if (!src.indexResource.empty())
		dst->indexResource = src.indexResource;
	for (const auto &item : src.vertexResources)
		SetCompiledVertexResourceBinding(&dst->vertexResources, item.slot, item.resource);
	dst->draws.insert(dst->draws.end(), src.draws.begin(), src.draws.end());
	dst->dispatches.insert(dst->dispatches.end(), src.dispatches.begin(), src.dispatches.end());
}

static bool CompileCommandListPlanRecursive(
	const std::wstring &name,
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans,
	std::unordered_set<std::wstring> *visiting,
	DX12CompiledCommandListPlan *outPlan)
{
	if (!compiledPlans || !visiting || !outPlan)
		return false;

	auto cached = compiledPlans->find(name);
	if (cached != compiledPlans->end()) {
		if (cached->second.unsupported)
			return false;
		*outPlan = cached->second;
		return true;
	}
	if (!visiting->insert(name).second)
		return false;

	auto listIt = commandLists.find(name);
	if (listIt == commandLists.end()) {
		visiting->erase(name);
		return false;
	}

	DX12CompiledCommandListPlan plan;
	for (const Bunny::CommandListAction &action : listIt->second.actions) {
		if (action.kind == Bunny::CommandListActionKind::Run) {
			DX12CompiledCommandListPlan childPlan;
			if (!CompileCommandListPlanRecursive(
				action.commandList, commandLists, compiledPlans, visiting, &childPlan)) {
				plan.unsupported = true;
				break;
			}
			AppendCompiledCommandListPlan(&plan, childPlan);
			continue;
		}
		if (action.kind == Bunny::CommandListActionKind::CheckTextureOverride ||
		    !AppendActionToCompiledPlan(action, &plan)) {
			plan.unsupported = true;
			break;
		}
	}

	visiting->erase(name);
	(*compiledPlans)[name] = plan;
	if (plan.unsupported)
		return false;
	*outPlan = plan;
	return true;
}

static void BuildDx12CompiledCommandListPlans(
	const Bunny::CommandListMap &commandLists,
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> *compiledPlans)
{
	if (!compiledPlans)
		return;

	compiledPlans->clear();
	for (const auto &item : commandLists) {
		std::unordered_set<std::wstring> visiting;
		DX12CompiledCommandListPlan plan;
		// Compile only direct, unconditional command lists. Lists that need
		// dynamic TextureOverride lookup keep using the legacy executor so the
		// compiled path can stay branch-light and preserve mod behavior.
		CompileCommandListPlanRecursive(
			item.first, commandLists, compiledPlans, &visiting, &plan);
	}
}

static void BuildDx12CompiledTextureOverridePlans(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	const std::unordered_map<std::wstring, DX12CompiledCommandListPlan> &compiledCommandListPlans,
	std::unordered_map<uint32_t, DX12CompiledTextureOverridePlan> *compiledPlans)
{
	if (!compiledPlans)
		return;

	compiledPlans->clear();
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			auto sectionIdIt = textureOverrideSectionIds.find(config.section);
			if (sectionIdIt == textureOverrideSectionIds.end() || !sectionIdIt->second)
				continue;

			DX12CompiledTextureOverridePlan plan;
			plan.hasActions = !config.actions.empty();
			if (config.actions.empty()) {
				plan.handlingSkip = config.handlingSkip;
				plan.indexResource = config.indexBufferResource;
				plan.vertexResources =
					CompileVertexResourceBindings(config.vertexBufferResources);
			} else {
				for (const Bunny::CommandListAction &action : config.actions) {
					switch (action.kind) {
					case Bunny::CommandListActionKind::Run: {
						auto commandPlan = compiledCommandListPlans.find(action.commandList);
						if (commandPlan == compiledCommandListPlans.end() ||
						    commandPlan->second.unsupported) {
							plan.hasUnsupportedCommandActions = true;
							break;
						}
						AppendCompiledCommandListToTexturePlan(&plan, commandPlan->second);
						break;
					}
					case Bunny::CommandListActionKind::CheckTextureOverride:
						plan.hasUnsupportedCommandActions = true;
						break;
					case Bunny::CommandListActionKind::HandlingSkip:
						plan.handlingSkip = true;
						break;
					case Bunny::CommandListActionKind::SetIndexBuffer:
						plan.indexResource = action.resource;
						break;
					case Bunny::CommandListActionKind::SetVertexBuffer:
						SetCompiledVertexResourceBinding(
							&plan.vertexResources, action.target.slot, action.resource);
						break;
					case Bunny::CommandListActionKind::Draw:
					case Bunny::CommandListActionKind::DrawIndexed:
					case Bunny::CommandListActionKind::DrawFromCaller:
					case Bunny::CommandListActionKind::DrawIndexedFromCaller: {
						DX12ModIaReplacement::DrawCall draw;
						draw.indexed = (action.kind == Bunny::CommandListActionKind::DrawIndexed ||
						                action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
						draw.fromCaller = (action.kind == Bunny::CommandListActionKind::DrawFromCaller ||
						                   action.kind == Bunny::CommandListActionKind::DrawIndexedFromCaller);
						if (!draw.fromCaller) {
							draw.count = action.args[0];
							draw.start = action.args[1];
							draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
						}
						plan.draws.push_back(draw);
						break;
					}
					case Bunny::CommandListActionKind::Dispatch: {
						DX12ModIaReplacement::DispatchCall dispatch;
						dispatch.groupsX = action.args[0];
						dispatch.groupsY = action.args[1];
						dispatch.groupsZ = action.args[2];
						plan.dispatches.push_back(dispatch);
						break;
					}
					}
				}
			}
			// This is the first DX12 compiled-plan layer: keep only direct IA
			// replacement actions here, and let recursive command actions keep
			// using the legacy executor until they have their own compiled form.
			(*compiledPlans)[sectionIdIt->second] = std::move(plan);
		}
	}
}

static void LogIaMatchLimited(
	const DX12IaHashState &iaState, uint32_t vertexCount, uint32_t indexCount,
	uint32_t instanceCount, uint32_t firstVertex, uint32_t firstIndex,
	const std::vector<DX12TriggeredTextureOverride> &overrides)
{
#if defined(_DEBUG)
	char vbText[512] = {};
	size_t used = 0;
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!buffer.hash)
			continue;
		int written = sprintf_s(vbText + used, sizeof(vbText) - used,
			"%s%u:%08x", used ? ";" : "", buffer.slot, buffer.hash);
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= sizeof(vbText) - 1)
			break;
	}

	std::wstring sections;
	for (const DX12TriggeredTextureOverride &entry : overrides) {
		const Bunny::TextureOverrideConfig &config =
			TriggeredTextureOverrideConfig(entry);
		if (!sections.empty())
			sections += L";";
		sections += config.section;
		sections += entry.indexBuffer ? L"@ib" : L"@vb";
		if (!entry.indexBuffer) {
			wchar_t slotText[16] = {};
			swprintf_s(slotText, L"%u", entry.vertexSlot);
			sections += slotText;
		}
		sections += entry.executeCommands ? L":exec" : L":bind";
		if (entry.preSkinAnchor)
			sections += L":preskin";
		else if (entry.directIaAnchor)
			sections += L":anchor";
		else if (entry.relatedMesh)
			sections += L":related";
	}

	DX12LogDebugJsonFunc("DX12IaTextureOverrideMatch",
		"\"matches\":%zu,\"ib\":\"%08x\",\"vbs\":\"%s\",\"vertexCount\":%u,\"indexCount\":%u,\"instanceCount\":%u,\"firstVertex\":%u,\"firstIndex\":%u,\"sections\":\"%S\"",
		overrides.size(),
		iaState.hasIndexBuffer ? iaState.indexHash : 0,
		vbText, vertexCount, indexCount, instanceCount, firstVertex, firstIndex,
		sections.c_str());
#else
	(void)iaState;
	(void)vertexCount;
	(void)indexCount;
	(void)instanceCount;
	(void)firstVertex;
	(void)firstIndex;
	(void)overrides;
#endif
}

static void LogIaReplacementLimited(
	const DX12IaHashState &iaState, uint32_t vertexCount, uint32_t indexCount,
	uint32_t instanceCount, const DX12ModIaReplacement &replacement,
	const std::vector<DX12TriggeredTextureOverride> &overrides)
{
#if defined(_DEBUG)
	std::wstring sections;
	for (const DX12TriggeredTextureOverride &entry : overrides) {
		const Bunny::TextureOverrideConfig &config =
			TriggeredTextureOverrideConfig(entry);
		if (!sections.empty())
			sections += L";";
		sections += config.section;
		sections += entry.indexBuffer ? L"@ib" : L"@vb";
		if (!entry.indexBuffer) {
			wchar_t slotText[16] = {};
			swprintf_s(slotText, L"%u", entry.vertexSlot);
			sections += slotText;
		}
		sections += entry.executeCommands ? L":exec" : L":bind";
		if (entry.preSkinAnchor)
			sections += L":preskin";
		else if (entry.directIaAnchor)
			sections += L":anchor";
		else if (entry.relatedMesh)
			sections += L":related";
	}

	char drawText[512] = {};
	size_t used = 0;
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		int written = 0;
		if (draw.indexed) {
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
				"%sDI:%u,%u,%d", used ? ";" : "", draw.count, draw.start,
				draw.baseVertex);
		} else {
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
				"%sD:%u,%u", used ? ";" : "", draw.count, draw.start);
		}
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= sizeof(drawText) - 1)
			break;
	}

	DX12LogDebugJsonFunc("DX12IaReplacement",
		"\"matches\":%zu,\"skip\":%s,\"draws\":%zu,\"drawList\":\"%s\",\"hasIB\":%s,\"vbStart\":%u,\"vbCount\":%zu,\"ib\":\"%08x\",\"vertexCount\":%u,\"indexCount\":%u,\"instanceCount\":%u,\"sections\":\"%S\"",
		overrides.size(), replacement.skip ? "true" : "false", replacement.draws.size(),
		drawText,
		replacement.hasIndexBuffer ? "true" : "false",
		replacement.vertexBufferStartSlot, replacement.vertexBuffers.size(),
		iaState.hasIndexBuffer ? iaState.indexHash : 0,
		vertexCount, indexCount, instanceCount, sections.c_str());
#else
	(void)iaState;
	(void)vertexCount;
	(void)indexCount;
	(void)instanceCount;
	(void)replacement;
	(void)overrides;
#endif
}

static bool ReplacementHasVertexSlot(const DX12ModIaReplacement &replacement, UINT slot)
{
	if (replacement.vertexBuffers.empty() || slot < replacement.vertexBufferStartSlot)
		return false;
	const UINT index = slot - replacement.vertexBufferStartSlot;
	if (index >= replacement.vertexBuffers.size())
		return false;
	const D3D12_VERTEX_BUFFER_VIEW &view = replacement.vertexBuffers[index];
	return view.BufferLocation != 0 && view.SizeInBytes != 0 && view.StrideInBytes != 0;
}

static bool ReplacementHasIndexedDraw(const DX12ModIaReplacement &replacement)
{
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		if (draw.indexed && draw.count)
			return true;
	}
	return false;
}

static size_t RemoveNonIndexedDraws(DX12ModIaReplacement *replacement)
{
	if (!replacement)
		return 0;
	const size_t oldCount = replacement->draws.size();
	replacement->draws.erase(
		std::remove_if(replacement->draws.begin(), replacement->draws.end(),
			[](const DX12ModIaReplacement::DrawCall &draw) {
				return !draw.indexed;
			}),
		replacement->draws.end());
	return oldCount - replacement->draws.size();
}

static void LogIaReplacementSuppressedLimited(
	const DX12IaHashState &iaState, const DX12ModIaReplacement &replacement,
	const char *reason)
{
#if defined(_DEBUG)
	DX12LogDebugJsonFunc("DX12IaReplacementSuppressed",
		"\"reason\":\"%s\",\"draws\":%zu,\"hasIB\":%s,\"vbStart\":%u,\"vbCount\":%zu,\"ib\":\"%08x\"",
		reason ? reason : "unknown", replacement.draws.size(),
		replacement.hasIndexBuffer ? "true" : "false",
		replacement.vertexBufferStartSlot, replacement.vertexBuffers.size(),
		iaState.hasIndexBuffer ? iaState.indexHash : 0);
#else
	(void)iaState;
	(void)replacement;
	(void)reason;
#endif
}

static void LogShaderOverrideCommandListLimited(
	const char *phase, const std::vector<const Bunny::ShaderOverrideConfig*> &configs,
	const DX12ModIaReplacement &replacement)
{
#if defined(_DEBUG)
	std::wstring sections;
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (!config)
			continue;
		if (!sections.empty())
			sections += L";";
		sections += config->section;
	}

	char drawText[512] = {};
	size_t used = 0;
	for (const DX12ModIaReplacement::DrawCall &draw : replacement.draws) {
		int written = 0;
		if (draw.indexed) {
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
				"%sDI:%u,%u,%d", used ? ";" : "", draw.count, draw.start,
				draw.baseVertex);
		} else {
			written = sprintf_s(drawText + used, sizeof(drawText) - used,
				"%sD:%u,%u", used ? ";" : "", draw.count, draw.start);
		}
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= sizeof(drawText) - 1)
			break;
	}

	DX12LogDebugJsonFunc("DX12ShaderOverrideCommandList",
		"\"phase\":\"%s\",\"matches\":%zu,\"skip\":%s,\"draws\":%zu,\"drawList\":\"%s\",\"sections\":\"%S\"",
		phase ? phase : "", configs.size(), replacement.skip ? "true" : "false",
		replacement.draws.size(), drawText, sections.c_str());
#else
	(void)phase;
	(void)configs;
	(void)replacement;
#endif
}

static void LogPreSkinningCandidatesLimited(const DX12IaHashState &iaState)
{
#if defined(_DEBUG)
	std::vector<DX12ComputeUavProducer> producers;
	AcquireSRWLockShared(&gPreSkinLock);
	producers = gRecentComputeUavs;
	ReleaseSRWLockShared(&gPreSkinLock);

	if (producers.empty()) {
		DX12LogDebugJsonFunc("DX12PreSkinningCandidate",
			"\"status\":\"none\",\"recentUavs\":0,\"overlapCount\":0,\"vlrLike\":0,\"vbCount\":%zu",
			iaState.vertexBuffers.size());
		return;
	}

	std::vector<DX12VertexLimitRaiseConfig> vlrs;
	AcquireSRWLockShared(&gModLock);
	vlrs = gVertexLimitRaiseConfigs;
	ReleaseSRWLockShared(&gModLock);

	size_t overlapping = 0;
	size_t vlrLike = 0;
	DX12ComputeUavProducer best = {};
	DX12IaBufferHash bestBuffer = {};
	bool hasBest = false;

	for (auto it = producers.rbegin(); it != producers.rend(); ++it) {
		const DX12ComputeUavProducer &producer = *it;
		for (const DX12VertexLimitRaiseConfig &vlr : vlrs) {
			if (ProducerMatchesReplacementTarget(producer, vlr)) {
				vlrLike++;
				break;
			}
		}
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			if (!ProducerMatchesIaBuffer(producer, buffer))
				continue;
			overlapping++;
			if (!hasBest) {
				best = producer;
				bestBuffer = buffer;
				hasBest = true;
			}
		}
	}

	if (hasBest && best.hasDescriptor) {
		const DX12DescriptorSummary &d = best.descriptor;
		DX12LogDebugJsonFunc("DX12PreSkinningCandidate",
			"\"status\":\"matched\",\"recentUavs\":%zu,\"overlapCount\":%zu,\"vlrLike\":%zu,\"vbSlot\":%u,\"vbHash\":\"%08x\",\"producerSerial\":%llu,\"root\":%u,\"range\":%u,\"uRegister\":%u,\"space\":%u,\"resource\":\"%p\",\"resourceWidth\":%llu,\"viewBytes\":%llu,\"viewOffset\":%llu,\"numElements\":%u,\"stride\":%u",
			producers.size(), overlapping, vlrLike, bestBuffer.slot, bestBuffer.hash,
			static_cast<unsigned long long>(best.serial),
			best.rootParameterIndex, best.rangeIndex, best.shaderRegister, best.registerSpace,
			best.descriptor.resource,
			static_cast<unsigned long long>(d.hasResourceDesc ? d.resourceDesc.Width : 0),
			static_cast<unsigned long long>(d.viewSize),
			static_cast<unsigned long long>(d.resourceOffset),
			d.numElements, d.structureByteStride);
		return;
	}

	DX12LogDebugJsonFunc("DX12PreSkinningCandidate",
		"\"status\":\"none\",\"recentUavs\":%zu,\"overlapCount\":%zu,\"vlrLike\":%zu,\"vbCount\":%zu",
		producers.size(), overlapping, vlrLike, iaState.vertexBuffers.size());
#else
	(void)iaState;
#endif
}

static void ReleaseLoadedResourcesLocked()
{
	for (auto &item : gLoadedResources) {
		DX12RetiredLoadedResource retired;
		retired.resource = item.second;
		retired.retirePresent = DX12GetPresentCount();
		gRetiredLoadedResources.push_back(retired);
		item.second = DX12LoadedResource();
	}
	gLoadedResources.clear();
}

static void ReleaseLoadedResourceObjects(DX12LoadedResource *resource)
{
	if (!resource)
		return;
	if (resource->resource) {
		resource->resource->Release();
		resource->resource = nullptr;
	}
	if (resource->uavResource) {
		RetirePreSkinResource(resource->uavResource);
		resource->uavResource = nullptr;
	}
	if (resource->uavHeap) {
		RetirePreSkinDescriptorHeapLocked(resource->uavHeap);
		resource->uavHeap = nullptr;
	}
	if (resource->srvResource) {
		resource->srvResource->Release();
		resource->srvResource = nullptr;
	}
	if (resource->srvHeap) {
		resource->srvHeap->Release();
		resource->srvHeap = nullptr;
	}
}

static void ReleaseExpiredRetiredLoadedResourcesLocked()
{
	const LONG present = DX12GetPresentCount();
	auto it = gRetiredLoadedResources.begin();
	while (it != gRetiredLoadedResources.end()) {
		if (present - it->retirePresent < RetiredLoadedResourcePresentDelay) {
			++it;
			continue;
		}
		ReleaseLoadedResourceObjects(&it->resource);
		it = gRetiredLoadedResources.erase(it);
	}
}

static bool CommandListRuntimeEffectLocked(const std::wstring &name);
static bool TextureOverrideRuntimeEffectLocked(
	const Bunny::TextureOverrideConfig &config, bool includePost);
static bool RunNamedCommandListLocked(
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement);

static bool TextureOverrideMatchCsSatisfiedLocked(const Bunny::TextureOverrideConfig &config)
{
	if (!config.hasMatchCs)
		return true;

	AcquireSRWLockShared(&gPreSkinLock);
	const bool active =
		gActivePreSkinTextureOverrides.find(config.section) != gActivePreSkinTextureOverrides.end();
	ReleaseSRWLockShared(&gPreSkinLock);
	return active;
}

void DX12ModBeginFrame()
{
	AcquireSRWLockExclusive(&gModLock);
	for (auto &item : gLoadedResources)
		item.second.uavWritten = false;
	gPreSkinSectionAppliedPresent.clear();
	AcquireSRWLockExclusive(&gPreSkinLock);
	ClearActivePreSkinTextureOverridesLocked();
	ReleaseSRWLockExclusive(&gPreSkinLock);
	gPreSkinCbvReadCache.clear();
	gPreSkinSrvNegativeCache.clear();
	gPreSkinSrvPositiveCache.clear();
	// Match results are pure INI/IA/hash decisions and their key already
	// includes reload and pre-skin generations. Keep them across frames so
	// repeated negative draw matches do not rescan all texture overrides.
	gIaReplacementPreparedFrameCache.clear();
	gIaReplacementPrepareCachePresent = DX12GetPresentCount();
	ReleaseExpiredRetiredLoadedResourcesLocked();
	ReleaseSRWLockExclusive(&gModLock);
}

bool DX12ModPreparePresentReplacement(
	ID3D12GraphicsCommandList *commandList, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!DX12ModNeedsPresentReplacement())
		return false;

	const LONG present = DX12GetPresentCount();
	// This guard runs from draw hooks, so keep the common "already executed for
	// this frame" case to a plain read before falling back to atomic ownership.
	if (gPresentCommandListExecutedPresent == present)
		return false;
	if (InterlockedCompareExchange(
		    &gPresentCommandListExecutedPresent, present, present) == present)
		return false;

	AcquireSRWLockExclusive(&gModLock);
	if (InterlockedCompareExchange(
		    &gPresentCommandListExecutedPresent, present, present) == present) {
		ReleaseSRWLockExclusive(&gModLock);
		return false;
	}
	InterlockedExchange(&gPresentCommandListExecutedPresent, present);
	const bool changed = RunNamedCommandListLocked(commandList, L"Present", replacement);
	ReleaseSRWLockExclusive(&gModLock);
	return changed;
}

static const Bunny::TextureOverrideConfig *FindTextureOverrideLocked(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance, bool requireSkip,
	bool indexBuffer, uint32_t vertexSlot)
{
	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverrideCandidatesLocked(hash, indexBuffer, vertexSlot);
	if (!candidateIndexes || candidateIndexes->empty())
		return nullptr;

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (!TextureOverrideMatchCsSatisfiedLocked(config))
			continue;
		if (!TextureOverrideMatchesIaBinding(config, indexBuffer, vertexSlot))
			continue;
		if (requireSkip && !config.handlingSkip)
			continue;
		if (!TextureOverrideMatchesDrawContext(
			config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		return &config;
	}
	return nullptr;
}

static bool TextureOverrideHasSkipLocked(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	std::wstring *section, bool indexBuffer, uint32_t vertexSlot)
{
	const Bunny::TextureOverrideConfig *config = FindTextureOverrideLocked(
		hash, vertexCount, indexCount, instanceCount, 0, 0, 0, true, indexBuffer, vertexSlot);
	if (!config)
		return false;
	if (section)
		*section = config->section;
	return true;
}

static const Bunny::TextureOverrideConfig *FindMatchingIaOverrideLocked(
	uint32_t ibHash, const uint32_t *vbHashes, size_t vbHashCount,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount)
{
	if (ibHash) {
		const Bunny::TextureOverrideConfig *config = FindTextureOverrideLocked(
			ibHash, vertexCount, indexCount, instanceCount, 0, 0, 0, false, true, 0);
		if (config)
			return config;
	}

	if (!vbHashes)
		return nullptr;
	for (size_t i = 0; i < vbHashCount; ++i) {
		if (!vbHashes[i])
			continue;
		const Bunny::TextureOverrideConfig *config = FindTextureOverrideLocked(
			vbHashes[i], vertexCount, indexCount, instanceCount, 0, 0, 0, false, false,
			static_cast<uint32_t>(i));
		if (config)
			return config;
	}
	return nullptr;
}

static const Bunny::TextureOverrideConfig *FindMatchingIaOverrideLocked(
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance)
{
	if (iaState.hasIndexBuffer && iaState.indexHash) {
		const Bunny::TextureOverrideConfig *config = FindTextureOverrideLocked(
			iaState.indexHash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, true, 0);
		if (config)
			return config;
	}

	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!buffer.hash)
			continue;
		const Bunny::TextureOverrideConfig *config = FindTextureOverrideLocked(
			buffer.hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, false, false, buffer.slot);
		if (!config)
			continue;
		return config;
	}
	return nullptr;
}

static void AppendTextureOverridesForHashLocked(
	uint32_t hash, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TriggeredTextureOverride> *overrides, std::unordered_set<uint32_t> *seen,
	std::unordered_set<std::wstring> *fallbackSeen,
	bool indexBuffer, uint32_t vertexSlot, bool executeCommands)
{
	if (!overrides || !seen || !fallbackSeen)
		return;

	const std::vector<size_t> *candidateIndexes =
		FindIaTextureOverrideCandidatesLocked(hash, indexBuffer, vertexSlot);
	if (!candidateIndexes || candidateIndexes->empty())
		return;

	for (size_t index : *candidateIndexes) {
		if (index >= gIaTextureOverrides.size())
			continue;
		const DX12IaTextureOverrideCandidate &candidate = gIaTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (!TextureOverrideMatchCsSatisfiedLocked(config))
			continue;
		if (!TextureOverrideMatchesIaBinding(config, indexBuffer, vertexSlot))
			continue;
		if (!TextureOverrideMatchesDrawContext(
			config, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance))
			continue;
		if (!indexBuffer && indexCount != 0 && !executeCommands &&
		    config.indexBufferResource.empty() &&
		    !TextureOverrideHasDrawContextMatch(config) &&
		    !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::Draw) &&
		    !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::DrawIndexed) &&
		    !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::DrawFromCaller) &&
		    !TextureOverrideHasDrawKind(config, Bunny::CommandListActionKind::DrawIndexedFromCaller)) {
			// Indexed draws often share secondary VB streams such as texcoords or
			// blend data across many unrelated meshes. A VB-only override that
			// carries no IB anchor and no draw-context filters is therefore too
			// broad to apply on its own in DX12. We only let those sections ride
			// along after an IB match has already anchored the target mesh.
			continue;
		}
		if (!executeCommands && !candidate.preRuntimeEffect)
			continue;
		if (candidate.sectionId) {
			if (!seen->insert(candidate.sectionId).second)
				continue;
		} else if (!fallbackSeen->insert(config.section).second) {
			continue;
		}
		DX12TriggeredTextureOverride entry;
		// IA match caches keep a stable pointer into gIaTextureOverrides rather
		// than copying the parsed TextureOverride. DX11 keeps override lists
		// resident after ini parsing; mirroring that shape keeps draw-time cache
		// hits small and avoids repeated string/vector copies.
		entry.config = &candidate.config;
		entry.sectionId = candidate.sectionId;
		entry.postRuntimeEffect = candidate.postRuntimeEffect;
		entry.indexBuffer = indexBuffer;
		entry.vertexSlot = vertexSlot;
		entry.executeCommands = executeCommands;
		entry.executeDrawActions = executeCommands;
		entry.directIaAnchor = indexBuffer || executeCommands ||
			TextureOverrideHasDrawContextMatch(config) ||
			!config.indexBufferResource.empty();
		overrides->push_back(entry);
	}
}

static bool ActivePreSkinProducerMatchesIaState(
	const DX12ActivePreSkinTextureOverride &active,
	const DX12IaHashState &iaState,
	uint32_t *matchedSlot)
{
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		for (const auto &target : active.config.vertexBufferResources) {
			if (target.first != buffer.slot)
				continue;
			if (!ProducerMatchesIaBuffer(active.producer, buffer))
				continue;
			if (matchedSlot)
				*matchedSlot = buffer.slot;
			return true;
		}
	}
	return false;
}

static void AppendActivePreSkinTextureOverridesLocked(
	const DX12IaHashState &iaState,
	std::vector<DX12TriggeredTextureOverride> *overrides,
	std::unordered_set<uint32_t> *seen,
	std::unordered_set<std::wstring> *fallbackSeen,
	bool executeCommands)
{
	if (!overrides || !seen || !fallbackSeen)
		return;

	std::vector<DX12ActivePreSkinTextureOverride> activeOverrides;
	AcquireSRWLockShared(&gPreSkinLock);
	for (const auto &item : gActivePreSkinTextureOverrides)
		activeOverrides.push_back(item.second);
	ReleaseSRWLockShared(&gPreSkinLock);
	if (activeOverrides.empty())
		return;

	for (const DX12ActivePreSkinTextureOverride &active : activeOverrides) {
		const Bunny::TextureOverrideConfig &config = active.config;
		if (config.vertexBufferResources.empty())
			continue;
		uint32_t matchedSlot = UINT_MAX;
		if (!ActivePreSkinProducerMatchesIaState(active, iaState, &matchedSlot))
			continue;
		if (!executeCommands && !TextureOverrideRuntimeEffectLocked(config, false))
			continue;

		auto sectionIdIt = gTextureOverrideSectionIds.find(config.section);
		const uint32_t sectionId =
			sectionIdIt != gTextureOverrideSectionIds.end() ? sectionIdIt->second : 0;
		if (sectionId) {
			if (!seen->insert(sectionId).second)
				continue;
		} else if (!fallbackSeen->insert(config.section).second) {
			continue;
		}

		DX12TriggeredTextureOverride entry;
		entry.fallbackConfig = config;
		entry.sectionId = sectionId;
		entry.postRuntimeEffect = TextureOverrideRuntimeEffectLocked(config, true);
		entry.indexBuffer = false;
		entry.vertexSlot = matchedSlot;
		entry.executeCommands = executeCommands;
		entry.executeDrawActions = executeCommands;
		entry.directIaAnchor = true;
		entry.preSkinAnchor = true;
		entry.preSkinResource = active.outputResource;
		entry.preSkinByteWidth = active.outputByteWidth;
		entry.preSkinStride = active.outputStride;
		overrides->push_back(entry);
	}
}
static void AppendRelatedMeshTextureOverridesLocked(
	std::vector<DX12TriggeredTextureOverride> *overrides,
	std::unordered_set<uint32_t> *seen,
	std::unordered_set<std::wstring> *fallbackSeen);

static void FindMatchingIaOverridesLocked(
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	std::vector<DX12TriggeredTextureOverride> *overrides)
{
	if (!overrides)
		return;
	overrides->clear();
	std::unordered_set<uint32_t> seen;
	std::unordered_set<std::wstring> fallbackSeen;
	const bool indexedCaller = indexCount != 0;
	if (iaState.hasIndexBuffer)
		AppendTextureOverridesForHashLocked(
			iaState.indexHash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, overrides, &seen, &fallbackSeen,
			true, 0, indexedCaller);
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		AppendTextureOverridesForHashLocked(
			buffer.hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, overrides, &seen, &fallbackSeen,
			false, buffer.slot, !indexedCaller);
	}
	if (iaState.hasIndexBuffer)
		AppendTextureOverridesForHashLocked(
			0, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, overrides, &seen, &fallbackSeen,
			true, 0, indexedCaller);
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		AppendTextureOverridesForHashLocked(
			0, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, overrides, &seen,
			&fallbackSeen, false, buffer.slot, !indexedCaller);
	}
	AppendActivePreSkinTextureOverridesLocked(
		iaState, overrides, &seen, &fallbackSeen, !indexedCaller);
	AppendRelatedMeshTextureOverridesLocked(overrides, &seen, &fallbackSeen);

}

bool DX12ModIaMayHaveTextureOverrideMatch(const DX12IaHashState &iaState, bool indexedCaller)
{
	if (gHasTextureOverrides == 0)
		return false;


	uint64_t cacheKey = 14695981039346656037ull;
	cacheKey = HashCombine64(cacheKey, gReloadGeneration);
	cacheKey = HashCombine64(cacheKey, gPreSkinActiveGeneration);
	cacheKey = HashCombine64(cacheKey, indexedCaller ? 1 : 0);
	cacheKey = HashCombine64(cacheKey, iaState.hasIndexBuffer ? iaState.indexHash : 0);
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!buffer.hash)
			continue;
		cacheKey = HashCombine64(cacheKey, buffer.slot);
		cacheKey = HashCombine64(cacheKey, buffer.hash);
	}

	AcquireSRWLockShared(&gModLock);
	auto cached = gIaTextureCandidateCache.find(cacheKey);
	if (cached != gIaTextureCandidateCache.end()) {
		const bool mayMatch = cached->second;
		ReleaseSRWLockShared(&gModLock);
		return mayMatch;
	}
	ReleaseSRWLockShared(&gModLock);

	// This is a draw-hot-path gate. It only consults immutable reload-time
	// indexes while holding the mod lock. Cache the yes/no candidate answer by
	// IA hashes so burst frames do not repeat the same shared-lock index scan
	// for hundreds of equivalent D3D12 draw records.
	AcquireSRWLockShared(&gModLock);
	bool mayMatch = false;
	if (iaState.hasIndexBuffer) {
		mayMatch =
			FindIaTextureOverrideCandidatesLocked(iaState.indexHash, true, 0) != nullptr ||
			FindIaTextureOverrideCandidatesLocked(0, true, 0) != nullptr;
	}
	if (!mayMatch) {
		for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
			if (FindIaTextureOverrideCandidatesLocked(buffer.hash, false, buffer.slot) ||
			    FindIaTextureOverrideCandidatesLocked(0, false, buffer.slot)) {
				mayMatch = true;
				break;
			}
		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!mayMatch && DX12ModHasActivePreSkinTextureOverrides())
		mayMatch = true;

	AcquireSRWLockExclusive(&gModLock);
	if (gIaTextureCandidateCache.size() > 4096)
		gIaTextureCandidateCache.clear();
	gIaTextureCandidateCache[cacheKey] = mayMatch;
	ReleaseSRWLockExclusive(&gModLock);
	return mayMatch;
}

static const DX12IaBufferHash *FindIaVertexSlot(const DX12IaHashState &iaState, uint32_t slot)
{
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (buffer.slot == slot)
			return &buffer;
	}
	return nullptr;
}

static void AppendTokenFromText(
	const std::wstring &text, std::set<std::wstring> *tokens)
{
	if (!tokens)
		return;
	std::wstring token;
	for (wchar_t ch : text) {
		const bool hex =
			(ch >= L'0' && ch <= L'9') ||
			(ch >= L'a' && ch <= L'f') ||
			(ch >= L'A' && ch <= L'F');
		if (hex) {
			token.push_back(static_cast<wchar_t>(towlower(ch)));
			continue;
		}
		if (token.length() >= 8)
			tokens->insert(token);
		token.clear();
	}
	if (token.length() >= 8)
		tokens->insert(token);
}

static void CollectTextureOverrideTokens(
	const Bunny::TextureOverrideConfig &config, std::set<std::wstring> *tokens)
{
	AppendTokenFromText(config.section, tokens);
	AppendTokenFromText(config.originalSection, tokens);
	AppendTokenFromText(config.indexBufferResource, tokens);
	for (const auto &item : config.vertexBufferResources)
		AppendTokenFromText(item.second, tokens);
}

static bool TextureOverrideSharesAnyToken(
	const std::set<std::wstring> &candidateTokens, const std::set<std::wstring> &tokens)
{
	if (candidateTokens.empty() || tokens.empty())
		return false;
	for (const std::wstring &token : candidateTokens) {
		if (tokens.find(token) != tokens.end())
			return true;
	}
	return false;
}

static void BuildDx12RelatedTextureOverrideIndex(
	const Bunny::TextureOverrideMap &textureOverrides,
	const std::unordered_map<std::wstring, bool> &textureOverridePreEffect,
	const std::unordered_map<std::wstring, uint32_t> &textureOverrideSectionIds,
	std::vector<DX12RelatedTextureOverrideCandidate> *relatedTextureOverrides,
	std::unordered_map<std::wstring, std::vector<size_t>> *relatedTokenIndex)
{
	if (!relatedTextureOverrides || !relatedTokenIndex)
		return;

	relatedTextureOverrides->clear();
	relatedTokenIndex->clear();
	for (const auto &bucket : textureOverrides) {
		for (const Bunny::TextureOverrideConfig &config : bucket.second) {
			auto effect = textureOverridePreEffect.find(config.section);
			if (effect == textureOverridePreEffect.end() || !effect->second)
				continue;

			DX12RelatedTextureOverrideCandidate candidate;
			candidate.config = config;
			auto sectionId = textureOverrideSectionIds.find(config.section);
			candidate.sectionId =
				sectionId != textureOverrideSectionIds.end() ? sectionId->second : 0;
			CollectTextureOverrideTokens(candidate.config, &candidate.tokens);
			if (candidate.tokens.empty())
				continue;

			const size_t index = relatedTextureOverrides->size();
			relatedTextureOverrides->push_back(std::move(candidate));
			for (const std::wstring &token : (*relatedTextureOverrides)[index].tokens)
				(*relatedTokenIndex)[token].push_back(index);
		}
	}
}

static void AppendRelatedMeshTextureOverridesLocked(
	std::vector<DX12TriggeredTextureOverride> *overrides,
	std::unordered_set<uint32_t> *seen,
	std::unordered_set<std::wstring> *fallbackSeen)
{
	if (!overrides || !seen || !fallbackSeen || overrides->empty())
		return;

	bool hasDirectIndexAnchor = false;
	bool hasPreSkinAnchor = false;
	std::unordered_set<std::wstring> namespaces;
	std::set<std::wstring> tokens;
	for (const DX12TriggeredTextureOverride &entry : *overrides) {
		if (!entry.directIaAnchor && !entry.preSkinAnchor)
			continue;
		const Bunny::TextureOverrideConfig &config =
			TriggeredTextureOverrideConfig(entry);
		// Only let the related-mesh heuristic expand around a draw that matched
		// an index-buffer override directly, or around an active PreSkin producer
		// that was verified against the current IA resource. Shared VB hashes
		// such as texcoord streams can legitimately appear on many unrelated
		// draws, and expanding those VB-only hits into sibling IB/LOD sections
		// corrupts the IA bindings for meshes that do not belong to this target.
		if (entry.indexBuffer)
			hasDirectIndexAnchor = true;
		if (entry.preSkinAnchor)
			hasPreSkinAnchor = true;
		if (!config.iniNamespace.empty())
			namespaces.insert(config.iniNamespace);
		CollectTextureOverrideTokens(config, &tokens);
	}
	if ((!hasDirectIndexAnchor && !hasPreSkinAnchor) || tokens.empty())
		return;

	std::vector<const std::vector<size_t>*> indexedLists;
	std::vector<size_t> indexedPositions;
	indexedLists.reserve(tokens.size());
	indexedPositions.reserve(tokens.size());
	for (const std::wstring &token : tokens) {
		auto indexed = gRelatedTextureOverrideTokenIndex.find(token);
		if (indexed == gRelatedTextureOverrideTokenIndex.end())
			continue;
		if (indexed->second.empty())
			continue;
		indexedLists.push_back(&indexed->second);
		indexedPositions.push_back(0);
	}

	// Token index lists are built in TextureOverride load order. Merge them by
	// candidate index instead of inserting into std::set so related-mesh
	// matching keeps DX11-style priority while avoiding per-draw tree nodes.
	while (!indexedLists.empty()) {
		size_t index = SIZE_MAX;
		for (size_t listIndex = 0; listIndex < indexedLists.size(); ++listIndex) {
			const std::vector<size_t> &list = *indexedLists[listIndex];
			const size_t position = indexedPositions[listIndex];
			if (position < list.size() && list[position] < index)
				index = list[position];
		}
		if (index == SIZE_MAX)
			break;
		for (size_t listIndex = 0; listIndex < indexedLists.size(); ++listIndex) {
			const std::vector<size_t> &list = *indexedLists[listIndex];
			size_t &position = indexedPositions[listIndex];
			while (position < list.size() && list[position] <= index)
				++position;
		}
		if (index >= gRelatedTextureOverrides.size())
			continue;
		const DX12RelatedTextureOverrideCandidate &candidate =
			gRelatedTextureOverrides[index];
		const Bunny::TextureOverrideConfig &config = candidate.config;
		if (!TextureOverrideMatchCsSatisfiedLocked(config))
			continue;
		if (!namespaces.empty() &&
		    namespaces.find(config.iniNamespace) == namespaces.end())
			continue;
		// The ini-load index prefilters by token. Reuse pre-parsed candidate
		// tokens here so related-mesh matching does not rebuild string sets on
		// the draw path.
		if (!TextureOverrideSharesAnyToken(candidate.tokens, tokens))
			continue;
		if (candidate.sectionId) {
			if (!seen->insert(candidate.sectionId).second)
				continue;
		} else if (!fallbackSeen->insert(config.section).second) {
			continue;
		}
		DX12TriggeredTextureOverride entry;
		entry.config = &candidate.config;
		entry.sectionId = candidate.sectionId;
		entry.postRuntimeEffect = true;
		entry.indexBuffer = !config.indexBufferResource.empty() ||
			config.vertexBufferResources.empty();
		entry.vertexSlot = config.vertexBufferResources.empty() ?
			0 : config.vertexBufferResources.begin()->first;
		entry.executeCommands = false;
		entry.executeDrawActions = hasPreSkinAnchor;
		entry.relatedMesh = true;
		overrides->push_back(entry);
	}
}

static ID3D12Resource *SelectIaResourceForViewLocked(
	ID3D12GraphicsCommandList *commandList, DX12LoadedResource *resource)
{
	if (!resource)
		return nullptr;
	if (resource->uavResource && resource->uavValid) {
		if (resource->uavBarrierPending) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = resource->uavResource;
			commandList->ResourceBarrier(1, &barrier);
			resource->uavBarrierPending = false;
		}
		if (resource->uavState != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource->uavResource;
			barrier.Transition.StateBefore = resource->uavState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);
			resource->uavState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		return resource->uavResource;
	}
	if (resource->srvResource && resource->srvByteWidth > resource->byteWidth)
		return resource->srvResource;
	return resource->resource;
}

static UINT IaViewByteWidthForResource(const DX12LoadedResource &resource, ID3D12Resource *iaResource)
{
	UINT64 byteWidth = resource.byteWidth;
	if (resource.uavResource && iaResource == resource.uavResource && resource.uavByteWidth)
		byteWidth = resource.uavByteWidth;
	else if (resource.srvResource && iaResource == resource.srvResource && resource.srvByteWidth)
		byteWidth = resource.srvByteWidth;
	return static_cast<UINT>((std::min)(byteWidth, static_cast<UINT64>(UINT_MAX)));
}
static void LogIaResourceBindLimited(
	const char *target, uint32_t slot, const DX12LoadedResource &resource,
	ID3D12Resource *iaResource)
{
#if defined(_DEBUG)
	const bool usingUav = resource.uavResource && iaResource == resource.uavResource;
	const bool usingSrv = resource.srvResource && iaResource == resource.srvResource;
	DX12LogDebugJsonFunc("DX12IaResourceBind",
		"\"target\":\"%s\",\"slot\":%u,\"resource\":\"%S\",\"source\":\"%s\",\"bytes\":%llu,\"viewBytes\":%u,\"uavBytes\":%llu,\"stride\":%u,\"gpuVa\":\"0x%llx\"",
		target ? target : "", slot, resource.name.c_str(),
		usingUav ? "uav" : (usingSrv ? "srv" : "upload"),
		static_cast<unsigned long long>(resource.byteWidth),
		IaViewByteWidthForResource(resource, iaResource),
		static_cast<unsigned long long>(resource.uavByteWidth),
		resource.stride,
		iaResource ? static_cast<unsigned long long>(iaResource->GetGPUVirtualAddress()) : 0ull);
#else
	(void)target;
	(void)slot;
	(void)resource;
	(void)iaResource;
#endif
}

static bool AppendPreSkinResourceViewLocked(
	ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const DX12TriggeredTextureOverride &entry, DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement || !entry.preSkinResource)
		return false;
	if (entry.preSkinByteWidth == 0 || entry.preSkinStride == 0)
		return false;

	if (replacement->vertexBuffers.empty() ||
	    entry.vertexSlot < replacement->vertexBufferStartSlot ||
	    entry.vertexSlot >= replacement->vertexBufferStartSlot + replacement->vertexBuffers.size()) {
		uint32_t minSlot = replacement->vertexBuffers.empty() ?
			entry.vertexSlot : (std::min)(replacement->vertexBufferStartSlot, entry.vertexSlot);
		uint32_t maxSlot = replacement->vertexBuffers.empty() ?
			entry.vertexSlot : (std::max)(
				replacement->vertexBufferStartSlot +
				static_cast<uint32_t>(replacement->vertexBuffers.size()) - 1,
				entry.vertexSlot);
		if (maxSlot - minSlot >= 32)
			return false;
		std::vector<D3D12_VERTEX_BUFFER_VIEW> resized(maxSlot - minSlot + 1);
		for (size_t i = 0; i < replacement->vertexBuffers.size(); ++i) {
			uint32_t slot = replacement->vertexBufferStartSlot + static_cast<uint32_t>(i);
			resized[slot - minSlot] = replacement->vertexBuffers[i];
		}
		replacement->vertexBufferStartSlot = minSlot;
		replacement->vertexBuffers.swap(resized);
	}

	D3D12_VERTEX_BUFFER_VIEW &view =
		replacement->vertexBuffers[entry.vertexSlot - replacement->vertexBufferStartSlot];
	view.BufferLocation = entry.preSkinResource->GetGPUVirtualAddress();
	view.SizeInBytes = static_cast<UINT>((std::min)(
		entry.preSkinByteWidth, static_cast<UINT64>(UINT_MAX)));
	const DX12IaBufferHash *sourceSlot = FindIaVertexSlot(iaState, entry.vertexSlot);
	view.StrideInBytes = entry.preSkinStride ? entry.preSkinStride :
		(sourceSlot ? sourceSlot->vertexView.StrideInBytes : 0);
	replacement->RetainResource(entry.preSkinResource);
	if (view.BufferLocation != 0 && view.SizeInBytes != 0 && view.StrideInBytes != 0) {
		RetainPreSkinResourceForCommandList(commandList, entry.preSkinResource);
		return true;
	}
	return false;
}
static bool AppendResourceViewsLocked(
	ID3D12Device *device, ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	const std::wstring &indexResource,
	const std::vector<DX12CompiledVertexResourceBinding> &vertexResources,
	DX12ModIaReplacement *replacement)
{
	if (!device || !commandList || !replacement)
		return false;

	bool changed = false;
	if (!indexResource.empty()) {
		DX12LoadedResource *resource = EnsureLoadedResourceLocked(device, indexResource);
		ID3D12Resource *iaResource = SelectIaResourceForViewLocked(commandList, resource);
		if (resource && iaResource) {
			replacement->indexBuffer.BufferLocation = iaResource->GetGPUVirtualAddress();
			replacement->indexBuffer.SizeInBytes =
				IaViewByteWidthForResource(*resource, iaResource);
			replacement->indexBuffer.Format =
				resource->format == DXGI_FORMAT_R16_UINT ||
				resource->format == DXGI_FORMAT_R32_UINT ?
				resource->format :
				(iaState.hasIndexBuffer && iaState.indexView.Format != DXGI_FORMAT_UNKNOWN ?
					iaState.indexView.Format : DXGI_FORMAT_R32_UINT);
			replacement->RetainResource(iaResource);
			replacement->hasIndexBuffer = true;
			LogIaResourceBindLimited("ib", 0, *resource, iaResource);
			changed = true;
		}
	}

	if (!vertexResources.empty()) {
		uint32_t minSlot = vertexResources.front().slot;
		uint32_t maxSlot = vertexResources.back().slot;
		if (maxSlot >= minSlot && maxSlot - minSlot < 32) {
			if (replacement->vertexBuffers.empty() ||
			    minSlot < replacement->vertexBufferStartSlot ||
			    maxSlot >= replacement->vertexBufferStartSlot + replacement->vertexBuffers.size()) {
				uint32_t oldStart = replacement->vertexBuffers.empty() ?
					minSlot : replacement->vertexBufferStartSlot;
				uint32_t oldEnd = replacement->vertexBuffers.empty() ?
					maxSlot : replacement->vertexBufferStartSlot +
						static_cast<uint32_t>(replacement->vertexBuffers.size()) - 1;
				uint32_t newStart = (std::min)(oldStart, minSlot);
				uint32_t newEnd = (std::max)(oldEnd, maxSlot);
				if (newEnd - newStart >= 32)
					return changed;

				std::vector<D3D12_VERTEX_BUFFER_VIEW> resized(newEnd - newStart + 1);
				for (size_t i = 0; i < replacement->vertexBuffers.size(); ++i) {
					uint32_t slot = replacement->vertexBufferStartSlot + static_cast<uint32_t>(i);
					resized[slot - newStart] = replacement->vertexBuffers[i];
				}
				replacement->vertexBufferStartSlot = newStart;
				replacement->vertexBuffers.swap(resized);
			}

			for (const auto &item : vertexResources) {
				DX12LoadedResource *resource = EnsureLoadedResourceLocked(device, item.resource);
				ID3D12Resource *iaResource = SelectIaResourceForViewLocked(commandList, resource);
				if (!resource || !iaResource)
					continue;
				D3D12_VERTEX_BUFFER_VIEW &view =
					replacement->vertexBuffers[item.slot - replacement->vertexBufferStartSlot];
				view.BufferLocation = iaResource->GetGPUVirtualAddress();
				view.SizeInBytes = IaViewByteWidthForResource(*resource, iaResource);
				const DX12IaBufferHash *sourceSlot = FindIaVertexSlot(iaState, item.slot);
				view.StrideInBytes = resource->stride ? resource->stride :
					(sourceSlot ? sourceSlot->vertexView.StrideInBytes : 0);
				replacement->RetainResource(iaResource);
				LogIaResourceBindLimited("vb", item.slot, *resource, iaResource);
				changed = true;
			}
		}
	}
	return changed;
}

static const Bunny::TextureOverrideConfig *FindTargetTextureOverrideLocked(
	const DX12IaHashState &iaState, const Bunny::CommandListTarget &target,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex)
{
	if (target.kind == Bunny::CommandListTargetKind::IndexBuffer) {
		if (!iaState.hasIndexBuffer || !iaState.indexHash)
			return nullptr;
		return FindTextureOverrideLocked(
			iaState.indexHash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, 0, false, true, 0);
	}

	if (target.kind == Bunny::CommandListTargetKind::VertexBuffer) {
		const DX12IaBufferHash *slot = FindIaVertexSlot(iaState, target.slot);
		if (!slot || !slot->hash)
			return nullptr;
		return FindTextureOverrideLocked(
			slot->hash, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, 0, false, false, target.slot);
	}

	return nullptr;
}

static bool CommandListRuntimeEffectLocked(const std::wstring &name)
{
	auto it = gCommandListRuntimeEffect.find(name);
	return it == gCommandListRuntimeEffect.end() || it->second;
}

static bool TextureOverrideRuntimeEffectLocked(
	const Bunny::TextureOverrideConfig &config, bool includePost)
{
	const auto &effects = includePost ?
		gTextureOverridePostRuntimeEffect :
		gTextureOverridePreRuntimeEffect;
	auto it = effects.find(config.section);
	return it == effects.end() || it->second;
}

static DX12IaHashState EmptyIaHashState()
{
	DX12IaHashState state = {};
	return state;
}

class DX12CommandListExecutor
{
public:
	DX12CommandListExecutor(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12IaHashState &iaState,
		uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
		uint32_t firstVertex, uint32_t firstIndex,
		DX12ModIaReplacement *replacement)
		: mDevice(device),
		  mCommandList(commandList),
		  mIaState(iaState),
		  mVertexCount(vertexCount),
		  mIndexCount(indexCount),
		  mInstanceCount(instanceCount),
		  mFirstVertex(firstVertex),
		  mFirstIndex(firstIndex),
		  mReplacement(replacement)
	{
	}

	void SetExecutionMode(bool executeCommands, bool executeDrawActions)
	{
		mExecuteCommands = executeCommands;
		mExecuteDrawActions = executeDrawActions;
	}

	bool Changed() const
	{
		return mChanged;
	}

	void ClearChanged()
	{
		mChanged = false;
	}

	void MarkExternalChange()
	{
		MarkChanged();
	}

	void RunTextureOverride(
		const Bunny::TextureOverrideConfig &config, bool includePost)
	{
		RunTextureOverride(config, 0, includePost);
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, bool includePost)
	{
		RunCommandListLinks(links, 0, includePost);
	}

	void RunPostTextureOverrideLists(
		const Bunny::TextureOverrideConfig &config, bool postRuntimeEffect)
	{
		if (!postRuntimeEffect)
			return;
		// Post lists are collected during prepare and replayed once after the
		// original draw. That keeps the expensive override search from running
		// twice for the same replacement while preserving the DX11-style order.
		for (const std::wstring &list : config.commandLists.post)
			mReplacement->AddPostCommandList(list);
	}

	void RunPostShaderOverrideLists(
		const Bunny::ShaderOverrideConfig &config)
	{
		Bunny::CommandListLinks postLinks;
		postLinks.main = config.commandLists.post;
		RunCommandListLinks(postLinks, 0, true);
	}

private:
	void MarkChanged()
	{
		mChanged = true;
	}

	void SetSkip()
	{
		if (mReplacement)
			mReplacement->skip = true;
	}

	void AppendDraw(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DrawCall draw;
		draw.indexed = action.kind == Bunny::CommandListActionKind::DrawIndexed;
		draw.count = action.args[0];
		draw.start = action.args[1];
		draw.baseVertex = draw.indexed ? static_cast<INT>(action.args[2]) : 0;
		mReplacement->draws.push_back(draw);
		MarkChanged();
	}

	void AppendDispatch(const Bunny::CommandListAction &action)
	{
		if (!mReplacement)
			return;
		DX12ModIaReplacement::DispatchCall dispatch;
		dispatch.groupsX = action.args[0];
		dispatch.groupsY = action.args[1];
		dispatch.groupsZ = action.args[2];
		mReplacement->dispatches.push_back(dispatch);
		MarkChanged();
	}

	void AppendResourceViews(
		const std::wstring &indexResource,
		const std::vector<DX12CompiledVertexResourceBinding> &vertexResources)
	{
		mChanged |= AppendResourceViewsLocked(
			mDevice, mCommandList, mIaState, indexResource, vertexResources, mReplacement);
	}

	const Bunny::TextureOverrideConfig *FindTargetTextureOverride(
		const Bunny::CommandListTarget &target) const
	{
		return FindTargetTextureOverrideLocked(
			mIaState, target, mVertexCount, mIndexCount, mInstanceCount,
			mFirstVertex, mFirstIndex);
	}

	void RunTextureOverride(
		const Bunny::TextureOverrideConfig &config, int depth, bool includePost)
	{
		if (!mReplacement || depth > 16)
			return;

		if (mExecuteCommands) {
			for (const std::wstring &list : config.commandLists.pre) {
				Bunny::CommandListLinks links;
				links.main.push_back(list);
				RunCommandListLinks(links, depth + 1, false);
			}
		}

		if (!RunCompiledTextureOverridePlan(config)) {
			RunTextureOverrideActions(config, depth, includePost);
		}

		if (mExecuteCommands) {
			for (const std::wstring &list : config.commandLists.main) {
				Bunny::CommandListLinks links;
				links.main.push_back(list);
				RunCommandListLinks(links, depth + 1, false);
			}

			if (includePost) {
				for (const std::wstring &list : config.commandLists.post) {
					Bunny::CommandListLinks links;
					links.main.push_back(list);
					RunCommandListLinks(links, depth + 1, true);
				}
			}
		}
	}

	bool RunCompiledTextureOverridePlan(const Bunny::TextureOverrideConfig &config)
	{
		auto sectionIdIt = gTextureOverrideSectionIds.find(config.section);
		if (sectionIdIt == gTextureOverrideSectionIds.end() || !sectionIdIt->second)
			return false;
		auto planIt = gCompiledTextureOverridePlans.find(sectionIdIt->second);
		if (planIt == gCompiledTextureOverridePlans.end())
			return false;
		const DX12CompiledTextureOverridePlan &plan = planIt->second;
		if (plan.hasUnsupportedCommandActions)
			return false;

		AppendResourceViews(plan.indexResource, plan.vertexResources);
		const bool executeActions = mExecuteCommands || mExecuteDrawActions;
		if (executeActions && plan.handlingSkip)
			SetSkip();
		if (executeActions) {
			for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
				mReplacement->draws.push_back(draw);
				MarkChanged();
			}
			for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
				mReplacement->dispatches.push_back(dispatch);
				MarkChanged();
			}
		}
		return true;
	}

	bool RunCompiledCommandListPlan(const std::wstring &name)
	{
		if (!EnterCommandList(name))
			return true;
		auto planIt = gCompiledCommandListPlans.find(name);
		if (planIt == gCompiledCommandListPlans.end() || planIt->second.unsupported) {
			LeaveCommandList();
			return false;
		}
		ExecuteCompiledCommandListPlan(planIt->second);
		LeaveCommandList();
		return true;
	}

	void ExecuteCompiledCommandListPlan(const DX12CompiledCommandListPlan &plan)
	{
		AppendResourceViews(plan.indexResource, plan.vertexResources);
		if (plan.handlingSkip)
			SetSkip();
		for (const DX12ModIaReplacement::DrawCall &draw : plan.draws) {
			mReplacement->draws.push_back(draw);
			MarkChanged();
		}
		for (const DX12ModIaReplacement::DispatchCall &dispatch : plan.dispatches) {
			mReplacement->dispatches.push_back(dispatch);
			MarkChanged();
		}
	}

	void RunTextureOverrideActions(
		const Bunny::TextureOverrideConfig &config, int depth, bool includePost)
	{
		if (!mReplacement)
			return;

		const bool executeActions = mExecuteCommands || mExecuteDrawActions;
		for (const Bunny::CommandListAction &action : config.actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				if (!mExecuteCommands)
					break;
				auto it = gCommandLists.find(action.commandList);
				if (it != gCommandLists.end())
					RunCommandList(it->second, depth + 1, includePost);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				if (!mExecuteCommands)
					break;
				const Bunny::TextureOverrideConfig *matchedConfig =
					FindTargetTextureOverride(action.target);
				if (matchedConfig)
					RunTextureOverride(*matchedConfig, depth + 1, includePost);
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				if (executeActions)
					SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				AppendResourceViews(
					L"", std::vector<DX12CompiledVertexResourceBinding>{
						{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				if (executeActions)
					AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				if (executeActions)
					AppendDispatch(action);
				break;
			}
		}
	}

	void RunCommandList(
		const Bunny::CommandListConfig &commandList, int depth, bool includePost)
	{
		if (!mReplacement || depth > 16)
			return;
		if (RunCompiledCommandListPlan(commandList.section))
			return;
		if (!EnterCommandList(commandList.section))
			return;
		for (const Bunny::CommandListAction &action : commandList.actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run: {
				auto it = gCommandLists.find(action.commandList);
				if (it != gCommandLists.end())
					RunCommandList(it->second, depth + 1, includePost);
				break;
			}
			case Bunny::CommandListActionKind::CheckTextureOverride: {
				const Bunny::TextureOverrideConfig *config =
					FindTargetTextureOverride(action.target);
				if (config)
					RunTextureOverride(*config, depth + 1, includePost);
				break;
			}
			case Bunny::CommandListActionKind::HandlingSkip:
				SetSkip();
				break;
			case Bunny::CommandListActionKind::SetIndexBuffer:
				AppendResourceViews(action.resource, std::vector<DX12CompiledVertexResourceBinding>());
				break;
			case Bunny::CommandListActionKind::SetVertexBuffer:
				AppendResourceViews(
					L"", std::vector<DX12CompiledVertexResourceBinding>{
						{ action.target.slot, action.resource }});
				break;
			case Bunny::CommandListActionKind::Draw:
			case Bunny::CommandListActionKind::DrawIndexed:
			case Bunny::CommandListActionKind::DrawFromCaller:
			case Bunny::CommandListActionKind::DrawIndexedFromCaller:
				AppendDraw(action);
				break;
			case Bunny::CommandListActionKind::Dispatch:
				AppendDispatch(action);
				break;
			}
		}
		LeaveCommandList();
	}

	void RunCommandListLinks(
		const Bunny::CommandListLinks &links, int depth, bool includePost)
	{
		if (depth > 16)
			return;

		for (const std::wstring &list : links.pre) {
			if (!CommandListRuntimeEffectLocked(list))
				continue;
			if (RunCompiledCommandListPlan(list))
				continue;
			auto it = gCommandLists.find(list);
			if (it != gCommandLists.end())
				RunCommandList(it->second, depth + 1, false);
		}
		for (const std::wstring &list : links.main) {
			if (!CommandListRuntimeEffectLocked(list))
				continue;
			if (RunCompiledCommandListPlan(list))
				continue;
			auto it = gCommandLists.find(list);
			if (it != gCommandLists.end())
				RunCommandList(it->second, depth + 1, includePost);
		}
		if (includePost) {
			for (const std::wstring &list : links.post) {
				if (!CommandListRuntimeEffectLocked(list))
					continue;
				if (RunCompiledCommandListPlan(list))
					continue;
				auto it = gCommandLists.find(list);
				if (it != gCommandLists.end())
					RunCommandList(it->second, depth + 1, true);
			}
		}
	}

	bool EnterCommandList(const std::wstring &name)
	{
		if (name.empty())
			return true;
		if (std::find(mCommandListStack.begin(), mCommandListStack.end(), name) ==
		    mCommandListStack.end()) {
			mCommandListStack.push_back(name);
			return true;
		}
#if defined(_DEBUG)
		DX12LogDebugJsonFunc("DX12CommandListCycleSuppressed",
			"\"section\":\"%S\",\"depth\":%zu", name.c_str(),
			mCommandListStack.size());
#endif
		// CommandList sections can reference each other through run= and
		// checktextureoverride=. D3D12 records commands sequentially; allowing
		// a cycle here multiplies draws/dispatches until the depth cap and can
		// freeze a frame. Suppress re-entry so one matched draw gets one replay
		// of each command-list node in the active graph.
		return false;
	}

	void LeaveCommandList()
	{
		if (!mCommandListStack.empty())
			mCommandListStack.pop_back();
	}

	ID3D12Device *mDevice = nullptr;
	ID3D12GraphicsCommandList *mCommandList = nullptr;
	const DX12IaHashState &mIaState;
	uint32_t mVertexCount = 0;
	uint32_t mIndexCount = 0;
	uint32_t mInstanceCount = 0;
	uint32_t mFirstVertex = 0;
	uint32_t mFirstIndex = 0;
	DX12ModIaReplacement *mReplacement = nullptr;
	bool mChanged = false;
	bool mExecuteCommands = true;
	bool mExecuteDrawActions = true;
	std::vector<std::wstring> mCommandListStack;
};

static bool RunNamedCommandListLocked(
	ID3D12GraphicsCommandList *commandList,
	const std::wstring &name,
	DX12ModIaReplacement *replacement)
{
	if (!commandList || !replacement)
		return false;
	if (!CommandListRuntimeEffectLocked(name))
		return false;

	auto it = gCommandLists.find(name);
	if (it == gCommandLists.end())
		return false;

	ID3D12Device *device = nullptr;
	if (FAILED(commandList->GetDevice(IID_PPV_ARGS(&device))) || !device)
		return false;

	DX12IaHashState emptyIa = EmptyIaHashState();
	DX12CommandListExecutor executor(
		device, commandList, emptyIa, 0, 0, 0, 0, 0, replacement);
	executor.RunCommandListLinks(Bunny::CommandListLinks{ {}, { name }, {} }, true);
	device->Release();
	return executor.Changed();
}

static uint64_t MakeIaSkipCacheKey(uint32_t ibHash, const uint32_t *vbHashes, size_t vbHashCount)
{
	uint64_t hash = 14695981039346656037ull;
	auto append = [&hash](const void *data, size_t size) {
		const unsigned char *bytes = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
	};
	append(&ibHash, sizeof(ibHash));
	for (size_t i = 0; vbHashes && i < vbHashCount; ++i) {
		if (!vbHashes[i])
			continue;
		append(&vbHashes[i], sizeof(vbHashes[i]));
	}
	return hash;
}

static uint64_t MakeIaReplacementCacheKey(
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	UINT64 vsHash, UINT64 psHash)
{
	uint64_t key = 14695981039346656037ull;
	key = HashCombine64(key, gReloadGeneration);
	key = HashCombine64(key, gPreSkinActiveGeneration);
	key = HashCombine64(key, vsHash);
	key = HashCombine64(key, psHash);
	key = HashCombine64(key, iaState.hasIndexBuffer ? iaState.indexHash : 0);
	key = HashCombine64(key, vertexCount);
	key = HashCombine64(key, indexCount);
	key = HashCombine64(key, instanceCount);
	key = HashCombine64(key, firstVertex);
	key = HashCombine64(key, firstIndex);
	key = HashCombine64(key, firstInstance);
	for (const DX12IaBufferHash &buffer : iaState.vertexBuffers) {
		if (!buffer.hash)
			continue;
		key = HashCombine64(key, buffer.slot);
		key = HashCombine64(key, buffer.hash);
	}
	return key;
}

bool DX12ModShouldSkipIa(
	uint32_t ibHash, const uint32_t *vbHashes, size_t vbHashCount,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount)
{
	if (gHasTextureOverrides == 0)
		return false;

	uint64_t cacheKey = MakeIaSkipCacheKey(ibHash, vbHashes, vbHashCount);
	cacheKey ^= static_cast<uint64_t>(vertexCount) << 1;
	cacheKey ^= static_cast<uint64_t>(indexCount) << 17;
	cacheKey ^= static_cast<uint64_t>(instanceCount) << 33;
	AcquireSRWLockShared(&gModLock);
	auto cached = gIaSkipCache.find(cacheKey);
	if (cached != gIaSkipCache.end()) {
		bool skip = cached->second;
		ReleaseSRWLockShared(&gModLock);
		return skip;
	}
	ReleaseSRWLockShared(&gModLock);

	bool skip = false;
	uint32_t matchedHash = 0;
	std::wstring section;
	AcquireSRWLockShared(&gModLock);
	if (ibHash && TextureOverrideHasSkipLocked(
		ibHash, vertexCount, indexCount, instanceCount, &section, true, 0)) {
		skip = true;
		matchedHash = ibHash;
	}
	if (!skip && vbHashes) {
		for (size_t i = 0; i < vbHashCount; ++i) {
			if (!vbHashes[i])
				continue;
			if (TextureOverrideHasSkipLocked(
				vbHashes[i], vertexCount, indexCount, instanceCount, &section,
				false, static_cast<uint32_t>(i))) {
				skip = true;
				matchedHash = vbHashes[i];
				break;
			}
		}
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	if (gIaSkipCache.size() > 4096)
		gIaSkipCache.clear();
	gIaSkipCache[cacheKey] = skip;
	ReleaseSRWLockExclusive(&gModLock);
	return skip;
}

bool DX12ModPrepareIaReplacement(
	ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement)
{
	if (!replacement)
		return false;
	*replacement = DX12ModIaReplacement();
	if (!commandList || gHasTextureOverrides == 0)
		return false;

	ID3D12PipelineState *pipelineState =
		DX12CommandListRuntimeGetPipelineState(commandList);
	DX12PsoShaderInfo shaderInfo = {};
	DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo);

	const uint64_t matchCacheKey = MakeIaReplacementCacheKey(
		iaState, vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, firstInstance,
		shaderInfo.hasVS ? shaderInfo.vs : 0,
		shaderInfo.hasPS ? shaderInfo.ps : 0);
	const LONG present = DX12GetPresentCount();
	const bool hasActivePreSkin = DX12ModHasActivePreSkinTextureOverrides();
	std::vector<DX12TriggeredTextureOverride> overrides;
	bool matchCacheHit = false;
	AcquireSRWLockShared(&gModLock);
	if (!hasActivePreSkin && gIaReplacementPrepareCachePresent == present) {
		auto prepared = gIaReplacementPreparedFrameCache.find(matchCacheKey);
		if (prepared != gIaReplacementPreparedFrameCache.end()) {
			// Prepared replacements contain command-list ready GPU views and
			// optional retained resources, so they are reused only within the
			// current frame. BeginFrame invalidation keeps GPU VA/resource
			// lifetime assumptions local while avoiding repeated IA work.
			*replacement = prepared->second;

			const bool changed = replacement->skip || replacement->hasIndexBuffer ||
				!replacement->vertexBuffers.empty() || !replacement->draws.empty() ||
				!replacement->dispatches.empty() || !replacement->postCommandLists.empty();
			ReleaseSRWLockShared(&gModLock);
			return changed;
		}
		auto cached = gIaReplacementMatchCache.find(matchCacheKey);
		if (cached != gIaReplacementMatchCache.end()) {
			overrides = cached->second;
			matchCacheHit = true;

		}
	}
	ReleaseSRWLockShared(&gModLock);

	if (!matchCacheHit) {
		AcquireSRWLockShared(&gModLock);
		FindMatchingIaOverridesLocked(
			iaState, vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, firstInstance, &overrides);
		ReleaseSRWLockShared(&gModLock);
		AcquireSRWLockExclusive(&gModLock);
		const LONG currentPresent = DX12GetPresentCount();
		if (gIaReplacementPrepareCachePresent != currentPresent) {
			gIaReplacementMatchCache.clear();
			gIaReplacementPreparedFrameCache.clear();
			gIaReplacementPrepareCachePresent = currentPresent;
		}
		if (!hasActivePreSkin) {
			if (gIaReplacementMatchCache.size() > 4096)
				gIaReplacementMatchCache.clear();
			gIaReplacementMatchCache[matchCacheKey] = overrides;
		}
		ReleaseSRWLockExclusive(&gModLock);
	}

	if (overrides.empty()) {
		return false;
	}
	LogIaMatchLimited(
		iaState, vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, overrides);

	// D3D12 draw hooks only record commands; resolve the device after a real
	// IA match so negative cache hits do not pay a COM GetDevice/AddRef cost.
	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	AcquireSRWLockExclusive(&gModLock);
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, replacement);
	for (const DX12TriggeredTextureOverride &entry : overrides) {
		executor.SetExecutionMode(entry.executeCommands, entry.executeDrawActions);
		executor.RunTextureOverride(TriggeredTextureOverrideConfig(entry), false);
		if (entry.preSkinAnchor) {
			if (AppendPreSkinResourceViewLocked(commandList, iaState, entry, replacement))
				executor.MarkExternalChange();
		}
		executor.RunPostTextureOverrideLists(
			TriggeredTextureOverrideConfig(entry), entry.postRuntimeEffect);
	}
	if (indexCount != 0 && ReplacementHasIndexedDraw(*replacement)) {
		const size_t removed = RemoveNonIndexedDraws(replacement);
#if defined(_DEBUG)
		if (removed) {
				DX12LogDebugJsonFunc("DX12IaReplacementDrawPrune",
					"\"reason\":\"indexed_caller_has_indexed_replacement\",\"removed\":%zu",
					removed);
		}
#else
		(void)removed;
#endif
	}
	// DX11 does NOT validate VB0; it lets the game's currently-bound VB0 be
	// used by replacement draws.  D3D12 command lists are sequential, so the
	// VB0 set by the game before the draw call is still valid when the
	// replacement draws execute.  Removing this check matches the DX11
	// behaviour and fixes the "mod broken" issue where indexed replacement
	// draws were silently dropped.
	LogIaReplacementLimited(
		iaState, vertexCount, indexCount, instanceCount, *replacement, overrides);
	if (gIaReplacementPreparedFrameCache.size() > 4096)
		gIaReplacementPreparedFrameCache.clear();
	if (!hasActivePreSkin)
		gIaReplacementPreparedFrameCache[matchCacheKey] = *replacement;
	ReleaseSRWLockExclusive(&gModLock);
	device->Release();

	return executor.Changed() || replacement->skip || !replacement->draws.empty() ||
		!replacement->dispatches.empty() || !replacement->postCommandLists.empty();
}

void DX12ModRunPostIaReplacement(
	ID3D12GraphicsCommandList *commandList, const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex, uint32_t firstInstance,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !commandList || gHasTextureOverrides == 0)
		return;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return;

	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, replacement);
	AcquireSRWLockExclusive(&gModLock);
	const std::vector<std::wstring> postCommandLists = replacement->postCommandLists;
	for (const std::wstring &listName : postCommandLists) {
		if (gCommandLists.find(listName) == gCommandLists.end())
			continue;
		Bunny::CommandListLinks links;
		links.main.push_back(listName);
		// Post command lists are now replayed from the prepared replacement so
		// the same override graph is not traversed twice per draw.
		// CRITICAL: iterate a snapshot and pass includePost=false so post replay
		// cannot append to the active replacement while we are consuming it.
		executor.RunCommandListLinks(links, false);
	}
	ReleaseSRWLockExclusive(&gModLock);
	device->Release();
}

static void AppendShaderOverrideForHashLocked(
	uint64_t hash, std::vector<const Bunny::ShaderOverrideConfig*> *configs,
	std::unordered_set<std::wstring> *seen)
{
	if (!hash || !configs || !seen)
		return;
	auto it = gShaderOverrides.find(hash);
	if (it == gShaderOverrides.end())
		return;
	if (!seen->insert(it->second.section).second)
		return;
	configs->push_back(&it->second);
}

static void FindShaderOverridesForPsoLocked(
	ID3D12PipelineState *pipelineState, bool dispatch,
	std::vector<const Bunny::ShaderOverrideConfig*> *configs)
{
	if (!configs)
		return;
	configs->clear();
	if (!pipelineState)
		return;

	auto cached = gShaderOverridePsoMatchCache.find(pipelineState);
	if (cached != gShaderOverridePsoMatchCache.end() &&
	    cached->second.generation == gReloadGeneration) {
		*configs = dispatch ? cached->second.dispatchConfigs : cached->second.drawConfigs;
#if defined(_DEBUG)
			DX12LogDebugJsonFunc("DX12ShaderOverridePsoMatchCache",
				"\"status\":\"hit\",\"pso\":\"%p\",\"dispatch\":%s,\"matches\":%zu",
				pipelineState, dispatch ? "true" : "false", configs->size());
#endif
		return;
	}

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return;

	std::unordered_set<std::wstring> seen;
	DX12ShaderOverridePsoMatchCache matchCache;
	matchCache.generation = gReloadGeneration;
	if (dispatch) {
		if (info.hasCS)
			AppendShaderOverrideForHashLocked(info.cs, &matchCache.dispatchConfigs, &seen);
		*configs = matchCache.dispatchConfigs;
		gShaderOverridePsoMatchCache[pipelineState] = std::move(matchCache);
		return;
	}

	if (info.hasVS)
		AppendShaderOverrideForHashLocked(info.vs, &matchCache.drawConfigs, &seen);
	if (info.hasPS)
		AppendShaderOverrideForHashLocked(info.ps, &matchCache.drawConfigs, &seen);
	*configs = matchCache.drawConfigs;
	gShaderOverridePsoMatchCache[pipelineState] = std::move(matchCache);
}

bool DX12ModPrepareShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex,
	DX12ModIaReplacement *replacement)
{
	if (!replacement)
		return false;
	*replacement = DX12ModIaReplacement();
	if (!commandList || !pipelineState || gHasShaderOverrides == 0)
		return false;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return false;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	AcquireSRWLockShared(&gModLock);
	FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
	ReleaseSRWLockShared(&gModLock);

	if (configs.empty()) {
		device->Release();
		return false;
	}

	AcquireSRWLockExclusive(&gModLock);
	DX12CommandListExecutor executor(
		device, commandList, iaState,
		vertexCount, indexCount, instanceCount,
		firstVertex, firstIndex, replacement);
	executor.SetExecutionMode(true, true);
	for (const Bunny::ShaderOverrideConfig *config : configs) {
		if (!config)
			continue;
		if (config->handlingSkip)
			replacement->skip = true;
		executor.RunCommandListLinks(config->commandLists, false);
	}
	// DX11 behaviour: let the game's currently-bound VB0 be used by
	// replacement draws; same rationale as the TextureOverride path above.
	LogShaderOverrideCommandListLimited("pre", configs, *replacement);
	ReleaseSRWLockExclusive(&gModLock);
	device->Release();

	return executor.Changed() || replacement->skip || !replacement->draws.empty() ||
		!replacement->dispatches.empty();
}

void DX12ModRunPostShaderOverrideReplacement(
	ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
	const DX12IaHashState &iaState,
	uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount,
	uint32_t firstVertex, uint32_t firstIndex,
	DX12ModIaReplacement *replacement)
{
	if (!replacement || !commandList || !pipelineState || gHasShaderOverrides == 0)
		return;

	ID3D12Device *device = AcquireModDevice(commandList);
	if (!device)
		return;

	std::vector<const Bunny::ShaderOverrideConfig*> configs;
	AcquireSRWLockShared(&gModLock);
	FindShaderOverridesForPsoLocked(pipelineState, false, &configs);
	ReleaseSRWLockShared(&gModLock);

	if (!configs.empty()) {
		AcquireSRWLockExclusive(&gModLock);
		DX12CommandListExecutor executor(
			device, commandList, iaState,
			vertexCount, indexCount, instanceCount,
			firstVertex, firstIndex, replacement);
		executor.SetExecutionMode(true, true);
		for (const Bunny::ShaderOverrideConfig *config : configs) {
			if (!config)
				continue;
			executor.RunPostShaderOverrideLists(*config);
		}
		LogShaderOverrideCommandListLimited("post", configs, *replacement);
		ReleaseSRWLockExclusive(&gModLock);
	}
	device->Release();
}

static bool ShaderOverrideHasSkipLocked(uint64_t hash)
{
	auto it = gShaderOverrides.find(hash);
	return it != gShaderOverrides.end() && it->second.handlingSkip;
}

static bool ShaderBytecodeHasSkipLocked(const D3D12_SHADER_BYTECODE &bytecode)
{
	if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength)
		return false;
	return ShaderOverrideHasSkipLocked(
		DX12ModHashShaderBytecode(bytecode.pShaderBytecode, bytecode.BytecodeLength));
}

static bool StoredPsoHasSkipLocked(const DX12StoredPso &record, bool dispatch)
{
	if (dispatch) {
		return record.kind == DX12PsoKind::Compute &&
			ShaderBytecodeHasSkipLocked(record.computeDesc.CS);
	}

	return record.kind == DX12PsoKind::Graphics &&
		(ShaderBytecodeHasSkipLocked(record.graphicsDesc.VS) ||
		 ShaderBytecodeHasSkipLocked(record.graphicsDesc.PS));
}

static void UpdateStoredPsoSkipLocked(DX12StoredPso *record)
{
	if (!record || record->skipGeneration == gReloadGeneration)
		return;

	record->skipDraw = false;
	record->skipDispatch = false;
	if (record->kind == DX12PsoKind::Compute) {
		record->skipDispatch = ShaderBytecodeHasSkipLocked(record->computeDesc.CS);
	} else {
		record->skipDraw =
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.VS) ||
			ShaderBytecodeHasSkipLocked(record->graphicsDesc.PS);
	}
	record->skipGeneration = gReloadGeneration;
}

bool DX12ModShouldSkipPipelineState(ID3D12PipelineState *pipelineState, bool dispatch)
{
	if (!pipelineState || gHasShaderOverrides == 0)
		return false;

	bool skip = false;
	AcquireSRWLockShared(&gModLock);
	auto record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		if (record->second.skipGeneration == gReloadGeneration) {
			skip = dispatch ? record->second.skipDispatch : record->second.skipDraw;
			ReleaseSRWLockShared(&gModLock);
			return skip;
		}
	}
	ReleaseSRWLockShared(&gModLock);

	AcquireSRWLockExclusive(&gModLock);
	record = gPsoRecords.find(pipelineState);
	if (record != gPsoRecords.end()) {
		UpdateStoredPsoSkipLocked(&record->second);
		skip = dispatch ? record->second.skipDispatch : record->second.skipDraw;
		ReleaseSRWLockExclusive(&gModLock);
		return skip;
	}
	ReleaseSRWLockExclusive(&gModLock);

	DX12PsoShaderInfo info = {};
	if (!DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return false;

	AcquireSRWLockShared(&gModLock);
	if (dispatch) {
		skip = info.hasCS && ShaderOverrideHasSkipLocked(info.cs);
	} else {
		skip = (info.hasVS && ShaderOverrideHasSkipLocked(info.vs)) ||
			(info.hasPS && ShaderOverrideHasSkipLocked(info.ps));
	}
	ReleaseSRWLockShared(&gModLock);
	return skip;
}

UINT64 DX12ModGetReloadGeneration()
{
	AcquireSRWLockShared(&gModLock);
	UINT64 generation = gReloadGeneration;
	ReleaseSRWLockShared(&gModLock);
	return generation;
}

void DX12ModRecordGraphicsPipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Graphics;
	record.device = device;
	record.device->AddRef();
	DeepCopyGraphicsDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);
	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

void DX12ModRecordComputePipelineState(
	ID3D12Device *device, ID3D12PipelineState *pipelineState,
	const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
	if (!device || !pipelineState || !desc)
		return;

	DX12StoredPso record;
	record.kind = DX12PsoKind::Compute;
	record.device = device;
	record.device->AddRef();
	DeepCopyComputeDesc(desc, &record);

	AcquireSRWLockExclusive(&gModLock);
	auto existing = gPsoRecords.find(pipelineState);
	if (existing != gPsoRecords.end())
		ReleaseStoredPso(&existing->second);
	gPsoRecords[pipelineState] = record;
	ReleaseSRWLockExclusive(&gModLock);
}

static bool HasShaderOverrideLocked(uint64_t hash)
{
	return gShaderOverrides.find(hash) != gShaderOverrides.end();
}

static bool GraphicsPsoNeedsReplacementLocked(const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc)
{
	if (desc.VS.pShaderBytecode && desc.VS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.VS.pShaderBytecode, desc.VS.BytecodeLength)))
		return true;
	if (desc.PS.pShaderBytecode && desc.PS.BytecodeLength &&
	    HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.PS.pShaderBytecode, desc.PS.BytecodeLength)))
		return true;
	return false;
}

static bool ComputePsoNeedsReplacementLocked(const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc)
{
	return desc.CS.pShaderBytecode && desc.CS.BytecodeLength &&
		HasShaderOverrideLocked(DX12ModHashShaderBytecode(desc.CS.pShaderBytecode, desc.CS.BytecodeLength));
}

static ID3D12PipelineState *CreateGraphicsReplacement(DX12StoredPso *record)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = record->graphicsDesc;
	D3D12_SHADER_BYTECODE vs = {};
	D3D12_SHADER_BYTECODE ps = {};
	std::vector<unsigned char> vsBytes;
	std::vector<unsigned char> psBytes;
	bool changed = false;

	if (DX12ModReplaceShaderBytecode("vs", desc.VS, &vs, &vsBytes)) {
		desc.VS = vs;
		changed = true;
	}
	if (DX12ModReplaceShaderBytecode("ps", desc.PS, &ps, &psBytes)) {
		desc.PS = ps;
		changed = true;
	}
	if (!changed)
		return nullptr;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12CreateGraphicsPipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	DX12LogDebugJsonFunc("DX12ReplacementPsoCreate",
		"\"kind\":\"graphics\",\"hr\":\"0x%lx\",\"pso\":\"%p\"",
		hr, replacement);
	return SUCCEEDED(hr) ? replacement : nullptr;
}

static ID3D12PipelineState *CreateComputeReplacement(DX12StoredPso *record)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = record->computeDesc;
	D3D12_SHADER_BYTECODE cs = {};
	std::vector<unsigned char> csBytes;
	if (!DX12ModReplaceShaderBytecode("cs", desc.CS, &cs, &csBytes))
		return nullptr;
	desc.CS = cs;

	ID3D12PipelineState *replacement = nullptr;
	HRESULT hr = DX12CreateComputePipelineStateOriginal(
		record->device, &desc, IID_PPV_ARGS(&replacement));
	DX12LogDebugJsonFunc("DX12ReplacementPsoCreate",
		"\"kind\":\"compute\",\"hr\":\"0x%lx\",\"pso\":\"%p\"",
		hr, replacement);
	return SUCCEEDED(hr) ? replacement : nullptr;
}

ID3D12PipelineState *DX12ModGetReplacementPipelineState(ID3D12PipelineState *pipelineState)
{
	if (!pipelineState || gHasShaderOverrides == 0)
		return nullptr;

	DX12StoredPso createRecord;
	bool shouldCreate = false;
	UINT64 generation = 0;

	AcquireSRWLockExclusive(&gModLock);
	auto it = gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end()) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	DX12StoredPso &record = it->second;
	generation = gReloadGeneration;
	if (record.replacement && record.replacementGeneration == generation) {
		ID3D12PipelineState *replacement = record.replacement;
		ReleaseSRWLockExclusive(&gModLock);
		return replacement;
	}

	if (record.replacement) {
		record.replacement->Release();
		record.replacement = nullptr;
		record.replacementGeneration = 0;
	}

	bool needsReplacement = record.kind == DX12PsoKind::Graphics ?
		GraphicsPsoNeedsReplacementLocked(record.graphicsDesc) :
		ComputePsoNeedsReplacementLocked(record.computeDesc);
	if (!needsReplacement) {
		ReleaseSRWLockExclusive(&gModLock);
		return nullptr;
	}

	createRecord.kind = record.kind;
	createRecord.device = record.device;
	createRecord.graphicsRootSignature = record.graphicsRootSignature;
	createRecord.computeRootSignature = record.computeRootSignature;
	createRecord.graphicsDesc = record.graphicsDesc;
	createRecord.computeDesc = record.computeDesc;
	createRecord.vsBytecode = record.vsBytecode;
	createRecord.psBytecode = record.psBytecode;
	createRecord.dsBytecode = record.dsBytecode;
	createRecord.hsBytecode = record.hsBytecode;
	createRecord.gsBytecode = record.gsBytecode;
	createRecord.csBytecode = record.csBytecode;
	if (!createRecord.vsBytecode.empty())
		createRecord.graphicsDesc.VS.pShaderBytecode = createRecord.vsBytecode.data();
	if (!createRecord.psBytecode.empty())
		createRecord.graphicsDesc.PS.pShaderBytecode = createRecord.psBytecode.data();
	if (!createRecord.dsBytecode.empty())
		createRecord.graphicsDesc.DS.pShaderBytecode = createRecord.dsBytecode.data();
	if (!createRecord.hsBytecode.empty())
		createRecord.graphicsDesc.HS.pShaderBytecode = createRecord.hsBytecode.data();
	if (!createRecord.gsBytecode.empty())
		createRecord.graphicsDesc.GS.pShaderBytecode = createRecord.gsBytecode.data();
	if (!createRecord.csBytecode.empty())
		createRecord.computeDesc.CS.pShaderBytecode = createRecord.csBytecode.data();
	if (createRecord.device)
		createRecord.device->AddRef();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->AddRef();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->AddRef();
	shouldCreate = true;
	ReleaseSRWLockExclusive(&gModLock);

	if (!shouldCreate)
		return nullptr;

	ID3D12PipelineState *newReplacement = createRecord.kind == DX12PsoKind::Graphics ?
		CreateGraphicsReplacement(&createRecord) :
		CreateComputeReplacement(&createRecord);
	if (createRecord.device)
		createRecord.device->Release();
	if (createRecord.graphicsRootSignature)
		createRecord.graphicsRootSignature->Release();
	if (createRecord.computeRootSignature)
		createRecord.computeRootSignature->Release();
	if (!newReplacement)
		return nullptr;

	AcquireSRWLockExclusive(&gModLock);
	it = gPsoRecords.find(pipelineState);
	if (it == gPsoRecords.end() || gReloadGeneration != generation) {
		ReleaseSRWLockExclusive(&gModLock);
		newReplacement->Release();
		return nullptr;
	}
	if (it->second.replacement)
		it->second.replacement->Release();
	it->second.replacement = newReplacement;
	it->second.replacementGeneration = generation;
	ReleaseSRWLockExclusive(&gModLock);
	return newReplacement;
}
