#include "DX12PreSkinCacheIdentity.hpp"
#include "DX12PreSkinOutputResizeCacheIdentity.hpp"

#include <cstdlib>
#include <map>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

int main()
{
	const DX12PreSkinBindingCacheIdentity stable = { 7, 11, 13 };
	Require(stable == DX12PreSkinBindingCacheIdentity{ 7, 11, 13 });
	Require(stable != DX12PreSkinBindingCacheIdentity{ 8, 11, 13 });
	Require(stable != DX12PreSkinBindingCacheIdentity{ 7, 12, 13 });
	Require(stable != DX12PreSkinBindingCacheIdentity{ 7, 11, 14 });
	Require(DX12PreSkinCacheIdentityUtils::CanReuseBindings(true, stable, stable));
	Require(!DX12PreSkinCacheIdentityUtils::CanReuseBindings(false, stable, stable));
	Require(!DX12PreSkinCacheIdentityUtils::CanReuseBindings(
		true, stable, DX12PreSkinBindingCacheIdentity{ 8, 11, 13 }));
	Require(!DX12PreSkinCacheIdentityUtils::CanReuseBindings(
		true,
		DX12PreSkinBindingCacheIdentity{ 7, 0, 13 },
		DX12PreSkinBindingCacheIdentity{ 7, 0, 13 }));
	Require(DX12PreSkinCacheIdentityUtils::CanReuseBindings(
		true,
		DX12PreSkinBindingCacheIdentity{
			7, DX12PreSkinCacheIdentityUtils::NoDescriptorHeapVersion, 13 },
		DX12PreSkinBindingCacheIdentity{
			7, DX12PreSkinCacheIdentityUtils::NoDescriptorHeapVersion, 13 }));

	unsigned rebuilds = 0;
	bool hasCachedBindings = false;
	DX12PreSkinBindingCacheIdentity cached;
	for (unsigned dispatch = 0; dispatch < 64; ++dispatch) {
		if (!DX12PreSkinCacheIdentityUtils::CanReuseBindings(
				hasCachedBindings, cached, stable)) {
			++rebuilds;
			cached = stable;
			hasCachedBindings = true;
		}
	}
	Require(rebuilds == 1);

	const std::uint64_t fingerprint = 0x123456789abcdef0ull;
	const std::uint64_t firstDispatch =
		DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(fingerprint, 21);
	Require(firstDispatch ==
		DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(fingerprint, 21));
	Require(firstDispatch !=
		DX12PreSkinCacheIdentityUtils::MakeProbeFingerprint(fingerprint, 22));

	const DX12PreSkinOutputResizeCacheIdentity resize = {
		0x1000, 705640, 40
	};
	Require(resize == DX12PreSkinOutputResizeCacheIdentity{ 0x1000, 705640, 40 });
	Require(resize != DX12PreSkinOutputResizeCacheIdentity{ 0x2000, 705640, 40 });
	Require(resize != DX12PreSkinOutputResizeCacheIdentity{ 0x1000, 705680, 40 });
	Require(resize != DX12PreSkinOutputResizeCacheIdentity{ 0x1000, 705640, 20 });

	std::map<DX12PreSkinOutputResizeCacheIdentity, unsigned> resizeCache;
	unsigned resizeCreations = 0;
	for (unsigned frame = 0; frame < 120; ++frame) {
		auto inserted = resizeCache.emplace(resize, ++resizeCreations);
		if (!inserted.second)
			--resizeCreations;
	}
	Require(resizeCache.size() == 1);
	Require(resizeCreations == 1);
	return 0;
}
