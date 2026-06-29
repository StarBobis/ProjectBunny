#pragma once

#include <Shlwapi.h>
#include <Windows.h>
#include <stdio.h>

#include <string>
#include <utility>
#include <vector>

#include "DX12FrameAnalysis.hpp"
#include "DX12FrameAnalysisManifest.hpp"
#include "utils/DX12JsonUtils.hpp"
#include "DX12ResourceDumpCoreUtils.hpp"
#include "DX12ResourceDumpNamingUtils.hpp"
#include "DX12ResourceDumpPrepare.hpp"
#include "DX12ResourceDumpTypes.hpp"
#include "utils/DX12ResourceDumpUtils.hpp"

class DX12ResourceDumpIndexWriter
{
public:
static void WriteIndexRow(
	FILE *index, const char *status, const DumpTask &task, const D3D12_RESOURCE_DESC &desc,
	UINT64 sourceOffset, UINT64 copyBytes, D3D12_RESOURCE_STATES sourceState,
	bool hasCurrentState, const wchar_t *semanticName, const wchar_t *filePath, const char *note)
{
	(void)index;
	(void)status;
	(void)semanticName;
	(void)note;
	if (task.sourceKind == DumpTaskSource::InputAssembler) {
		DX12FrameAnalysisManifest::WriteIaBinding(
			task.iaBuffer, desc, sourceOffset, copyBytes, sourceState,
			hasCurrentState, filePath);
		return;
	}

	DX12FrameAnalysisManifest::WriteResourceBinding(
		task.binding, desc, sourceOffset, copyBytes, sourceState,
		hasCurrentState, filePath);
}


static void AddDumpedTaskRow(
	std::vector<DumpedTaskRow> *rows, const char *status, const DumpTask &task,
	const D3D12_RESOURCE_DESC &desc, UINT64 sourceOffset, UINT64 copyBytes,
	D3D12_RESOURCE_STATES sourceState, bool stateKnown,
	const wchar_t *semanticName, const std::wstring &dedupePath, const char *note)
{
	if (!rows)
		return;
	DumpedTaskRow row;
	row.sourceKind = task.sourceKind;
	row.binding = task.binding;
	row.iaBuffer = task.iaBuffer;
	row.outputBinding = task.outputBinding;
	row.desc = desc;
	row.sourceOffset = sourceOffset;
	row.copyBytes = copyBytes;
	row.sourceState = sourceState;
	row.stateKnown = stateKnown;
	row.isTexture = task.isTexture;
	row.semanticName = semanticName ? semanticName : L"";
	row.dedupePath = dedupePath;
	row.status = status ? status : "";
	row.note = note ? note : "";
	rows->push_back(std::move(row));
}

static void AddBindingRow(
	std::vector<DumpedTaskRow> *rows, const char *status, const DX12FrameResourceBinding &binding,
	const std::wstring &dedupePath, const wchar_t *semanticName, const char *note)
{
	if (!rows)
		return;
	DumpTask task;
	task.sourceKind = DumpTaskSource::Descriptor;
	task.binding = binding;
	task.isTexture = binding.descriptor.hasResourceDesc &&
		DX12ResourceDumpUtils::IsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
	const D3D12_RESOURCE_DESC desc = binding.descriptor.hasResourceDesc ?
		binding.descriptor.resourceDesc : D3D12_RESOURCE_DESC();
	const UINT64 sourceOffset = binding.usageBytes ? 0 : binding.descriptor.resourceOffset;
	const UINT64 copyBytes = binding.usageBytes ? binding.usageBytes : binding.descriptor.viewSize;
	const D3D12_RESOURCE_STATES sourceState = binding.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(binding.descriptor.currentState) :
		DX12ResourceDumpPrepare::GuessDescriptorSourceState(binding);
	DX12ResourceDumpIndexWriter::AddDumpedTaskRow(rows, status, task, desc, sourceOffset, copyBytes,
		sourceState, binding.descriptor.hasCurrentState, semanticName, dedupePath, note);
}

static void AddOutputBindingRow(
	std::vector<DumpedTaskRow> *rows, const char *status, const DX12FrameOutputBinding &output,
	const std::wstring &dedupePath, const wchar_t *semanticName, const char *note)
{
	if (!rows)
		return;
	DumpTask task;
	task.sourceKind = DumpTaskSource::Output;
	task.outputBinding = output;
	task.binding = DX12ResourceDumpCoreUtils::ResourceBindingFromOutputBinding(output);
	task.isTexture = output.descriptor.hasResourceDesc &&
		DX12ResourceDumpUtils::IsTextureResourceDimension(output.descriptor.resourceDesc.Dimension);
	const D3D12_RESOURCE_DESC desc = output.descriptor.hasResourceDesc ?
		output.descriptor.resourceDesc : D3D12_RESOURCE_DESC();
	const D3D12_RESOURCE_STATES sourceState = output.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(output.descriptor.currentState) :
		DX12ResourceDumpPrepare::GuessDescriptorSourceState(task.binding);
	DX12ResourceDumpIndexWriter::AddDumpedTaskRow(rows, status, task, desc,
		output.descriptor.resourceOffset, output.descriptor.viewSize,
		sourceState, output.descriptor.hasCurrentState, semanticName, dedupePath, note);
}


static void WriteDescriptorResourceBindingCsvRow(FILE *file, const DumpedTaskRow &row)
{
	if (!file)
		return;

	const DX12FrameResourceBinding &binding = row.binding;
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	const char *kind = DX12ResourceDumpCoreUtils::BindingKindName(binding);
	const char *rowType =
		binding.bindSpace == "hunting_resource" ? "resource" :
		(binding.bindSpace == "hunting_metadata" ? "metadata" :
		(binding.dispatchId ? "dispatch" : "draw"));
	char cs[32] = "-";
	if (binding.shaderInfo.hasCS)
		sprintf_s(cs, sizeof(cs), "%016llx",
			static_cast<unsigned long long>(binding.shaderInfo.cs));
	char fileHash[16];
	DX12ResourceDumpNamingUtils::FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
	const uint32_t huntHash = DX12ResourceDumpNamingUtils::DescriptorHuntHash(binding, row.copyBytes);
	char semanticSlot[32] = "-";
	DX12ResourceDumpUtils::BuildSemanticSlot(binding, semanticSlot, ARRAYSIZE(semanticSlot));
	fprintf(file,
		"%llu,%s,%llu,%s,%s,%s,%u,%u,%u,%u,%p,%S,%s,%08x,%S,%llu,%llu,%u,%u,%u,%s,%s,%s\n",
		static_cast<unsigned long long>(binding.eventSerial),
		rowType,
		static_cast<unsigned long long>(binding.psoIndex),
		cs,
		kind,
		binding.bindSpace.c_str(),
		binding.rootParameterIndex,
		binding.rangeIndex,
		binding.shaderRegister,
		binding.registerSpace,
		descriptor.resource,
		DX12ResourceDumpNamingUtils::FileNameFromPath(row.dedupePath),
		fileHash,
		huntHash,
		DX12ResourceDumpNamingUtils::FileNameFromPath(row.semanticName),
		static_cast<unsigned long long>(row.sourceOffset),
		static_cast<unsigned long long>(row.copyBytes),
		descriptor.structureByteStride,
		descriptor.viewFormat,
		binding.shaderVisibility,
		semanticSlot,
		row.status.c_str(),
		row.note.c_str());
}

static void WriteOutputResourceBindingCsvRow(FILE *file, const DumpedTaskRow &row)
{
	if (!file)
		return;

	const DX12FrameResourceBinding binding = row.binding.hasDescriptor ?
		row.binding : DX12ResourceDumpCoreUtils::ResourceBindingFromOutputBinding(row.outputBinding);
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	char cs[32] = "-";
	if (binding.shaderInfo.hasCS)
		sprintf_s(cs, sizeof(cs), "%016llx",
			static_cast<unsigned long long>(binding.shaderInfo.cs));
	char fileHash[16];
	DX12ResourceDumpNamingUtils::FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
	const uint32_t huntHash = DX12ResourceDumpNamingUtils::DescriptorHuntHash(binding, row.copyBytes);
	char semanticSlot[32] = "-";
	DX12ResourceDumpUtils::BuildSemanticSlot(binding, semanticSlot, ARRAYSIZE(semanticSlot));
	fprintf(file,
		"%llu,output,%llu,%s,%s,output,%u,%u,%u,%u,%p,%S,%s,%08x,%S,%llu,%llu,%u,%u,%u,%s,%s,%s\n",
		static_cast<unsigned long long>(binding.eventSerial),
		static_cast<unsigned long long>(binding.psoIndex),
		cs,
		DX12ResourceDumpCoreUtils::BindingKindName(binding),
		binding.rootParameterIndex,
		binding.rangeIndex,
		binding.shaderRegister,
		binding.registerSpace,
		descriptor.resource,
		DX12ResourceDumpNamingUtils::FileNameFromPath(row.dedupePath),
		fileHash,
		huntHash,
		DX12ResourceDumpNamingUtils::FileNameFromPath(row.semanticName),
		static_cast<unsigned long long>(row.sourceOffset),
		static_cast<unsigned long long>(row.copyBytes),
		descriptor.structureByteStride,
		descriptor.viewFormat,
		binding.shaderVisibility,
		semanticSlot,
		row.status.c_str(),
		row.note.c_str());
}

static void WriteIaResourceBindingCsvRow(FILE *file, const DumpedTaskRow &row)
{
	if (!file)
		return;

	const DX12FrameIaBufferBinding &buffer = row.iaBuffer;
	char cs[32] = "-";
	if (buffer.shaderInfo.hasCS)
		sprintf_s(cs, sizeof(cs), "%016llx",
			static_cast<unsigned long long>(buffer.shaderInfo.cs));
	char fileHash[16];
	DX12ResourceDumpNamingUtils::FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
	const UINT64 eventId = buffer.dispatchId ? buffer.dispatchId : buffer.drawId;
	fprintf(file,
		"%llu,ia,%llu,%s,%s,ia,%u,0,%u,0,%p,%S,%s,%08x,%S,%llu,%llu,%u,%u,%u,%s,%s,%s\n",
		static_cast<unsigned long long>(eventId),
		static_cast<unsigned long long>(buffer.psoIndex),
		cs,
		buffer.role.empty() ? "BUF" : buffer.role.c_str(),
		buffer.slot,
		buffer.slot,
		buffer.resource.resource,
		DX12ResourceDumpNamingUtils::FileNameFromPath(row.dedupePath),
		fileHash,
		buffer.huntHash,
		DX12ResourceDumpNamingUtils::FileNameFromPath(row.semanticName),
		static_cast<unsigned long long>(row.sourceOffset),
		static_cast<unsigned long long>(row.copyBytes),
		buffer.stride,
		buffer.format,
		D3D12_SHADER_VISIBILITY_ALL,
		"-",
		row.status.c_str(),
		row.note.c_str());
}

static void WriteResourceBindingsCsv(const wchar_t *dir, const std::vector<DumpedTaskRow> &rows)
{
	if (!dir)
		return;
	wchar_t summaryDir[MAX_PATH];
	swprintf_s(summaryDir, L"%s\\summary", dir);
	DX12ResourceDumpCoreUtils::EnsureDirectory(summaryDir);
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\ResourceBindingsDX12.csv", summaryDir);
	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return;
	fprintf(file,
		"call_index,type,pso,cs,kind,bind,root,range,reg,space,resource,file,file_hash,hunt_hash,semantic_file,offset,size,stride,format,visibility,slot,status,note\n");
	for (const DumpedTaskRow &row : rows) {
		if (row.sourceKind == DumpTaskSource::Descriptor ||
			row.sourceKind == DumpTaskSource::Resource)
			WriteDescriptorResourceBindingCsvRow(file, row);
		else if (row.sourceKind == DumpTaskSource::InputAssembler)
			WriteIaResourceBindingCsvRow(file, row);
		else if (row.sourceKind == DumpTaskSource::Output)
			WriteOutputResourceBindingCsvRow(file, row);
	}
	fclose(file);
	char fields[512] = "";
	DX12JsonUtils::AppendWStringField(fields, sizeof(fields), "path", path);
	DX12JsonUtils::AppendRawField(fields, sizeof(fields), "rows", std::to_string(rows.size()).c_str());
	DX12FrameAnalysis::LogJsonFunc("ResourceBindingsCsvWritten", "%s", fields + 1);
}


};
