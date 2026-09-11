#pragma once

// DX12ResourceAliasStore.h - runtime resource alias table for the DX11
// "ResourceX = ref ResourceY" copy directives (phase 3).
//
// DX11 models those lines as draw-time reference copies between custom
// resources: "Resource\ZZMI\Diffuse = ref Resource_Body_Diffuse" makes the
// namespaced name an alias of the concrete texture, and every later binding
// of the alias resolves to the target. The DX12 architecture resolves
// resource names into loaded resources at bind time, so the equivalent is a
// name-level alias store consulted wherever a resource name becomes a GPU
// binding:
//   * aliases are registered while the mod's command lists run (the action
//     "Resource\ZZMI\Diffuse = ref Resource_Body_Diffuse" executes inside a
//     branch and registers the mapping), which keeps DX11's conditionality;
//   * the store is cleared on every reload together with the other config
//     globals;
//   * a low-cost gate skips the (locked) lookup entirely for configs that
//     register no aliases.
// "ref ps-t0" style captures of the game's currently-bound texture (saving a
// live SRV reference into a placeholder name) are handled at draw time by
// DX12DrawTextureCaptureUtils and register directly in gLoadedResources.

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "MigotoIniCommandListUtils.hpp"

namespace Bunny {

class DX12ResourceAliasStore
{
public:
	// Registers name -> target. Name keys are normalized to lower case so
	// ini-style case differences cannot split an alias; the target keeps its
	// original spelling because it is used verbatim for resource loading.
	void Add(const std::wstring &name, const std::wstring &target)
	{
		if (name.empty() || target.empty())
			return;
		std::lock_guard<std::mutex> lock(mMutex);
		mAliases[MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(name))] = target;
		mHasAny.store(true, std::memory_order_release);
	}

	// Follows alias chains (bounded, cycle safe). Terminal names keep the
	// spelling stored at registration (they are used verbatim for resource
	// loading); a cycle or overlong chain reports the original query.
	std::wstring Resolve(const std::wstring &name) const
	{
		if (name.empty() || !mHasAny.load(std::memory_order_acquire))
			return name;
		std::lock_guard<std::mutex> lock(mMutex);
		const std::wstring original = name;
		std::wstring current = MigotoIniCommandListUtils::ToLower(
			MigotoIniCommandListUtils::Trim(original));
		std::wstring lastTarget;
		for (unsigned depth = 0; depth < 16; ++depth) {
			auto found = mAliases.find(current);
			if (found == mAliases.end())
				return depth == 0 ? original : lastTarget;
			lastTarget = found->second;
			current = MigotoIniCommandListUtils::ToLower(
				MigotoIniCommandListUtils::Trim(found->second));
		}
		return original; // cycle or overlong chain
	}

	void Clear()
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mAliases.clear();
		mHasAny.store(false, std::memory_order_release);
	}

	bool HasAny() const
	{
		return mHasAny.load(std::memory_order_acquire);
	}

	// Returns true when the value is an alias-style reference
	// ("ref <name>" where <name> names a Resource section, i.e. starts with
	// the Resource prefix); capture-style references ("ref ps-tN") return
	// false. The "ref" keyword is matched case-insensitively like every
	// other ini token.
	static bool IsResourceAliasValue(const std::wstring &value)
	{
		const std::wstring target = AliasTarget(value);
		if (target.empty())
			return false;
		return MigotoIniCommandListUtils::StartsWithI(target, L"Resource");
	}

	static std::wstring AliasTarget(const std::wstring &value)
	{
		const std::wstring text = MigotoIniCommandListUtils::Trim(value);
		if (text.size() < 5 ||
		    MigotoIniCommandListUtils::ToLower(text.substr(0, 4)) != L"ref ")
			return L"";
		return MigotoIniCommandListUtils::Trim(text.substr(4));
	}

	// Binding-value normalization: strips the optional DX11 "ref " prefix so
	// the alias store and the load throat both see the bare resource name
	// ("ps-tN = ref ResourceX" binds exactly like "ps-tN = ResourceX").
	static std::wstring BindingNameWithoutRefPrefix(const std::wstring &value)
	{
		const std::wstring target = AliasTarget(value);
		return target.empty() ? value : target;
	}

	mutable std::mutex mMutex; // const Resolve() locks
	std::unordered_map<std::wstring, std::wstring> mAliases;
	std::atomic<bool> mHasAny{ false };
};

} // namespace Bunny
