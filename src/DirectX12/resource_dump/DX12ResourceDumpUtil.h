#pragma once

#include <d3d12.h>

#include <string>

#include "DX12BindingTracker.h"

bool DX12DumpIsTextureResourceDimension(D3D12_RESOURCE_DIMENSION dimension);
bool DX12DumpShouldDumpBinding(const DX12FrameResourceBinding &binding);
const char *DX12DumpShortBindSpace(const std::string &bindSpace);
const char *DX12DumpShortDescriptorKind(const std::string &kind);
