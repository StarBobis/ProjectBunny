#pragma once

// Draw-time texture capture: "ResourceX = ref ps-tN" saves the game's currently
// bound texture into a placeholder resource so a later "ps-tN = ref ResourceX"
// rebind resolves onto the captured texture.
//
// This used to be a fragment spliced into ModRuntime/DX12ModRuntimeInternal.h at
// a fixed line, which meant it could only see the declarations that happened to
// precede that line. It now carries its own includes and can be read on its own.
#include "../Core/Diag/DX12Diag.h"
#include "../Core/ModState/DX12LoadedResourceState.h"
#include "DX12DrawTextureSrvViewUtils.h"


// Phase 3 capture mode: "ResourceX = ref ps-tN" (executed at draw time inside
// a TextureOverride/command-list action stream) saves the game's currently
// bound texture into the placeholder resource X so a later
// "ps-tN = ref ResourceX" rebind resolves through the alias store and the
// load throat onto the captured texture.
//
// Implementation follows DX11's "ref" semantics literally: keep a live
// reference to the game's resource (AddRef) and build our own SRV descriptor
// onto it with the game's exact view desc. No GPU copy, no barriers, no
// command-list involvement - the game's state bookkeeping is completely
// undisturbed, and queue order keeps the captured view valid for the draws
// that consume it. Holding the resource reference also keeps the underlying
// allocation alive for committed and placed resources alike, so a stale
// capture degrades to "reads whatever the game left there" instead of
// dangling memory. Recapture retires the previous reference/heap through the
// deferred-destruction path, so an older frame still in flight keeps valid
// descriptors.
class DX12DrawTextureCaptureUtils
{
public:
	static bool CaptureTextureToLoadedResource(
		ID3D12Device *device,
		const std::wstring &name,
		const DX12DescriptorSummary &source)
	{
		if (!device || name.empty() || !source.resource ||
		    !source.hasResourceDesc)
			return false;
		// Only texture SRVs are captureable; buffer/CBV targets have no
		// view contract this path can reproduce.
		if (source.resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			return false;
		const DXGI_FORMAT viewFormat =
			DX12DrawTextureSrvViewUtils::InferViewFormatFromDescriptor(source);
		if (viewFormat == DXGI_FORMAT_UNKNOWN)
			return false;

		DX12_LOCK_ACQUIRE_EXCLUSIVE(&gModLock, "Lock.modExclusive");
		DX12LoadedResource &entry = gLoadedResources[name];
		// Drop any previous contents (failed placeholder, earlier capture, or
		// a DDS load that lost the race); deferred release keeps descriptors
		// already copied into in-flight tables valid.
		ReleaseLoadedResourceObjects(&entry);
		entry = DX12LoadedResource();
		entry.name = name;
		entry.path = L"<captured>";
		entry.drawSrvWidth = static_cast<UINT>(source.resourceDesc.Width);
		entry.drawSrvHeight = source.resourceDesc.Height;
		entry.drawSrvMipLevels = source.resourceDesc.MipLevels;
		entry.drawSrvFormat = viewFormat;

		ID3D12DescriptorHeap *srvHeap = nullptr;
		if (!DX12DrawTextureSrvViewUtils::CreateSrvHeap(device, &srvHeap)) {
			ReleaseSRWLockExclusive(&gModLock);
			return false;
		}
		ID3D12Resource *texture = source.resource;
		texture->AddRef();
		entry.drawSrvTextureResource = texture;
		entry.drawSrvHeap = srvHeap;
		entry.drawSrvCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();

		// Reproduce the game's exact SRV view when the tracker observed the
		// descriptor write; otherwise fall back to a full-texture view.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		if (source.hasSrvDesc) {
			srvDesc = source.srvDesc;
		} else {
			srvDesc = DX12DrawTextureSrvViewUtils::BuildFallbackTexture2DSrvDesc(
				&entry, viewFormat);
		}
		DX12DrawTextureSrvViewUtils::CreateSrvFromDesc(
			device, &entry, srvDesc, entry.drawSrvCpu);
		entry.failed = false;
		ReleaseSRWLockExclusive(&gModLock);

		DX12_LOG("DX12ResourceAliasDiag",
			"event=capture_saved name=%S resource=%p format=%u width=%u height=%u mips=%u",
			name.c_str(), texture, static_cast<UINT>(viewFormat),
			entry.drawSrvWidth, entry.drawSrvHeight, entry.drawSrvMipLevels);
		return true;
	}
};
