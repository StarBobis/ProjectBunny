#pragma once

#include "Mods/DX12ConfigModel.hpp"
#include "MigotoCommandVmUtils.hpp"

class DX12TextureOverrideMatch
{
public:
	const Bunny::TextureOverrideConfig *config = nullptr;
	bool hasThisSrvTarget = false;
	bool compute = false;
	DX12CurrentShaderResourceBinding thisSrvBinding;
	uint32_t matchedHash = 0;
	uint32_t resourceHash = 0;
	uint32_t descriptorHash = 0;
};




class DX12ExecuteIndirectRuntime
{
public:
	using Fn = DX12CommandListHookSignatures::ExecuteIndirect;

	class PreSkinProbe
	{
	public:
		std::vector<DX12CurrentComputeUavBinding> uavs;
		std::vector<DX12CurrentComputeUavBinding> srvs;
		std::vector<DX12CurrentComputeUavBinding> cbvs;
		std::vector<DX12CurrentRootConstants> rootConstants;
		bool bindingsFound = false;
		bool applied = false;
		uint64_t computeShaderHash = 0;
		UINT originalVertexCount = 0;
		UINT overrideVertexCount = 0;
		DX12PreSkinDispatchOverride dispatchOverride;
	};

	static void Execute(
		ID3D12GraphicsCommandList *commandList,
		ID3D12CommandSignature *commandSignature,
		UINT maxCommandCount,
		ID3D12Resource *argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource *countBuffer,
		UINT64 countBufferOffset,
		Fn originalExecuteIndirect)
	{
		PreSkinProbe &probe = ScratchProbe();
		PreparePreSkinProbe(commandList, &probe);

		if (originalExecuteIndirect) {
			originalExecuteIndirect(commandList, commandSignature, maxCommandCount,
				argumentBuffer, argumentBufferOffset, countBuffer, countBufferOffset);
		}

		FinishPreSkinProbe(commandList, probe);
	}

	static PreSkinProbe &ScratchProbe()
	{
		static thread_local PreSkinProbe probe;
		probe.uavs.clear();
		probe.srvs.clear();
		probe.cbvs.clear();
		probe.rootConstants.clear();
		probe.bindingsFound = false;
		probe.applied = false;
		probe.computeShaderHash = 0;
		probe.originalVertexCount = 0;
		probe.overrideVertexCount = 0;
		probe.dispatchOverride = DX12PreSkinDispatchOverride();
		return probe;
	}

	static void PreparePreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		PreSkinProbe *probe)
	{
		if (!probe || !DX12ModNeedsPreSkinningUavProbe())
			return;

		ID3D12PipelineState *pipelineState =
			DX12CommandListRuntime::GetPipelineState(commandList);
		if (!pipelineState) {
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no PSO from runtime state\n");
#endif
			return;
		}

		DX12PsoShaderInfo shaderInfo = {};
		if (!DX12GetPipelineStateShaderInfo(pipelineState, &shaderInfo)) {
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: GetShaderInfo failed\n");
#endif
			return;
		}
		if (!shaderInfo.hasCS) {
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: PSO is not compute\n");
#endif
			return;
		}

		probe->computeShaderHash = shaderInfo.cs;
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: compute PSO cs=%016llx\n",
			static_cast<unsigned long long>(probe->computeShaderHash));
#endif
		if (!DX12ModShouldTrackPreSkinBindingsForCs(probe->computeShaderHash)) {
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: CS not in pre-skin list\n");
#endif
			return;
		}

	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin CS match\n");
#endif
		probe->bindingsFound = DX12BindingTracker::GetCurrentComputeAll(
			commandList, &probe->uavs, &probe->srvs, &probe->cbvs, &probe->rootConstants);
		if (!probe->bindingsFound) {
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: no compute bindings found\n");
#endif
			return;
		}

	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12LogManager::Text(
			"DX12ExecuteIndirectRuntime: bindings found uavs=%zu srvs=%zu cbvs=%zu\n",
			probe->uavs.size(), probe->srvs.size(), probe->cbvs.size());
#endif
		probe->applied = DX12ModApplyPreSkinningUavReplacement(
			commandList, probe->computeShaderHash,
			probe->uavs, probe->srvs, probe->cbvs, probe->rootConstants,
			&probe->originalVertexCount, &probe->overrideVertexCount,
			&probe->dispatchOverride);
		if (probe->applied) {
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
			DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin applied origVtx=%u newVtx=%u\n",
				probe->originalVertexCount, probe->overrideVertexCount);
#endif
			return;
		}
	#if defined(_DEBUG) && defined(DX12_ENABLE_DETAILED_TIMING)
		DX12LogManager::Text("DX12ExecuteIndirectRuntime: pre-skin matching failed\n");
#endif
	}

	static void FinishPreSkinProbe(
		ID3D12GraphicsCommandList *commandList,
		const PreSkinProbe &probe)
	{
		(void)probe;
		DX12ModRestorePreSkinningUavReplacement(commandList);
	}
};


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

	static bool IsHashChar(wchar_t value)
	{
		return (value >= L'0' && value <= L'9') ||
			(value >= L'a' && value <= L'f') ||
			(value >= L'A' && value <= L'F');
	}

	static bool IsHashText(const wchar_t *text)
	{
		if (!text)
			return false;
		for (size_t i = 0; i < 8; ++i) {
			if (!IsHashChar(text[i]))
				return false;
		}
		return true;
	}

	static void CopyHashText(const wchar_t *source, char *hash, size_t hashCount)
	{
		if (!hash || hashCount == 0)
			return;
		hash[0] = '\0';
		if (!source)
			return;
		size_t i = 0;
		for (; i < hashCount - 1 && i < 8 && source[i]; ++i)
			hash[i] = static_cast<char>(source[i]);
		hash[i] = '\0';
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
		const wchar_t *hashMarker = nullptr;
		for (const wchar_t *cursor = wcsstr(name, L"-hash"); cursor;
				cursor = wcsstr(cursor + 1, L"-hash")) {
			const wchar_t *candidate = cursor + 5;
			if (IsHashText(candidate))
				hashMarker = candidate;
		}
		if (hashMarker) {
			CopyHashText(hashMarker, hash, hashCount);
			return;
		}

		if (IsHashText(name) && name[8] == L'_' &&
				IsHashText(name + 9) &&
				(name[17] == L'-' || name[17] == L'.' || name[17] == L'\0')) {
			CopyHashText(name + 9, hash, hashCount);
			return;
		}

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



class DX12FrameAnalysisManifest
{
public:
	static void WriteCall(
		const char *functionName, UINT64 eventSerial, UINT64 drawId, UINT64 dispatchId,
		ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState,
		const DX12PsoShaderInfo &shaderInfo, D3D12_PRIMITIVE_TOPOLOGY topology,
		UINT vertexCountPerInstance, UINT indexCountPerInstance, UINT startVertexLocation,
		UINT startIndexLocation, INT baseVertexLocation, UINT instanceCount,
		UINT startInstanceLocation, UINT threadGroupCountX, UINT threadGroupCountY,
		UINT threadGroupCountZ, bool indexBufferValid,
		D3D12_GPU_VIRTUAL_ADDRESS indexBufferGpuVa, UINT indexBufferSize,
		DXGI_FORMAT indexBufferFormat)
	{
		char vs[32], ps[32], cs[32];
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			shaderInfo.vs, shaderInfo.hasVS, vs, ARRAYSIZE(vs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			shaderInfo.ps, shaderInfo.hasPS, ps, ARRAYSIZE(ps));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			shaderInfo.cs, shaderInfo.hasCS, cs, ARRAYSIZE(cs));

		char functionJson[64], vsJson[64], psJson[64], csJson[64], topologyJson[32];
		char cmdlistJson[32], psoJson[32];
		DX12JsonUtils::EscapeString(functionJson, sizeof(functionJson),
			DX12FrameAnalysisManifestFormatUtils::ApiFunctionName(functionName));
		DX12JsonUtils::EscapeString(vsJson, sizeof(vsJson), vs);
		DX12JsonUtils::EscapeString(psJson, sizeof(psJson), ps);
		DX12JsonUtils::EscapeString(csJson, sizeof(csJson), cs);
		DX12JsonUtils::EscapeString(topologyJson, sizeof(topologyJson),
			DX12FrameAnalysisManifestFormatUtils::TopologyName(topology));
		sprintf_s(cmdlistJson, sizeof(cmdlistJson), "\"%p\"", commandList);
		sprintf_s(psoJson, sizeof(psoJson), "\"%p\"", pipelineState);

		char buffer[3072];
		if (dispatchId) {
			sprintf_s(buffer, sizeof(buffer),
				"\"func\":%s,"
				"\"type\":\"call.dispatch\","
				"\"call_index\":%llu,"
				"\"cmdlist\":%s,"
				"\"pipeline_state\":%s,"
				"\"pso\":%llu,"
				"\"cs\":%s,"
				"\"groups_x\":%u,"
				"\"groups_y\":%u,"
				"\"groups_z\":%u",
				functionJson,
				static_cast<unsigned long long>(eventSerial),
				cmdlistJson,
				psoJson,
				static_cast<unsigned long long>(shaderInfo.psoIndex),
				csJson,
				threadGroupCountX,
				threadGroupCountY,
				threadGroupCountZ);
		} else {
			sprintf_s(buffer, sizeof(buffer),
				"\"func\":%s,"
				"\"type\":\"call.draw\","
				"\"call_index\":%llu,"
				"\"draw_id\":%llu,"
				"\"cmdlist\":%s,"
				"\"pipeline_state\":%s,"
				"\"pso\":%llu,"
				"\"cs\":%s,"
				"\"vs\":%s,"
				"\"ps\":%s,"
				"\"topology\":%s,"
				"\"vertex_count\":%u,"
				"\"index_count\":%u,"
				"\"start_vertex\":%u,"
				"\"start_index\":%u,"
				"\"base_vertex\":%d,"
				"\"instance_count\":%u,"
				"\"start_instance\":%u,"
				"\"ib_gpu\":\"0x%llx\","
				"\"ib_bytes\":%u,"
				"\"ib_fmt\":%u",
				functionJson,
				static_cast<unsigned long long>(eventSerial),
				static_cast<unsigned long long>(drawId),
				cmdlistJson,
				psoJson,
				static_cast<unsigned long long>(shaderInfo.psoIndex),
				csJson,
				vsJson,
				psJson,
				topologyJson,
				vertexCountPerInstance,
				indexCountPerInstance,
				startVertexLocation,
				startIndexLocation,
				baseVertexLocation,
				instanceCount,
				startInstanceLocation,
				static_cast<unsigned long long>(indexBufferValid ? indexBufferGpuVa : 0),
				indexBufferValid ? indexBufferSize : 0,
				indexBufferValid ? static_cast<UINT>(indexBufferFormat) : 0);
		}

		DX12FrameAnalysis::LogJsonFields(buffer);
	}

	static void WriteFileDump(
		const wchar_t *filePath, bool isTexture, UINT64 bytes, const char *status,
		const char *note)
	{
		char hash[16];
		wchar_t textPath[MAX_PATH];
		DX12FrameAnalysisManifestFormatUtils::FormatFileHash(filePath, hash, ARRAYSIZE(hash));
		textPath[0] = L'\0';
		if (!isTexture) {
			const wchar_t *name = DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath);
			const wchar_t *dot = name ? wcsrchr(name, L'.') : nullptr;
			if (dot) {
				const size_t prefixLength = static_cast<size_t>(dot - name);
				wcsncpy_s(textPath, ARRAYSIZE(textPath), name, prefixLength);
			} else if (name) {
				wcsncpy_s(textPath, ARRAYSIZE(textPath), name, _TRUNCATE);
			}
			wcscat_s(textPath, ARRAYSIZE(textPath), L".txt");
		}

		char statusJson[64], kindJson[16], hashJson[32], noteJson[128];
		char fileJson[512], textJson[512];
		DX12JsonUtils::EscapeString(statusJson, sizeof(statusJson), status);
		DX12JsonUtils::EscapeString(kindJson, sizeof(kindJson), isTexture ? "texture" : "buffer");
		DX12JsonUtils::EscapeString(hashJson, sizeof(hashJson), hash);
		DX12JsonUtils::EscapeString(noteJson, sizeof(noteJson), note);
		DX12JsonUtils::EscapeWString(fileJson, sizeof(fileJson),
			DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath));
		if (isTexture) {
			strcpy_s(textJson, sizeof(textJson), "null");
		} else {
			DX12JsonUtils::EscapeWString(textJson, sizeof(textJson), textPath);
		}

		char buffer[1024];
		sprintf_s(buffer, sizeof(buffer),
			"\"func\":\"FileDump\","
			"\"status\":%s,"
			"\"kind\":%s,"
			"\"file\":%s,"
			"\"text\":%s,"
			"\"hash\":%s,"
			"\"bytes\":%llu,"
			"\"note\":%s",
			statusJson,
			kindJson,
			fileJson,
			textJson,
			hashJson,
			static_cast<unsigned long long>(bytes),
			noteJson);

		DX12FrameAnalysis::LogJsonFields(buffer);
	}

	static void WriteIaBinding(
		const DX12FrameIaBufferBinding &buffer,
		const D3D12_RESOURCE_DESC &desc, UINT64 sourceOffset, UINT64 copyBytes,
		D3D12_RESOURCE_STATES sourceState, bool hasCurrentState,
		const wchar_t *filePath)
	{
		char vs[32], ps[32], cs[32], hash[16];
		char huntHash[16];
		char fileHash[16];
		char producerCs[32];
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.shaderInfo.vs, buffer.shaderInfo.hasVS, vs, ARRAYSIZE(vs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.shaderInfo.ps, buffer.shaderInfo.hasPS, ps, ARRAYSIZE(ps));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.shaderInfo.cs, buffer.shaderInfo.hasCS, cs, ARRAYSIZE(cs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
			buffer.producerShaderInfo.cs, buffer.producerShaderInfo.hasCS,
			producerCs, ARRAYSIZE(producerCs));
		if (!buffer.producerShaderInfo.hasCS && buffer.producerPsoIndex) {
			DX12PsoShaderSummary producerSummary = {};
			if (DX12GetPsoShaderSummary(buffer.producerPsoIndex, &producerSummary)) {
				DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(
					producerSummary.cs, producerSummary.hasCS,
					producerCs, ARRAYSIZE(producerCs));
			}
		}
		DX12FrameAnalysisManifestFormatUtils::FormatFileHash(
			filePath, fileHash, ARRAYSIZE(fileHash));
		sprintf_s(huntHash, sizeof(huntHash), "%08x", buffer.huntHash);
		sprintf_s(hash, sizeof(hash), "%08x", buffer.huntHash);

		char vsJson[64], psJson[64], csJson[64], producerCsJson[64];
		char roleJson[32], dimJson[32], fmtNameJson[64], skinSourceJson[64];
		char producerBindJson[64], hashJson[32], huntHashJson[32], fileHashJson[32];
		char fileJson[512], resourceJson[32];
		DX12JsonUtils::EscapeString(vsJson, sizeof(vsJson), vs);
		DX12JsonUtils::EscapeString(psJson, sizeof(psJson), ps);
		DX12JsonUtils::EscapeString(csJson, sizeof(csJson), cs);
		DX12JsonUtils::EscapeString(producerCsJson, sizeof(producerCsJson), producerCs);
		DX12JsonUtils::EscapeString(roleJson, sizeof(roleJson), buffer.role.c_str());
		DX12JsonUtils::EscapeString(dimJson, sizeof(dimJson),
			DX12FrameAnalysisManifestFormatUtils::ResourceDimensionName(desc.Dimension));
		DX12JsonUtils::EscapeString(fmtNameJson, sizeof(fmtNameJson),
			DX12FrameAnalysisManifestFormatUtils::DxgiFormatName(buffer.format));
		DX12JsonUtils::EscapeString(skinSourceJson, sizeof(skinSourceJson),
			buffer.skinningClass.empty() ? "unknown" : buffer.skinningClass.c_str());
		DX12JsonUtils::EscapeString(producerBindJson, sizeof(producerBindJson),
			buffer.producerBindSpace.empty() ? "-" : buffer.producerBindSpace.c_str());
		DX12JsonUtils::EscapeString(hashJson, sizeof(hashJson), hash);
		DX12JsonUtils::EscapeString(huntHashJson, sizeof(huntHashJson), huntHash);
		DX12JsonUtils::EscapeString(fileHashJson, sizeof(fileHashJson), fileHash);
		DX12JsonUtils::EscapeWString(fileJson, sizeof(fileJson),
			DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath));
		sprintf_s(resourceJson, sizeof(resourceJson), "\"%p\"", buffer.resource.resource);

		char buffer2[3072];
		const bool ib = buffer.role == "IB";
		sprintf_s(buffer2, sizeof(buffer2),
			"\"func\":\"BindIA\","
			"\"call_index\":%llu,"
			"\"pso\":%llu,"
			"\"vs\":%s,"
			"\"ps\":%s,"
			"\"cs\":%s,"
			"\"role\":%s,"
			"\"slot\":%u,"
			"\"resource\":%s,"
			"\"dim\":%s,"
			"\"gpu\":\"0x%llx\","
			"\"offset\":%llu,"
			"\"bytes\":%llu,"
			"\"file_offset\":%llu,"
			"\"file_bytes\":%llu,"
			"\"view_gpu\":\"0x%llx\","
			"\"view_offset\":%llu,"
			"\"view_bytes\":%llu,"
			"\"draw_gpu\":\"0x%llx\","
			"\"draw_offset\":%llu,"
			"\"draw_bytes\":%llu,"
			"\"dump_scope\":\"%s\","
			"\"dump_is_draw_slice\":%s,"
			"\"stride\":%u,"
			"\"fmt\":%u,"
			"\"fmt_name\":%s,"
			"\"start_index\":%u,"
			"\"index_count\":%u,"
			"\"base_vertex\":%d,"
			"\"state\":\"0x%x\","
			"\"state_known\":%s,"
			"\"skin_source\":%s,"
			"\"producer_call_index\":%llu,"
			"\"producer_pso\":%llu,"
			"\"producer_cs\":%s,"
			"\"producer_bind\":%s,"
			"\"producer_root\":%u,"
			"\"producer_reg\":%u,"
			"\"file\":%s,"
			"\"hunt_hash\":%s,"
			"\"hash\":%s,"
			"\"file_hash\":%s",
			static_cast<unsigned long long>(buffer.eventSerial),
			static_cast<unsigned long long>(buffer.psoIndex),
			vsJson,
			psJson,
			csJson,
			roleJson,
			buffer.slot,
			resourceJson,
			dimJson,
			static_cast<unsigned long long>(buffer.gpuVa),
			static_cast<unsigned long long>(buffer.viewOffset),
			static_cast<unsigned long long>(buffer.viewSize),
			static_cast<unsigned long long>(sourceOffset),
			static_cast<unsigned long long>(copyBytes),
			static_cast<unsigned long long>(buffer.viewGpuVa),
			static_cast<unsigned long long>(buffer.viewOffset),
			static_cast<unsigned long long>(buffer.viewSize),
			static_cast<unsigned long long>(buffer.drawGpuVa ? buffer.drawGpuVa : buffer.viewGpuVa),
			static_cast<unsigned long long>(buffer.drawOffset),
			static_cast<unsigned long long>(buffer.drawSize ? buffer.drawSize : copyBytes),
			"full_view",
			"false",
			buffer.stride,
			buffer.format,
			fmtNameJson,
			buffer.drawStartIndex,
			buffer.drawIndexCount,
			buffer.drawBaseVertex,
			static_cast<UINT>(sourceState),
			hasCurrentState ? "true" : "false",
			skinSourceJson,
			static_cast<unsigned long long>(buffer.producerEventSerial),
			static_cast<unsigned long long>(buffer.producerPsoIndex),
			producerCsJson,
			producerBindJson,
			buffer.producerRootParameterIndex,
			buffer.producerShaderRegister,
			fileJson,
			huntHashJson,
			hashJson,
			fileHashJson);

		DX12FrameAnalysis::LogJsonFields(buffer2);
	}

	static void WriteResourceBinding(
		const DX12FrameResourceBinding &binding,
		const D3D12_RESOURCE_DESC &desc, UINT64 sourceOffset, UINT64 copyBytes,
		D3D12_RESOURCE_STATES sourceState, bool hasCurrentState,
		const wchar_t *filePath, const char *status = "", const char *note = "")
	{
		DX12PsoShaderSummary shaders;
		const bool hasShaders = DX12GetPsoShaderSummary(binding.psoIndex, &shaders);
		char vs[32], ps[32], cs[32], hash[16];
		char fileHash[16];
		const bool hasVS = binding.shaderInfo.hasVS || (hasShaders && shaders.hasVS);
		const bool hasPS = binding.shaderInfo.hasPS || (hasShaders && shaders.hasPS);
		const bool hasCS = binding.shaderInfo.hasCS || (hasShaders && shaders.hasCS);
		const UINT64 vsHash = binding.shaderInfo.hasVS ? binding.shaderInfo.vs : shaders.vs;
		const UINT64 psHash = binding.shaderInfo.hasPS ? binding.shaderInfo.ps : shaders.ps;
		const UINT64 csHash = binding.shaderInfo.hasCS ? binding.shaderInfo.cs : shaders.cs;
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(vsHash, hasVS, vs, ARRAYSIZE(vs));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(psHash, hasPS, ps, ARRAYSIZE(ps));
		DX12FrameAnalysisManifestFormatUtils::FormatShaderHash(csHash, hasCS, cs, ARRAYSIZE(cs));
		DX12FrameAnalysisManifestFormatUtils::FormatFileHash(
			filePath, fileHash, ARRAYSIZE(fileHash));

		const DX12DescriptorSummary &descriptor = binding.descriptor;
		const UINT64 fallbackGpuVa = descriptor.gpuVirtualAddress ?
			descriptor.gpuVirtualAddress : binding.gpuVirtualAddress;
		const UINT64 fallbackSize = descriptor.viewSize ? descriptor.viewSize : copyBytes;
		const uint32_t descriptorHash = DX12HashDescriptorBufferView(
			binding.hasDescriptor ? &descriptor : nullptr, fallbackGpuVa, fallbackSize);
		uint32_t contentHash = 0;
		const bool hasContentHash =
			desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
			DX12TryGetTextureHash(descriptor.resource, &contentHash);
		const uint32_t viewHash = hasContentHash ? contentHash : descriptorHash;
		sprintf_s(hash, sizeof(hash), "%08x", viewHash);
		char huntHash[16];
		sprintf_s(huntHash, sizeof(huntHash), "%08x", viewHash);
		char descriptorHashText[16], contentHashText[16];
		sprintf_s(descriptorHashText, sizeof(descriptorHashText), "%08x", descriptorHash);
		sprintf_s(contentHashText, sizeof(contentHashText), "%08x", hasContentHash ? contentHash : 0);

		char vsJson[64], psJson[64], csJson[64];
		char bindJson[64], kindJson[32], dimJson[32], fmtNameJson[64], statusJson[64], noteJson[128];
		char hashJson[32], huntHashJson[32], fileHashJson[32], descriptorHashJson[32];
		char contentHashJson[32], hashSourceJson[32], fileJson[512], resourceJson[32];
		const char *kind = descriptor.kind.empty() && binding.rootDescriptor ?
			DX12FrameAnalysisManifestFormatUtils::RootDescriptorKindName(binding.rangeType) :
			descriptor.kind.c_str();
		DX12JsonUtils::EscapeString(vsJson, sizeof(vsJson), vs);
		DX12JsonUtils::EscapeString(psJson, sizeof(psJson), ps);
		DX12JsonUtils::EscapeString(csJson, sizeof(csJson), cs);
		DX12JsonUtils::EscapeString(bindJson, sizeof(bindJson), binding.bindSpace.c_str());
		DX12JsonUtils::EscapeString(kindJson, sizeof(kindJson), kind);
		DX12JsonUtils::EscapeString(dimJson, sizeof(dimJson),
			DX12FrameAnalysisManifestFormatUtils::ResourceDimensionName(desc.Dimension));
		DX12JsonUtils::EscapeString(fmtNameJson, sizeof(fmtNameJson),
			DX12FrameAnalysisManifestFormatUtils::DxgiFormatName(static_cast<UINT>(desc.Format)));
		DX12JsonUtils::EscapeString(statusJson, sizeof(statusJson), status);
		DX12JsonUtils::EscapeString(noteJson, sizeof(noteJson), note);
		DX12JsonUtils::EscapeString(hashJson, sizeof(hashJson), hash);
		DX12JsonUtils::EscapeString(huntHashJson, sizeof(huntHashJson), huntHash);
		DX12JsonUtils::EscapeString(fileHashJson, sizeof(fileHashJson), fileHash);
		DX12JsonUtils::EscapeString(descriptorHashJson, sizeof(descriptorHashJson), descriptorHashText);
		DX12JsonUtils::EscapeString(contentHashJson, sizeof(contentHashJson), contentHashText);
		DX12JsonUtils::EscapeString(hashSourceJson, sizeof(hashSourceJson),
			hasContentHash ? "content" : "descriptor_fallback");
		DX12JsonUtils::EscapeWString(fileJson, sizeof(fileJson),
			DX12FrameAnalysisManifestFormatUtils::DedupedFileName(filePath));
		sprintf_s(resourceJson, sizeof(resourceJson), "\"%p\"", descriptor.resource);

		char buffer[3072];
		sprintf_s(buffer, sizeof(buffer),
			"\"func\":\"BindResource\","
			"\"call_index\":%llu,"
			"\"draw_id\":%llu,"
			"\"dispatch_id\":%llu,"
			"\"pso\":%llu,"
			"\"vs\":%s,"
			"\"ps\":%s,"
			"\"cs\":%s,"
			"\"bind\":%s,"
			"\"root\":%u,"
			"\"range\":%u,"
			"\"reg\":%u,"
			"\"space\":%u,"
			"\"desc\":%llu,"
			"\"kind\":%s,"
			"\"resource\":%s,"
			"\"dim\":%s,"
			"\"width\":%llu,"
			"\"height\":%u,"
			"\"fmt\":%u,"
			"\"fmt_name\":%s,"
			"\"view_dimension\":%u,"
			"\"first_element\":%llu,"
			"\"num_elements\":%u,"
			"\"structure_byte_stride\":%u,"
			"\"buffer_view_offset\":%llu,"
			"\"buffer_view_bytes\":%llu,"
			"\"gpu\":\"0x%llx\","
			"\"offset\":%llu,"
			"\"bytes\":%llu,"
			"\"state\":\"0x%x\","
			"\"state_known\":%s,"
			"\"file\":%s,"
			"\"status\":%s,"
			"\"note\":%s,"
			"\"hash\":%s,"
			"\"hunt_hash\":%s,"
			"\"file_hash\":%s,"
			"\"hash_source\":%s,"
			"\"content_hash\":%s,"
			"\"descriptor_hash\":%s",
			static_cast<unsigned long long>(binding.eventSerial),
			static_cast<unsigned long long>(binding.drawId),
			static_cast<unsigned long long>(binding.dispatchId),
			static_cast<unsigned long long>(binding.psoIndex),
			vsJson,
			psJson,
			csJson,
			bindJson,
			binding.rootParameterIndex,
			binding.rangeIndex,
			binding.shaderRegister,
			binding.registerSpace,
			static_cast<unsigned long long>(binding.descriptorIndex),
			kindJson,
			resourceJson,
			dimJson,
			static_cast<unsigned long long>(desc.Width),
			desc.Height,
			static_cast<UINT>(desc.Format),
			fmtNameJson,
			descriptor.viewDimension,
			static_cast<unsigned long long>(descriptor.firstElement),
			descriptor.numElements,
			descriptor.structureByteStride,
			static_cast<unsigned long long>(descriptor.bufferViewOffset),
			static_cast<unsigned long long>(descriptor.bufferViewBytes),
			static_cast<unsigned long long>(descriptor.gpuVirtualAddress),
			static_cast<unsigned long long>(sourceOffset),
			static_cast<unsigned long long>(copyBytes),
			static_cast<UINT>(sourceState),
			hasCurrentState ? "true" : "false",
			fileJson,
			statusJson,
			noteJson,
			hashJson,
			huntHashJson,
			fileHashJson,
			hashSourceJson,
			contentHashJson,
			descriptorHashJson);

		DX12FrameAnalysis::LogJsonFields(buffer);
	}
};



class DX12VmFrameVariablesUtils
{
public:
	class FrameSnapshot
	{
	public:
		HWND hwnd = nullptr;
		double windowWidth = 0.0;
		double windowHeight = 0.0;
		double resWidth = 0.0;
		double resHeight = 0.0;
		double rtWidth = 0.0;
		double rtHeight = 0.0;
		double cursorScreenX = 0.0;
		double cursorScreenY = 0.0;
		double cursorWindowX = 0.0;
		double cursorWindowY = 0.0;
		double cursorX = 0.0;
		double cursorY = 0.0;
		double cursorShowing = 0.0;
		double cursorHotspotX = 0.0;
		double cursorHotspotY = 0.0;
		double effectiveDpi = 96.0;
	};

	static void UpdateForSwapChain(IDXGISwapChain *swapChain)
	{
		FrameSnapshot frame;
		const bool collected = CollectFrameSnapshot(swapChain, &frame);
		ApplyGlobals(frame);
		LogFrameVariables(frame, collected);
	}

	static bool CollectFrameSnapshot(IDXGISwapChain *swapChain, FrameSnapshot *frame)
	{
		if (!frame)
			return false;
		*frame = FrameSnapshot();
		if (!swapChain)
			return false;

		DXGI_SWAP_CHAIN_DESC desc = {};
		if (FAILED(swapChain->GetDesc(&desc)))
			return false;

		frame->hwnd = desc.OutputWindow;
		frame->resWidth = PositiveDimension(desc.BufferDesc.Width);
		frame->resHeight = PositiveDimension(desc.BufferDesc.Height);

		CollectSwapChain1Size(swapChain, frame);
		CollectWindowSize(frame);

		frame->rtWidth = frame->resWidth > 0.0 ? frame->resWidth : frame->windowWidth;
		frame->rtHeight = frame->resHeight > 0.0 ? frame->resHeight : frame->windowHeight;
		frame->windowWidth = ResolveWindowWidth(*frame);
		frame->windowHeight = ResolveWindowHeight(*frame);

		if (frame->resWidth <= 0.0)
			frame->resWidth = frame->windowWidth;
		if (frame->resHeight <= 0.0)
			frame->resHeight = frame->windowHeight;
		if (frame->rtWidth <= 0.0)
			frame->rtWidth = frame->resWidth;
		if (frame->rtHeight <= 0.0)
			frame->rtHeight = frame->resHeight;

		CollectCursor(frame);
		frame->effectiveDpi = ResolveEffectiveDpi(frame->hwnd);
		return true;
	}

	static void ApplyGlobals(const FrameSnapshot &frame)
	{
		MigotoCommandVmUtils::SetGlobal(L"window_width", frame.windowWidth);
		MigotoCommandVmUtils::SetGlobal(L"window_height", frame.windowHeight);
		MigotoCommandVmUtils::SetGlobal(L"res_width", frame.resWidth);
		MigotoCommandVmUtils::SetGlobal(L"res_height", frame.resHeight);
		MigotoCommandVmUtils::SetGlobal(L"rt_width", frame.rtWidth);
		MigotoCommandVmUtils::SetGlobal(L"rt_height", frame.rtHeight);
		MigotoCommandVmUtils::SetGlobal(L"cursor_screen_x", frame.cursorScreenX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_screen_y", frame.cursorScreenY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_window_x", frame.cursorWindowX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_window_y", frame.cursorWindowY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_x", frame.cursorX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_y", frame.cursorY);
		MigotoCommandVmUtils::SetGlobal(L"cursor_showing", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobal(L"cursor_visible", frame.cursorShowing);
		MigotoCommandVmUtils::SetGlobal(L"cursor_hotspot_x", frame.cursorHotspotX);
		MigotoCommandVmUtils::SetGlobal(L"cursor_hotspot_y", frame.cursorHotspotY);
		MigotoCommandVmUtils::SetGlobal(L"effective_dpi", frame.effectiveDpi);
		MigotoCommandVmUtils::SetGlobal(L"hunting", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"frame_analysis", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"sli", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"stereo_active", 0.0);
		MigotoCommandVmUtils::SetGlobal(L"stereo_available", 0.0);
	}

	static void CollectSwapChain1Size(IDXGISwapChain *swapChain, FrameSnapshot *frame)
	{
		if (!swapChain || !frame)
			return;

		IDXGISwapChain1 *swapChain1 = nullptr;
		if (FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&swapChain1))) || !swapChain1)
			return;

		DXGI_SWAP_CHAIN_DESC1 desc1 = {};
		if (SUCCEEDED(swapChain1->GetDesc1(&desc1))) {
			if (desc1.Width > 0)
				frame->resWidth = PositiveDimension(desc1.Width);
			if (desc1.Height > 0)
				frame->resHeight = PositiveDimension(desc1.Height);
		}
		swapChain1->Release();
	}

	static void CollectWindowSize(FrameSnapshot *frame)
	{
		if (!frame || !frame->hwnd || !IsWindow(frame->hwnd))
			return;

		RECT client = {};
		if (!GetClientRect(frame->hwnd, &client))
			return;

		frame->windowWidth = PositiveSignedDimension(client.right - client.left);
		frame->windowHeight = PositiveSignedDimension(client.bottom - client.top);
	}

	static void CollectCursor(FrameSnapshot *frame)
	{
		if (!frame)
			return;

		CURSORINFO cursorInfo = {};
		cursorInfo.cbSize = sizeof(cursorInfo);
		POINT screenPoint = {};
		if (GetCursorInfo(&cursorInfo)) {
			screenPoint = cursorInfo.ptScreenPos;
			frame->cursorShowing = (cursorInfo.flags & CURSOR_SHOWING) ? 1.0 : 0.0;
			CollectCursorHotspot(cursorInfo.hCursor, frame);
		} else if (!GetCursorPos(&screenPoint)) {
			return;
		}

		frame->cursorScreenX = static_cast<double>(screenPoint.x);
		frame->cursorScreenY = static_cast<double>(screenPoint.y);

		POINT windowPoint = screenPoint;
		if (frame->hwnd && IsWindow(frame->hwnd) && ScreenToClient(frame->hwnd, &windowPoint)) {
			frame->cursorWindowX = static_cast<double>(windowPoint.x);
			frame->cursorWindowY = static_cast<double>(windowPoint.y);
		} else {
			frame->cursorWindowX = frame->cursorScreenX;
			frame->cursorWindowY = frame->cursorScreenY;
		}

		frame->cursorX = ResolveScaledCursor(
			frame->cursorWindowX, frame->cursorScreenX, frame->windowWidth);
		frame->cursorY = ResolveScaledCursor(
			frame->cursorWindowY, frame->cursorScreenY, frame->windowHeight);
	}

	static void CollectCursorHotspot(HCURSOR cursor, FrameSnapshot *frame)
	{
		if (!cursor || !frame)
			return;

		ICONINFO iconInfo = {};
		if (!GetIconInfo(cursor, &iconInfo))
			return;

		frame->cursorHotspotX = static_cast<double>(iconInfo.xHotspot);
		frame->cursorHotspotY = static_cast<double>(iconInfo.yHotspot);
		if (iconInfo.hbmMask)
			DeleteObject(iconInfo.hbmMask);
		if (iconInfo.hbmColor)
			DeleteObject(iconInfo.hbmColor);
	}

	static double ResolveWindowWidth(const FrameSnapshot &frame)
	{
		if (frame.windowWidth >= 640.0)
			return frame.windowWidth;
		if (frame.resWidth >= 640.0)
			return frame.resWidth;
		if (frame.rtWidth >= 640.0)
			return frame.rtWidth;
		return PositiveSignedDimension(GetSystemMetrics(SM_CXSCREEN));
	}

	static double ResolveWindowHeight(const FrameSnapshot &frame)
	{
		if (frame.windowHeight >= 480.0)
			return frame.windowHeight;
		if (frame.resHeight >= 480.0)
			return frame.resHeight;
		if (frame.rtHeight >= 480.0)
			return frame.rtHeight;
		return PositiveSignedDimension(GetSystemMetrics(SM_CYSCREEN));
	}

	static double ResolveScaledCursor(double windowCoord, double screenCoord, double dimension)
	{
		if (dimension <= 0.000001)
			return 0.0;

		double value = windowCoord / dimension;
		if (Abs(value) > 0.000001)
			return value;

		value = screenCoord / dimension;
		if (Abs(value) > 0.000001)
			return value;
		return 0.0;
	}

	static double ResolveEffectiveDpi(HWND hwnd)
	{
		typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
		static GetDpiForWindowFn getDpiForWindow = LoadGetDpiForWindow();

		UINT dpi = 0;
		if (getDpiForWindow && hwnd)
			dpi = getDpiForWindow(hwnd);
		if (dpi < 96)
			dpi = ResolveDeviceDpi(hwnd);
		return dpi >= 96 ? static_cast<double>(dpi) : 96.0;
	}

	static UINT(WINAPI *LoadGetDpiForWindow())(HWND)
	{
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
			user32 = LoadLibraryW(L"user32.dll");
		return user32 ?
			reinterpret_cast<UINT(WINAPI *)(HWND)>(GetProcAddress(user32, "GetDpiForWindow")) :
			nullptr;
	}

	static UINT ResolveDeviceDpi(HWND hwnd)
	{
		HWND dcWindow = hwnd;
		HDC dc = GetDC(dcWindow);
		if (!dc)
		{
			dcWindow = nullptr;
			dc = GetDC(nullptr);
		}
		if (!dc)
			return 96;

		const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
		ReleaseDC(dcWindow, dc);
		return dpi >= 96 ? static_cast<UINT>(dpi) : 96;
	}

	static double PositiveDimension(UINT value)
	{
		return value > 0 ? static_cast<double>(value) : 0.0;
	}

	static double PositiveSignedDimension(LONG value)
	{
		return value > 0 ? static_cast<double>(value) : 0.0;
	}

	static double Abs(double value)
	{
		return value < 0.0 ? -value : value;
	}

	static void LogFrameVariables(const FrameSnapshot &frame, bool collected)
	{
		DX12LogManager::Fields("DX12VmFrameVariables",
			"\"status\":\"%s\",\"hwnd\":\"%p\",\"windowWidth\":%.0f,\"windowHeight\":%.0f,"
			"\"resWidth\":%.0f,\"resHeight\":%.0f,\"rtWidth\":%.0f,\"rtHeight\":%.0f,"
			"\"cursorScreenX\":%.0f,\"cursorScreenY\":%.0f,\"cursorWindowX\":%.0f,"
			"\"cursorWindowY\":%.0f,\"cursorX\":%.6f,\"cursorY\":%.6f,"
			"\"cursorShowing\":%.0f,\"effectiveDpi\":%.0f",
			collected ? "updated" : "fallback",
			frame.hwnd,
			frame.windowWidth,
			frame.windowHeight,
			frame.resWidth,
			frame.resHeight,
			frame.rtWidth,
			frame.rtHeight,
			frame.cursorScreenX,
			frame.cursorScreenY,
			frame.cursorWindowX,
			frame.cursorWindowY,
			frame.cursorX,
			frame.cursorY,
			frame.cursorShowing,
			frame.effectiveDpi);
	}
};
