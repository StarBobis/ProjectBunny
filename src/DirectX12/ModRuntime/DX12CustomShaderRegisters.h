#pragma once

// DX12CustomShaderRegisters.h - mapping between the 3DMigoto "xNN" shader
// registers and the float4 array the custom UI shaders consume.
//
// DX11 custom UI shaders read the ini params through a 1D float4 texture
// bound at register t120 ("IniParams"), where register xNN maps to element
// [N].x (x87/y87/z87/w87 == IniParams[87].xyzw, see the mod's draw_2d.hlsl).
// The DX12 VM stores each register as a separate global (x87, y87, ...), so
// the executor packs them back into float4 slots before uploading. This file
// is pure string/math logic and stays free of D3D12 and VM types so the
// probe can verify the packing rules.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace Bunny {

class DX12CustomShaderRegisters
{
public:
	// The ini params texture is a 1D float4 array; register xNN lives at
	// element NN. 512 slots (8 KB) comfortably covers every value 3DMigoto
	// mods write (x0..x511).
	static constexpr unsigned kIniParamsFloat4Count = 512;
	// Register the texture is visible at in custom shaders.
	static constexpr unsigned kIniParamsShaderRegister = 120;

	// Upper bound of custom shader elements replayed in a single present.
	// Every replayed element owns its own descriptor-table region and
	// ini-params upload region for the frame: descriptor writes and upload
	// memcpys are CPU-side and take effect before the GPU executes any draw
	// of the list, so sharing a region between two elements of one frame
	// would make every earlier draw read the last element's state. Elements
	// beyond the cap are skipped with a diagnostic (visible degradation,
	// never corruption).
	static constexpr unsigned kMaxCustomElementsPerFrame = 64;

	// Builds the register name for a slot/component pair ("x87", "w88", ...).
	static std::wstring RegisterName(unsigned slot, unsigned component)
	{
		static const wchar_t kComponents[] = L"xyzw";
		if (component > 3)
			return L"";
		wchar_t buffer[16];
		swprintf_s(buffer, L"%c%u", kComponents[component], slot);
		return buffer;
	}

	// Packs the register values observed through `lookup` into `count` float4
	// slots. `lookup` receives the register name (e.g. "x87") and returns its
	// VM value; registers nobody wrote read back 0, so the pack is exact even
	// for sparse writes.
	static std::vector<float> PackFloat4(
		unsigned count,
		const std::function<double(const std::wstring &)> &lookup)
	{
		if (count > kIniParamsFloat4Count)
			count = kIniParamsFloat4Count;
		std::vector<float> values(count * 4, 0.0f);
		for (unsigned slot = 0; slot < count; ++slot) {
			for (unsigned component = 0; component < 4; ++component) {
				const std::wstring name = RegisterName(slot, component);
				values[slot * 4 + component] =
					static_cast<float>(lookup ? lookup(name) : 0.0);
			}
		}
		return values;
	}
};

} // namespace Bunny
