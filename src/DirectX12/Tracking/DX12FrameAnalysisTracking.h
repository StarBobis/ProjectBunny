// Frame analysis, resource state tracker and tracking policy.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12DescriptorArena.h"

class DX12FrameAnalysis
{
public:
	static bool Begin()
	{
		return DX12FrameAnalysisState::Begin();
	}

	static void End()
	{
		DX12FrameAnalysisState::End();
	}

	static void RequestCapture()
	{
		DX12FrameAnalysisState::RequestCapture();
		DX12HotPathState::Update();
	}

	static void BeginCapture()
	{
		DX12FrameAnalysisState::BeginCapture();
		DX12HotPathState::Update();
	}

	static bool EndCapture()
	{
		return DX12FrameAnalysisState::EndCapture();
	}

	static bool IsActive()
	{
		return DX12FrameAnalysisState::IsActive();
	}

	static bool IsCaptureRequested()
	{
		return DX12FrameAnalysisState::IsCaptureRequested();
	}

	static bool IsCapturing()
	{
		return DX12FrameAnalysisState::IsCapturing();
	}

	static bool GetPath(wchar_t *path, size_t pathCount)
	{
		return DX12FrameAnalysisState::GetPath(path, pathCount);
	}


	static void LogInfo(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"LogInfo\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!IsActive())
			return;

		char funcJson[512];
		char extra[4096];
		char fields[4608];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
		extra[0] = '\0';

		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(extra, sizeof(extra), fmt, args);
			va_end(args);
		}

		if (extra[0])
			sprintf_s(fields, sizeof(fields), "\"func\":%s,%s", funcJson, extra);
		else
			sprintf_s(fields, sizeof(fields), "\"func\":%s", funcJson);
		LogJsonFields(fields);
	}

	static void LogJsonFields(const char *fields)
	{
		DX12FrameAnalysisState::LogJsonFields(fields);
	}
};


class DX12ResourceStateTracker
{
public:
	// Microsoft documents ResourceBarrier as the explicit D3D12 transition
	// mechanism. These inference helpers are only used when no observed state
	// is available from creation or barrier tracking.
	static D3D12_RESOURCE_STATES FromBarrierLayout(D3D12_BARRIER_LAYOUT layout)
	{
		switch (layout) {
		case D3D12_BARRIER_LAYOUT_COMMON:
			return D3D12_RESOURCE_STATE_COMMON;
		case D3D12_BARRIER_LAYOUT_GENERIC_READ:
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
			return D3D12_RESOURCE_STATE_DEPTH_READ;
		case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
			return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case D3D12_BARRIER_LAYOUT_COPY_DEST:
			return D3D12_RESOURCE_STATE_COPY_DEST;
		case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
			return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
			return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		default:
			return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	static D3D12_RESOURCE_STATES InferDescriptorSourceState(const DX12FrameResourceBinding &binding)
	{
		if (binding.descriptor.kind == DescriptorKind::CBV)
			return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		if (binding.descriptor.kind == DescriptorKind::UAV)
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (binding.descriptor.kind == DescriptorKind::RTV)
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (binding.descriptor.kind == DescriptorKind::DSV)
			return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	static D3D12_RESOURCE_STATES InferIaBufferSourceState(const DX12FrameIaBufferBinding &buffer)
	{
		if (buffer.role == IaBufferRole::IB)
			return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	}

	static bool HeapTypeAllowsDirectCopy(UINT heapType, bool hasHeapType)
	{
		return hasHeapType &&
			(heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK);
	}
};


struct PlacedResourceInfo
{
	bool hasPlacedHeap = false;
	UINT64 heapOffset = 0;
	UINT64 heapSize = 0;
};

bool DX12ModHasActiveShaderOverrides();
bool DX12ModHasActiveTextureOverrides();
bool DX12ModHasConfiguredTextureOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsShaderDescriptorTracking();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsBusy();
bool DX12HuntNeedsHotPath();

class DX12ResourceTrackingPolicyUtils
{
public:
	static bool NeedsFrameDumpTracking()
	{
		return DX12FrameAnalysisState::IsActive();
	}

	static bool NeedsShaderDumpTracking()
	{
		return DX12ShaderDumpIsBusy();
	}

	static bool NeedsDumpOrHuntingTracking()
	{
		return DX12HuntNeedsHotPath() ||
			NeedsFrameDumpTracking() ||
			NeedsShaderDumpTracking();
	}

	static bool NeedsIaContentTracking()
	{
		return DX12ModHasConfiguredTextureOverrides();
	}

	static bool NeedsFullDescriptorHistory()
	{
		return NeedsFrameDumpTracking();
	}

	static bool NeedsFullResourceMetadata()
	{
		return NeedsDumpOrHuntingTracking();
	}

	static bool NeedsTextureResourceTracking()
	{
		return true;
	}

};
