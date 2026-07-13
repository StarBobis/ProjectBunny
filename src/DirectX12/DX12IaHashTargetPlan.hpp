#pragma once

#include <Windows.h>

#include <stdint.h>

class DX12IaHashTargetPlan
{
public:
	bool requireAllIaBuffers = true;
	bool requireIndexBuffer = false;
	bool requireIndexBufferView = false;
	bool requireVertexBuffers[32] = {};
	bool hasDescriptorTargets = false;
	bool hasTextureOverrideChecks = false;

	void ResetKnown()
	{
		requireAllIaBuffers = false;
		requireIndexBuffer = false;
		requireIndexBufferView = false;
		hasDescriptorTargets = false;
		hasTextureOverrideChecks = false;
		for (UINT slot = 0; slot < 32; ++slot)
			requireVertexBuffers[slot] = false;
	}

	void RequireAll()
	{
		requireAllIaBuffers = true;
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		for (UINT slot = 0; slot < 32; ++slot)
			requireVertexBuffers[slot] = true;
	}

	void RequireIndexBuffer()
	{
		requireIndexBuffer = true;
		requireIndexBufferView = true;
		hasTextureOverrideChecks = true;
	}

	void RequireIndexBufferView()
	{
		requireIndexBufferView = true;
	}

	void RequireVertexBuffer(UINT slot)
	{
		hasTextureOverrideChecks = true;
		if (slot >= 32) {
			RequireAll();
			return;
		}
		requireVertexBuffers[slot] = true;
	}

	void RequireDescriptorTarget()
	{
		hasDescriptorTargets = true;
		hasTextureOverrideChecks = true;
	}

	bool RequiresIndexBufferHash(bool indexed) const
	{
		return indexed && (requireAllIaBuffers || requireIndexBuffer);
	}

	bool RequiresIndexBuffer(bool indexed) const
	{
		return requireIndexBufferView || RequiresIndexBufferHash(indexed);
	}

	bool RequiresVertexBuffer(UINT slot) const
	{
		return requireAllIaBuffers || (slot < 32 && requireVertexBuffers[slot]);
	}

	bool HasIaTargets() const
	{
		if (requireAllIaBuffers || requireIndexBuffer || requireIndexBufferView)
			return true;
		for (UINT slot = 0; slot < 32; ++slot) {
			if (requireVertexBuffers[slot])
				return true;
		}
		return false;
	}

	bool HasSameIaHashTargets(const DX12IaHashTargetPlan &other) const
	{
		if (requireAllIaBuffers != other.requireAllIaBuffers ||
			requireIndexBuffer != other.requireIndexBuffer ||
			requireIndexBufferView != other.requireIndexBufferView)
			return false;
		for (UINT slot = 0; slot < 32; ++slot) {
			if (requireVertexBuffers[slot] != other.requireVertexBuffers[slot])
				return false;
		}
		return true;
	}

	UINT64 Fingerprint() const
	{
		UINT64 key = requireAllIaBuffers ? 0xf17a11b7a11b7a11ull : 0x27a7b125f00d1234ull;
		key ^= requireIndexBuffer ? 0x9e3779b97f4a7c15ull : 0ull;
		key ^= requireIndexBufferView ? 0xb7e151628aed2a6bull : 0ull;
		key ^= hasDescriptorTargets ? 0x51ed1234cafe9876ull : 0ull;
		for (UINT slot = 0; slot < 32; ++slot) {
			if (!requireVertexBuffers[slot])
				continue;
			key ^= static_cast<UINT64>(slot + 1) * 0x100000001b3ull;
			key ^= key >> 33;
			key *= 0xff51afd7ed558ccdull;
		}
		return key ? key : 1;
	}
};
