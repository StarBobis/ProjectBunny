#pragma once

#include <d3d12.h>

#include <string>
#include <vector>

#include "DX12BindingTracker.h"

enum class DumpTaskSource
{
	Descriptor,
	InputAssembler,
	Output,
};

struct DumpTask
{
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

D3D12_RESOURCE_STATES DX12DumpGuessDescriptorSourceState(const DX12FrameResourceBinding &binding);
D3D12_RESOURCE_STATES DX12DumpGuessIaBufferSourceState(const DX12FrameIaBufferBinding &buffer);
bool DX12DumpGpuResourceCopyEnabled();

bool DX12DumpPrepareDescriptorTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameResourceBinding &binding, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes);
bool DX12DumpPrepareHuntingMetadataTextureTask(
	ID3D12Device *device, const DX12DescriptorSummary &descriptor, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes);
bool DX12DumpPrepareIaBufferTaskForCurrentMode(
	ID3D12Device *device, const DX12FrameIaBufferBinding &buffer, DumpTask *task,
	UINT64 *frameReadbackBytes, UINT64 frameReadbackBudgetBytes);
void DX12DumpReleaseTasks(std::vector<DumpTask> *tasks);
size_t DX12DumpCountCopiedTasks(const std::vector<DumpTask> &tasks);
