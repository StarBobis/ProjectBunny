#pragma once

#include <stddef.h>

class DX12FrameAnalysisDumpResult
{
public:
	bool ok = false;
	size_t written = 0;
	size_t skipped = 0;
	size_t failed = 0;
	const char *reason = "";
};
