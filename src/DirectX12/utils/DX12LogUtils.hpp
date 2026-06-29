#pragma once

#include <Windows.h>

#include <stdio.h>

class DX12LogUtils
{
public:
	static void MakeLogTime(char *buffer, size_t bufferSize)
	{
		if (!buffer || !bufferSize)
			return;
		SYSTEMTIME localTime;
		GetLocalTime(&localTime);
		snprintf(buffer, bufferSize,
			"\"%02u:%02u:%02u.%03u\"",
			localTime.wHour, localTime.wMinute, localTime.wSecond,
			localTime.wMilliseconds);
	}
};
