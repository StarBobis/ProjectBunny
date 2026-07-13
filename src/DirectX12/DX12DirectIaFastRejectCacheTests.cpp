#include "DX12DirectIaFastRejectCacheUtils.hpp"

#include <cstdlib>

static void Require(bool condition)
{
	if (!condition)
		std::abort();
}

int main()
{
	DX12ActiveIaState ia;
	ia.hasVertexBuffer[0] = true;
	ia.vertexBuffers[0] = { 0x1000, 4096, 32 };
	ia.hasVertexBuffer[7] = true;
	ia.vertexBuffers[7] = { 0x9000, 2048, 16 };

	DX12IaHashTargetPlan plan;
	plan.ResetKnown();
	plan.RequireVertexBuffer(0);

	DX12IaDrawInvocation draw;
	draw.vertexCount = 300;
	draw.instanceCount = 1;

	DX12DirectIaFastRejectCache cache;
	const DX12DirectIaFastRejectCacheKey key =
		DX12DirectIaFastRejectCacheUtils::MakeKey(
			11, 13, 17, ia, plan, draw);
	Require(!DX12DirectIaFastRejectCacheUtils::Contains(cache, key));
	Require(DX12DirectIaFastRejectCacheUtils::Store(&cache, key));
	Require(DX12DirectIaFastRejectCacheUtils::Contains(cache, key));

	DX12ActiveIaState unrelatedChange = ia;
	unrelatedChange.vertexBuffers[7].BufferLocation = 0xa000;
	const DX12DirectIaFastRejectCacheKey sameObservedState =
		DX12DirectIaFastRejectCacheUtils::MakeKey(
			11, 13, 17, unrelatedChange, plan, draw);
	Require(key == sameObservedState);

	DX12ActiveIaState observedChange = ia;
	observedChange.vertexBuffers[0].BufferLocation = 0x2000;
	Require(!DX12DirectIaFastRejectCacheUtils::Contains(
		cache,
		DX12DirectIaFastRejectCacheUtils::MakeKey(
			11, 13, 17, observedChange, plan, draw)));
	Require(!DX12DirectIaFastRejectCacheUtils::Contains(
		cache,
		DX12DirectIaFastRejectCacheUtils::MakeKey(
			11, 14, 17, ia, plan, draw)));
	Require(!DX12DirectIaFastRejectCacheUtils::Contains(
		cache,
		DX12DirectIaFastRejectCacheUtils::MakeKey(
			11, 13, 18, ia, plan, draw)));

	DX12IaDrawInvocation differentDraw = draw;
	differentDraw.firstVertex = 1;
	Require(!DX12DirectIaFastRejectCacheUtils::Contains(
		cache,
		DX12DirectIaFastRejectCacheUtils::MakeKey(
			11, 13, 17, ia, plan, differentDraw)));
	return 0;
}
