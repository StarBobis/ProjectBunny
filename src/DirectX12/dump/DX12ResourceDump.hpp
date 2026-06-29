#pragma once

#include <Windows.h>

#include <string>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DX12BindingTracker.hpp"
#include "DX12CommandQueueState.hpp"
#include "DX12FrameAnalysis.hpp"
#include "DX12FrameAnalysisManifest.hpp"
#include "DX12FrameAnalysisTypes.hpp"
#include "utils/DX12JsonUtils.hpp"
#include "DX12LogManager.hpp"
#include "DX12ResourceDumpCore.hpp"
#include "DX12ResourceDumpFileWriter.hpp"
#include "DX12ResourceDumpGpuCopy.hpp"
#include "DX12ResourceDumpIndexWriter.hpp"
#include "DX12ResourceDumpNaming.hpp"
#include "DX12ResourceDumpPrepare.hpp"
#include "DX12ResourceDumpTypes.hpp"
#include "DX12ResourceDumpPolicy.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12ResourceStateTracker.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderDump.hpp"
#include "DX12ResourceDumpSafetyUtils.hpp"

class DX12ResourceDump
{
public:
static bool IsDiscoveryDuplicateTask(const DumpTask &task)
{
	if (task.sourceKind == DumpTaskSource::Resource)
		return task.binding.bindSpace == "hunting_resource";
	if (task.sourceKind != DumpTaskSource::Descriptor)
		return false;
	return task.binding.bindSpace == "hunting_metadata" ||
		task.binding.bindSpace == "hunting_resource";
}

static DX12FrameAnalysisDumpResult DumpCurrentFrameResources(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<DX12FrameResourceBinding> bindings;
	DX12BindingResourceExport::GetCurrentFrameResourceBindings(&bindings);
	std::vector<DX12FrameIaBufferBinding> iaBuffers;
	DX12BindingFrameExport::GetCurrentFrameIaBuffers(&iaBuffers);
	std::vector<DX12FrameOutputBinding> outputBindings;
	DX12BindingResourceExport::GetCurrentFrameOutputBindings(&outputBindings);

	wchar_t resourceDir[MAX_PATH];
	wcsncpy_s(resourceDir, dir, _TRUNCATE);
	if (!DX12ResourceDumpCore::EnsureDirectory(resourceDir)) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"resource_dir_create_failed\",\"error\":%lu",
			GetLastError());
		result.failed = 1;
		result.reason = "resource_dir_create_failed";
		return result;
	}
	wchar_t dedupeDir[MAX_PATH];
	DX12ResourceDumpCore::GetDedupedDirectory(dir, dedupeDir, ARRAYSIZE(dedupeDir));

	const UINT64 frameReadbackBudget = DX12ResourceDumpCore::FrameReadbackBudgetBytes();
	UINT64 frameReadbackBytes = 0;
	const char *dumpMode = DX12RuntimeState::IsModMode() ? "Mod" : "Hunting";
	DX12FrameAnalysis::LogJsonFunc("ResourceDump",
		"\"mode\":\"%s\",\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu,\"unsafeCopy\":%u,\"gpuCopy\":%u,\"stateTracking\":1",
		dumpMode,
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget),
		DX12ResourceDumpCore::UnsafeResourceCopyEnabled() ? 1 : 0,
		DX12ResourceDumpPrepare::GpuResourceCopyEnabled() ? 1 : 0);
	DX12LogManager::JsonFunc("DX12ResourceDump",
		"\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu",
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget));

	ID3D12CommandQueue *queue = DX12CommandQueueState::Acquire();
	ID3D12Device *device = nullptr;
	if (!queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
		DX12FrameAnalysis::LogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"missing_command_queue\"");
		if (queue)
			queue->Release();
		result.failed = 1;
		result.reason = "missing_command_queue";
		return result;
	}

	std::vector<DumpTask> tasks;
	std::vector<DumpTask> duplicateTasks;
	std::vector<DumpTask> skippedTasks;
	std::vector<DX12ResourceSummary> retainedMetadataResources;
	std::unordered_map<std::string, size_t> canonicalTaskByKey;
	UINT duplicates = 0;
	UINT candidateCbv = 0;
	UINT candidateSrv = 0;
	UINT candidateUav = 0;
	UINT candidateTextures = 0;
	UINT candidateRtv = 0;
	UINT candidateDsv = 0;
	UINT metadataTextureCandidates = 0;
	UINT metadataBufferCandidates = 0;
	UINT metadataCbvBufferCandidates = 0;
	UINT metadataSrvBufferCandidates = 0;
	UINT metadataUavBufferCandidates = 0;
	UINT metadataMissingResource = 0;
	UINT metadataMissingResourceDesc = 0;
	UINT metadataUnsupportedKindOrDimension = 0;
	UINT metadataDuplicates = 0;
	UINT metadataPrepareFailed = 0;
	UINT metadataBufferCopiesQueued = 0;
	UINT metadataBufferCopyBudgetSkipped = 0;
	UINT64 metadataBufferCopyBytesQueued = 0;
	UINT bindingTasksQueued = 0;
	UINT iaTasksQueued = 0;
	UINT outputTasksQueued = 0;
	UINT metadataTextureTasksQueued = 0;
	UINT metadataBufferTasksQueued = 0;
	UINT resourceTasksQueued = 0;
	UINT untrackedDescriptorSlots = 0;
	UINT untrackedComputeUavSlots = 0;

	for (const DX12FrameResourceBinding &binding : bindings) {
		if (!DX12ResourceDumpPolicy::ShouldDumpBinding(binding)) {
			if (!binding.hasDescriptor && !binding.rootDescriptor &&
					(binding.bindSpace == "compute_cbv_srv_uav" ||
					 binding.bindSpace == "graphics_cbv_srv_uav")) {
				DumpTask skipped;
				skipped.sourceKind = DumpTaskSource::Descriptor;
				skipped.binding = binding;
				skipped.skipNote = "untracked_descriptor";
				skippedTasks.push_back(std::move(skipped));
				untrackedDescriptorSlots++;
				if (binding.bindSpace == "compute_cbv_srv_uav" &&
						binding.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
					untrackedComputeUavSlots++;
			}
			continue;
		}
		const char *bindingKind = DX12ResourceDumpCore::BindingKindName(binding);
		if (!strcmp(bindingKind, DescriptorKind::CBV.data()))
			candidateCbv++;
		else if (!strcmp(bindingKind, DescriptorKind::SRV.data()))
			candidateSrv++;
		else if (!strcmp(bindingKind, DescriptorKind::UAV.data()))
			candidateUav++;
		if (binding.descriptor.hasResourceDesc &&
			DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;
		if (!binding.hasDescriptor && !binding.rootDescriptor) {
			DumpTask skipped;
			skipped.binding = binding;
			skipped.skipNote = binding.rootDescriptor ? "root_descriptor_summary_only" : "untracked_descriptor";
			skippedTasks.push_back(std::move(skipped));
			continue;
		}

		char key[128];
		if (binding.rootDescriptor) {
			sprintf_s(key, "root|0x%llx",
				static_cast<unsigned long long>(binding.gpuVirtualAddress));
		} else {
			DX12ResourceDumpNaming::BuildBindingDedupeKeyForCurrentMode(binding, key, ARRAYSIZE(key));
		}
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.binding = binding;
			if (binding.rootDescriptor) {
				duplicate.binding.hasDescriptor = true;
				duplicate.binding.descriptor = tasks[canonicalIt->second].binding.descriptor;
			}
			duplicate.sourceKind = DumpTaskSource::Descriptor;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			bindingTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			if (task.skipNote == nullptr)
				task.skipNote = "unsupported_or_no_resource_pointer";
			skippedTasks.push_back(std::move(task));
		}
	}

	for (const DX12FrameIaBufferBinding &buffer : iaBuffers) {
		char key[128];
		sprintf_s(key, "ia|%p|%llu|%llu|%08x|%u|%u",
			buffer.resource.resource,
			static_cast<unsigned long long>(buffer.resource.resourceOffset),
			static_cast<unsigned long long>(buffer.size),
			buffer.huntHash,
			buffer.stride,
			buffer.format);
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.sourceKind = DumpTaskSource::InputAssembler;
			duplicate.iaBuffer = buffer;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareIaBufferTaskForCurrentMode(device, buffer, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12BindingFrameExport::BuildIaBufferFileName(buffer, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			iaTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			task.sourceKind = DumpTaskSource::InputAssembler;
			task.iaBuffer = buffer;
			if (task.skipNote == nullptr)
				task.skipNote = buffer.resolved ? "unsupported_or_no_resource_pointer" : "unresolved_gpu_va";
			skippedTasks.push_back(std::move(task));
		}
	}

	for (const DX12FrameOutputBinding &output : outputBindings) {
		DX12FrameResourceBinding binding = DX12ResourceDumpCore::ResourceBindingFromOutputBinding(output);
		if (!DX12ResourceDumpPolicy::ShouldDumpBinding(binding)) {
			DumpTask skipped;
			skipped.sourceKind = DumpTaskSource::Output;
			skipped.outputBinding = output;
			skipped.binding = binding;
			skipped.skipNote = output.hasDescriptor ?
				"unsupported_or_no_resource_pointer" : "untracked_output_descriptor";
			skippedTasks.push_back(std::move(skipped));
			continue;
		}
		if (binding.descriptor.kind == DescriptorKind::RTV)
			candidateRtv++;
		else if (binding.descriptor.kind == DescriptorKind::DSV)
			candidateDsv++;
		if (binding.descriptor.hasResourceDesc &&
			DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;

		char key[160];
		DX12ResourceDumpNaming::BuildOutputDedupeKeyForCurrentMode(output, binding, key, ARRAYSIZE(key));
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.sourceKind = DumpTaskSource::Output;
			duplicate.outputBinding = output;
			duplicate.binding = binding;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12ResourceDumpPrepare::PrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			wchar_t fileName[MAX_PATH];
			DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
			outputTasksQueued++;
			tasks.push_back(std::move(task));
		} else {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			task.binding = binding;
			if (task.skipNote == nullptr)
				task.skipNote = "unsupported_or_no_resource_pointer";
			skippedTasks.push_back(std::move(task));
		}
	}

	if (DX12RuntimeState::IsHuntingMode()) {
		std::vector<DX12DescriptorSummary> metadataDescriptors;
		DX12GetResourceMetadataSnapshot(nullptr, &metadataDescriptors, nullptr, nullptr);
		DX12FrameAnalysis::LogJsonFunc("HuntingMetadataDescriptorsBegin",
			"\"descriptors\":%zu,\"existingTasks\":%zu,\"duplicates\":%u",
			metadataDescriptors.size(), tasks.size(), duplicates);
		UINT64 metadataIndex = 0;
		for (const DX12DescriptorSummary &descriptor : metadataDescriptors) {
			metadataIndex++;
			if ((metadataIndex % 4096) == 0) {
				DX12FrameAnalysis::LogJsonFunc("HuntingMetadataDescriptorsProgress",
					"\"index\":%llu,\"descriptors\":%zu,\"textureCandidates\":%u,\"bufferCandidates\":%u,\"tasks\":%zu,\"duplicates\":%u,\"bufferCopiesQueued\":%u,\"bufferCopyBytesQueued\":%llu,\"budgetSkipped\":%u",
					static_cast<unsigned long long>(metadataIndex),
					metadataDescriptors.size(), metadataTextureCandidates,
					metadataBufferCandidates, tasks.size(), duplicates,
					metadataBufferCopiesQueued,
					static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
					metadataBufferCopyBudgetSkipped);
			}
			if (!descriptor.resource) {
				metadataMissingResource++;
				continue;
			}
			if (!descriptor.hasResourceDesc) {
				metadataMissingResourceDesc++;
				continue;
			}

			const bool isMetadataTexture =
				DX12ResourceDumpPolicy::IsTextureResourceDimension(descriptor.resourceDesc.Dimension);
			const bool isMetadataBuffer =
				descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
			const bool isDumpableMetadataKind =
				descriptor.kind == DescriptorKind::CBV ||
				descriptor.kind == DescriptorKind::SRV ||
				descriptor.kind == DescriptorKind::UAV ||
				descriptor.kind == DescriptorKind::RTV ||
				descriptor.kind == DescriptorKind::DSV;
			if (!isDumpableMetadataKind || (!isMetadataTexture && !isMetadataBuffer)) {
				metadataUnsupportedKindOrDimension++;
				continue;
			}
			if (isMetadataTexture)
				metadataTextureCandidates++;
			if (isMetadataBuffer) {
				metadataBufferCandidates++;
				if (descriptor.kind == DescriptorKind::CBV)
					metadataCbvBufferCandidates++;
				else if (descriptor.kind == DescriptorKind::SRV)
					metadataSrvBufferCandidates++;
				else if (descriptor.kind == DescriptorKind::UAV)
					metadataUavBufferCandidates++;
			}

			char key[160];
			// Hunting metadata is a resource-discovery pass. Deduplicate at
			// resource granularity so descriptor-table churn cannot explode into
			// tens of thousands of duplicate readbacks. Buffers also use
			// resource granularity here; detailed descriptor views remain in
			// ResourceMetadataDX12.txt, while Dump files must finish quickly.
			DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(descriptor, key, ARRAYSIZE(key));
			auto canonicalIt = canonicalTaskByKey.find(key);
			if (canonicalIt != canonicalTaskByKey.end()) {
				DumpTask duplicate;
				duplicate = tasks[canonicalIt->second];
				duplicate.sourceKind = DumpTaskSource::Descriptor;
				duplicate.binding = DX12FrameResourceBinding();
				duplicate.binding.bindSpace = "hunting_metadata";
				duplicate.binding.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				duplicate.binding.descriptorIndex = metadataIndex;
				duplicate.binding.descriptor = descriptor;
				duplicate.binding.hasDescriptor = true;
				duplicate.dedupeKey = key;
				wchar_t fileName[MAX_PATH];
				DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
				duplicate.fileName = fileName;
				duplicate.skipNote = "already_dumped";
				duplicateTasks.push_back(std::move(duplicate));
				duplicates++;
				metadataDuplicates++;
				continue;
			}

			DumpTask task;
			const bool prepared = isMetadataTexture ?
				DX12ResourceDumpPrepare::PrepareHuntingMetadataTextureTask(
					device, descriptor, &task, &frameReadbackBytes, frameReadbackBudget) :
				DX12ResourceDumpPrepare::PrepareHuntingMetadataBufferTask(
					device, descriptor, &task, &frameReadbackBytes, frameReadbackBudget);
			if (prepared) {
				task.binding.descriptorIndex = metadataIndex;
				task.dedupeKey = key;
				wchar_t fileName[MAX_PATH];
				DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
				task.fileName = fileName;
				canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
				if (isMetadataBuffer) {
					metadataBufferCopiesQueued++;
					metadataBufferCopyBytesQueued += task.copyBytes;
					metadataBufferTasksQueued++;
				} else {
					metadataTextureTasksQueued++;
				}
				tasks.push_back(std::move(task));
			} else {
				task.sourceKind = DumpTaskSource::Descriptor;
				task.binding.bindSpace = "hunting_metadata";
				task.binding.descriptorIndex = metadataIndex;
				task.binding.descriptor = descriptor;
				task.binding.hasDescriptor = descriptor.resource != nullptr;
				if (task.skipNote == nullptr)
					task.skipNote = isMetadataTexture ?
						"metadata_texture_prepare_failed" :
						"metadata_buffer_prepare_failed";
				metadataPrepareFailed++;
				skippedTasks.push_back(std::move(task));
			}
		}
		DX12FrameAnalysis::LogJsonFunc("HuntingMetadataDescriptorsQueued",
			"\"descriptors\":%zu,\"textureCandidates\":%u,\"bufferCandidates\":%u,\"cbvBufferCandidates\":%u,\"srvBufferCandidates\":%u,\"uavBufferCandidates\":%u,\"tasks\":%zu,\"duplicates\":%u,\"metadataDuplicates\":%u,\"prepareFailed\":%u,\"missingResource\":%u,\"missingResourceDesc\":%u,\"unsupportedKindOrDimension\":%u,\"bufferCopiesQueued\":%u,\"bufferCopyBytesQueued\":%llu,\"bufferCopyBudgetSkipped\":%u",
			metadataDescriptors.size(), metadataTextureCandidates, metadataBufferCandidates,
			metadataCbvBufferCandidates, metadataSrvBufferCandidates,
			metadataUavBufferCandidates, tasks.size(), duplicates, metadataDuplicates,
			metadataPrepareFailed, metadataMissingResource, metadataMissingResourceDesc,
			metadataUnsupportedKindOrDimension, metadataBufferCopiesQueued,
			static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
			metadataBufferCopyBudgetSkipped);

		DX12GetRetainedTrackedResourceSnapshot(&retainedMetadataResources);
		UINT64 resourceIndex = 0;
		UINT resourceTextureCandidates = 0;
		UINT resourceBufferCandidates = 0;
		UINT resourceDuplicates = 0;
		UINT resourceCopiesQueued = 0;
		UINT resourceCopyBudgetSkipped = 0;
		UINT resourceCopyUnsafeSkipped = 0;
		UINT64 resourceCopyBytesQueued = 0;
		for (const DX12ResourceSummary &resource : retainedMetadataResources) {
			resourceIndex++;
			if (!resource.resource || !resource.hasResourceDesc ||
				(resource.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
				 !DX12ResourceDumpPolicy::IsTextureResourceDimension(resource.resourceDesc.Dimension)))
				continue;
			if (resource.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
				resourceBufferCandidates++;
			else
				resourceTextureCandidates++;

			char key[160];
			DX12ResourceDumpNaming::BuildResourceDedupeKey(resource, key, ARRAYSIZE(key));
			const DX12FrameResourceBinding binding =
				DX12ResourceDumpCore::ResourceBindingFromTrackedResource(resource, resourceIndex);
			auto canonicalIt = canonicalTaskByKey.find(key);
			if (canonicalIt != canonicalTaskByKey.end()) {
				DumpTask duplicate;
				duplicate = tasks[canonicalIt->second];
				duplicate.sourceKind = DumpTaskSource::Resource;
				duplicate.binding = binding;
				duplicate.dedupeKey = key;
				wchar_t fileName[MAX_PATH];
				DX12ResourceDumpNaming::BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
				duplicate.fileName = fileName;
				duplicate.skipNote = "already_dumped";
				duplicateTasks.push_back(std::move(duplicate));
				duplicates++;
				resourceDuplicates++;
				continue;
			}

			DumpTask task;
			if (DX12ResourceDumpPrepare::PrepareHuntingResourceTask(
					device, resource, &task, &frameReadbackBytes, frameReadbackBudget)) {
				task.sourceKind = DumpTaskSource::Resource;
				task.binding = binding;
				task.dedupeKey = key;
				if (!DX12ResourceDumpSafetyUtils::ResourceDiscoveryBudgetAllows(
						resourceCopiesQueued, resourceCopyBytesQueued, task.copyBytes)) {
					task.skipNote = "resource_discovery_copy_budget_exceeded";
					if (task.readback) {
						task.readback->Release();
						task.readback = nullptr;
					}
					if (frameReadbackBytes >= task.copyBytes)
						frameReadbackBytes -= task.copyBytes;
					task.ready = false;
					resourceCopyBudgetSkipped++;
					skippedTasks.push_back(std::move(task));
					continue;
				}
				wchar_t fileName[MAX_PATH];
				DX12ResourceDumpNaming::BuildFileName(task, fileName, ARRAYSIZE(fileName));
				task.fileName = fileName;
				canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
				resourceCopiesQueued++;
				resourceCopyBytesQueued += task.copyBytes;
				resourceTasksQueued++;
				tasks.push_back(std::move(task));
			} else {
				task.sourceKind = DumpTaskSource::Resource;
				task.binding = binding;
				if (task.skipNote == nullptr)
					task.skipNote = "resource_texture_prepare_failed";
				else if (!strcmp(task.skipNote, "resource_state_unknown") ||
					!strcmp(task.skipNote, "resource_state_create_only") ||
					!strcmp(task.skipNote, "resource_discovery_copy_unsafe"))
					resourceCopyUnsafeSkipped++;
				skippedTasks.push_back(std::move(task));
			}
		}
		DX12FrameAnalysis::LogJsonFunc("HuntingResourcesQueued",
			"\"resources\":%zu,\"textureCandidates\":%u,\"bufferCandidates\":%u,\"tasks\":%zu,\"duplicates\":%u,\"resourceDuplicates\":%u,\"resourceCopiesQueued\":%u,\"resourceCopyBytesQueued\":%llu,\"resourceCopyUnsafeSkipped\":%u,\"resourceCopyBudgetSkipped\":%u,\"resourceCopyBudgetBytes\":%llu,\"resourceCopyTaskBudget\":%u,\"retained\":1",
			retainedMetadataResources.size(), resourceTextureCandidates, resourceBufferCandidates, tasks.size(),
			duplicates, resourceDuplicates, resourceCopiesQueued,
			static_cast<unsigned long long>(resourceCopyBytesQueued),
			resourceCopyUnsafeSkipped, resourceCopyBudgetSkipped,
			static_cast<unsigned long long>(DX12ResourceDumpSafetyUtils::HuntingResourceCopyBudgetBytes()),
			DX12ResourceDumpSafetyUtils::HuntingResourceCopyTaskBudget());
	}
	DX12FrameAnalysis::LogJsonFunc("ResourceDumpTasksQueued",
		"\"tasks\":%zu,\"binding\":%u,\"ia\":%u,\"output\":%u,\"metadataTexture\":%u,\"metadataBuffer\":%u,\"resource\":%u,\"duplicates\":%u,\"skippedSoFar\":%zu",
		tasks.size(), bindingTasksQueued, iaTasksQueued, outputTasksQueued,
		metadataTextureTasksQueued, metadataBufferTasksQueued,
		resourceTasksQueued, duplicates, skippedTasks.size());

	UINT dumpedTextures = 0;
	UINT dumpedBuffers = 0;
	UINT failed = 0;
	UINT linked = 0;
	UINT skipped = 0;
	UINT skippedDiscoveryDuplicates = 0;
	UINT skippedAbortedDuplicateAliases = 0;
	UINT copiedTasks = 0;
	UINT copyFailedTasks = 0;
	UINT directMapTasks = 0;
	bool readbackCopyOk = true;
	bool copyBatchAborted = false;
	std::unordered_map<std::string, std::wstring> dedupePathByKey;
	std::unordered_map<std::string, const char*> dedupeFailureReasonByKey;
	std::vector<DumpedTaskRow> dumpedRows;
	std::unordered_set<std::wstring> loggedFileDumps;
	for (DumpTask &task : tasks) {
		bool ok = task.directMap || task.copied;
		if (task.directMap)
			directMapTasks++;
		if (!ok && task.ready && task.readback && !copyBatchAborted) {
			ok = DX12ResourceDumpGpuCopy::ExecuteCopyTask(device, queue, &task);
			if (ok)
				copiedTasks++;
			else {
				copyFailedTasks++;
				readbackCopyOk = false;
				HRESULT removedHr = S_OK;
				const bool deviceRemoved = DX12ResourceDumpGpuCopy::DeviceRemoved(device, &removedHr);
				const bool timedOut = task.skipNote && !strcmp(task.skipNote, "copy_wait_timeout");
				if (deviceRemoved || timedOut) {
					copyBatchAborted = true;
					const char *abortNote = deviceRemoved ?
						"copy_aborted_device_removed" : "copy_aborted_wait_timeout";
					DX12FrameAnalysis::LogJsonFunc("ResourceCopyBatchAborted",
						"\"reason\":\"%s\",\"deviceHr\":\"0x%lx\",\"copied\":%u,\"copyFailed\":%u,\"tasks\":%zu",
						abortNote, removedHr, copiedTasks, copyFailedTasks, tasks.size());
				}
			}
		} else if (!ok && copyBatchAborted && !task.skipNote) {
			task.skipNote = "copy_aborted";
		}
		std::wstring dedupePath;
		if (ok) {
			ok = DX12ResourceDumpFileWriter::MapAndWriteTask(dedupeDir, resourceDir, &task, &dedupePath);
			if (ok && !task.dedupeKey.empty())
				dedupePathByKey[task.dedupeKey] = dedupePath;
		}

		if (ok && task.isTexture)
			dumpedTextures++;
		else if (ok)
			dumpedBuffers++;
		else
			failed++;

		if (!ok) {
			if (!task.dedupeKey.empty())
				dedupeFailureReasonByKey[task.dedupeKey] =
					task.skipNote ? task.skipNote : "resource_dump_failed";
			DX12ResourceDumpIndexWriter::AddDumpedTaskRow(&dumpedRows, "failed", task, task.desc,
				task.sourceOffset, task.copyBytes, task.sourceState,
				task.stateKnown, task.fileName.c_str(), L"",
				task.skipNote ? task.skipNote : "resource_dump_failed");
			continue;
		}

		if (loggedFileDumps.insert(dedupePath).second) {
			DX12FrameAnalysisManifest::WriteFileDump(
				dedupePath.c_str(), task.isTexture, task.copyBytes, "dumped", "");
		}
		DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "dumped", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupePath.c_str(), "");
		DX12ResourceDumpIndexWriter::AddDumpedTaskRow(&dumpedRows, "dumped", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupePath, "");
	}

	DX12FrameAnalysis::LogJsonFunc("ResourceCopyResult",
		"\"gpuCopy\":%u,\"readbackOk\":%u,\"copied\":%u,\"copyFailed\":%u,\"directMap\":%u,\"tasks\":%zu,\"duplicates\":%u,\"streamedWrite\":1",
		DX12ResourceDumpPrepare::GpuResourceCopyEnabled() ? 1 : 0,
		readbackCopyOk ? 1 : 0, copiedTasks, copyFailedTasks, directMapTasks,
		tasks.size(), duplicates);

	for (DumpTask &task : duplicateTasks) {
		if (DX12ResourceDump::IsDiscoveryDuplicateTask(task)) {
			skipped++;
			skippedDiscoveryDuplicates++;
			continue;
		}

		auto dedupeIt = dedupePathByKey.find(task.dedupeKey);
		if (dedupeIt == dedupePathByKey.end()) {
			auto reasonIt = dedupeFailureReasonByKey.find(task.dedupeKey);
			const char *reason = reasonIt != dedupeFailureReasonByKey.end() ?
				reasonIt->second : "dedupe_source_missing";
			if (copyBatchAborted) {
				skipped++;
				skippedAbortedDuplicateAliases++;
				continue;
			}
			failed++;
			if (task.sourceKind == DumpTaskSource::Descriptor)
				DX12ResourceDumpIndexWriter::AddBindingRow(&dumpedRows, "failed", task.binding, L"",
					task.fileName.c_str(), reason);
			else if (task.sourceKind == DumpTaskSource::Resource)
				DX12ResourceDumpIndexWriter::AddDumpedTaskRow(&dumpedRows, "failed", task, task.desc,
					task.sourceOffset, task.copyBytes, task.sourceState,
					task.stateKnown, task.fileName.c_str(), L"", reason);
			else if (task.sourceKind == DumpTaskSource::Output)
				DX12ResourceDumpIndexWriter::AddOutputBindingRow(&dumpedRows, "failed", task.outputBinding, L"",
					task.fileName.c_str(), reason);
			continue;
		}

		linked++;
		DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "linked", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupeIt->second.c_str(), "already_dumped");
		DX12ResourceDumpIndexWriter::AddDumpedTaskRow(&dumpedRows, "linked", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupeIt->second, "already_dumped");
	}

	for (const DumpTask &task : skippedTasks) {
		skipped++;
		if (task.sourceKind == DumpTaskSource::Descriptor) {
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
			DX12ResourceDumpIndexWriter::AddBindingRow(&dumpedRows, "skipped", task.binding, L"",
				task.fileName.c_str(), task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::InputAssembler) {
			const D3D12_RESOURCE_DESC desc = task.iaBuffer.resource.hasResourceDesc ?
				task.iaBuffer.resource.resourceDesc : D3D12_RESOURCE_DESC();
			const D3D12_RESOURCE_STATES state = task.iaBuffer.resource.hasCurrentState ?
				static_cast<D3D12_RESOURCE_STATES>(task.iaBuffer.resource.currentState) :
				DX12ResourceStateTracker::InferIaBufferSourceState(task.iaBuffer);
			DX12ResourceDumpIndexWriter::AddDumpedTaskRow(&dumpedRows, "skipped", task, desc,
				task.iaBuffer.resource.resourceOffset, task.iaBuffer.size,
				state, task.iaBuffer.resource.hasCurrentState,
				task.fileName.c_str(), L"", task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::Output) {
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
			DX12ResourceDumpIndexWriter::AddOutputBindingRow(&dumpedRows, "skipped", task.outputBinding, L"",
				task.fileName.c_str(), task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::Resource) {
			const UINT64 skippedOffset = task.copyBytes ? task.sourceOffset : task.binding.descriptor.resourceOffset;
			const UINT64 skippedBytes = task.copyBytes ? task.copyBytes : task.binding.descriptor.viewSize;
			const D3D12_RESOURCE_STATES skippedState = task.stateKnown ?
				task.sourceState :
				(task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12ResourceStateTracker::InferDescriptorSourceState(task.binding));
			const bool skippedStateKnown = task.stateKnown || task.binding.descriptor.hasCurrentState;
			DX12ResourceDumpIndexWriter::WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				skippedOffset, skippedBytes, skippedState, skippedStateKnown, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
			DX12ResourceDumpIndexWriter::AddDumpedTaskRow(&dumpedRows, "skipped", task,
				task.binding.descriptor.resourceDesc,
				skippedOffset, skippedBytes, skippedState, skippedStateKnown,
				task.fileName.c_str(), L"",
				task.skipNote ? task.skipNote : "skipped");
		}
	}

	DX12ResourceDumpPrepare::ReleaseTasks(&tasks);
	DX12ReleaseTrackedResourceSnapshot(&retainedMetadataResources);
	device->Release();
	queue->Release();
	DX12ResourceDumpIndexWriter::WriteResourceBindingsCsv(dir, dumpedRows);

	DX12FrameAnalysis::LogJsonFunc("ResourceSummary",
		"\"textures\":%u,\"buffers\":%u,\"linked\":%u,\"skipped\":%u,\"duplicates\":%u,\"failed\":%u,\"readbackBytes\":%llu,\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"candidate_cbv\":%u,\"candidate_srv\":%u,\"candidate_uav\":%u,\"candidate_rtv\":%u,\"candidate_dsv\":%u,\"candidate_textures\":%u,\"metadata_texture_candidates\":%u,\"metadata_buffer_candidates\":%u,\"metadata_cbv_buffer_candidates\":%u,\"metadata_srv_buffer_candidates\":%u,\"metadata_uav_buffer_candidates\":%u,\"metadata_prepare_failed\":%u,\"metadata_buffer_copies_queued\":%u,\"metadata_buffer_copy_bytes_queued\":%llu,\"metadata_buffer_copy_budget_skipped\":%u,\"metadata_missing_resource\":%u,\"metadata_missing_resource_desc\":%u,\"metadata_unsupported_kind_or_dimension\":%u,\"untracked_descriptor_slots\":%u,\"untracked_compute_uav_slots\":%u,\"skipped_discovery_duplicate_aliases\":%u,\"skipped_aborted_duplicate_aliases\":%u",
		dumpedTextures, dumpedBuffers, linked, skipped, duplicates, failed,
		static_cast<unsigned long long>(frameReadbackBytes),
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		candidateCbv, candidateSrv, candidateUav, candidateRtv, candidateDsv,
		candidateTextures, metadataTextureCandidates, metadataBufferCandidates,
		metadataCbvBufferCandidates, metadataSrvBufferCandidates,
		metadataUavBufferCandidates, metadataPrepareFailed,
		metadataBufferCopiesQueued,
		static_cast<unsigned long long>(metadataBufferCopyBytesQueued),
		metadataBufferCopyBudgetSkipped,
		metadataMissingResource, metadataMissingResourceDesc,
		metadataUnsupportedKindOrDimension,
		untrackedDescriptorSlots, untrackedComputeUavSlots,
		skippedDiscoveryDuplicates, skippedAbortedDuplicateAliases);
	result.ok = failed == 0;
	result.written = static_cast<size_t>(dumpedTextures) + dumpedBuffers + linked;
	result.skipped = skipped;
	result.failed = failed;
	result.reason = failed == 0 ? "" : "resource_failures";
	return result;
}





};

