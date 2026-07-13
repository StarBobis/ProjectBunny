#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "DX12ResourceDumpTypes.hpp"
#include "DX12PsoInputLayoutUtils.hpp"

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
