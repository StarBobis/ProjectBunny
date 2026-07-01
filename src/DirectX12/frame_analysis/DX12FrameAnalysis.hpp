#pragma once

#include <Windows.h>
#include <Shlwapi.h>
#include <share.h>
#include <stdarg.h>

#include "DX12FrameAnalysisState.hpp"
#include "utils/DX12JsonUtils.hpp"
#include "DX12ResourceTracker.hpp"
#include "DX12HotPathState.hpp"

class DX12FrameAnalysis
{
public:
	static bool Begin()
	{
		return DX12FrameAnalysisState::Begin();
	}

	static void End()
	{
		DX12FrameAnalysisState::End();
	}

	static void RequestCapture()
	{
		DX12FrameAnalysisState::RequestCapture();
		DX12HotPathState::Update();
	}

	static void BeginCapture()
	{
		DX12FrameAnalysisState::BeginCapture();
		DX12HotPathState::Update();
	}

	static bool EndCapture()
	{
		return DX12FrameAnalysisState::EndCapture();
	}

	static bool IsActive()
	{
		return DX12FrameAnalysisState::IsActive();
	}

	static bool IsCaptureRequested()
	{
		return DX12FrameAnalysisState::IsCaptureRequested();
	}

	static bool IsCapturing()
	{
		return DX12FrameAnalysisState::IsCapturing();
	}

	static bool GetPath(wchar_t *path, size_t pathCount)
	{
		return DX12FrameAnalysisState::GetPath(path, pathCount);
	}

	static void LogEvent(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"Event\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogInfo(const char *fmt, ...)
	{
		va_list args;
		char buffer[1024];
		char fields[4096];

		if (!IsActive())
			return;

		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);

		strcpy_s(fields, sizeof(fields), "\"func\":\"LogInfo\"");
		DX12JsonUtils::AppendLogFieldsFromText(fields, sizeof(fields), buffer);
		LogJsonFields(fields);
	}

	static void LogJsonFunc(const char *func, const char *fmt, ...)
	{
		if (!IsActive())
			return;

		char funcJson[512];
		char extra[4096];
		char fields[4608];
		DX12JsonUtils::EscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
		extra[0] = '\0';

		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(extra, sizeof(extra), fmt, args);
			va_end(args);
		}

		if (extra[0])
			sprintf_s(fields, sizeof(fields), "\"func\":%s,%s", funcJson, extra);
		else
			sprintf_s(fields, sizeof(fields), "\"func\":%s", funcJson);
		LogJsonFields(fields);
	}

	static void LogJsonFields(const char *fields)
	{
		DX12FrameAnalysisState::LogJsonFields(fields);
	}
};



