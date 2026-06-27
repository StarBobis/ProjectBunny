#include "DX12ResourceTracker.h"

#include <Shlwapi.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "DX12FrameAnalysis.h"
#include "DX12Json.h"

static void GetSummaryDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
{
	if (!dir || !path || pathCount == 0)
		return;
	swprintf_s(path, pathCount, L"%s\\summary", dir);
	CreateDirectoryW(path, nullptr);
}

static const char *DescriptorHeapTypeName(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
	switch (type) {
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		return "CBV_SRV_UAV";
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
		return "SAMPLER";
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
		return "RTV";
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
		return "DSV";
	default:
		return "UNKNOWN";
	}
}

static const char *ResourceDimensionName(D3D12_RESOURCE_DIMENSION dimension)
{
	switch (dimension) {
	case D3D12_RESOURCE_DIMENSION_BUFFER:
		return "BUFFER";
	case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
		return "TEXTURE1D";
	case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
		return "TEXTURE2D";
	case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
		return "TEXTURE3D";
	default:
		return "UNKNOWN";
	}
}

static void WriteResourceDesc(FILE *file, const DX12DescriptorSummary &record)
{
	if (!record.hasResourceDesc) {
		fprintf(file, ",,,,,,,,,");
		return;
	}

	const D3D12_RESOURCE_DESC &desc = record.resourceDesc;
	fprintf(file, ",%s,%llu,%u,%u,%u,%u,0x%llx,0x%llx,%llu",
		ResourceDimensionName(desc.Dimension),
		static_cast<unsigned long long>(desc.Width),
		desc.Height,
		desc.DepthOrArraySize,
		desc.MipLevels,
		static_cast<UINT>(desc.Format),
		static_cast<unsigned long long>(desc.Flags),
		static_cast<unsigned long long>(record.gpuVirtualAddress),
		static_cast<unsigned long long>(desc.SampleDesc.Count));
}

void DX12DumpResourceMetadata(const wchar_t *dir)
{
	if (!dir)
		return;

	std::vector<DX12RootSignatureSummary> rootSignatures;
	std::vector<DX12DescriptorHeapSummary> descriptorHeaps;
	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12PsoRootSummary> psoRoots;
	DX12GetResourceMetadataSnapshot(
		&rootSignatures, &descriptors, &psoRoots, &descriptorHeaps);

	wchar_t path[MAX_PATH];
	wchar_t summaryDir[MAX_PATH];
	GetSummaryDirectory(dir, summaryDir, ARRAYSIZE(summaryDir));
	swprintf_s(path, L"%s\\ResourceMetadataDX12.txt", summaryDir);
	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return;

	fprintf(file, "DX12 Resource Metadata\n");
	fprintf(file, "======================\n");
	fprintf(file, "root_signatures=%zu descriptor_heaps=%zu descriptors=%zu pso_roots=%zu\n\n",
		rootSignatures.size(), descriptorHeaps.size(), descriptors.size(), psoRoots.size());

	fprintf(file, "PSO Root Signatures\n");
	fprintf(file, "pso,kind,pipeline_state,root_signature\n");
	for (const DX12PsoRootSummary &record : psoRoots) {
		fprintf(file, "%llu,%s,%p,%p\n",
			static_cast<unsigned long long>(record.psoIndex),
			record.kind.c_str(), record.pipelineState, record.rootSignature);
	}

	fprintf(file, "\nRoot Signatures\n");
	fprintf(file, "index,root_signature,hash,size,node_mask,version,flags,static_samplers,parsed,parameters\n");
	for (size_t i = 0; i < rootSignatures.size(); ++i) {
		const DX12RootSignatureSummary &record = rootSignatures[i];
		fprintf(file, "%zu,%p,%016llx,%zu,%u,0x%x,0x%x,%u,%u,%zu\n",
			i,
			record.rootSignature,
			static_cast<unsigned long long>(record.hash),
			record.size,
			record.nodeMask,
			record.version,
			record.flags,
			record.staticSamplerCount,
			record.parsed ? 1 : 0,
			record.parameters.size());
	}

	fprintf(file, "\nRoot Parameters\n");
	fprintf(file, "root_signature,root_param,type,visibility,shader_register,space,root_descriptor_flags,num32bit_values,range_count\n");
	for (const DX12RootSignatureSummary &record : rootSignatures) {
		for (const DX12RootParameterSummary &parameter : record.parameters) {
			fprintf(file, "%p,%u,%u,%u,%u,%u,0x%x,%u,%zu\n",
				record.rootSignature,
				parameter.rootParameterIndex,
				parameter.parameterType,
				parameter.shaderVisibility,
				parameter.shaderRegister,
				parameter.registerSpace,
				parameter.rootDescriptorFlags,
				parameter.num32BitValues,
				parameter.ranges.size());
		}
	}

	fprintf(file, "\nRoot Descriptor Ranges\n");
	fprintf(file, "root_signature,root_param,range_index,range_type,num_descriptors,base_shader_register,space,offset,effective_offset,flags,visibility\n");
	for (const DX12RootSignatureSummary &record : rootSignatures) {
		for (const DX12RootParameterSummary &parameter : record.parameters) {
			for (const DX12RootDescriptorRangeSummary &range : parameter.ranges) {
				fprintf(file, "%p,%u,%u,%u,%u,%u,%u,%u,%u,0x%x,%u\n",
					record.rootSignature,
					range.rootParameterIndex,
					range.rangeIndex,
					range.rangeType,
					range.numDescriptors,
					range.baseShaderRegister,
					range.registerSpace,
					range.offsetInDescriptorsFromTableStart,
					range.effectiveOffset,
					range.flags,
					range.shaderVisibility);
			}
		}
	}

	fprintf(file, "\nDescriptor Heaps\n");
	fprintf(file, "index,heap,type,num_descriptors,flags,node_mask,cpu_start,gpu_start,increment\n");
	for (size_t i = 0; i < descriptorHeaps.size(); ++i) {
		const DX12DescriptorHeapSummary &record = descriptorHeaps[i];
		fprintf(file, "%zu,%p,%s,%u,0x%x,%u,0x%llx,0x%llx,%u\n",
			i,
			record.heap,
			DescriptorHeapTypeName(static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(record.type)),
			record.numDescriptors,
			record.flags,
			record.nodeMask,
			static_cast<unsigned long long>(record.cpuStart),
			static_cast<unsigned long long>(record.gpuStart),
			record.increment);
	}

	fprintf(file, "\nDescriptors\n");
	fprintf(file,
		"index,kind,cpu_handle,resource,counter_resource,resource_dimension,width,height,depth_or_array,mips,format,flags,gpu_va,sample_count,resource_offset,view_size,heap_type,current_state,has_current_state,has_view_desc,view_format,view_dimension,cbv_size,first_element,num_elements,stride,buffer_view_offset,buffer_view_bytes\n");
	for (size_t i = 0; i < descriptors.size(); ++i) {
		const DX12DescriptorSummary &record = descriptors[i];
		fprintf(file, "%zu,%s,0x%llx,%p,%p",
			i,
			record.kind.c_str(),
			static_cast<unsigned long long>(record.cpuHandle),
			record.resource,
			record.counterResource);
		WriteResourceDesc(file, record);
		fprintf(file, ",%llu,%llu,%u,0x%x,%u,%u,%u,%u,%u,%llu,%u,%u,%llu,%llu\n",
			static_cast<unsigned long long>(record.resourceOffset),
			static_cast<unsigned long long>(record.viewSize),
			record.hasResourceHeapType ? record.resourceHeapType : 0,
			record.currentState,
			record.hasCurrentState ? 1 : 0,
			record.hasDesc ? 1 : 0,
			record.viewFormat,
			record.viewDimension,
			record.cbvSize,
			static_cast<unsigned long long>(record.firstElement),
			record.numElements,
			record.structureByteStride,
			static_cast<unsigned long long>(record.bufferViewOffset),
			static_cast<unsigned long long>(record.bufferViewBytes));
	}

	fclose(file);
	char fields[512] = "";
	DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
	DX12JsonAppendRawField(fields, sizeof(fields), "roots", std::to_string(rootSignatures.size()).c_str());
	DX12JsonAppendRawField(fields, sizeof(fields), "heaps", std::to_string(descriptorHeaps.size()).c_str());
	DX12JsonAppendRawField(fields, sizeof(fields), "descriptors", std::to_string(descriptors.size()).c_str());
	DX12JsonAppendRawField(fields, sizeof(fields), "psoRoots", std::to_string(psoRoots.size()).c_str());
	DX12FrameAnalysisLogJsonFunc("ResourceMetadataWritten", "%s", fields + 1);
}
