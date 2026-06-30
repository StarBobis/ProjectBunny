#pragma once

#include <d3d12.h>

#include <string>
#include <vector>

#include "DX12BindingTracker.hpp"

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
	D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COMMON;
	bool needsBarrier = false;
	bool isTexture = false;
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

class DumpedTaskRow
{
public:
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

