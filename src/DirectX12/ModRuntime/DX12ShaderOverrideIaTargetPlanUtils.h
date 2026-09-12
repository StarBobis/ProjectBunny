#pragma once

// IA/descriptor target-plan construction for shader overrides: walk a matched
// override's command lists and actions and merge their targets into one plan.
//
// This used to be a fragment spliced into ModRuntime/DX12ModRuntimeInternal.h at
// a fixed line, which meant it could only see the declarations that happened to
// precede that line. It now carries its own includes and can be read on its own.
#include "../Config/DX12Config.h"
#include "../DX12Core.h"


class DX12ShaderOverrideIaTargetPlanUtils
{
public:
	static bool CommandListRuntimeEffectLocked(const std::wstring &name)
	{
		std::unordered_map<std::wstring, bool>::const_iterator found =
			gCommandListPreRuntimeEffect.find(name);
		return found == gCommandListPreRuntimeEffect.end() || found->second;
	}

	static void RequireTargetLocked(
		const Bunny::CommandListTarget &target,
		DX12IaHashTargetPlan *targetPlan)
	{
		if (!targetPlan)
			return;

		switch (target.kind) {
		case Bunny::CommandListTargetKind::IndexBuffer:
			targetPlan->RequireIndexBuffer();
			break;
		case Bunny::CommandListTargetKind::VertexBuffer:
			targetPlan->RequireVertexBuffer(target.slot);
			break;
		case Bunny::CommandListTargetKind::ConstantBuffer:
		case Bunny::CommandListTargetKind::ShaderResource:
		case Bunny::CommandListTargetKind::UnorderedAccessView:
			targetPlan->RequireDescriptorTarget(
				static_cast<uint32_t>(target.kind),
				static_cast<uint32_t>(target.stage),
				target.slot);
			break;
		default:
			targetPlan->RequireAll();
			break;
		}
	}

	static void CollectCommandListLocked(
		const std::wstring &name,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect);

	static void CollectActionsLocked(
		const std::vector<Bunny::CommandListAction> &actions,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const Bunny::CommandListAction &action : actions) {
			switch (action.kind) {
			case Bunny::CommandListActionKind::Run:
				CollectCommandListLocked(
					action.commandList, depth + 1, commandListStack,
					targetPlan, textureOverrideTargetsCollected,
					collectTextureOverrideTargets,
					hasDirectNonTextureEffect);
				break;
			case Bunny::CommandListActionKind::CheckTextureOverride:
				RequireTargetLocked(action.target, targetPlan);
				if (collectTextureOverrideTargets &&
				    textureOverrideTargetsCollected &&
				    !*textureOverrideTargetsCollected) {
					*textureOverrideTargetsCollected = true;
					CollectAllTextureOverrideTargetsLocked(
						commandListStack, targetPlan);
				}
				break;
			default:
				if (hasDirectNonTextureEffect)
					*hasDirectNonTextureEffect = true;
				break;
			}
		}
	}

	static void CollectCommandListLinksLocked(
		const Bunny::CommandListLinks &links,
		bool includePost,
		UINT depth,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect)
	{
		if (!targetPlan || depth > 16)
			return;

		for (const std::wstring &name : links.pre) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
		for (const std::wstring &name : links.main) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
		if (!includePost)
			return;
		for (const std::wstring &name : links.post) {
			CollectCommandListLocked(
				name, depth + 1, commandListStack, targetPlan,
				textureOverrideTargetsCollected, collectTextureOverrideTargets,
				hasDirectNonTextureEffect);
		}
	}

	static void CollectShaderOverrideConfigLocked(
		const Bunny::ShaderOverrideConfig &config,
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan,
		bool *textureOverrideTargetsCollected,
		bool collectTextureOverrideTargets,
		bool includePostLinks,
		bool *hasDirectNonTextureEffect)
	{
		CollectCommandListLinksLocked(
			config.commandLists, includePostLinks, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
		CollectActionsLocked(
			config.actions, 0, commandListStack, targetPlan,
			textureOverrideTargetsCollected, collectTextureOverrideTargets,
			hasDirectNonTextureEffect);
	}

	static void CollectAllTextureOverrideTargetsLocked(
		std::unordered_set<std::wstring> *commandListStack,
		DX12IaHashTargetPlan *targetPlan)
	{
		if (!targetPlan)
			return;

		Bunny::TextureOverrideMap::const_iterator bucket = gTextureOverrides.begin();
		for (; bucket != gTextureOverrides.end(); ++bucket) {
			const Bunny::TextureOverrideList &configs = bucket->second;
			for (size_t index = 0; index < configs.size(); ++index) {
				const Bunny::TextureOverrideConfig &config = configs[index];
				CollectCommandListLinksLocked(
					config.commandLists, true, 0, commandListStack,
					targetPlan, nullptr, false, nullptr);
				CollectActionsLocked(
					config.actions, 0, commandListStack, targetPlan,
					nullptr, false, nullptr);
			}
		}
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const DX12ShaderOverridePsoMatchCache &cache,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect = nullptr)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig *config : cache.drawConfigs) {
			if (!config)
				continue;
			CollectShaderOverrideConfigLocked(
				*config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		for (const Bunny::ShaderOverrideConfig &config : cache.regexDrawConfigs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		return targetPlan;
	}

	static DX12IaHashTargetPlan BuildDrawPlanLocked(
		const std::vector<Bunny::ShaderOverrideConfig> &configs,
		bool collectTextureOverrideTargets,
		bool *hasDirectNonTextureEffect = nullptr)
	{
		DX12IaHashTargetPlan targetPlan;
		targetPlan.ResetKnown();
		bool textureOverrideTargetsCollected = false;
		std::unordered_set<std::wstring> commandListStack;

		for (const Bunny::ShaderOverrideConfig &config : configs) {
			CollectShaderOverrideConfigLocked(
				config, &commandListStack, &targetPlan,
				collectTextureOverrideTargets ? &textureOverrideTargetsCollected : nullptr,
				collectTextureOverrideTargets,
				false,
				hasDirectNonTextureEffect);
		}
		return targetPlan;
	}
};
