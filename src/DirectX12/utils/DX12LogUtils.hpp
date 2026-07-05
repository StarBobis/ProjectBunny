#pragma once

#include <Windows.h>

#include <stdio.h>

// ── DX11-aligned global log utilities ──────────────────────────────
// Replaces JSON wrapping with plain TSV (tab-separated values) format.
// Format: raw_tick<TAB>FuncName<TAB>k1=v1<TAB>k2=v2<NEWLINE>
//
// Time is GetTickCount64() raw value — zero formatting overhead.
// No SYSTEMTIME conversion, no snprintf time fields, no JSON escaping.

class DX12LogUtils
{
public:
	// Raw tick timestamp — GetTickCount64() (~20ns), output as raw uint64.
	// Zero formatting overhead vs. the old GetLocalTime() + snprintf HH:MM:SS.mmm.
	static void MakeLogTimePlain(char *buffer, size_t bufferSize)
	{
		if (!buffer || !bufferSize)
			return;
		snprintf(buffer, bufferSize, "%llu",
			static_cast<unsigned long long>(GetTickCount64()));
	}
};
