#include "DX12ResourceDump.h"

#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "crc32c.h"
#include "DX12BindingTracker.h"
#include "DX12FrameAnalysis.h"
#include "DX12FrameAnalysisManifest.h"
#include "DX12Json.h"
#include "DX12ResourceDumpPrepare.h"
#include "DX12ResourceTracker.h"
#include "DX12ShaderDump.h"
#include "DX12ResourceDumpUtil.h"
#include "DX12State.h"


struct DumpedTaskRow
{
	DumpTaskSource sourceKind = DumpTaskSource::Descriptor;
	DX12FrameResourceBinding binding;
	DX12FrameIaBufferBinding iaBuffer;
	DX12FrameOutputBinding outputBinding;
	D3D12_RESOURCE_DESC desc = {};
	UINT64 sourceOffset = 0;
	UINT64 copyBytes = 0;
	D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON;
	bool stateKnown = false;
	bool isTexture = false;
	std::wstring semanticName;
	std::wstring dedupePath;
	std::string status;
	std::string note;
};

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
		DX12FrameAnalysisLogJsonFunc("Crc32cFailed",
			"\"exception\":\"0x%lx\",\"size\":%zu", GetExceptionCode(), size);
		return 0;
	}
}

static void GetDedupedDirectory(const wchar_t *dir, wchar_t *path, size_t pathCount)
{
	swprintf_s(path, pathCount, L"%s\\deduped", dir);
	EnsureDirectory(path);
}

static bool UnsafeResourceCopyEnabled() { return true; }
static UINT64 FrameReadbackBudgetBytes() { return 0; }

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

static const char *BindingKindName(const DX12FrameResourceBinding &binding)
{
	if (!binding.descriptor.kind.empty())
		return binding.descriptor.kind.c_str();
	if (binding.rootDescriptor)
		return RootDescriptorKindName(binding.rangeType);
	return "";
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

static void WriteResourceBarrier(
	ID3D12GraphicsCommandList *commandList, ID3D12Resource *resource,
	D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
	if (!commandList || !resource || beforeState == afterState)
		return;

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	commandList->ResourceBarrier(1, &barrier);
}

static bool WaitForFence(ID3D12CommandQueue *queue, ID3D12Device *device)
{
	ID3D12Fence *fence = nullptr;
	HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(hr) || !fence)
		return false;

	HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!eventHandle) {
		fence->Release();
		return false;
	}

	hr = queue->Signal(fence, 1);
	if (SUCCEEDED(hr) && fence->GetCompletedValue() < 1) {
		hr = fence->SetEventOnCompletion(1, eventHandle);
		if (SUCCEEDED(hr))
			WaitForSingleObject(eventHandle, 5000);
	}

	CloseHandle(eventHandle);
	fence->Release();
	return SUCCEEDED(hr);
}

static void WriteU32(FILE *file, uint32_t value)
{
	fwrite(&value, sizeof(value), 1, file);
}

static bool IsBlockCompressedFormat(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return true;
	default:
		return false;
	}
}

static uint32_t BitsPerPixel(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;
	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_Y416:
	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		return 64;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_Y410:
	case DXGI_FORMAT_YUY2:
		return 32;
	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
		return 24;
	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 16;
	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_NV11:
		return 12;
	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
		return 8;
	case DXGI_FORMAT_R1_UNORM:
		return 1;
	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;
	case DXGI_FORMAT_BC2_TYPELESS:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_TYPELESS:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC5_TYPELESS:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_BC6H_TYPELESS:
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
	case DXGI_FORMAT_BC7_TYPELESS:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		return 8;
	default:
		return 0;
	}
}

static UINT MipSize(UINT64 value, UINT mip)
{
	UINT64 size = value >> mip;
	return static_cast<UINT>(size ? size : 1);
}

static UINT64 TextureMipBytes(const D3D12_RESOURCE_DESC &desc, UINT mip)
{
	const UINT width = MipSize(desc.Width, mip);
	const UINT height = MipSize(desc.Height, mip);
	if (IsBlockCompressedFormat(desc.Format)) {
		const UINT blockBytes = BitsPerPixel(desc.Format) == 4 ? 8 : 16;
		const UINT blocksWide = std::max<UINT>(1, (width + 3) / 4);
		const UINT blocksHigh = std::max<UINT>(1, (height + 3) / 4);
		return static_cast<UINT64>(blocksWide) * blocksHigh * blockBytes;
	}
	const uint32_t bpp = BitsPerPixel(desc.Format);
	if (!bpp)
		return 0;
	return (static_cast<UINT64>(width) * height * bpp + 7) / 8;
}

static void WriteDDSHeader(FILE *file, const D3D12_RESOURCE_DESC &desc)
{
	WriteU32(file, MakeFourCC('D', 'D', 'S', ' '));
	WriteU32(file, 124);
	uint32_t flags = 0x00001007;
	if (desc.MipLevels > 1)
		flags |= 0x00020000;
	if (IsBlockCompressedFormat(desc.Format))
		flags |= 0x00080000;
	else
		flags |= 0x00000008;
	WriteU32(file, flags);
	WriteU32(file, desc.Height);
	WriteU32(file, static_cast<uint32_t>(desc.Width));
	WriteU32(file, static_cast<uint32_t>(TextureMipBytes(desc, 0)));
	WriteU32(file, 0);
	WriteU32(file, desc.MipLevels ? desc.MipLevels : 1);
	for (int i = 0; i < 11; ++i)
		WriteU32(file, 0);

	WriteU32(file, 32);
	WriteU32(file, 0x00000004);
	WriteU32(file, MakeFourCC('D', 'X', '1', '0'));
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	uint32_t caps = 0x00001000;
	if (desc.MipLevels > 1)
		caps |= 0x00400008;
	WriteU32(file, caps);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	WriteU32(file, static_cast<uint32_t>(desc.Format));
	WriteU32(file, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
	WriteU32(file, 0);
	WriteU32(file, desc.DepthOrArraySize ? desc.DepthOrArraySize : 1);
	WriteU32(file, 0);
}

static bool WriteTextureDDSFile(FILE *file, const DumpTask &task, const void *mappedData)
{
	if (!file || !mappedData)
		return false;

	if (task.textureLayouts.size() != task.textureRows.size() ||
		task.textureLayouts.size() != task.textureRowSizes.size())
		return false;

	WriteDDSHeader(file, task.desc);
	const uint8_t *mapped = static_cast<const uint8_t*>(mappedData);
	for (size_t subresource = 0; subresource < task.textureLayouts.size(); ++subresource) {
		const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout = task.textureLayouts[subresource];
		const uint8_t *base = mapped + layout.Offset;
		for (UINT row = 0; row < task.textureRows[subresource]; ++row) {
			const uint8_t *src = base + static_cast<size_t>(row) * layout.Footprint.RowPitch;
			fwrite(src, 1, static_cast<size_t>(task.textureRowSizes[subresource]), file);
		}
	}
	return true;
}

static bool WriteBufferFileData(FILE *file, const DumpTask &task, const void *mappedData)
{
	if (!file || !mappedData)
		return false;
	const uint8_t *src = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	fwrite(src, 1, static_cast<size_t>(task.copyBytes), file);
	return true;
}

static void BuildTextFileName(const std::wstring &fileName, wchar_t *textName, size_t textNameCount)
{
	if (!textName || textNameCount == 0)
		return;
	textName[0] = L'\0';
	if (fileName.empty())
		return;
	wcsncpy_s(textName, textNameCount, fileName.c_str(), _TRUNCATE);
	wcscat_s(textName, textNameCount, L".txt");
}

static bool WriteGenericBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	fprintf(file, "bytes: %llu\n", static_cast<unsigned long long>(task.copyBytes));
	fprintf(file, "offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	const UINT stride = task.sourceKind == DumpTaskSource::InputAssembler ?
		task.iaBuffer.stride : task.binding.descriptor.structureByteStride;
	if (stride)
		fprintf(file, "stride: %u\n", stride);

	const UINT64 floatCount = task.copyBytes / sizeof(float);
	const float *floats = reinterpret_cast<const float*>(data);
	for (UINT64 i = 0; i < floatCount; ++i)
		fprintf(file, "buf[%llu]: %.9g\n", static_cast<unsigned long long>(i), floats[i]);
	return true;
}

static bool WriteIndexBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
	fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	fprintf(file, "index count: %llu\n", static_cast<unsigned long long>(
		buffer.format == DXGI_FORMAT_R16_UINT ? task.copyBytes / 2 : task.copyBytes / 4));
	fprintf(file, "format: %s\n", buffer.format == DXGI_FORMAT_R16_UINT ?
		"DXGI_FORMAT_R16_UINT" : buffer.format == DXGI_FORMAT_R32_UINT ?
		"DXGI_FORMAT_R32_UINT" : "UNKNOWN");

	if (buffer.format == DXGI_FORMAT_R16_UINT) {
		const uint16_t *indices = reinterpret_cast<const uint16_t*>(data);
		const UINT64 count = task.copyBytes / 2;
		for (UINT64 i = 0; i < count; ++i) {
			if (i % 3 == 0)
				fprintf(file, "\n");
			else
				fprintf(file, " ");
			fprintf(file, "%u", indices[i]);
		}
		fprintf(file, "\n");
		return true;
	}
	if (buffer.format == DXGI_FORMAT_R32_UINT) {
		const uint32_t *indices = reinterpret_cast<const uint32_t*>(data);
		const UINT64 count = task.copyBytes / 4;
		for (UINT64 i = 0; i < count; ++i) {
			if (i % 3 == 0)
				fprintf(file, "\n");
			else
				fprintf(file, " ");
			fprintf(file, "%u", indices[i]);
		}
		fprintf(file, "\n");
		return true;
	}
	return WriteGenericBufferTextFile(file, task, data);
}

static bool WriteVertexBufferTextFile(FILE *file, const DumpTask &task, const uint8_t *data)
{
	if (!file || !data)
		return false;

	const DX12FrameIaBufferBinding &buffer = task.iaBuffer;
	fprintf(file, "byte offset: %llu\n", static_cast<unsigned long long>(task.sourceOffset));
	fprintf(file, "slot: %u\n", buffer.slot);
	fprintf(file, "stride: %u\n", buffer.stride);

	const UINT stride = buffer.stride ? buffer.stride : 16;
	const UINT64 vertexCount = stride ? task.copyBytes / stride : 0;
	fprintf(file, "vertex count: %llu\n", static_cast<unsigned long long>(vertexCount));
	for (UINT64 v = 0; v < vertexCount; ++v) {
		const uint8_t *vertex = data + v * stride;
		fprintf(file, "vertex[%llu]", static_cast<unsigned long long>(v));
		const UINT floatCount = stride / sizeof(float);
		const float *floats = reinterpret_cast<const float*>(vertex);
		for (UINT c = 0; c < floatCount; ++c)
			fprintf(file, " f%u=%.9g", c, floats[c]);
		fprintf(file, "\n");
	}
	return true;
}

static bool WriteBufferTextFile(const wchar_t *path, const DumpTask &task, const void *mappedData)
{
	if (!path || !mappedData || task.isTexture)
		return false;

	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return false;

	const uint8_t *data = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	bool ok = false;
	if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == IaBufferRole::IB)
		ok = WriteIndexBufferTextFile(file, task, data);
	else if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == IaBufferRole::VB)
		ok = WriteVertexBufferTextFile(file, task, data);
	else
		ok = WriteGenericBufferTextFile(file, task, data);

	fclose(file);
	return ok;
}

static uint32_t HashDumpTaskData(const DumpTask &task, const void *mappedData)
{
	uint32_t hash = 0;
	hash = HashBytes(hash, &task.desc, sizeof(task.desc));
	hash = HashBytes(hash, &task.sourceOffset, sizeof(task.sourceOffset));
	hash = HashBytes(hash, &task.copyBytes, sizeof(task.copyBytes));
	hash = HashBytes(hash, &task.isTexture, sizeof(task.isTexture));

	if (task.isTexture) {
		const uint8_t *mapped = static_cast<const uint8_t*>(mappedData);
		for (size_t subresource = 0; subresource < task.textureLayouts.size(); ++subresource) {
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &layout = task.textureLayouts[subresource];
			hash = HashBytes(hash, &layout.Footprint.RowPitch, sizeof(layout.Footprint.RowPitch));
			hash = HashBytes(hash, &task.textureRows[subresource], sizeof(task.textureRows[subresource]));
			hash = HashBytes(hash, &task.textureRowSizes[subresource], sizeof(task.textureRowSizes[subresource]));
			const uint8_t *base = mapped + layout.Offset;
			for (UINT row = 0; row < task.textureRows[subresource]; ++row) {
				const uint8_t *src = base + static_cast<size_t>(row) * layout.Footprint.RowPitch;
				hash = HashBytes(hash, src, static_cast<size_t>(task.textureRowSizes[subresource]));
			}
		}
		return hash;
	}

	const uint8_t *src = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	return HashBytes(hash, src, static_cast<size_t>(task.copyBytes));
}

static void BuildDedupedRelativePath(const wchar_t *dedupeFileName, wchar_t *path, size_t pathCount)
{
	if (!path || pathCount == 0)
		return;
	path[0] = L'\0';
	if (!dedupeFileName || !dedupeFileName[0])
		return;
	swprintf_s(path, pathCount, L"deduped\\%s", dedupeFileName);
}

static bool WriteDedupedResourceFile(
	const wchar_t *dedupeDir, const wchar_t *resourceDir,
	const DumpTask &task, const void *mappedData,
	std::wstring *dedupePathOut = nullptr)
{
	if (!dedupeDir || !resourceDir || !mappedData)
		return false;

	const wchar_t *ext = task.isTexture ? L"dds" : L"buf";
	const uint32_t hash = HashDumpTaskData(task, mappedData);
	wchar_t dedupeFileName[MAX_PATH];
	swprintf_s(dedupeFileName, L"%08x-%s.%s",
		hash, task.isTexture ? L"texture" : L"buffer", ext);
	wchar_t dedupePath[MAX_PATH];
	swprintf_s(dedupePath, L"%s\\%s", dedupeDir, dedupeFileName);
	if (dedupePathOut) {
		wchar_t relativePath[MAX_PATH];
		BuildDedupedRelativePath(dedupeFileName, relativePath, ARRAYSIZE(relativePath));
		*dedupePathOut = relativePath;
	}

	if (GetFileAttributesW(dedupePath) == INVALID_FILE_ATTRIBUTES) {
		FILE *file = _wfsopen(dedupePath, L"wb", _SH_DENYNO);
		if (!file)
			return false;
		bool ok = task.isTexture ? WriteTextureDDSFile(file, task, mappedData) :
			WriteBufferFileData(file, task, mappedData);
		fclose(file);
		if (!ok) {
			DeleteFileW(dedupePath);
			return false;
		}
	}
	if (!task.isTexture) {
		wchar_t dedupeTextPath[MAX_PATH];
		BuildTextFileName(dedupePath, dedupeTextPath, ARRAYSIZE(dedupeTextPath));
		if (GetFileAttributesW(dedupeTextPath) == INVALID_FILE_ATTRIBUTES)
			WriteBufferTextFile(dedupeTextPath, task, mappedData);
	}
	return true;
}

static bool LinkOrCopyFile(const wchar_t *sourcePath, const wchar_t *destPath,
	const DumpTask &task, const char *reason)
{
	if (!sourcePath || !destPath)
		return false;
	if (GetFileAttributesW(destPath) != INVALID_FILE_ATTRIBUTES)
		return true;
	if (CreateHardLinkW(destPath, sourcePath, nullptr))
		return true;
	const DWORD hardLinkError = GetLastError();
	if (CopyFileW(sourcePath, destPath, TRUE))
		return true;
	const DWORD copyError = GetLastError();
	DX12FrameAnalysisLogJsonFunc("ResourceLinkSkipped",
		"\"reason\":\"%s\",\"sourceKind\":\"%s\",\"resource\":\"%p\",\"bytes\":%llu,\"hardLinkError\":%lu,\"copyError\":%lu",
		reason ? reason : "copy_failed",
		DumpTaskSourceName(task.sourceKind),
		task.source,
		static_cast<unsigned long long>(task.copyBytes),
		hardLinkError,
		copyError);
	return false;
}

static bool LinkTaskTextSidecar(const wchar_t *sourcePath, const wchar_t *destPath,
	const DumpTask &task)
{
	if (task.isTexture)
		return true;
	wchar_t sourceTextPath[MAX_PATH];
	wchar_t destTextPath[MAX_PATH];
	BuildTextFileName(sourcePath, sourceTextPath, ARRAYSIZE(sourceTextPath));
	BuildTextFileName(destPath, destTextPath, ARRAYSIZE(destTextPath));
	if (GetFileAttributesW(sourceTextPath) == INVALID_FILE_ATTRIBUTES)
		return true;
	return LinkOrCopyFile(sourceTextPath, destTextPath, task, "sidecar_copy_failed");
}

static bool LinkTaskToDedupedFile(
	const wchar_t *resourceDir, const DumpTask &task, const std::wstring &dedupePath)
{
	if (!resourceDir || task.fileName.empty() || dedupePath.empty()) {
		DX12FrameAnalysisLogJsonFunc("ResourceLinkSkipped",
			"\"reason\":\"invalid_args\",\"sourceKind\":\"%s\",\"resource\":\"%p\",\"bytes\":%llu",
			DumpTaskSourceName(task.sourceKind),
			task.source,
			static_cast<unsigned long long>(task.copyBytes));
		return false;
	}

	wchar_t sourcePath[MAX_PATH];
	wchar_t destPath[MAX_PATH];
	swprintf_s(sourcePath, L"%s\\%s", resourceDir, dedupePath.c_str());
	swprintf_s(destPath, L"%s\\%s", resourceDir, task.fileName.c_str());

	if (GetFileAttributesW(sourcePath) == INVALID_FILE_ATTRIBUTES) {
		DX12FrameAnalysisLogJsonFunc("ResourceLinkSkipped",
			"\"reason\":\"dedupe_source_missing\",\"sourceKind\":\"%s\",\"resource\":\"%p\",\"bytes\":%llu",
			DumpTaskSourceName(task.sourceKind),
			task.source,
			static_cast<unsigned long long>(task.copyBytes));
		return false;
	}
	if (!LinkOrCopyFile(sourcePath, destPath, task, "copy_failed"))
		return false;
	// The human-readable .buf.txt belongs to the dumped buffer as much as the
	// binary .buf. Link it with the semantic resource name so dedupe never
	// hides the text sidecar from FrameAnalysis users.
	return LinkTaskTextSidecar(sourcePath, destPath, task);
}

static bool MapAndWriteTask(
	const wchar_t *dedupeDir, const wchar_t *resourceDir, DumpTask *task,
	std::wstring *dedupePath)
{
	if (!dedupeDir || !resourceDir || !task)
		return false;

	bool ok = false;
	__try {
		void *mapped = nullptr;
		D3D12_RANGE range = { static_cast<SIZE_T>(task->readbackOffset),
			static_cast<SIZE_T>(task->readbackOffset + task->copyBytes) };
		ID3D12Resource *mapResource = task->directMap ? task->source : task->readback;
		if (!mapResource) {
			task->skipNote = task->directMap ? "missing_direct_resource" : "missing_readback";
			DX12FrameAnalysisLogJsonFunc("ResourceMapWriteSkipped",
				"\"reason\":\"%s\",\"sourceKind\":\"%s\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
				task->skipNote,
				DumpTaskSourceName(task->sourceKind),
				task->source, task->readback, task->directMap ? 1 : 0,
				static_cast<unsigned long long>(task->readbackOffset),
				static_cast<unsigned long long>(task->copyBytes));
			return false;
		}
		const HRESULT hr = mapResource->Map(0, &range, &mapped);
		if (SUCCEEDED(hr) && mapped) {
			ok = WriteDedupedResourceFile(dedupeDir, resourceDir, *task, mapped, dedupePath);
			D3D12_RANGE emptyRange = { 0, 0 };
			mapResource->Unmap(0, &emptyRange);
		} else {
			task->skipNote = "map_failed";
			DX12FrameAnalysisLogJsonFunc("ResourceMapWriteSkipped",
				"\"reason\":\"map_failed\",\"hr\":\"0x%lx\",\"sourceKind\":\"%s\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
				hr,
				DumpTaskSourceName(task->sourceKind),
				task->source, task->readback, task->directMap ? 1 : 0,
				static_cast<unsigned long long>(task->readbackOffset),
				static_cast<unsigned long long>(task->copyBytes));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "map_or_write_exception";
		DX12FrameAnalysisLogJsonFunc("ResourceMapWriteSkipped",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"readback\":\"%p\",\"direct\":%u,\"offset\":%llu,\"bytes\":%llu",
			GetExceptionCode(), task->source, task->readback, task->directMap ? 1 : 0,
			static_cast<unsigned long long>(task->readbackOffset),
			static_cast<unsigned long long>(task->copyBytes));
		ok = false;
	}
	return ok;
}

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
		DX12BuildIaBufferFileName(task.iaBuffer, fileName, fileNameCount);
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
		outputBinding = ResourceBindingFromOutputBinding(task.outputBinding);
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
	if (DX12DumpBuildSemanticSlot(*binding, semanticSlot, ARRAYSIZE(semanticSlot))) {
		swprintf_s(fileName, fileNameCount,
			L"%s-pso%llu%s-%S-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
			eventPrefix,
			static_cast<unsigned long long>(binding->psoIndex),
			shaderPart,
			DX12DumpShortBindSpace(binding->bindSpace),
			semanticSlot,
			binding->rootParameterIndex,
			static_cast<unsigned long long>(binding->descriptorIndex),
			DX12DumpShortDescriptorKind(binding->descriptor.kind),
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
		DX12DumpShortBindSpace(binding->bindSpace),
		binding->rootParameterIndex,
		static_cast<unsigned long long>(binding->descriptorIndex),
		DX12DumpShortDescriptorKind(binding->descriptor.kind),
		binding->descriptor.resource,
		static_cast<unsigned long long>(task.sourceOffset),
		static_cast<unsigned long long>(task.copyBytes),
		ext);
}

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
		DX12FrameAnalysisManifestWriteIaBinding(
			task.iaBuffer, desc, sourceOffset, copyBytes, sourceState,
			hasCurrentState, filePath);
		return;
	}

	DX12FrameAnalysisManifestWriteResourceBinding(
		task.binding, desc, sourceOffset, copyBytes, sourceState,
		hasCurrentState, filePath);
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
	binding.hasDescriptor = resource.resource != nullptr;
	return binding;
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
		DX12DumpIsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension);
	const D3D12_RESOURCE_DESC desc = binding.descriptor.hasResourceDesc ?
		binding.descriptor.resourceDesc : D3D12_RESOURCE_DESC();
	const UINT64 sourceOffset = binding.descriptor.resourceOffset;
	const UINT64 copyBytes = binding.descriptor.viewSize;
	const D3D12_RESOURCE_STATES sourceState = binding.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(binding.descriptor.currentState) :
		DX12DumpGuessDescriptorSourceState(binding);
	AddDumpedTaskRow(rows, status, task, desc, sourceOffset, copyBytes,
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
	task.binding = ResourceBindingFromOutputBinding(output);
	task.isTexture = output.descriptor.hasResourceDesc &&
		DX12DumpIsTextureResourceDimension(output.descriptor.resourceDesc.Dimension);
	const D3D12_RESOURCE_DESC desc = output.descriptor.hasResourceDesc ?
		output.descriptor.resourceDesc : D3D12_RESOURCE_DESC();
	const D3D12_RESOURCE_STATES sourceState = output.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(output.descriptor.currentState) :
		DX12DumpGuessDescriptorSourceState(task.binding);
	AddDumpedTaskRow(rows, status, task, desc,
		output.descriptor.resourceOffset, output.descriptor.viewSize,
		sourceState, output.descriptor.hasCurrentState, semanticName, dedupePath, note);
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

static void WriteDescriptorResourceBindingCsvRow(FILE *file, const DumpedTaskRow &row)
{
	if (!file)
		return;

	const DX12FrameResourceBinding &binding = row.binding;
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	const char *kind = BindingKindName(binding);
	const char *rowType = binding.bindSpace == "hunting_metadata" ? "metadata" :
		(binding.dispatchId ? "dispatch" : "draw");
	char cs[32] = "-";
	if (binding.shaderInfo.hasCS)
		sprintf_s(cs, sizeof(cs), "%016llx",
			static_cast<unsigned long long>(binding.shaderInfo.cs));
	char fileHash[16];
	FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
	const uint32_t huntHash = DescriptorHuntHash(binding, row.copyBytes);
	char semanticSlot[32] = "-";
	DX12DumpBuildSemanticSlot(binding, semanticSlot, ARRAYSIZE(semanticSlot));
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
		FileNameFromPath(row.dedupePath),
		fileHash,
		huntHash,
		FileNameFromPath(row.semanticName),
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
		row.binding : ResourceBindingFromOutputBinding(row.outputBinding);
	const DX12DescriptorSummary &descriptor = binding.descriptor;
	char cs[32] = "-";
	if (binding.shaderInfo.hasCS)
		sprintf_s(cs, sizeof(cs), "%016llx",
			static_cast<unsigned long long>(binding.shaderInfo.cs));
	char fileHash[16];
	FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
	const uint32_t huntHash = DescriptorHuntHash(binding, row.copyBytes);
	char semanticSlot[32] = "-";
	DX12DumpBuildSemanticSlot(binding, semanticSlot, ARRAYSIZE(semanticSlot));
	fprintf(file,
		"%llu,output,%llu,%s,%s,output,%u,%u,%u,%u,%p,%S,%s,%08x,%S,%llu,%llu,%u,%u,%u,%s,%s,%s\n",
		static_cast<unsigned long long>(binding.eventSerial),
		static_cast<unsigned long long>(binding.psoIndex),
		cs,
		BindingKindName(binding),
		binding.rootParameterIndex,
		binding.rangeIndex,
		binding.shaderRegister,
		binding.registerSpace,
		descriptor.resource,
		FileNameFromPath(row.dedupePath),
		fileHash,
		huntHash,
		FileNameFromPath(row.semanticName),
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
	FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
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
		FileNameFromPath(row.dedupePath),
		fileHash,
		buffer.huntHash,
		FileNameFromPath(row.semanticName),
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
	EnsureDirectory(summaryDir);
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
	DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
	DX12JsonAppendRawField(fields, sizeof(fields), "rows", std::to_string(rows.size()).c_str());
	DX12FrameAnalysisLogJsonFunc("ResourceBindingsCsvWritten", "%s", fields + 1);
}

static bool ExecuteCopyTask(
	ID3D12Device *device, ID3D12CommandQueue *queue, DumpTask *task)
{
	if (!device || !queue || !task || !task->ready || !task->readback)
		return false;
	task->copied = false;

	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *commandList = nullptr;
	HRESULT hr = device->GetDeviceRemovedReason();
	if (FAILED(hr)) {
		DX12FrameAnalysisLogJsonFunc("ResourceCopySkipped",
			"\"reason\":\"device_removed\",\"hr\":\"0x%lx\"", hr);
		return false;
	}

	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
	if (SUCCEEDED(hr))
		hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&commandList));
	if (FAILED(hr) || !allocator || !commandList) {
		DX12FrameAnalysisLogJsonFunc("ResourceCopySetupFailed",
			"\"hr\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu",
			hr, task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes));
		if (commandList)
			commandList->Release();
		if (allocator)
			allocator->Release();
		return false;
	}

	__try {
		if (task->needsBarrier)
			WriteResourceBarrier(commandList, task->source, task->sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);

		if (task->isTexture) {
			for (UINT subresource = 0; subresource < task->textureLayouts.size(); ++subresource) {
				D3D12_TEXTURE_COPY_LOCATION src = {};
				src.pResource = task->source;
				src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				src.SubresourceIndex = subresource;
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = task->readback;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dst.PlacedFootprint = task->textureLayouts[subresource];
				commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
		} else {
			commandList->CopyBufferRegion(task->readback, 0, task->source,
				task->sourceOffset, task->copyBytes);
		}

		if (task->needsBarrier)
			WriteResourceBarrier(commandList, task->source, D3D12_RESOURCE_STATE_COPY_SOURCE, task->sourceState);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "copy_record_exception";
		DX12FrameAnalysisLogJsonFunc("ResourceCopyTaskFailed",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_record_exception\"",
			GetExceptionCode(), task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		commandList->Release();
		allocator->Release();
		return false;
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		task->skipNote = "copy_close_failed";
		DX12FrameAnalysisLogJsonFunc("ResourceCopyTaskFailed",
			"\"hr\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_close_failed\"",
			hr, task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		commandList->Release();
		allocator->Release();
		return false;
	}
	ID3D12CommandList *lists[] = { commandList };
	bool ok = false;
	__try {
		queue->ExecuteCommandLists(1, lists);
		ok = WaitForFence(queue, device);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		task->skipNote = "copy_execute_exception";
		DX12FrameAnalysisLogJsonFunc("ResourceCopyTaskFailed",
			"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"copy_execute_exception\"",
			GetExceptionCode(), task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState));
		ok = false;
	}

	commandList->Release();
	allocator->Release();
	if (!ok) {
		if (!task->skipNote)
			task->skipNote = "copy_execute_failed";
		DX12FrameAnalysisLogJsonFunc("ResourceCopyTaskFailed",
			"\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\",\"note\":\"%s\"",
			task->source, task->isTexture ? 1 : 0,
			static_cast<unsigned long long>(task->sourceOffset),
			static_cast<unsigned long long>(task->copyBytes),
			static_cast<UINT>(task->sourceState),
			task->skipNote ? task->skipNote : "copy_execute_failed");
		return false;
	}

	task->copied = true;
	task->skipNote = nullptr;
	return true;
}

static size_t ExecuteReadbackCopyTasks(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks,
	bool *readbackOkOut)
{
	if (readbackOkOut)
		*readbackOkOut = true;
	if (!tasks)
		return 0;

	size_t eligible = 0;
	size_t copied = 0;
	for (size_t i = 0; i < tasks->size(); ++i) {
		DumpTask &task = (*tasks)[i];
		if (task.directMap || !task.ready || !task.readback)
			continue;
		eligible++;
		if (ExecuteCopyTask(device, queue, &task))
			copied++;
	}

	if (eligible == 0)
		return DX12DumpCountCopiedTasks(*tasks);

	if (readbackOkOut)
		*readbackOkOut = eligible == copied;
	DX12FrameAnalysisLogJsonFunc("ResourceCopyResultDetail",
		"\"mode\":\"per_resource\",\"eligible\":%zu,\"copied\":%zu,\"failed\":%zu",
		eligible, copied, eligible >= copied ? eligible - copied : 0);
	return DX12DumpCountCopiedTasks(*tasks);
}

DX12FrameAnalysisDumpResult DX12DumpCurrentFrameResources(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	if (!dir)
	{
		result.reason = "missing_directory";
		return result;
	}

	std::vector<DX12FrameResourceBinding> bindings;
	DX12GetCurrentFrameResourceBindings(&bindings);
	std::vector<DX12FrameIaBufferBinding> iaBuffers;
	DX12GetCurrentFrameIaBuffers(&iaBuffers);
	std::vector<DX12FrameOutputBinding> outputBindings;
	DX12GetCurrentFrameOutputBindings(&outputBindings);

	wchar_t resourceDir[MAX_PATH];
	wcsncpy_s(resourceDir, dir, _TRUNCATE);
	if (!EnsureDirectory(resourceDir)) {
		DX12FrameAnalysisLogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"resource_dir_create_failed\",\"error\":%lu",
			GetLastError());
		result.failed = 1;
		result.reason = "resource_dir_create_failed";
		return result;
	}
	wchar_t dedupeDir[MAX_PATH];
	GetDedupedDirectory(dir, dedupeDir, ARRAYSIZE(dedupeDir));

	const UINT64 frameReadbackBudget = FrameReadbackBudgetBytes();
	UINT64 frameReadbackBytes = 0;
	const char *dumpMode = DX12IsModMode() ? "Mod" : "Hunting";
	DX12FrameAnalysisLogJsonFunc("ResourceDump",
		"\"mode\":\"%s\",\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu,\"unsafeCopy\":%u,\"gpuCopy\":%u,\"stateTracking\":1",
		dumpMode,
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget),
		UnsafeResourceCopyEnabled() ? 1 : 0,
		DX12DumpGpuResourceCopyEnabled() ? 1 : 0);
	DX12LogJsonFunc("DX12ResourceDump",
		"\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"frameReadbackBudget\":%llu",
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		static_cast<unsigned long long>(frameReadbackBudget));

	ID3D12CommandQueue *queue = DX12AcquireCommandQueue();
	ID3D12Device *device = nullptr;
	if (!queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
		DX12FrameAnalysisLogJsonFunc("ResourceDump",
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
	std::unordered_map<std::string, size_t> canonicalTaskByKey;
	UINT duplicates = 0;
	UINT candidateCbv = 0;
	UINT candidateSrv = 0;
	UINT candidateUav = 0;
	UINT candidateTextures = 0;
	UINT candidateRtv = 0;
	UINT candidateDsv = 0;
	UINT metadataTextureCandidates = 0;

	for (const DX12FrameResourceBinding &binding : bindings) {
		if (!DX12DumpShouldDumpBinding(binding))
			continue;
		if (binding.descriptor.kind == DescriptorKind::CBV)
			candidateCbv++;
		else if (binding.descriptor.kind == DescriptorKind::SRV)
			candidateSrv++;
		else if (binding.descriptor.kind == DescriptorKind::UAV)
			candidateUav++;
		if (binding.descriptor.hasResourceDesc &&
			DX12DumpIsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
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
			BuildDescriptorDedupeKey(binding.descriptor, key, ARRAYSIZE(key));
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
			BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12DumpPrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
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
			BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12DumpPrepareIaBufferTaskForCurrentMode(device, buffer, &task, &frameReadbackBytes, frameReadbackBudget)) {
			wchar_t fileName[MAX_PATH];
			DX12BuildIaBufferFileName(buffer, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
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
		DX12FrameResourceBinding binding = ResourceBindingFromOutputBinding(output);
		if (!DX12DumpShouldDumpBinding(binding)) {
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
			DX12DumpIsTextureResourceDimension(binding.descriptor.resourceDesc.Dimension))
			candidateTextures++;

		char key[160];
		BuildOutputDedupeKey(output, binding, key, ARRAYSIZE(key));
		auto canonicalIt = canonicalTaskByKey.find(key);
		if (canonicalIt != canonicalTaskByKey.end()) {
			DumpTask duplicate;
			duplicate = tasks[canonicalIt->second];
			duplicate.sourceKind = DumpTaskSource::Output;
			duplicate.outputBinding = output;
			duplicate.binding = binding;
			duplicate.dedupeKey = key;
			wchar_t fileName[MAX_PATH];
			BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (DX12DumpPrepareDescriptorTaskForCurrentMode(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
			task.sourceKind = DumpTaskSource::Output;
			task.outputBinding = output;
			wchar_t fileName[MAX_PATH];
			BuildFileName(task, fileName, ARRAYSIZE(fileName));
			task.fileName = fileName;
			task.dedupeKey = key;
			canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
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

	if (DX12IsHuntingMode()) {
		std::vector<DX12DescriptorSummary> metadataDescriptors;
		DX12GetResourceMetadataSnapshot(nullptr, &metadataDescriptors, nullptr, nullptr);
		UINT64 metadataIndex = 0;
		for (const DX12DescriptorSummary &descriptor : metadataDescriptors) {
			metadataIndex++;
			if (!descriptor.resource || !descriptor.hasResourceDesc ||
				!DX12DumpIsTextureResourceDimension(descriptor.resourceDesc.Dimension))
				continue;
			metadataTextureCandidates++;

			char key[160];
			BuildDescriptorDedupeKey(descriptor, key, ARRAYSIZE(key));
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
				BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
				duplicate.fileName = fileName;
				duplicate.skipNote = "already_dumped";
				duplicateTasks.push_back(std::move(duplicate));
				duplicates++;
				continue;
			}

			DumpTask task;
			if (DX12DumpPrepareHuntingMetadataTextureTask(
					device, descriptor, &task, &frameReadbackBytes, frameReadbackBudget)) {
				task.binding.descriptorIndex = metadataIndex;
				task.dedupeKey = key;
				wchar_t fileName[MAX_PATH];
				BuildFileName(task, fileName, ARRAYSIZE(fileName));
				task.fileName = fileName;
				canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
				tasks.push_back(std::move(task));
			} else {
				task.sourceKind = DumpTaskSource::Descriptor;
				task.binding.bindSpace = "hunting_metadata";
				task.binding.descriptorIndex = metadataIndex;
				task.binding.descriptor = descriptor;
				task.binding.hasDescriptor = descriptor.resource != nullptr;
				if (task.skipNote == nullptr)
					task.skipNote = "metadata_texture_prepare_failed";
				skippedTasks.push_back(std::move(task));
			}
		}
		DX12FrameAnalysisLogJsonFunc("HuntingMetadataTexturesQueued",
			"\"descriptors\":%zu,\"candidates\":%u,\"tasks\":%zu,\"duplicates\":%u",
			metadataDescriptors.size(), metadataTextureCandidates, tasks.size(), duplicates);

		std::vector<DX12ResourceSummary> metadataResources;
		DX12GetTrackedResourceSnapshot(&metadataResources);
		UINT64 resourceIndex = 0;
		UINT resourceTextureCandidates = 0;
		UINT resourceDuplicates = 0;
		for (const DX12ResourceSummary &resource : metadataResources) {
			resourceIndex++;
			if (!resource.resource || !resource.hasResourceDesc ||
				!DX12DumpIsTextureResourceDimension(resource.resourceDesc.Dimension))
				continue;
			resourceTextureCandidates++;

			char key[160];
			BuildResourceDedupeKey(resource, key, ARRAYSIZE(key));
			const DX12FrameResourceBinding binding =
				ResourceBindingFromTrackedResource(resource, resourceIndex);
			auto canonicalIt = canonicalTaskByKey.find(key);
			if (canonicalIt != canonicalTaskByKey.end()) {
				DumpTask duplicate;
				duplicate = tasks[canonicalIt->second];
				duplicate.sourceKind = DumpTaskSource::Resource;
				duplicate.binding = binding;
				duplicate.dedupeKey = key;
				wchar_t fileName[MAX_PATH];
				BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
				duplicate.fileName = fileName;
				duplicate.skipNote = "already_dumped";
				duplicateTasks.push_back(std::move(duplicate));
				duplicates++;
				resourceDuplicates++;
				continue;
			}

			DumpTask task;
			if (DX12DumpPrepareHuntingResourceTextureTask(
					device, resource, &task, &frameReadbackBytes, frameReadbackBudget)) {
				task.sourceKind = DumpTaskSource::Resource;
				task.binding = binding;
				task.dedupeKey = key;
				wchar_t fileName[MAX_PATH];
				BuildFileName(task, fileName, ARRAYSIZE(fileName));
				task.fileName = fileName;
				canonicalTaskByKey.emplace(task.dedupeKey, tasks.size());
				tasks.push_back(std::move(task));
			} else {
				task.sourceKind = DumpTaskSource::Resource;
				task.binding = binding;
				if (task.skipNote == nullptr)
					task.skipNote = "resource_texture_prepare_failed";
				skippedTasks.push_back(std::move(task));
			}
		}
		DX12FrameAnalysisLogJsonFunc("HuntingResourceTexturesQueued",
			"\"resources\":%zu,\"candidates\":%u,\"tasks\":%zu,\"duplicates\":%u,\"resourceDuplicates\":%u",
			metadataResources.size(), resourceTextureCandidates, tasks.size(),
			duplicates, resourceDuplicates);
	}

	bool readbackCopyOk = false;
	const size_t copiedTasks = ExecuteReadbackCopyTasks(device, queue, &tasks, &readbackCopyOk);
	DX12FrameAnalysisLogJsonFunc("ResourceCopyResult",
		"\"gpuCopy\":%u,\"readbackOk\":%u,\"copied\":%zu,\"tasks\":%zu,\"duplicates\":%u",
		DX12DumpGpuResourceCopyEnabled() ? 1 : 0,
		readbackCopyOk ? 1 : 0, copiedTasks, tasks.size(), duplicates);

	UINT dumpedTextures = 0;
	UINT dumpedBuffers = 0;
	UINT failed = 0;
	UINT linked = 0;
	std::unordered_map<std::string, std::wstring> dedupePathByKey;
	std::unordered_map<std::string, const char*> dedupeFailureReasonByKey;
	std::vector<DumpedTaskRow> dumpedRows;
	std::unordered_set<std::wstring> loggedFileDumps;
	for (DumpTask &task : tasks) {
		bool ok = task.copied;
		std::wstring dedupePath;
		if (ok) {
			ok = MapAndWriteTask(dedupeDir, resourceDir, &task, &dedupePath);
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
			continue;
		}

		if (!LinkTaskToDedupedFile(resourceDir, task, dedupePath)) {
			failed++;
			if (!task.dedupeKey.empty())
				dedupeFailureReasonByKey[task.dedupeKey] = "dedupe_link_failed";
			continue;
		}

		if (loggedFileDumps.insert(dedupePath).second) {
			DX12FrameAnalysisManifestWriteFileDump(
				dedupePath.c_str(), task.isTexture, task.copyBytes, "dumped", "");
		}
		WriteIndexRow(nullptr, "dumped", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupePath.c_str(), "");
		AddDumpedTaskRow(&dumpedRows, "dumped", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupePath, "");
	}

	for (DumpTask &task : duplicateTasks) {
		auto dedupeIt = dedupePathByKey.find(task.dedupeKey);
		if (dedupeIt == dedupePathByKey.end()) {
			failed++;
			auto reasonIt = dedupeFailureReasonByKey.find(task.dedupeKey);
			const char *reason = reasonIt != dedupeFailureReasonByKey.end() ?
				reasonIt->second : "dedupe_source_missing";
			if (task.sourceKind == DumpTaskSource::Descriptor)
				AddBindingRow(&dumpedRows, "failed", task.binding, L"",
					task.fileName.c_str(), reason);
			else if (task.sourceKind == DumpTaskSource::Resource)
				AddDumpedTaskRow(&dumpedRows, "failed", task, task.desc,
					task.sourceOffset, task.copyBytes, task.sourceState,
					task.stateKnown, task.fileName.c_str(), L"", reason);
			else if (task.sourceKind == DumpTaskSource::Output)
				AddOutputBindingRow(&dumpedRows, "failed", task.outputBinding, L"",
					task.fileName.c_str(), reason);
			continue;
		}

		if (!LinkTaskToDedupedFile(resourceDir, task, dedupeIt->second)) {
			failed++;
			if (task.sourceKind == DumpTaskSource::Descriptor)
				AddBindingRow(&dumpedRows, "failed", task.binding, L"",
					task.fileName.c_str(), "dedupe_link_failed");
			else if (task.sourceKind == DumpTaskSource::Resource)
				AddDumpedTaskRow(&dumpedRows, "failed", task, task.desc,
					task.sourceOffset, task.copyBytes, task.sourceState,
					task.stateKnown, task.fileName.c_str(), L"", "dedupe_link_failed");
			else if (task.sourceKind == DumpTaskSource::Output)
				AddOutputBindingRow(&dumpedRows, "failed", task.outputBinding, L"",
					task.fileName.c_str(), "dedupe_link_failed");
			continue;
		}

		linked++;
		WriteIndexRow(nullptr, "linked", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupeIt->second.c_str(), "already_dumped");
		AddDumpedTaskRow(&dumpedRows, "linked", task, task.desc,
			task.sourceOffset, task.copyBytes, task.sourceState, task.stateKnown,
			task.fileName.c_str(), dedupeIt->second, "already_dumped");
	}

	UINT skipped = 0;
	for (const DumpTask &task : skippedTasks) {
		skipped++;
		if (task.sourceKind == DumpTaskSource::Descriptor) {
			WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12DumpGuessDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
			AddBindingRow(&dumpedRows, "skipped", task.binding, L"",
				task.fileName.c_str(), task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::InputAssembler) {
			const D3D12_RESOURCE_DESC desc = task.iaBuffer.resource.hasResourceDesc ?
				task.iaBuffer.resource.resourceDesc : D3D12_RESOURCE_DESC();
			const D3D12_RESOURCE_STATES state = task.iaBuffer.resource.hasCurrentState ?
				static_cast<D3D12_RESOURCE_STATES>(task.iaBuffer.resource.currentState) :
				DX12DumpGuessIaBufferSourceState(task.iaBuffer);
			AddDumpedTaskRow(&dumpedRows, "skipped", task, desc,
				task.iaBuffer.resource.resourceOffset, task.iaBuffer.size,
				state, task.iaBuffer.resource.hasCurrentState,
				task.fileName.c_str(), L"", task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::Output) {
			WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12DumpGuessDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
			AddOutputBindingRow(&dumpedRows, "skipped", task.outputBinding, L"",
				task.fileName.c_str(), task.skipNote ? task.skipNote : "skipped");
		} else if (task.sourceKind == DumpTaskSource::Resource) {
			WriteIndexRow(nullptr, "skipped", task, task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset, task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12DumpGuessDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState, L"", L"",
				task.skipNote ? task.skipNote : "skipped");
			AddDumpedTaskRow(&dumpedRows, "skipped", task,
				task.binding.descriptor.resourceDesc,
				task.binding.descriptor.resourceOffset,
				task.binding.descriptor.viewSize,
				task.binding.descriptor.hasCurrentState ?
					static_cast<D3D12_RESOURCE_STATES>(task.binding.descriptor.currentState) :
					DX12DumpGuessDescriptorSourceState(task.binding),
				task.binding.descriptor.hasCurrentState,
				task.fileName.c_str(), L"",
				task.skipNote ? task.skipNote : "skipped");
		}
	}

	DX12DumpReleaseTasks(&tasks);
	device->Release();
	queue->Release();
	WriteResourceBindingsCsv(dir, dumpedRows);

	DX12FrameAnalysisLogJsonFunc("ResourceSummary",
		"\"textures\":%u,\"buffers\":%u,\"linked\":%u,\"skipped\":%u,\"duplicates\":%u,\"failed\":%u,\"readbackBytes\":%llu,\"bindings\":%zu,\"ia\":%zu,\"outputs\":%zu,\"candidate_cbv\":%u,\"candidate_srv\":%u,\"candidate_uav\":%u,\"candidate_rtv\":%u,\"candidate_dsv\":%u,\"candidate_textures\":%u,\"metadata_texture_candidates\":%u",
		dumpedTextures, dumpedBuffers, linked, skipped, duplicates, failed,
		static_cast<unsigned long long>(frameReadbackBytes),
		bindings.size(), iaBuffers.size(), outputBindings.size(),
		candidateCbv, candidateSrv, candidateUav, candidateRtv, candidateDsv,
		candidateTextures, metadataTextureCandidates);
	result.ok = failed == 0;
	result.written = static_cast<size_t>(dumpedTextures) + dumpedBuffers + linked;
	result.skipped = skipped;
	result.failed = failed;
	result.reason = failed == 0 ? "" : "resource_failures";
	return result;
}
