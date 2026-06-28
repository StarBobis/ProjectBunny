#pragma once

#include <d3d12.h>

#include <string>

#include "DX12BindingTracker.h"

bool DX12DumpIsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension);
bool DX12DumpShouldDumpBinding(const DX12FrameResourceBinding &binding);
const char *DX12DumpShortBindSpace(const std::string &bindSpace);
const char *DX12DumpShortDescriptorKind(const std::string &kind);
const char *DX12DumpShaderVisibilityPrefix(UINT shaderVisibility, const std::string &bindSpace);
char DX12DumpRegisterPrefix(UINT rangeType, const std::string &descriptorKind);
bool DX12DumpBuildSemanticSlot(
	const DX12FrameResourceBinding &binding, char *text, size_t textCount);
