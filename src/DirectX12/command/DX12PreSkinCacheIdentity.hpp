#pragma once

#include <cstdint>

class DX12PreSkinBindingCacheIdentity
{
public:
	std::uint64_t bindingSerial = 0;
	std::uint64_t descriptorHeapVersion = 0;
	std::uint64_t bufferResolveGeneration = 0;

	bool operator==(const DX12PreSkinBindingCacheIdentity &other) const
	{
		return bindingSerial == other.bindingSerial &&
			descriptorHeapVersion == other.descriptorHeapVersion &&
			bufferResolveGeneration == other.bufferResolveGeneration;
	}

	bool operator!=(const DX12PreSkinBindingCacheIdentity &other) const
	{
		return !(*this == other);
	}
};

class DX12PreSkinCacheIdentityUtils
{
public:
	static constexpr std::uint64_t NoDescriptorHeapVersion =
		~std::uint64_t{0};

	static bool CanReuseBindings(
		bool hasCachedBindings,
		const DX12PreSkinBindingCacheIdentity &cached,
		const DX12PreSkinBindingCacheIdentity &current)
	{
		// A bound heap with version 0 is unknown to the resource tracker. Reusing
		// its descriptor snapshot could hide a later descriptor update from Mod
		// matching, so keep the conservative rebuild behavior in that case.
		return hasCachedBindings && current.descriptorHeapVersion != 0 &&
			cached == current;
	}

	static std::uint64_t MakeProbeFingerprint(
		std::uint64_t bindingFingerprint,
		std::uint64_t iaContentEpoch)
	{
		std::uint64_t fingerprint = bindingFingerprint;
		fingerprint ^= iaContentEpoch + 0x9e3779b97f4a7c15ull +
			(fingerprint << 6) + (fingerprint >> 2);
		return fingerprint ? fingerprint : 1;
	}
};
