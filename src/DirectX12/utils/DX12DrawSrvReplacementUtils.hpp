#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <map>
#include <string>
#include <vector>

#include "DX12BindingTracker.hpp"
#include "DX12CommandListRuntime.hpp"
#include "DX12LogManager.hpp"
#include "DX12ModRuntime.hpp"
#include "DX12TextureOverrideMatch.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12RuntimeState.hpp"

class DX12DrawSrvPatchRequest
{
public:
	DX12CurrentShaderResourceBinding binding;
	DX12LoadedResource *resource = nullptr;
	bool compute = false;
	const char *source = "";
	D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = {};
	DXGI_FORMAT replacementViewFormat = DXGI_FORMAT_UNKNOWN;
};

class DX12DrawSrvPatchTable
{
public:
	bool compute = false;
	UINT rootParameterIndex = 0;
	SIZE_T cpuStart = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
	ID3D12DescriptorHeap *heap = nullptr;
	UINT increment = 0;
	UINT count = 0;
	UINT tempOffset = 0;
};

class DX12DrawSrvReplacementUtils
{
public:
	static bool ShaderVisibilityMatchesPixel(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_PIXEL;
	}

	static bool ShaderVisibilityMatchesVertex(UINT visibility, bool compute)
	{
		if (compute)
			return true;
		return visibility == D3D12_SHADER_VISIBILITY_ALL ||
			visibility == D3D12_SHADER_VISIBILITY_VERTEX;
	}

	static bool BindingCanBePatched(const DX12CurrentShaderResourceBinding &binding)
	{
		return binding.hasDescriptor &&
			!binding.rootDescriptor &&
			binding.tableCpuStart != 0 &&
			binding.tableGpuStart.ptr != 0 &&
			binding.descriptorIncrement != 0 &&
			binding.tableHeap != nullptr &&
			binding.descriptor.kind == "SRV";
	}

	static void CollectSrvBindings(
		ID3D12GraphicsCommandList *commandList,
		bool compute,
		std::vector<DX12CurrentShaderResourceBinding> *bindings)
	{
		if (!bindings)
			return;
		bindings->clear();
		if (!commandList)
			return;
		DX12BindingTracker::GetCurrentShaderResourceBindings(
			commandList, compute, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bindings);
#if defined(_DEBUG)
		DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
			"stage=collect_srvs,bindSpace=%s,count=%zu,present=%ld",
			compute ? "compute" : "graphics",
			bindings->size(),
			DX12RuntimeState::GetPresentCount());
#endif
	}

	static uint64_t BindingKey(const DX12CurrentShaderResourceBinding &binding, bool compute)
	{
		uint64_t key = compute ? 0x9ae16a3b2f90404full : 0xc3a5c85c97cb3127ull;
		key ^= static_cast<uint64_t>(binding.rootParameterIndex) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.descriptorOffset) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		key ^= static_cast<uint64_t>(binding.tableGpuStart.ptr) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
		return key;
	}

	static void AddOrReplaceRequest(
		ID3D12Device *device,
		const DX12CurrentShaderResourceBinding &binding,
		DX12LoadedResource *resource,
		bool compute,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!requests || !resource || !resource->drawSrvCpu.ptr)
			return;
		if (!DX12DrawSrvReplacementUtils::BindingCanBePatched(binding))
			return;
		// Use the DDS default SRV directly. This matches DX11 behavior:
		// DirectX::CreateDDSTextureFromFileEx creates the replacement SRV
		// with DEFAULT component mapping, and PSSetShaderResources uses it
		// as-is without any "compatibility" conversion.
		D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = resource->drawSrvCpu;
		DXGI_FORMAT replacementViewFormat = resource->drawSrvFormat;
		const uint64_t key = DX12DrawSrvReplacementUtils::BindingKey(binding, compute);
		for (DX12DrawSrvPatchRequest &request : *requests) {
			if (DX12DrawSrvReplacementUtils::BindingKey(request.binding, request.compute) == key) {
				request.resource = resource;
				request.source = source ? source : "";
				request.replacementCpu = replacementCpu;
				request.replacementViewFormat = replacementViewFormat;
				return;
			}
		}
		DX12DrawSrvPatchRequest request;
		request.binding = binding;
		request.resource = resource;
		request.compute = compute;
		request.source = source ? source : "";
		request.replacementCpu = replacementCpu;
		request.replacementViewFormat = replacementViewFormat;
		requests->push_back(request);
	}

	static UINT AddSlotRequestsFromBindings(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12CurrentShaderResourceBinding> &bindings,
		bool compute,
		UINT slot,
		const std::wstring &resourceName,
		bool pixelStage,
		const char *source,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		UINT added = 0;
		DX12LoadedResource *resource = nullptr;
		for (const DX12CurrentShaderResourceBinding &binding : bindings) {
			if (binding.shaderRegister != slot || binding.registerSpace != 0)
				continue;
			const bool visibilityMatches = pixelStage ?
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesPixel(binding.shaderVisibility, compute) :
				DX12DrawSrvReplacementUtils::ShaderVisibilityMatchesVertex(binding.shaderVisibility, compute);
			if (!visibilityMatches)
				continue;
			if (!resource)
				resource = DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
					device, commandList, resourceName);
			if (!resource)
				return added;
			DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
				device, binding, resource, compute, source, requests);
			++added;
		}
		return added;
	}

	static UINT AddThisRequestFromMatch(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const DX12TextureOverrideMatch *match,
		const std::wstring &resourceName,
		std::vector<DX12DrawSrvPatchRequest> *requests)
	{
		if (!device || !commandList || !requests || resourceName.empty())
			return 0;
		if (!match || !match->hasThisSrvTarget) {
			DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
				"stage=slot_patch_fail,source=this,reason=no_this_target,resource=%S",
				resourceName.c_str());
			return 0;
		}
		DX12LoadedResource *resource =
			DX12DdsTextureLoadUtils::EnsureLoadedDrawTextureSrv(
				device, commandList, resourceName);
		if (!resource)
			return 0;
		DX12DrawSrvReplacementUtils::AddOrReplaceRequest(
			device, match->thisSrvBinding, resource, match->compute, "this", requests);
		return 1;
	}

	// Write replacement SRVs directly into the game's shader-visible heap.
	// NO save, NO restore — exactly like DX11 PSSetShaderResources.
	// The game's next SetGraphicsRootDescriptorTable call naturally
	// overwrites our modification with the original descriptor.
	static bool ApplyRequests(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const std::vector<DX12DrawSrvPatchRequest> &requests,
		DX12ModIaReplacement *replacement)
	{
		if (!device || !commandList || !replacement || requests.empty())
			return false;

		for (const DX12DrawSrvPatchRequest &request : requests) {
			if (!request.resource)
				continue;

			D3D12_CPU_DESCRIPTOR_HANDLE replacementCpu = request.replacementCpu.ptr ?
				request.replacementCpu : request.resource->drawSrvCpu;
			if (!replacementCpu.ptr)
				continue;

			D3D12_CPU_DESCRIPTOR_HANDLE targetCpu = {};
			targetCpu.ptr = request.binding.tableCpuStart +
				static_cast<SIZE_T>(request.binding.descriptorOffset) *
				request.binding.descriptorIncrement;
			if (!targetCpu.ptr)
				continue;

			device->CopyDescriptorsSimple(
				1, targetCpu, replacementCpu,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

#if defined(_DEBUG)
			DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
				"stage=slot_patched,source=%s,bindSpace=%s,slot=%u,root=%u,offset=%u,"
				"resource=%S,resourceFormat=%u,originalViewFormat=%u,"
				"replacementViewFormat=%u,originalViewDimension=%u,"
				"hasSrvDesc=%s,hasResourceDesc=%s,"
				"resourceDescFormat=%u,resourceDescDimension=%u,resourceDescMips=%u,"
				"originalMipStart=%u,originalMipLevels=%u,"
				"componentMapping=%u,currentState=%u,cpu=0x%zx",
				request.source ? request.source : "",
				request.compute ? "compute" : "graphics",
				request.binding.shaderRegister,
				request.binding.rootParameterIndex,
				request.binding.descriptorOffset,
				request.resource->name.c_str(),
				static_cast<UINT>(request.resource->drawSrvFormat),
				request.binding.descriptor.viewFormat,
				static_cast<UINT>(request.replacementViewFormat),
				request.binding.descriptor.viewDimension,
				request.binding.descriptor.hasSrvDesc ? "true" : "false",
				request.binding.descriptor.hasResourceDesc ? "true" : "false",
				request.binding.descriptor.hasResourceDesc ?
					static_cast<UINT>(request.binding.descriptor.resourceDesc.Format) : 0,
				request.binding.descriptor.hasResourceDesc ?
					static_cast<UINT>(request.binding.descriptor.resourceDesc.Dimension) : 0,
				request.binding.descriptor.hasResourceDesc ?
					static_cast<UINT>(request.binding.descriptor.resourceDesc.MipLevels) : 0,
				request.binding.descriptor.hasSrvDesc ?
					request.binding.descriptor.srvDesc.Texture2D.MostDetailedMip : 0,
				request.binding.descriptor.hasSrvDesc ?
					request.binding.descriptor.srvDesc.Texture2D.MipLevels : 0,
				request.binding.descriptor.hasSrvDesc ?
					request.binding.descriptor.srvDesc.Shader4ComponentMapping : 0,
				request.binding.descriptor.currentState,
				targetCpu.ptr);
#endif
		}

#if defined(_DEBUG)
		DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
			"stage=descriptors_patched,requests=%zu,present=%ld",
			requests.size(),
			DX12RuntimeState::GetPresentCount());
#endif
		return true;
	}

	static bool ApplyConfig(
		ID3D12Device *device,
		ID3D12GraphicsCommandList *commandList,
		const Bunny::TextureOverrideConfig &config,
		const DX12TextureOverrideMatch *match,
		DX12ModIaReplacement *replacement)
	{
		if (!device || !commandList || !replacement)
			return false;
		if (config.drawPsSrvResources.empty() &&
		    config.drawVsSrvResources.empty() &&
		    config.drawReplaceThisResource.empty())
			return false;

		std::vector<DX12CurrentShaderResourceBinding> graphicsSrvs;
		DX12DrawSrvReplacementUtils::CollectSrvBindings(commandList, false, &graphicsSrvs);

		std::vector<DX12DrawSrvPatchRequest> requests;
		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawPsSrvResources.begin();
		     it != config.drawPsSrvResources.end(); ++it) {
			UINT added = DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
				device, commandList, graphicsSrvs, false, it->first, it->second,
				true, "ps-t", &requests);
			if (!added) {
				DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=ps-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(), graphicsSrvs.size());
			}
		}

		for (std::map<uint32_t, std::wstring>::const_iterator it = config.drawVsSrvResources.begin();
		     it != config.drawVsSrvResources.end(); ++it) {
			UINT added = DX12DrawSrvReplacementUtils::AddSlotRequestsFromBindings(
				device, commandList, graphicsSrvs, false, it->first, it->second,
				false, "vs-t", &requests);
			if (!added) {
				DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=vs-t,reason=graphics_binding_missing,slot=%u,resource=%S,graphicsCount=%zu",
					it->first, it->second.c_str(), graphicsSrvs.size());
			}
		}

		if (!config.drawReplaceThisResource.empty()) {
			UINT added = DX12DrawSrvReplacementUtils::AddThisRequestFromMatch(
				device, commandList, match,
				config.drawReplaceThisResource, &requests);
			if (!added) {
				DX12LogManager::JsonFunc("DX12DrawSrvReplacement",
					"stage=slot_patch_fail,source=this,reason=no_matched_target,hash=%08x,resource=%S,graphicsCount=%zu",
					static_cast<uint32_t>(config.hash),
					config.drawReplaceThisResource.c_str(), graphicsSrvs.size());
			}
		}

		return DX12DrawSrvReplacementUtils::ApplyRequests(
			device, commandList, requests, replacement);
	}

	// No-op: we no longer save/restore. The game's next
	// SetGraphicsRootDescriptorTable naturally overwrites our patch.
	static void Restore(
		ID3D12GraphicsCommandList *commandList,
		DX12ModIaReplacement *replacement)
	{
		(void)commandList;
		(void)replacement;
	}
};
