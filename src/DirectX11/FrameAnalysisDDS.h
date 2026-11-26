#pragma once

#include <d3d11.h>
#include <string>

// On success, path names the file to link into the frame dump.
HRESULT SaveFrameAnalysisDDS(ID3D11DeviceContext* context, ID3D11Texture2D* texture,
    bool all_mips, bool deduplicate, std::wstring& path);
