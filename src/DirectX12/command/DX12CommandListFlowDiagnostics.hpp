#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "DX12LogManager.hpp"
#include "DX12RuntimeState.hpp"
#include "utils/DX12JsonUtils.hpp"

class DX12CommandListFlowDiagnostics
{
public:
#if defined(_DEBUG)
	static void LogDX12Call(const char *api, const void *object, const char *fmt = nullptr, ...)
	{
		if (!DX12LogManager::ShouldLogHookCall(api))
			return;

		char detail[1024];
		detail[0] = '\0';
		if (fmt && fmt[0]) {
			va_list args;
			va_start(args, fmt);
			vsnprintf(detail, sizeof(detail), fmt, args);
			va_end(args);
		}

		char apiJson[256];
		char detailJson[2048];
		DX12JsonUtils::EscapeString(apiJson, sizeof(apiJson), api ? api : "");
		DX12JsonUtils::EscapeString(detailJson, sizeof(detailJson), detail);
		DX12LogManager::DebugJsonFunc("DX12HookCall",
			"\"api\":%s,\"present\":%ld,\"this\":\"%p\",\"detail\":%s",
			apiJson, DX12RuntimeState::GetPresentCount(), object, detailJson);
	}
#else
	static __forceinline void LogDX12Call(const char *, const void *, const char * = nullptr, ...) {}
#endif

	static void LogQueueStage(
		const char *api, const char *stage, ID3D12CommandQueue *queue,
		UINT count = 0, ID3D12Fence *fence = nullptr, UINT64 value = 0, HRESULT hr = S_OK,
		ULONGLONG elapsedMs = 0)
	{
#if defined(_DEBUG)
		if (!DX12LogManager::VerboseDiagnosticsEnabled())
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && stage && !strcmp(stage, "beforeOriginal"))
			return;
		if (SUCCEEDED(hr) && elapsedMs < 4 && api)
			return;
		DX12LogManager::DebugJsonFunc("DX12QueueStage",
			"\"api\":\"%s\",\"stage\":\"%s\",\"present\":%ld,\"queue\":\"%p\","
			"\"count\":%u,\"fence\":\"%p\",\"value\":%llu,\"hr\":\"0x%lx\",\"elapsedMs\":%llu",
			api ? api : "", stage ? stage : "", DX12RuntimeState::GetPresentCount(), queue,
			count, fence, static_cast<unsigned long long>(value), hr,
			static_cast<unsigned long long>(elapsedMs));
#else
		(void)api;
		(void)stage;
		(void)queue;
		(void)count;
		(void)fence;
		(void)value;
		(void)hr;
		(void)elapsedMs;
#endif
	}
};
