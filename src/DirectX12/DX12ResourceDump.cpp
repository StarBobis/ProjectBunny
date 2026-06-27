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
#include "DX12ShaderDump.h"
#include "DX12State.h"

static constexpr UINT64 MaxResourceDumpBytes = 256ull * 1024ull * 1024ull;
static constexpr UINT64 DefaultFrameReadbackBudgetBytes = 1536ull * 1024ull * 1024ull;
static constexpr UINT64 MaxGenericBufferTextScalars = 4096;
static constexpr UINT64 MaxIaBufferTextRows = 1024ull * 1024ull;

enum class DumpTaskSource
{
	Descriptor,
	InputAssembler,
};

struct DumpTask
{
	DumpTaskSource sourceKind = DumpTaskSource::Descriptor;
	DX12FrameResourceBinding binding;
	DX12FrameIaBufferBinding iaBuffer;
	ID3D12Resource *source = nullptr;
	ID3D12Resource *readback = nullptr;
	D3D12_RESOURCE_DESC desc = {};
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT textureLayout = {};
	UINT textureRows = 0;
	UINT64 sourceOffset = 0;
	UINT64 copyBytes = 0;
	UINT64 readbackOffset = 0;
	D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON;
	bool needsBarrier = false;
	bool isTexture = false;
	bool ready = false;
	bool copied = false;
	bool stateKnown = false;
	bool directMap = false;
	std::wstring fileName;
	std::string dedupeKey;
	const char *skipNote = nullptr;
};

struct DumpedTaskRow
{
	DumpTaskSource sourceKind = DumpTaskSource::Descriptor;
	DX12FrameResourceBinding binding;
	DX12FrameIaBufferBinding iaBuffer;
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

static bool EnsureDirectory(const wchar_t *path)
{
	return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
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

static bool UnsafeResourceCopyEnabled()
{
	wchar_t value[16] = {};
	DWORD chars = GetEnvironmentVariableW(L"MIGOTO_DX12_UNSAFE_RESOURCE_COPY", value, ARRAYSIZE(value));
	return chars > 0 && value[0] == L'1';
}

static bool GpuResourceCopyEnabled()
{
	wchar_t value[16] = {};
	DWORD chars = GetEnvironmentVariableW(L"MIGOTO_DX12_ENABLE_GPU_RESOURCE_COPY", value, ARRAYSIZE(value));
	return chars == 0 || value[0] != L'0';
}

static UINT64 FrameReadbackBudgetBytes()
{
	wchar_t value[32] = {};
	DWORD chars = GetEnvironmentVariableW(L"MIGOTO_DX12_MAX_FRAME_READBACK_MB", value, ARRAYSIZE(value));
	if (chars == 0)
		return DefaultFrameReadbackBudgetBytes;
	UINT64 mb = wcstoull(value, nullptr, 10);
	if (mb == 0)
		return DefaultFrameReadbackBudgetBytes;
	return mb * 1024ull * 1024ull;
}

static UINT64 AlignUp(UINT64 value, UINT64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static D3D12_RESOURCE_STATES GuessSourceState(const DX12FrameResourceBinding &binding)
{
	if (binding.descriptor.kind == "CBV")
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	if (binding.descriptor.kind == "UAV")
		return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (binding.descriptor.kind == "RTV")
		return D3D12_RESOURCE_STATE_RENDER_TARGET;
	if (binding.descriptor.kind == "DSV")
		return D3D12_RESOURCE_STATE_DEPTH_WRITE;
	return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

static const char *RootDescriptorKindName(UINT rangeType)
{
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		return "CBV";
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV)
		return "SRV";
	if (rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV)
		return "UAV";
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

static D3D12_RESOURCE_STATES GuessIaBufferSourceState(const DX12FrameIaBufferBinding &buffer)
{
	if (buffer.role == "IB")
		return D3D12_RESOURCE_STATE_INDEX_BUFFER;
	return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
}

static bool ResourceNeedsBarrier(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return false;
	return true;
}

static bool ResourceNeedsBarrier(const DX12BufferResourceSummary &resource)
{
	if (resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return false;
	return true;
}

static bool StateTrackingAllowsCopy(const DX12DescriptorSummary &descriptor)
{
	if (descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return true;
	return descriptor.hasCurrentState;
}

static bool StateTrackingAllowsCopy(const DX12BufferResourceSummary &resource)
{
	if (resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK))
		return true;
	return resource.hasCurrentState;
}

static bool ResourceIsCpuVisible(const DX12DescriptorSummary &descriptor)
{
	return descriptor.hasResourceHeapType &&
		(descriptor.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			descriptor.resourceHeapType == D3D12_HEAP_TYPE_READBACK);
}

static bool ResourceIsCpuVisible(const DX12BufferResourceSummary &resource)
{
	return resource.hasResourceHeapType &&
		(resource.resourceHeapType == D3D12_HEAP_TYPE_UPLOAD ||
			resource.resourceHeapType == D3D12_HEAP_TYPE_READBACK);
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

static void WriteDDSHeader(FILE *file, const D3D12_RESOURCE_DESC &desc, UINT64 rowPitch)
{
	WriteU32(file, MakeFourCC('D', 'D', 'S', ' '));
	WriteU32(file, 124);
	WriteU32(file, 0x0002100f);
	WriteU32(file, desc.Height);
	WriteU32(file, static_cast<uint32_t>(desc.Width));
	WriteU32(file, static_cast<uint32_t>(rowPitch));
	WriteU32(file, 0);
	WriteU32(file, 1);
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

	WriteU32(file, 0x00001000);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);
	WriteU32(file, 0);

	WriteU32(file, static_cast<uint32_t>(desc.Format));
	WriteU32(file, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
	WriteU32(file, 0);
	WriteU32(file, 1);
	WriteU32(file, 0);
}

static bool WriteTextureDDSFile(FILE *file, const DumpTask &task, const void *mappedData)
{
	if (!file || !mappedData)
		return false;

	WriteDDSHeader(file, task.desc, task.textureLayout.Footprint.RowPitch);
	const uint8_t *base = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
	for (UINT row = 0; row < task.textureRows; ++row) {
		const uint8_t *src = base + static_cast<size_t>(row) * task.textureLayout.Footprint.RowPitch;
		fwrite(src, 1, task.textureLayout.Footprint.RowPitch, file);
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
	const UINT64 writtenCount = floatCount > MaxGenericBufferTextScalars ?
		MaxGenericBufferTextScalars : floatCount;
	if (floatCount > writtenCount)
		fprintf(file, "truncated: 1\nshown floats: %llu\n", static_cast<unsigned long long>(writtenCount));
	const float *floats = reinterpret_cast<const float*>(data);
	for (UINT64 i = 0; i < writtenCount; ++i)
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
		const UINT64 writtenCount = count > MaxIaBufferTextRows * 3 ?
			MaxIaBufferTextRows * 3 : count;
		if (count > writtenCount)
			fprintf(file, "truncated: 1\nshown indices: %llu\n", static_cast<unsigned long long>(writtenCount));
		for (UINT64 i = 0; i < writtenCount; ++i) {
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
		const UINT64 writtenCount = count > MaxIaBufferTextRows * 3 ?
			MaxIaBufferTextRows * 3 : count;
		if (count > writtenCount)
			fprintf(file, "truncated: 1\nshown indices: %llu\n", static_cast<unsigned long long>(writtenCount));
		for (UINT64 i = 0; i < writtenCount; ++i) {
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
	const UINT64 writtenCount = vertexCount > MaxIaBufferTextRows ?
		MaxIaBufferTextRows : vertexCount;
	fprintf(file, "vertex count: %llu\n", static_cast<unsigned long long>(vertexCount));
	if (vertexCount > writtenCount)
		fprintf(file, "truncated: 1\nshown vertices: %llu\n", static_cast<unsigned long long>(writtenCount));
	for (UINT64 v = 0; v < writtenCount; ++v) {
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
	if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == "IB")
		ok = WriteIndexBufferTextFile(file, task, data);
	else if (task.sourceKind == DumpTaskSource::InputAssembler && task.iaBuffer.role == "VB")
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
		hash = HashBytes(hash, &task.textureLayout.Footprint.RowPitch,
			sizeof(task.textureLayout.Footprint.RowPitch));
		hash = HashBytes(hash, &task.textureRows, sizeof(task.textureRows));
		const uint8_t *base = static_cast<const uint8_t*>(mappedData) + task.readbackOffset;
		for (UINT row = 0; row < task.textureRows; ++row) {
			const uint8_t *src = base + static_cast<size_t>(row) * task.textureLayout.Footprint.RowPitch;
			hash = HashBytes(hash, src, static_cast<size_t>(task.textureLayout.Footprint.RowPitch));
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

static bool LinkTaskToDedupedFile(
	const wchar_t *resourceDir, const DumpTask &task, const std::wstring &dedupePath)
{
	(void)resourceDir;
	(void)task;
	if (dedupePath.empty())
		return false;
	return true;
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
		if (mapResource && SUCCEEDED(mapResource->Map(0, &range, &mapped)) && mapped) {
			ok = WriteDedupedResourceFile(dedupeDir, resourceDir, *task, mapped, dedupePath);
			D3D12_RANGE emptyRange = { 0, 0 };
			mapResource->Unmap(0, &emptyRange);
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

static bool ShouldDumpBinding(const DX12FrameResourceBinding &binding)
{
	const bool isDispatch = binding.dispatchId != 0;
	const bool isDraw = binding.drawId != 0;
	if (!isDispatch && !isDraw)
		return false;

	const bool isGraphicsSpace =
		binding.bindSpace == "graphics_cbv_srv_uav" ||
		binding.bindSpace == "graphics_root";
	const bool isComputeSpace =
		binding.bindSpace == "compute_cbv_srv_uav" ||
		binding.bindSpace == "compute_root";
	if ((isDispatch && !isComputeSpace) || (!isDispatch && !isGraphicsSpace))
		return false;
	if (binding.rootDescriptor)
		return binding.gpuVirtualAddress != 0 &&
			(binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
			 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
			 binding.rangeType == D3D12_ROOT_PARAMETER_TYPE_UAV);
	if (!binding.hasDescriptor)
		return false;
	const bool isBufferResource =
		binding.descriptor.hasResourceDesc &&
		binding.descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
		binding.descriptor.resource != nullptr;
	if (!isBufferResource)
		return false;
	if (isDispatch)
		return binding.descriptor.kind == "CBV" ||
			binding.descriptor.kind == "SRV" ||
			binding.descriptor.kind == "UAV";
	return binding.descriptor.kind == "CBV";
}

static bool CreateReadbackForTask(
	ID3D12Device *device, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task)
		return false;

	if (task->copyBytes == 0 || task->copyBytes > MaxResourceDumpBytes) {
		task->skipNote = "copy_size_zero_or_too_large";
		return false;
	}
	if (frameReadbackBytes && frameReadbackBudgetBytes > 0 &&
		*frameReadbackBytes <= frameReadbackBudgetBytes &&
		task->copyBytes > frameReadbackBudgetBytes - *frameReadbackBytes) {
		task->skipNote = "frame_readback_budget_exceeded";
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC readbackDesc = {};
	readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	readbackDesc.Width = task->copyBytes;
	readbackDesc.Height = 1;
	readbackDesc.DepthOrArraySize = 1;
	readbackDesc.MipLevels = 1;
	readbackDesc.SampleDesc.Count = 1;
	readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&task->readback));
	if (FAILED(hr) || !task->readback) {
		task->skipNote = "readback_create_failed";
		return false;
	}

	if (frameReadbackBytes)
		*frameReadbackBytes += task->copyBytes;
	task->ready = true;
	return true;
}

static bool PrepareDirectMapTask(DumpTask *task)
{
	if (!task || !task->source)
		return false;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "gpu_copy_disabled";
		return false;
	}
	if (task->copyBytes == 0 || task->copyBytes > MaxResourceDumpBytes) {
		task->skipNote = "copy_size_zero_or_too_large";
		return false;
	}

	task->directMap = true;
	task->ready = true;
	task->copied = true;
	task->readbackOffset = task->sourceOffset;
	return true;
}

static bool PrepareTask(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task || !ShouldDumpBinding(binding))
		return false;

	const bool unsafeCopy = UnsafeResourceCopyEnabled();
	const bool gpuCopy = GpuResourceCopyEnabled();
	task->sourceKind = DumpTaskSource::Descriptor;
	task->binding = binding;
	if (binding.rootDescriptor) {
		DX12BufferResourceSummary resource;
		if (!DX12ResolveBufferResourceByGpuVa(binding.gpuVirtualAddress, 1, &resource) ||
			!resource.resource || !resource.hasResourceDesc) {
			task->skipNote = "unresolved_root_cbv";
			return false;
		}
		task->source = resource.resource;
		task->desc = resource.resourceDesc;
		task->binding.hasDescriptor = true;
		task->binding.descriptor.kind = RootDescriptorKindName(binding.rangeType);
		task->binding.descriptor.resource = resource.resource;
		task->binding.descriptor.resourceDesc = resource.resourceDesc;
		task->binding.descriptor.hasResourceDesc = resource.hasResourceDesc;
		task->binding.descriptor.resourceHeapType = resource.resourceHeapType;
		task->binding.descriptor.hasResourceHeapType = resource.hasResourceHeapType;
		task->binding.descriptor.gpuVirtualAddress = resource.gpuVirtualAddress;
		task->binding.descriptor.resourceOffset = resource.resourceOffset;
		const UINT64 availableBytes = task->desc.Width > resource.resourceOffset ?
			task->desc.Width - resource.resourceOffset : 0;
		task->binding.descriptor.viewSize = availableBytes > 4096 ? 4096 : availableBytes;
		task->binding.descriptor.currentState = resource.currentState;
		task->binding.descriptor.hasCurrentState = resource.hasCurrentState;
	} else {
		task->source = binding.descriptor.resource;
		task->desc = binding.descriptor.resourceDesc;
	}
	task->sourceState = task->binding.descriptor.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(task->binding.descriptor.currentState) :
		GuessSourceState(task->binding);
	task->stateKnown = StateTrackingAllowsCopy(task->binding.descriptor);
	if (!unsafeCopy && !task->stateKnown) {
		task->skipNote = "state_unknown";
		return false;
	}
	task->needsBarrier = ResourceNeedsBarrier(task->binding.descriptor);

	if (task->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
		if (task->desc.SampleDesc.Count > 1) {
			task->skipNote = "multisampled_texture_not_supported";
			return false;
		}
		UINT64 rowSize = 0;
		device->GetCopyableFootprints(&task->desc, 0, 1, 0,
			&task->textureLayout, &task->textureRows, &rowSize, &task->copyBytes);
		task->isTexture = true;
	} else if (task->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->sourceOffset = task->binding.descriptor.resourceOffset;
		task->copyBytes = task->binding.descriptor.viewSize ? task->binding.descriptor.viewSize : task->desc.Width;
		if (task->sourceOffset + task->copyBytes > task->desc.Width)
			task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
		task->isTexture = false;
	} else {
		task->skipNote = "unsupported_dimension";
		return false;
	}

	if (!gpuCopy) {
		if (ResourceIsCpuVisible(binding.descriptor))
			return PrepareDirectMapTask(task);
		task->skipNote = "gpu_copy_disabled";
		return false;
	}

	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static bool PrepareIaBufferTask(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes)
{
	if (!device || !task || !buffer.resolved || !buffer.resource.resource ||
		!buffer.resource.hasResourceDesc)
		return false;

	const bool unsafeCopy = UnsafeResourceCopyEnabled();
	const bool gpuCopy = GpuResourceCopyEnabled();
	task->sourceKind = DumpTaskSource::InputAssembler;
	task->iaBuffer = buffer;
	task->source = buffer.resource.resource;
	task->desc = buffer.resource.resourceDesc;
	if (task->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
		task->skipNote = "unsupported_dimension";
		return false;
	}
	task->sourceOffset = buffer.resource.resourceOffset;
	task->copyBytes = buffer.size;
	if (task->sourceOffset + task->copyBytes > task->desc.Width)
		task->copyBytes = task->desc.Width > task->sourceOffset ? task->desc.Width - task->sourceOffset : 0;
	task->sourceState = buffer.resource.hasCurrentState ?
		static_cast<D3D12_RESOURCE_STATES>(buffer.resource.currentState) :
		GuessIaBufferSourceState(buffer);
	task->stateKnown = StateTrackingAllowsCopy(buffer.resource);
	if (!unsafeCopy && !task->stateKnown) {
		task->skipNote = "state_unknown";
		return false;
	}
	task->needsBarrier = ResourceNeedsBarrier(buffer.resource);
	task->isTexture = false;

	if (!gpuCopy) {
		if (ResourceIsCpuVisible(buffer.resource))
			return PrepareDirectMapTask(task);
		task->skipNote = "gpu_copy_disabled";
		return false;
	}

	return CreateReadbackForTask(device, task, frameReadbackBytes, frameReadbackBudgetBytes);
}

static void ReleaseTasks(std::vector<DumpTask> *tasks)
{
	if (!tasks)
		return;
	for (DumpTask &task : *tasks) {
		if (task.readback) {
			task.readback->Release();
			task.readback = nullptr;
		}
	}
}

static size_t CountCopiedTasks(const std::vector<DumpTask> &tasks)
{
	size_t copied = 0;
	for (const DumpTask &task : tasks) {
		if (task.copied)
			copied++;
	}
	return copied;
}

static const char *ShortBindSpace(const std::string &bindSpace)
{
	if (bindSpace == "graphics_cbv_srv_uav")
		return "gfx";
	if (bindSpace == "graphics_sampler")
		return "gsp";
	if (bindSpace == "compute_cbv_srv_uav")
		return "cmp";
	if (bindSpace == "compute_sampler")
		return "csp";
	return bindSpace.empty() ? "unk" : bindSpace.c_str();
}

static const char *ShortDescriptorKind(const std::string &kind)
{
	if (kind == "CBV")
		return "cbv";
	if (kind == "SRV")
		return "srv";
	if (kind == "UAV")
		return "uav";
	if (kind == "CONSTANT_BUFFER_VIEW")
		return "cbv";
	if (kind == "SHADER_RESOURCE_VIEW")
		return "srv";
	if (kind == "UNORDERED_ACCESS_VIEW")
		return "uav";
	if (kind == "SAMPLER")
		return "smp";
	if (kind == "RENDER_TARGET_VIEW")
		return "rtv";
	if (kind == "DEPTH_STENCIL_VIEW")
		return "dsv";
	return kind.empty() ? "res" : kind.c_str();
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
	const wchar_t *ext = task.isTexture ? L"dds" : L"buf";
	wchar_t eventPrefix[32];
	FormatEventPrefix(task.binding.drawId, task.binding.dispatchId,
		eventPrefix, ARRAYSIZE(eventPrefix));
	wchar_t shaderPart[96];
	BuildShaderHashNamePart(task.binding.shaderInfo, shaderPart, ARRAYSIZE(shaderPart));
	swprintf_s(fileName, fileNameCount,
		L"%s-pso%llu%s-%S-r%u-d%llu-%S_%p_o%llu_b%llu.%s",
		eventPrefix,
		static_cast<unsigned long long>(task.binding.psoIndex),
		shaderPart,
		ShortBindSpace(task.binding.bindSpace),
		task.binding.rootParameterIndex,
		static_cast<unsigned long long>(task.binding.descriptorIndex),
		ShortDescriptorKind(task.binding.descriptor.kind),
		task.binding.descriptor.resource,
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
		"call_index,type,pso,cs,kind,bind,root,range,reg,space,resource,file,file_hash,hunt_hash,semantic_file,offset,size,stride,format,status,note\n");
	for (const DumpedTaskRow &row : rows) {
		if (row.sourceKind != DumpTaskSource::Descriptor)
			continue;
		const DX12FrameResourceBinding &binding = row.binding;
		const DX12DescriptorSummary &descriptor = binding.descriptor;
		const char *kind = BindingKindName(binding);
		char cs[32] = "-";
		if (binding.shaderInfo.hasCS)
			sprintf_s(cs, sizeof(cs), "%016llx",
				static_cast<unsigned long long>(binding.shaderInfo.cs));
		char fileHash[16];
		FileHashFromPath(row.dedupePath, fileHash, ARRAYSIZE(fileHash));
		const uint32_t huntHash = DescriptorHuntHash(binding, row.copyBytes);
		fprintf(file,
			"%llu,%s,%llu,%s,%s,%s,%u,%u,%u,%u,%p,%S,%s,%08x,%S,%llu,%llu,%u,%u,%s,%s\n",
			static_cast<unsigned long long>(binding.eventSerial),
			binding.dispatchId ? "dispatch" : "draw",
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
			row.status.c_str(),
			row.note.c_str());
	}
	fclose(file);
	char fields[512] = "";
	DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
	DX12JsonAppendRawField(fields, sizeof(fields), "rows", std::to_string(rows.size()).c_str());
	DX12FrameAnalysisLogJsonFunc("ResourceBindingsCsvWritten", "%s", fields + 1);
}

static bool ExecuteCopyBatch(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks)
{
	if (!device || !queue || !tasks || tasks->empty())
		return false;

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
			"\"hr\":\"0x%lx\",\"tasks\":%zu",
			hr, tasks->size());
		if (commandList)
			commandList->Release();
		if (allocator)
			allocator->Release();
		return false;
	}

	for (DumpTask &task : *tasks) {
		if (!task.ready || !task.readback)
			continue;
		__try {
			if (task.needsBarrier)
				WriteResourceBarrier(commandList, task.source, task.sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);

			if (task.isTexture) {
				D3D12_TEXTURE_COPY_LOCATION src = {};
				src.pResource = task.source;
				src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				src.SubresourceIndex = 0;
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = task.readback;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dst.PlacedFootprint = task.textureLayout;
				dst.PlacedFootprint.Offset = 0;
				commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			} else {
				commandList->CopyBufferRegion(task.readback, 0, task.source,
					task.sourceOffset, task.copyBytes);
			}

			if (task.needsBarrier)
				WriteResourceBarrier(commandList, task.source, D3D12_RESOURCE_STATE_COPY_SOURCE, task.sourceState);
			task.copied = true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			task.copied = false;
			task.skipNote = "copy_record_exception";
			DX12FrameAnalysisLogJsonFunc("ResourceCopyTaskSkipped",
				"\"exception\":\"0x%lx\",\"source\":\"%p\",\"texture\":%u,\"offset\":%llu,\"bytes\":%llu,\"state\":\"0x%x\"",
				GetExceptionCode(), task.source, task.isTexture ? 1 : 0,
				static_cast<unsigned long long>(task.sourceOffset),
				static_cast<unsigned long long>(task.copyBytes),
				static_cast<UINT>(task.sourceState));
		}
	}

	hr = commandList->Close();
	if (FAILED(hr)) {
		DX12FrameAnalysisLogJsonFunc("ResourceCopyBatchCloseFailed",
			"\"hr\":\"0x%lx\",\"tasks\":%zu",
			hr, tasks->size());
		commandList->Release();
		allocator->Release();
		return false;
	}
	ID3D12CommandList *lists[] = { commandList };
	if (tasks->size() > 1)
		DX12FrameAnalysisLogJsonFunc("ResourceCopyBatchExecuting",
			"\"tasks\":%zu", tasks->size());
	bool ok = false;
	__try {
		queue->ExecuteCommandLists(1, lists);
		ok = WaitForFence(queue, device);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12FrameAnalysisLogJsonFunc("ResourceCopyBatchExecuteFailed",
			"\"exception\":\"0x%lx\",\"tasks\":%zu",
			GetExceptionCode(), tasks->size());
		ok = false;
	}

	commandList->Release();
	allocator->Release();
	return ok;
}

static bool ExecuteCopyTask(ID3D12Device *device, ID3D12CommandQueue *queue, DumpTask *task)
{
	if (!device || !queue || !task || !task->ready || !task->readback)
		return false;

	task->copied = false;
	std::vector<DumpTask> singleTask;
	singleTask.push_back(std::move(*task));
	const bool ok = ExecuteCopyBatch(device, queue, &singleTask);
	*task = std::move(singleTask[0]);
	return ok && task->copied;
}

static size_t ExecuteCopyTasksWithFallback(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks, bool *batchOkOut)
{
	if (batchOkOut)
		*batchOkOut = false;
	if (!device || !queue || !tasks || tasks->empty())
		return 0;

	const bool batchOk = ExecuteCopyBatch(device, queue, tasks);
	if (batchOk) {
		size_t copied = 0;
		for (const DumpTask &task : *tasks) {
			if (task.copied)
				copied++;
		}
		if (batchOkOut)
			*batchOkOut = true;
		return copied;
	}

	DX12FrameAnalysisLogJsonFunc("ResourceCopyBatchFallback",
		"\"tasks\":%zu",
		tasks->size());
	size_t copied = 0;
	size_t failed = 0;
	bool stopped = false;
	for (DumpTask &task : *tasks) {
		if (!task.ready || !task.readback)
			continue;
		if (FAILED(device->GetDeviceRemovedReason())) {
			DX12FrameAnalysisLogJsonFunc("ResourceCopyFallbackStopped",
				"\"reason\":\"device_removed\",\"copied\":%zu,\"tasks\":%zu",
				copied, tasks->size());
			stopped = true;
			break;
		}
		if (ExecuteCopyTask(device, queue, &task)) {
			copied++;
			continue;
		}
		failed++;
		task.copied = false;
		if (!task.skipNote)
			task.skipNote = "copy_failed";
	}
	DX12FrameAnalysisLogJsonFunc("ResourceCopyFallbackComplete",
		"\"tasks\":%zu,\"copied\":%zu,\"failed\":%zu,\"stopped\":%s",
		tasks->size(), copied, failed, stopped ? "true" : "false");
	return copied;
}

static size_t ExecuteReadbackCopyTasks(
	ID3D12Device *device, ID3D12CommandQueue *queue, std::vector<DumpTask> *tasks, bool *batchOkOut)
{
	if (batchOkOut)
		*batchOkOut = true;
	if (!tasks)
		return 0;

	std::vector<size_t> readbackIndexes;
	readbackIndexes.reserve(tasks->size());
	std::vector<DumpTask> readbackTasks;
	for (size_t i = 0; i < tasks->size(); ++i) {
		DumpTask &task = (*tasks)[i];
		if (task.directMap || !task.ready || !task.readback)
			continue;
		readbackIndexes.push_back(i);
		readbackTasks.push_back(std::move(task));
	}

	if (readbackTasks.empty())
		return CountCopiedTasks(*tasks);

	bool batchOk = false;
	ExecuteCopyTasksWithFallback(device, queue, &readbackTasks, &batchOk);
	if (batchOkOut)
		*batchOkOut = batchOk;
	for (size_t i = 0; i < readbackTasks.size(); ++i)
		(*tasks)[readbackIndexes[i]] = std::move(readbackTasks[i]);
	return CountCopiedTasks(*tasks);
}

void DX12DumpCurrentFrameResources(const wchar_t *dir)
{
	if (!dir)
		return;

	std::vector<DX12FrameResourceBinding> bindings;
	DX12GetCurrentFrameResourceBindings(&bindings);
	std::vector<DX12FrameIaBufferBinding> iaBuffers;
	DX12GetCurrentFrameIaBuffers(&iaBuffers);

	wchar_t resourceDir[MAX_PATH];
	wcsncpy_s(resourceDir, dir, _TRUNCATE);
	wchar_t dedupeDir[MAX_PATH];
	GetDedupedDirectory(dir, dedupeDir, ARRAYSIZE(dedupeDir));

	const UINT64 frameReadbackBudget = FrameReadbackBudgetBytes();
	UINT64 frameReadbackBytes = 0;
	DX12FrameAnalysisLogJsonFunc("ResourceDump",
		"\"bindings\":%zu,\"ia\":%zu,\"maxBytes\":%llu,\"frameReadbackBudget\":%llu,\"unsafeCopy\":%u,\"gpuCopy\":%u,\"stateTracking\":1",
		bindings.size(), iaBuffers.size(), static_cast<unsigned long long>(MaxResourceDumpBytes),
		static_cast<unsigned long long>(frameReadbackBudget),
		UnsafeResourceCopyEnabled() ? 1 : 0,
		GpuResourceCopyEnabled() ? 1 : 0);

	ID3D12CommandQueue *queue = DX12AcquireCommandQueue();
	ID3D12Device *device = nullptr;
	if (!queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) || !device) {
		DX12FrameAnalysisLogJsonFunc("ResourceDump",
			"\"status\":\"failed\",\"reason\":\"missing_command_queue\"");
		if (queue)
			queue->Release();
		return;
	}

	std::vector<DumpTask> tasks;
	std::vector<DumpTask> duplicateTasks;
	std::vector<DumpTask> skippedTasks;
	std::unordered_map<std::string, size_t> canonicalTaskByKey;
	UINT duplicates = 0;

	for (const DX12FrameResourceBinding &binding : bindings) {
		if (!ShouldDumpBinding(binding))
			continue;
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
			sprintf_s(key, "%p|%llu|%llu",
				binding.descriptor.resource,
				static_cast<unsigned long long>(binding.descriptor.resourceOffset),
				static_cast<unsigned long long>(binding.descriptor.viewSize));
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
			wchar_t fileName[MAX_PATH];
			BuildFileName(duplicate, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.readback = nullptr;
			duplicate.ready = false;
			duplicate.copied = false;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (PrepareTask(device, binding, &task, &frameReadbackBytes, frameReadbackBudget)) {
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
			wchar_t fileName[MAX_PATH];
			DX12BuildIaBufferFileName(buffer, fileName, ARRAYSIZE(fileName));
			duplicate.fileName = fileName;
			duplicate.readback = nullptr;
			duplicate.ready = false;
			duplicate.copied = false;
			duplicate.skipNote = "already_dumped";
			duplicateTasks.push_back(std::move(duplicate));
			duplicates++;
			continue;
		}

		DumpTask task;
		if (PrepareIaBufferTask(device, buffer, &task, &frameReadbackBytes, frameReadbackBudget)) {
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

	bool readbackCopyOk = false;
	const size_t copiedTasks = ExecuteReadbackCopyTasks(device, queue, &tasks, &readbackCopyOk);
	DX12FrameAnalysisLogJsonFunc("ResourceCopyResult",
		"\"gpuCopy\":%u,\"readbackOk\":%u,\"copied\":%zu,\"tasks\":%zu,\"duplicates\":%u",
		GpuResourceCopyEnabled() ? 1 : 0,
		readbackCopyOk ? 1 : 0, copiedTasks, tasks.size(), duplicates);

	UINT dumpedTextures = 0;
	UINT dumpedBuffers = 0;
	UINT failed = 0;
	UINT linked = 0;
	std::unordered_map<std::string, std::wstring> dedupePathByKey;
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

		if (!ok)
			continue;

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
		(void)task;
		skipped++;
	}

	ReleaseTasks(&tasks);
	device->Release();
	queue->Release();
	WriteResourceBindingsCsv(dir, dumpedRows);

	DX12FrameAnalysisLogJsonFunc("ResourceSummary",
		"\"textures\":%u,\"buffers\":%u,\"linked\":%u,\"skipped\":%u,\"duplicates\":%u,\"failed\":%u,\"readbackBytes\":%llu,\"bindings\":%zu,\"ia\":%zu",
		dumpedTextures, dumpedBuffers, linked, skipped, duplicates, failed,
		static_cast<unsigned long long>(frameReadbackBytes),
		bindings.size(), iaBuffers.size());
}
