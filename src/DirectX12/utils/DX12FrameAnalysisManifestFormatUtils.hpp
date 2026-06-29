#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

class DX12FrameAnalysisManifestFormatUtils
{
public:
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

	static const char *TopologyName(D3D12_PRIMITIVE_TOPOLOGY topology)
	{
		switch (topology) {
		case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:
			return "POINTLIST";
		case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
			return "LINELIST";
		case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
			return "LINESTRIP";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
			return "TRIANGLELIST";
		case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
			return "TRIANGLESTRIP";
		default:
			return "UNKNOWN";
		}
	}

	static const char *DxgiFormatName(UINT format)
	{
		switch (format) {
		case DXGI_FORMAT_UNKNOWN:
			return "DXGI_FORMAT_UNKNOWN";
		case DXGI_FORMAT_R16_UINT:
			return "DXGI_FORMAT_R16_UINT";
		case DXGI_FORMAT_R32_UINT:
			return "DXGI_FORMAT_R32_UINT";
		case DXGI_FORMAT_R32_FLOAT:
			return "DXGI_FORMAT_R32_FLOAT";
		case DXGI_FORMAT_R32G32_FLOAT:
			return "DXGI_FORMAT_R32G32_FLOAT";
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return "DXGI_FORMAT_R32G32B32_FLOAT";
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return "DXGI_FORMAT_R32G32B32A32_FLOAT";
		case DXGI_FORMAT_R16G16_FLOAT:
			return "DXGI_FORMAT_R16G16_FLOAT";
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return "DXGI_FORMAT_R16G16B16A16_FLOAT";
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return "DXGI_FORMAT_R8G8B8A8_UNORM";
		case DXGI_FORMAT_BC1_UNORM:
			return "DXGI_FORMAT_BC1_UNORM";
		case DXGI_FORMAT_BC2_UNORM:
			return "DXGI_FORMAT_BC2_UNORM";
		case DXGI_FORMAT_BC3_UNORM:
			return "DXGI_FORMAT_BC3_UNORM";
		case DXGI_FORMAT_BC4_UNORM:
			return "DXGI_FORMAT_BC4_UNORM";
		case DXGI_FORMAT_BC5_UNORM:
			return "DXGI_FORMAT_BC5_UNORM";
		case DXGI_FORMAT_BC6H_UF16:
			return "DXGI_FORMAT_BC6H_UF16";
		case DXGI_FORMAT_BC7_UNORM:
			return "DXGI_FORMAT_BC7_UNORM";
		default:
			return "DXGI_FORMAT_OTHER";
		}
	}

	static void FormatShaderHash(UINT64 hash, bool hasHash, char *text, size_t textCount)
	{
		if (!text || textCount == 0)
			return;
		if (hasHash)
			sprintf_s(text, textCount, "%016llx", static_cast<unsigned long long>(hash));
		else
			sprintf_s(text, textCount, "-");
	}

	static void FormatTextPath(const wchar_t *filePath, wchar_t *textPath, size_t textPathCount)
	{
		if (!textPath || textPathCount == 0)
			return;
		textPath[0] = L'\0';
		if (!filePath || !filePath[0])
			return;
		wcsncpy_s(textPath, textPathCount, filePath, _TRUNCATE);
		wcscat_s(textPath, textPathCount, L".txt");
	}

	static void FormatFileHash(const wchar_t *filePath, char *hash, size_t hashCount)
	{
		if (!hash || hashCount == 0)
			return;
		hash[0] = '\0';
		if (!filePath || !filePath[0])
			return;

		const wchar_t *name = wcsrchr(filePath, L'\\');
		name = name ? name + 1 : filePath;
		size_t i = 0;
		for (; i < hashCount - 1 && i < 8 && name[i] && name[i] != L'-'; ++i)
			hash[i] = static_cast<char>(name[i]);
		hash[i] = '\0';
	}

	static const wchar_t *DedupedFileName(const wchar_t *filePath)
	{
		if (!filePath)
			return L"";
		const wchar_t *name = wcsrchr(filePath, L'\\');
		return name ? name + 1 : filePath;
	}

	static void FormatDedupedTextName(
		const wchar_t *filePath, wchar_t *textPath, size_t textPathCount)
	{
		if (!textPath || textPathCount == 0)
			return;
		textPath[0] = L'\0';
		wcsncpy_s(textPath, textPathCount, DedupedFileName(filePath), _TRUNCATE);
		if (textPath[0])
			wcscat_s(textPath, textPathCount, L".txt");
	}

	static const char *ApiFunctionName(const char *kind)
	{
		if (!kind)
			return "Unknown";
		if (!strcmp(kind, "draw_indexed"))
			return "DrawIndexedInstanced";
		if (!strcmp(kind, "draw"))
			return "DrawInstanced";
		if (!strcmp(kind, "dispatch"))
			return "Dispatch";
		return kind;
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
};
