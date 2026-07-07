#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <string>
#include <vector>

class DX12PsoInputElementInfo
{
public:
	std::string semanticName;
	UINT semanticIndex = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	UINT inputSlot = 0;
	UINT alignedByteOffset = 0;
	bool appendAlignedElement = false;
	UINT resolvedByteOffset = 0;
	D3D12_INPUT_CLASSIFICATION inputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	UINT instanceDataStepRate = 0;
};

class DX12PsoInputLayoutInfo
{
public:
	UINT64 psoIndex = 0;
	std::vector<DX12PsoInputElementInfo> elements;
};

class DX12PsoShaderInfo
{
public:
	UINT64 psoIndex = 0;
	bool hasVS = false;
	bool hasPS = false;
	bool hasCS = false;
	UINT64 vs = 0;
	UINT64 ps = 0;
	UINT64 cs = 0;
	std::string vsModel;
	std::string psModel;
	std::string csModel;
};

class DX12PsoShaderSummary
{
public:
	UINT64 psoIndex = 0;
	bool hasVS = false;
	bool hasPS = false;
	bool hasCS = false;
	UINT64 vs = 0;
	UINT64 ps = 0;
	UINT64 cs = 0;
	std::string vsModel;
	std::string psModel;
	std::string csModel;
};
