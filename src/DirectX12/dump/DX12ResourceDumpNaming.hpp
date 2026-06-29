#pragma once

#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include <string>

#include "DX12BindingTracker.hpp"
#include "DX12ResourceDumpCore.hpp"
#include "DX12ResourceDumpTypes.hpp"
#include "DX12ResourceDumpPolicy.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12RuntimeState.hpp"
#include "DX12ShaderDump.hpp"

class DX12ResourceDumpNaming
{
public:
static const wchar_t *FileNameFromPath(const std::wstring &path)
{
	const wchar_t *text = path.c_str();
	const wchar_t *slash = wcsrchr(text, L'\\');
	return slash ? slash + 1 : text;
}

static void FileHashFromPath(const std::wstring &path, char *hash, size_t hashCount)
{
	if (!hash || hashCount == 0)
		return;
	hash[0] = '\0';
	const wchar_t *name = FileNameFromPath(path);
	size_t i = 0;
	for (; i < hashCount - 1 && i < 8 && name[i] && name[i] != L'-'; ++i)
		hash[i] = static_cast<char>(name[i]);
	hash[i] = '\0';
}

static uint32_t DescriptorHuntHash(const DX12FrameResourceBinding &binding, UINT64 copyBytes)
{
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	if (!binding.hasDescriptor) {
		uint32_t hash = DX12ResourceDumpCore::MakeFourCC('D', 'S', 'L', 'T');
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.psoIndex, sizeof(binding.psoIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rootParameterIndex, sizeof(binding.rootParameterIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rangeIndex, sizeof(binding.rangeIndex));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.rangeType, sizeof(binding.rangeType));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.shaderRegister, sizeof(binding.shaderRegister));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.registerSpace, sizeof(binding.registerSpace));
		hash = DX12ResourceDumpCore::HashBytes(hash, &binding.descriptorIndex, sizeof(binding.descriptorIndex));
		return hash;
	}
	const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
		descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
	const UINT64 fallbackSize = descriptor.viewSize ? descriptor.viewSize : copyBytes;
	return DX12HashDescriptorBufferView(
		binding.hasDescriptor ? &descriptor : nullptr, fallbackGpuVa, fallbackSize);
}

static void FormatEventPrefix(UINT64 drawId, UINT64 dispatchId, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (dispatchId)
		swprintf_s(text, textCount, L"c%06llu",
			static_cast<unsigned long long>(dispatchId));
	else if (drawId)
		swprintf_s(text, textCount, L"d%06llu",
			static_cast<unsigned long long>(drawId));
	else
		swprintf_s(text, textCount, L"e%06llu",
			static_cast<unsigned long long>(0));
}

static void FormatMetadataPrefix(UINT64 descriptorIndex, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	swprintf_s(text, textCount, L"m%06llu",
		static_cast<unsigned long long>(descriptorIndex));
}

static void FormatEventPrefixWithSerial(
	UINT64 drawId, UINT64 dispatchId, UINT64 eventSerial, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	if (dispatchId)
		swprintf_s(text, textCount, L"c%06llu",
			static_cast<unsigned long long>(dispatchId));
	else if (drawId)
		swprintf_s(text, textCount, L"d%06llu",
			static_cast<unsigned long long>(drawId));
	else
		swprintf_s(text, textCount, L"e%06llu",
			static_cast<unsigned long long>(eventSerial));
}

static void AppendShaderHashNamePart(
	wchar_t *text, size_t textCount, const char *stage, bool hasHash, UINT64 hash)
{
	if (!text || textCount == 0 || !stage || !hasHash)
		return;
	const size_t used = wcslen(text);
	if (used >= textCount)
		return;
	swprintf_s(text + used, textCount - used, L"-%S%016llx",
		stage, static_cast<unsigned long long>(hash));
}

static bool HasAnyShaderHash(const DX12PsoShaderInfo &info)
{
	return info.hasVS || info.hasPS || info.hasCS;
}

static void BuildShaderHashNamePart(
	const DX12PsoShaderInfo &info, wchar_t *text, size_t textCount)
{
	if (!text || textCount == 0)
		return;
	text[0] = L'\0';
	AppendShaderHashNamePart(text, textCount, "vs", info.hasVS, info.vs);
	AppendShaderHashNamePart(text, textCount, "ps", info.hasPS, info.ps);
	AppendShaderHashNamePart(text, textCount, "cs", info.hasCS, info.cs);
}

static void BuildFileName(const DumpTask &task, wchar_t *fileName, size_t fileNameCount)
{
	if (task.sourceKind == DumpTaskSource::InputAssembler) {
		DX12BindingFrameExport::BuildIaBufferFileName(task.iaBuffer, fileName, fileNameCount);
		return;
	}

	const wchar_t *ext = task.isTexture ? L"dds" : L"buf";
	DX12FrameResourceBinding outputBinding;
	const DX12FrameResourceBinding *binding = &task.binding;
	wchar_t eventPrefix[32];
	if (task.sourceKind == DumpTaskSource::Resource) {
		FormatMetadataPrefix(task.binding.descriptorIndex,
			eventPrefix, ARRAYSIZE(eventPrefix));
	} else if (task.sourceKind == DumpTaskSource::Output) {
		outputBinding = DX12ResourceDumpCore::ResourceBindingFromOutputBinding(task.outputBinding);
		binding = &outputBinding;
		FormatEventPrefixWithSerial(
			task.outputBinding.drawId, task.outputBinding.dispatchId,
			task.outputBinding.eventSerial, eventPrefix, ARRAYSIZE(eventPrefix));
	} else {
		if (task.binding.bindSpace == "hunting_metadata")
			FormatMetadataPrefix(task.binding.descriptorIndex,
				eventPrefix, ARRAYSIZE(eventPrefix));
		else
			FormatEventPrefix(task.binding.drawId, task.binding.dispatchId,
				eventPrefix, ARRAYSIZE(eventPrefix));
	}
	wchar_t shaderPart[96];
	// Metadata texture discovery is not tied to a draw/dispatch PSO. Keeping
	// shader hashes out of those names avoids presenting missing context as a
	// bogus slot, which showed up as debug-fill cccccccc in hunting dumps.
	if (HasAnyShaderHash(binding->shaderInfo))
		BuildShaderHashNamePart(binding->shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
	else
		shaderPart[0] = L'\0';
	char semanticSlot[32];
	if (DX12ResourceDumpPolicy::BuildSemanticSlot(*binding, semanticSlot, ARRAYSIZE(semanticSlot))) {
		swprintf_s(fileName, fileNameCount,
			L"%s-pso%llu%s-%S-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
			eventPrefix,
			static_cast<unsigned long long>(binding->psoIndex),
			shaderPart,
			DX12ResourceDumpPolicy::ShortBindSpace(binding->bindSpace),
			semanticSlot,
			binding->rootParameterIndex,
			static_cast<unsigned long long>(binding->descriptorIndex),
			DX12ResourceDumpPolicy::ShortDescriptorKind(binding->descriptor.kind),
			binding->descriptor.resource,
			static_cast<unsigned long long>(task.sourceOffset),
			static_cast<unsigned long long>(task.copyBytes),
			ext);
		return;
	}
	swprintf_s(fileName, fileNameCount,
		L"%s-pso%llu%s-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
		eventPrefix,
		static_cast<unsigned long long>(binding->psoIndex),
		shaderPart,
		DX12ResourceDumpPolicy::ShortBindSpace(binding->bindSpace),
		binding->rootParameterIndex,
		static_cast<unsigned long long>(binding->descriptorIndex),
		DX12ResourceDumpPolicy::ShortDescriptorKind(binding->descriptor.kind),
		binding->descriptor.resource,
		static_cast<unsigned long long>(task.sourceOffset),
		static_cast<unsigned long long>(task.copyBytes),
		ext);
}


static void BuildOutputDedupeKey(
	const DX12FrameOutputBinding &output, const DX12FrameResourceBinding &binding,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	const char *kind = !binding.descriptor.kind.empty() ?
		binding.descriptor.kind.c_str() : output.kind.c_str();
	sprintf_s(key, keyCount, "output|%s|slot%u|%p|%llu|%llu|%u|%u",
		kind ? kind : "",
		output.slot,
		binding.descriptor.resource,
		static_cast<unsigned long long>(binding.descriptor.resourceOffset),
		static_cast<unsigned long long>(binding.descriptor.viewSize),
		binding.descriptor.viewFormat,
		binding.descriptor.viewDimension);
}

static void BuildDescriptorDedupeKey(
	const DX12DescriptorSummary &descriptor, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|%llu|%llu|%u|%u",
		descriptor.resource,
		static_cast<unsigned long long>(descriptor.resourceOffset),
		static_cast<unsigned long long>(descriptor.viewSize),
		descriptor.viewFormat,
		descriptor.viewDimension);
}

static void BuildDescriptorResourceDedupeKey(
	const DX12DescriptorSummary &descriptor, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|0|0|%u|%u",
		descriptor.resource,
		descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Format) : 0,
		descriptor.hasResourceDesc ? static_cast<UINT>(descriptor.resourceDesc.Dimension) : 0);
}

static void BuildBindingDedupeKeyForCurrentMode(
	const DX12FrameResourceBinding &binding, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	if (DX12RuntimeState::IsHuntingMode() &&
		binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension)) {
		// Hunting is a resource discovery pass. The descriptor slot and view
		// still describe where the texture was seen, but one ID3D12Resource must
		// produce one readback, matching RenderDoc-style capture granularity.
		DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(binding.descriptor, key, keyCount);
		return;
	}
	BuildDescriptorDedupeKey(binding.descriptor, key, keyCount);
}

static void BuildOutputDedupeKeyForCurrentMode(
	const DX12FrameOutputBinding &output, const DX12FrameResourceBinding &binding,
	char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	if (DX12RuntimeState::IsHuntingMode() &&
		binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpPolicy::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension)) {
		DX12ResourceDumpNaming::BuildDescriptorResourceDedupeKey(binding.descriptor, key, keyCount);
		return;
	}
	BuildOutputDedupeKey(output, binding, key, keyCount);
}

static void BuildResourceDedupeKey(
	const DX12ResourceSummary &resource, char *key, size_t keyCount)
{
	if (!key || keyCount == 0)
		return;
	sprintf_s(key, keyCount, "%p|0|0|%u|%u",
		resource.resource,
		resource.hasResourceDesc ? static_cast<UINT>(resource.resourceDesc.Format) : 0,
		resource.hasResourceDesc ? static_cast<UINT>(resource.resourceDesc.Dimension) : 0);
}


};

