#pragma once

// DX12CustomShaderPipeline.h - maps a DX12CustomShaderElementSpec onto the
// D3D12 root signature / pipeline state pieces a custom draw needs
// (phase 2.3, device-free half).
//
// Design:
//  * One shared root signature for every custom element: root parameter 0 is
//    a descriptor table of 128 SRVs (t0..t127, register space 0, all-stage
//    visibility) plus one static sampler at s0. Mod shaders address their
//    textures by absolute register (ps-t100 -> t100) and read the ini params
//    texture at t120 (see DX12CustomShaderRegisters), so a plain register
//    table reproduces DX11 binding exactly - no remapping needed.
//  * Pipeline state is assembled per element from its spec: shader blobs
//    (DX12CustomShaderCompile), blend slots, rasterizer overrides, topology
//    type and the draw target RTV format. Missing state keys use documented
//    defaults because a D3D12 PSO cannot merge the game's OM/RS state at
//    record time the way DX11 did (see the plan document).
//
// Serialization/building here is device-free; device-side creation + caching
// happens in the draw executor.

#include <Windows.h>
#include <d3d12.h>
#include <d3dcommon.h>

#include "DX12CustomShaderElement.h"

namespace Bunny {

class DX12CustomShaderPipelineAssembler
{
public:
	// Register range of the shared root descriptor table (t0..t127).
	static constexpr UINT kTableBaseRegister = 0;
	static constexpr UINT kTableRegisterCount = 128;
	// Static sampler register (s0): mod shaders that Sample() rely on the
	// default linear clamp sampler the wrapper provided on DX11.
	static constexpr UINT kStaticSamplerRegister = 0;

	static HRESULT SerializeRootSignature(ID3DBlob **signature, ID3DBlob **errorBlob)
	{
		if (!signature)
			return E_INVALIDARG;
		*signature = nullptr;
		if (errorBlob)
			*errorBlob = nullptr;

		// Resolved lazily: the runtime never imports d3d12.dll (the loader
		// resolves its exports dynamically), so exported helpers are fetched
		// the same way here.
		static HMODULE sD3D12Module = LoadLibraryW(L"d3d12.dll");
		using SerializeRootSignatureFn = HRESULT(WINAPI *)(
			const D3D12_ROOT_SIGNATURE_DESC *,
			D3D_ROOT_SIGNATURE_VERSION,
			ID3DBlob **,
			ID3DBlob **);
		static SerializeRootSignatureFn sSerialize =
			sD3D12Module ?
				reinterpret_cast<SerializeRootSignatureFn>(
					GetProcAddress(sD3D12Module, "D3D12SerializeRootSignature")) :
				nullptr;
		if (!sSerialize)
			return E_NOINTERFACE;

		D3D12_DESCRIPTOR_RANGE range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = kTableRegisterCount;
		range.BaseShaderRegister = kTableBaseRegister;
		range.RegisterSpace = 0;
		range.OffsetInDescriptorsFromTableStart = 0;

		D3D12_ROOT_PARAMETER parameter = {};
		parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameter.DescriptorTable.NumDescriptorRanges = 1;
		parameter.DescriptorTable.pDescriptorRanges = &range;
		parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MipLODBias = 0.0f;
		sampler.MaxAnisotropy = 16;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = kStaticSamplerRegister;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = 1;
		desc.pParameters = &parameter;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &sampler;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		return sSerialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1, signature, errorBlob);
	}

	static D3D12_SHADER_BYTECODE ShaderByteCode(ID3DBlob *blob)
	{
		D3D12_SHADER_BYTECODE bytecode = {};
		if (blob) {
			bytecode.pShaderBytecode = blob->GetBufferPointer();
			bytecode.BytecodeLength = blob->GetBufferSize();
		}
		return bytecode;
	}

	// IASetPrimitiveTopology value (D3D12 shares the D3D11 numeric layout).
	static D3D12_PRIMITIVE_TOPOLOGY TopologyForBind(
		DX12CustomShaderTopology topology, unsigned patchControlPoints)
	{
		switch (topology) {
		case DX12CustomShaderTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		case DX12CustomShaderTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case DX12CustomShaderTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case DX12CustomShaderTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case DX12CustomShaderTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case DX12CustomShaderTopology::LineListAdj: return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
		case DX12CustomShaderTopology::LineStripAdj: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
		case DX12CustomShaderTopology::TriangleListAdj: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
		case DX12CustomShaderTopology::TriangleStripAdj: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
		case DX12CustomShaderTopology::PatchList: {
			// D3D patch lists span 1..32 control points; an out-of-range
			// count would wrap into an unrelated topology enum value.
			const unsigned points = patchControlPoints >= 1 && patchControlPoints <= 32 ?
				patchControlPoints : 1;
			return static_cast<D3D12_PRIMITIVE_TOPOLOGY>(
				D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + points - 1);
		}
		}
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	// PSO primitive topology type.
	static D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyTypeForPso(
		DX12CustomShaderTopology topology)
	{
		switch (topology) {
		case DX12CustomShaderTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case DX12CustomShaderTopology::LineList:
		case DX12CustomShaderTopology::LineStrip:
		case DX12CustomShaderTopology::LineListAdj:
		case DX12CustomShaderTopology::LineStripAdj: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case DX12CustomShaderTopology::PatchList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		}
	}

	static D3D12_BLEND_OP MapBlendOp(DX12CustomShaderBlendOp op)
	{
		switch (op) {
		case DX12CustomShaderBlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
		case DX12CustomShaderBlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
		case DX12CustomShaderBlendOp::Min: return D3D12_BLEND_OP_MIN;
		case DX12CustomShaderBlendOp::Max: return D3D12_BLEND_OP_MAX;
		case DX12CustomShaderBlendOp::Add: break;
		}
		return D3D12_BLEND_OP_ADD;
	}

	static D3D12_BLEND MapBlendFactor(DX12CustomShaderBlendFactor factor)
	{
		switch (factor) {
		case DX12CustomShaderBlendFactor::Zero: return D3D12_BLEND_ZERO;
		case DX12CustomShaderBlendFactor::One: return D3D12_BLEND_ONE;
		case DX12CustomShaderBlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
		case DX12CustomShaderBlendFactor::InvSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
		case DX12CustomShaderBlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
		case DX12CustomShaderBlendFactor::InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
		case DX12CustomShaderBlendFactor::DestAlpha: return D3D12_BLEND_DEST_ALPHA;
		case DX12CustomShaderBlendFactor::InvDestAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
		case DX12CustomShaderBlendFactor::DestColor: return D3D12_BLEND_DEST_COLOR;
		case DX12CustomShaderBlendFactor::InvDestColor: return D3D12_BLEND_INV_DEST_COLOR;
		case DX12CustomShaderBlendFactor::SrcAlphaSat: return D3D12_BLEND_SRC_ALPHA_SAT;
		case DX12CustomShaderBlendFactor::BlendFactor: return D3D12_BLEND_BLEND_FACTOR;
		case DX12CustomShaderBlendFactor::InvBlendFactor: return D3D12_BLEND_INV_BLEND_FACTOR;
		case DX12CustomShaderBlendFactor::Src1Color: return D3D12_BLEND_SRC1_COLOR;
		case DX12CustomShaderBlendFactor::InvSrc1Color: return D3D12_BLEND_INV_SRC1_COLOR;
		case DX12CustomShaderBlendFactor::Src1Alpha: return D3D12_BLEND_SRC1_ALPHA;
		case DX12CustomShaderBlendFactor::InvSrc1Alpha: return D3D12_BLEND_INV_SRC1_ALPHA;
		}
		return D3D12_BLEND_ONE;
	}

	static void FillBlendDesc(
		const DX12CustomShaderElementSpec &spec, D3D12_BLEND_DESC *blend)
	{
		if (!blend)
			return;
		*blend = D3D12_BLEND_DESC();
		for (UINT i = 0; i < 8; ++i) {
			blend->RenderTarget[i].RenderTargetWriteMask =
				D3D12_COLOR_WRITE_ENABLE_ALL;
		}
		blend->IndependentBlendEnable = FALSE;

		const DX12CustomShaderBlendSlot *slot = nullptr;
		if (!spec.blendDisabled && !spec.blendSlots.empty())
			slot = &spec.blendSlots[0];
		if (!slot || !slot->enabled)
			return; // blending disabled

		for (UINT i = 0; i < 8; ++i) {
			D3D12_RENDER_TARGET_BLEND_DESC &rt = blend->RenderTarget[i];
			rt.BlendEnable = TRUE;
			rt.LogicOpEnable = FALSE;
			rt.SrcBlend = MapBlendFactor(slot->src);
			rt.DestBlend = MapBlendFactor(slot->dst);
			rt.BlendOp = MapBlendOp(slot->op);
			rt.SrcBlendAlpha = MapBlendFactor(slot->alphaSrc);
			rt.DestBlendAlpha = MapBlendFactor(slot->alphaDst);
			rt.BlendOpAlpha = MapBlendOp(slot->alphaOp);
			rt.RenderTargetWriteMask = slot->writeMask;
		}
	}

	static void FillRasterizerDesc(
		const DX12CustomShaderElementSpec &spec, D3D12_RASTERIZER_DESC *rasterizer)
	{
		if (!rasterizer)
			return;
		*rasterizer = D3D12_RASTERIZER_DESC();
		rasterizer->FillMode = spec.hasFill && spec.fill == DX12CustomShaderFill::Wireframe ?
			D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
		switch (spec.cull) {
		case DX12CustomShaderCull::None: rasterizer->CullMode = D3D12_CULL_MODE_NONE; break;
		case DX12CustomShaderCull::Front: rasterizer->CullMode = D3D12_CULL_MODE_FRONT; break;
		case DX12CustomShaderCull::Back: default: rasterizer->CullMode = D3D12_CULL_MODE_BACK; break;
		}
		rasterizer->FrontCounterClockwise =
			spec.hasFrontDirection && spec.frontClockwise ? TRUE : FALSE;
		rasterizer->DepthBias = 0;
		rasterizer->DepthBiasClamp = 0.0f;
		rasterizer->SlopeScaledDepthBias = 0.0f;
		rasterizer->DepthClipEnable = TRUE;
		rasterizer->MultisampleEnable = FALSE;
		rasterizer->AntialiasedLineEnable = FALSE;
		rasterizer->ForcedSampleCount = 0;
		rasterizer->ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	}

	static void FillDepthStencilDesc(D3D12_DEPTH_STENCIL_DESC *depth)
	{
		if (!depth)
			return;
		// Custom element draws target the back buffer without a depth
		// buffer; DX11 left depth handling to the game's merged state, and
		// the documented DX12 default for bb draws is off/off.
		*depth = D3D12_DEPTH_STENCIL_DESC();
		depth->DepthEnable = FALSE;
		depth->StencilEnable = FALSE;
	}

	// Fills a complete graphics pipeline description for the element; the
	// caller supplies the shader blobs (both may be null when the element
	// only overrides one stage - DX11 swapped stages independently and kept
	// the rest of the pipeline, which a fresh PSO cannot do; elements that
	// only override one stage are logged and skipped by the executor).
	static void FillGraphicsPipelineDesc(
		const DX12CustomShaderElementSpec &spec,
		DXGI_FORMAT rtvFormat,
		ID3DBlob *vsBlob,
		ID3DBlob *psBlob,
		D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
	{
		if (!desc)
			return;
		*desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC();
		desc->pRootSignature = nullptr; // set by the device-side creator
		desc->VS = ShaderByteCode(vsBlob);
		desc->PS = ShaderByteCode(psBlob);
		FillBlendDesc(spec, &desc->BlendState);
		desc->SampleMask = spec.sampleMask;
		FillRasterizerDesc(spec, &desc->RasterizerState);
		FillDepthStencilDesc(&desc->DepthStencilState);
		desc->InputLayout = { nullptr, 0 };
		desc->PrimitiveTopologyType = TopologyTypeForPso(spec.topology);
		desc->NumRenderTargets = 1;
		desc->RTVFormats[0] = rtvFormat;
		for (UINT i = 1; i < 8; ++i)
			desc->RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
		desc->SampleDesc.Count = 1;
		desc->SampleDesc.Quality = 0;
	}

private:
	DX12CustomShaderPipelineAssembler() = delete;
};

} // namespace Bunny
