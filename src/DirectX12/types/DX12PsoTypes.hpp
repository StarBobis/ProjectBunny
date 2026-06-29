#pragma once

#include <Windows.h>

#include <string>

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
