#pragma once

// DX12CustomShaderElement.h - load-time model of 3DMigoto [CustomShaderX]
// sections ("custom shader elements") for the DX12 runtime.
//
// DX11 treats such a section as a bundle of optional per-stage shader file
// overrides plus pipeline state overrides (blend / cull / topology / depth /
// stencil) around a command body that performs the actual draws
// (RunCustomShaderCommand::run in the DX11 CommandList.cpp). The DX12
// architecture already parses every [CustomShaderX] body into a plain
// CommandListConfig action stream, so this model is rebuilt from those same
// actions (SetShader / SetRenderState / control flow / Draw) - no second
// parser. It stays deliberately free of D3D12 types: the executor maps the
// enums below onto D3D12 structures at draw time, keeping the config layer
// independent of graphics objects (same rule as DX12Config.h).
//
// Phase-2 plan doc: docs/DX12_CustomShader_and_ConditionalBinding_Plan.md
// (stage 2.1: element semantics model).

#include <algorithm>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Config/DX12Config.h"

namespace Bunny {

enum class DX12CustomShaderBlendOp
{
	Add,
	Subtract,
	RevSubtract,
	Min,
	Max
};

// Factor names mirror the DX11 token tables (IniHandler.cpp BlendFactors)
// without the D3D11_BLEND_ prefix; SRC_ALPHA_SAT maps to D3D12_BLEND_SRC_ALPHA_SAT.
enum class DX12CustomShaderBlendFactor
{
	Zero,
	One,
	SrcColor,
	InvSrcColor,
	SrcAlpha,
	InvSrcAlpha,
	DestAlpha,
	InvDestAlpha,
	DestColor,
	InvDestColor,
	SrcAlphaSat,
	BlendFactor,
	InvBlendFactor,
	Src1Color,
	InvSrc1Color,
	Src1Alpha,
	InvSrc1Alpha
};

enum class DX12CustomShaderFill
{
	Solid,
	Wireframe
};

enum class DX12CustomShaderCull
{
	None,
	Front,
	Back
};

enum class DX12CustomShaderTopology
{
	PointList,
	LineList,
	LineStrip,
	TriangleList,
	TriangleStrip,
	LineListAdj,
	LineStripAdj,
	TriangleListAdj,
	TriangleStripAdj,
	PatchList // control points stored in patchControlPoints
};

// One render target blend slot ("blend = op src dst" + optional separate
// "alpha = op src dst"), mirroring the DX11 RenderTarget blend descriptor.
class DX12CustomShaderBlendSlot
{
public:
	DX12CustomShaderBlendOp op = DX12CustomShaderBlendOp::Add;
	DX12CustomShaderBlendFactor src = DX12CustomShaderBlendFactor::One;
	DX12CustomShaderBlendFactor dst = DX12CustomShaderBlendFactor::Zero;
	DX12CustomShaderBlendOp alphaOp = DX12CustomShaderBlendOp::Add;
	DX12CustomShaderBlendFactor alphaSrc = DX12CustomShaderBlendFactor::One;
	DX12CustomShaderBlendFactor alphaDst = DX12CustomShaderBlendFactor::Zero;
	unsigned writeMask = 0xFu;
	bool enabled = false; // "blend = disable" or no blend key
};

class DX12CustomShaderElementSpec
{
public:
	std::wstring section;         // namespaced section, matches CommandListMap keys
	std::wstring originalSection;
	std::wstring sourceDir;       // ini directory the section was loaded from
	std::wstring iniNamespace;

	// Optional per-stage shader files (vs =/ps =/... lines as written).
	std::wstring stageFiles[6];   // order: vs, hs, ds, gs, ps, cs
	bool hasStage[6] = {};        // same order
	static constexpr size_t StageCount = 6;

	// Rasterizer/OM overrides written on the element (only when a key was
	// present: DX11 merges missing keys against the game state at draw time;
	// the DX12 executor substitutes explicit, documented defaults).
	bool hasFill = false;
	DX12CustomShaderFill fill = DX12CustomShaderFill::Solid;
	bool hasCull = false;
	DX12CustomShaderCull cull = DX12CustomShaderCull::Back;
	bool hasFrontDirection = false;
	bool frontClockwise = false; // DX11 "front = Clockwise" default
	bool hasTopology = false;
	DX12CustomShaderTopology topology = DX12CustomShaderTopology::TriangleList;
	unsigned patchControlPoints = 0;

	std::vector<DX12CustomShaderBlendSlot> blendSlots; // index 0 = all-RT default
	bool hasBlend = false;
	bool blendDisabled = false; // "blend = disable"
	unsigned sampleMask = 0xffffffffu;

	// Every render-state line in key order (blend/cull/topology/depth/...),
	// kept verbatim so later pipeline keys (depth/stencil/sampler...) can be
	// interpreted without re-parsing the action stream.
	std::vector<std::pair<std::wstring, std::wstring>> stateLines;

	// oN render-target assignments in the body ("o0 = set_viewport bb").
	bool hasBackbufferTarget = false; // an oN value refers to the swap chain

	bool hasDrawCommand = false;   // body records Draw/DrawIndexed/...
	bool hasControlFlow = false;   // body branches on VM state
};

class DX12CustomShaderElementParser
{
public:
	static bool IsCustomShaderSectionName(const std::wstring &namespacedSection)
	{
		return MigotoIniCommandListUtils::StartsWithI(namespacedSection, L"CustomShader");
	}

	// Rebuilds an element spec from the parsed command-list actions of its
	// section. Returns false when the section carries no stage files and no
	// render state, i.e. it is an ordinary command list that happens to live
	// under the CustomShader prefix.
	static bool BuildSpec(const CommandListConfig &config, DX12CustomShaderElementSpec *out)
	{
		if (!out)
			return false;
		*out = DX12CustomShaderElementSpec();
		out->section = config.section;
		out->originalSection = config.originalSection;
		out->sourceDir = config.sourceDir;
		out->iniNamespace = config.iniNamespace;

		bool anyState = false;
		for (const CommandListAction &action : config.actions) {
			switch (action.kind) {
			case CommandListActionKind::SetShader:
				if (AssignStageFile(action.key, action.value, out))
					anyState = true;
				break;
			case CommandListActionKind::SetRenderState:
				if (ApplyRenderStateLine(action.key, action.value, out))
					anyState = true;
				break;
			case CommandListActionKind::ControlFlow:
				out->hasControlFlow = true;
				break;
			case CommandListActionKind::Draw:
			case CommandListActionKind::DrawIndexed:
			case CommandListActionKind::DrawIndexedAuto:
			case CommandListActionKind::DrawFromCaller:
			case CommandListActionKind::DrawIndexedFromCaller:
				out->hasDrawCommand = true;
				break;
			case CommandListActionKind::Dispatch:
				break;
			default:
				break;
			}
		}
		return anyState;
	}

	static void BuildRegistry(
		const CommandListMap &commandLists,
		std::unordered_map<std::wstring, DX12CustomShaderElementSpec> *registry)
	{
		if (!registry)
			return;
		registry->clear();
		for (const auto &item : commandLists) {
			if (!IsCustomShaderSectionName(item.first))
				continue;
			DX12CustomShaderElementSpec spec;
			if (BuildSpec(item.second, &spec))
				(*registry)[item.first] = std::move(spec);
		}
	}

	// -------------------------------------------------------------------
	// State text parsing (token names are case insensitive, no D3D11_/D3D12_
	// prefixes; matches the DX11 IniHandler token tables exactly).
	// -------------------------------------------------------------------

	static bool ParseBlendOpToken(const std::wstring &token, DX12CustomShaderBlendOp *out)
	{
		if (!out)
			return false;
		std::wstring t = MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(token));
		if (t == L"add") { *out = DX12CustomShaderBlendOp::Add; return true; }
		if (t == L"subtract") { *out = DX12CustomShaderBlendOp::Subtract; return true; }
		if (t == L"rev_subtract") { *out = DX12CustomShaderBlendOp::RevSubtract; return true; }
		if (t == L"min") { *out = DX12CustomShaderBlendOp::Min; return true; }
		if (t == L"max") { *out = DX12CustomShaderBlendOp::Max; return true; }
		return false;
	}

	static bool ParseBlendFactorToken(const std::wstring &token, DX12CustomShaderBlendFactor *out)
	{
		if (!out)
			return false;
		std::wstring t = MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(token));
		if (t == L"zero") { *out = DX12CustomShaderBlendFactor::Zero; return true; }
		if (t == L"one") { *out = DX12CustomShaderBlendFactor::One; return true; }
		if (t == L"src_color") { *out = DX12CustomShaderBlendFactor::SrcColor; return true; }
		if (t == L"inv_src_color") { *out = DX12CustomShaderBlendFactor::InvSrcColor; return true; }
		if (t == L"src_alpha") { *out = DX12CustomShaderBlendFactor::SrcAlpha; return true; }
		if (t == L"inv_src_alpha") { *out = DX12CustomShaderBlendFactor::InvSrcAlpha; return true; }
		if (t == L"dest_alpha") { *out = DX12CustomShaderBlendFactor::DestAlpha; return true; }
		if (t == L"inv_dest_alpha") { *out = DX12CustomShaderBlendFactor::InvDestAlpha; return true; }
		if (t == L"dest_color") { *out = DX12CustomShaderBlendFactor::DestColor; return true; }
		if (t == L"inv_dest_color") { *out = DX12CustomShaderBlendFactor::InvDestColor; return true; }
		if (t == L"src_alpha_sat") { *out = DX12CustomShaderBlendFactor::SrcAlphaSat; return true; }
		if (t == L"blend_factor") { *out = DX12CustomShaderBlendFactor::BlendFactor; return true; }
		if (t == L"inv_blend_factor") { *out = DX12CustomShaderBlendFactor::InvBlendFactor; return true; }
		if (t == L"src1_color") { *out = DX12CustomShaderBlendFactor::Src1Color; return true; }
		if (t == L"inv_src1_color") { *out = DX12CustomShaderBlendFactor::InvSrc1Color; return true; }
		if (t == L"src1_alpha") { *out = DX12CustomShaderBlendFactor::Src1Alpha; return true; }
		if (t == L"inv_src1_alpha") { *out = DX12CustomShaderBlendFactor::InvSrc1Alpha; return true; }
		return false;
	}

	// "blend = op src dst" (alpha channel falls back to ADD ONE ZERO unless
	// a separate "alpha =" line follows, mirroring DX11).
	static bool ParseBlendLine(
		const std::wstring &value, DX12CustomShaderBlendSlot *slot, bool *disabled)
	{
		if (!slot)
			return false;
		*slot = DX12CustomShaderBlendSlot();
		std::wstring text = MigotoIniCommandListUtils::Trim(value);
		if (disabled) {
			*disabled = MigotoIniCommandListUtils::EqualsI(text, L"disable");
			if (*disabled)
				return true;
		}
		std::vector<std::wstring> tokens = SplitTokens(text);
		if (tokens.size() < 3 || tokens.size() > 6)
			return false;
		DX12CustomShaderBlendOp op = DX12CustomShaderBlendOp::Add;
		DX12CustomShaderBlendFactor src = DX12CustomShaderBlendFactor::Zero;
		DX12CustomShaderBlendFactor dst = DX12CustomShaderBlendFactor::Zero;
		if (!ParseBlendOpToken(tokens[0], &op) ||
		    !ParseBlendFactorToken(tokens[1], &src) ||
		    !ParseBlendFactorToken(tokens[2], &dst))
			return false;
		slot->op = op;
		slot->src = src;
		slot->dst = dst;
		slot->enabled = true;
		// Optional "alpha_op alpha_src alpha_dst" suffix configures the alpha
		// channel of the same slot.
		if (tokens.size() >= 6) {
			if (!ParseBlendOpToken(tokens[3], &slot->alphaOp) ||
			    !ParseBlendFactorToken(tokens[4], &slot->alphaSrc) ||
			    !ParseBlendFactorToken(tokens[5], &slot->alphaDst))
				return false;
		}
		return true;
	}

	static bool ParseTopologyToken(
		const std::wstring &token, DX12CustomShaderTopology *out, unsigned *patchControlPoints)
	{
		if (!out)
			return false;
		std::wstring t = MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(token));
		if (patchControlPoints)
			*patchControlPoints = 0;
		if (t == L"point_list") { *out = DX12CustomShaderTopology::PointList; return true; }
		if (t == L"line_list") { *out = DX12CustomShaderTopology::LineList; return true; }
		if (t == L"line_strip") { *out = DX12CustomShaderTopology::LineStrip; return true; }
		if (t == L"triangle_list") { *out = DX12CustomShaderTopology::TriangleList; return true; }
		if (t == L"triangle_strip") { *out = DX12CustomShaderTopology::TriangleStrip; return true; }
		if (t == L"line_list_adj") { *out = DX12CustomShaderTopology::LineListAdj; return true; }
		if (t == L"line_strip_adj") { *out = DX12CustomShaderTopology::LineStripAdj; return true; }
		if (t == L"triangle_list_adj") { *out = DX12CustomShaderTopology::TriangleListAdj; return true; }
		if (t == L"triangle_strip_adj") { *out = DX12CustomShaderTopology::TriangleStripAdj; return true; }
		// "N_control_point_patch_list" (DX11 token table, 1..32 control
		// points): the digit run precedes a fixed 25-char suffix.
		static constexpr wchar_t kPatchSuffix[] = L"_control_point_patch_list";
		constexpr size_t kPatchSuffixLength =
			(sizeof(kPatchSuffix) / sizeof(kPatchSuffix[0])) - 1;
		if (t.size() > kPatchSuffixLength &&
		    t.compare(t.size() - kPatchSuffixLength, kPatchSuffixLength, kPatchSuffix) == 0) {
			const std::wstring digits = t.substr(0, t.size() - kPatchSuffixLength);
			if (!digits.empty() && digits.find_first_not_of(L"0123456789") == std::wstring::npos) {
				*out = DX12CustomShaderTopology::PatchList;
				if (patchControlPoints)
					*patchControlPoints = static_cast<unsigned>(std::stoul(digits));
				return true;
			}
		}
		return false;
	}

private:
	static std::vector<std::wstring> SplitTokens(const std::wstring &text)
	{
		std::vector<std::wstring> tokens;
		size_t start = 0;
		while (start <= text.size()) {
			const size_t space = text.find_first_of(L" \t\r\n", start);
			const std::wstring token = space == std::wstring::npos ?
				text.substr(start) : text.substr(start, space - start);
			if (!token.empty())
				tokens.push_back(token);
			if (space == std::wstring::npos)
				break;
			start = space + 1;
		}
		return tokens;
	}

	// Maps a SetShader action key onto the stage-file slot. Returns true when
	// the action declared a shader file for one of the six stages.
	static bool AssignStageFile(
		const std::wstring &key, const std::wstring &value, DX12CustomShaderElementSpec *out)
	{
		std::wstring stage = MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(key));
		size_t index = DX12CustomShaderElementSpec::StageCount;
		if (stage == L"vs") index = 0;
		else if (stage == L"hs") index = 1;
		else if (stage == L"ds") index = 2;
		else if (stage == L"gs") index = 3;
		else if (stage == L"ps") index = 4;
		else if (stage == L"cs") index = 5;
		if (index >= DX12CustomShaderElementSpec::StageCount)
			return false;
		if (MigotoIniCommandListUtils::Trim(value).empty())
			return false;
		out->stageFiles[index] = MigotoIniCommandListUtils::Trim(value);
		out->hasStage[index] = true;
		return true;
	}

	static bool ApplyRenderStateLine(
		const std::wstring &key, const std::wstring &value, DX12CustomShaderElementSpec *out)
	{
		std::wstring k = MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(key));
		out->stateLines.emplace_back(k, MigotoIniCommandListUtils::Trim(value));

		if (k == L"blend") {
			DX12CustomShaderBlendSlot slot;
			bool disabled = false;
			if (!ParseBlendLine(value, &slot, &disabled))
				return false;
			out->blendDisabled = disabled;
			if (out->blendSlots.empty())
				out->blendSlots.emplace_back(slot);
			else
				out->blendSlots[0] = slot;
			out->hasBlend = true;
			return true;
		}
		if (k == L"alpha" && !out->blendSlots.empty()) {
			DX12CustomShaderBlendSlot slot;
			bool disabled = false;
			if (!ParseBlendLine(value, &slot, &disabled))
				return false;
			out->blendSlots[0].alphaOp = slot.op;
			out->blendSlots[0].alphaSrc = slot.src;
			out->blendSlots[0].alphaDst = slot.dst;
			return true;
		}
		if (k == L"fill") {
			std::wstring v = MigotoIniCommandListUtils::ToLower(
				MigotoIniCommandListUtils::Trim(value));
			if (v == L"wireframe") out->fill = DX12CustomShaderFill::Wireframe;
			else if (v == L"solid") out->fill = DX12CustomShaderFill::Solid;
			else return false;
			out->hasFill = true;
			return true;
		}
		if (k == L"cull") {
			std::wstring v = MigotoIniCommandListUtils::ToLower(
				MigotoIniCommandListUtils::Trim(value));
			if (v == L"none") out->cull = DX12CustomShaderCull::None;
			else if (v == L"front") out->cull = DX12CustomShaderCull::Front;
			else if (v == L"back") out->cull = DX12CustomShaderCull::Back;
			else return false;
			out->hasCull = true;
			return true;
		}
		if (k == L"front") {
			std::wstring v = MigotoIniCommandListUtils::ToLower(
				MigotoIniCommandListUtils::Trim(value));
			if (v == L"clockwise") out->frontClockwise = true;
			else if (v == L"counterclockwise") out->frontClockwise = false;
			else return false;
			out->hasFrontDirection = true;
			return true;
		}
		if (k == L"topology") {
			if (!ParseTopologyToken(value, &out->topology, &out->patchControlPoints))
				return false;
			out->hasTopology = true;
			return true;
		}
		if (k == L"sample_mask") {
			// "sample_mask" hex/dec value, stored for the PSO builder.
			const std::wstring v = MigotoIniCommandListUtils::Trim(value);
			wchar_t *end = nullptr;
			const unsigned long parsed = std::wcstoul(v.c_str(), &end, 0);
			if (end && MigotoIniCommandListUtils::Trim(end).empty())
				out->sampleMask = static_cast<unsigned>(parsed);
			return true;
		}
		if (k == L"o0" || k == L"o1" || k == L"o2" || k == L"o3" ||
		    k == L"o4" || k == L"o5" || k == L"o6" || k == L"o7") {
			// Render target assignment/copy directive. It is still executed
			// as a SetRenderState action at runtime; the spec only records
			// that the element targets the back buffer so the draw path can
			// prepare a bb RTV.
			std::wstring v = MigotoIniCommandListUtils::ToLower(
				MigotoIniCommandListUtils::Trim(value));
			if (v.find(L"bb") != std::wstring::npos)
				out->hasBackbufferTarget = true;
			return true;
		}
		// Other state keys (depth/stencil/sampler/blend[n]...) are stored in
		// stateLines verbatim for the executor to interpret; the spec does
		// not treat unknown-but-render-state lines as failures.
		return true;
	}
};

} // namespace Bunny
