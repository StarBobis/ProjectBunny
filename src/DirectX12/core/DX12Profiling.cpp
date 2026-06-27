#include "DX12Profiling.h"

#include "DX12State.h"

namespace DX12Profiling {

Counter gDrawInstanced;
Counter gDrawIndexedInstanced;
Counter gDispatch;
Counter gSetPipelineState;
Counter gSetDescriptorHeaps;
Counter gSetComputeRootSignature;
Counter gSetGraphicsRootSignature;
Counter gSetComputeRootDescriptorTable;
Counter gSetGraphicsRootDescriptorTable;
Counter gSetComputeRootConstantBufferView;
Counter gSetGraphicsRootConstantBufferView;
Counter gSetComputeRootShaderResourceView;
Counter gSetGraphicsRootShaderResourceView;
Counter gSetComputeRootUnorderedAccessView;
Counter gSetGraphicsRootUnorderedAccessView;
Counter gIASetIndexBuffer;
Counter gIASetVertexBuffers;
Counter gIASetPrimitiveTopology;
Counter gResourceBarrier;
Counter gExecuteCommandLists;
Counter gPresent;
Counter gResetCommandList;
Counter gCloseCommandList;
Counter gIaDrawOverrideChecks;
Counter gIaHashStateHits;
Counter gIaHashStateMisses;
Counter gIaTextureMayHits;
Counter gIaTextureMayMisses;
Counter gIaPrepareCalls;
Counter gIaApplied;
Counter gIaCandidateTests;
Counter gIaCandidateHits;
Counter gIaHuntIaUpdates;
Counter gIaReplacementDraws;
Counter gIaReplacementIndexedDraws;
Counter gIaReplacementDispatches;
Counter gPreSkinCsTests;
Counter gPreSkinUavTests;
Counter gPreSkinUavHits;
Counter gPreSkinApplied;
Counter gPreSkinDispatchResized;

Counter gAllDraws;
Counter gAllDispatches;
Counter gAllBindings;
Counter gAllCommands;
Counter gFastForwardHits;

Mode gMode = Mode::NONE;
volatile LONG gCollectCounters = 0;
volatile LONG gFramePresentCount = 0;
LARGE_INTEGER gFrameStartTime = {};
LARGE_INTEGER gPerfFrequency = {};
wchar_t gOverlayText[2048] = L"";

ScopedTimer::ScopedTimer(Counter &counter)
	: mCounter(counter)
{
	if (!ShouldCollectCounters()) {
		mStart.QuadPart = 0;
		return;
	}
	InterlockedIncrement(&mCounter.calls);
	mStart.QuadPart = 0;
	if (gMode == Mode::SUMMARY)
		QueryPerformanceCounter(&mStart);
}

ScopedTimer::~ScopedTimer()
{
	if (mStart.QuadPart == 0)
		return;
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);
	InterlockedAdd64(&mCounter.cpuTicks, endTime.QuadPart - mStart.QuadPart);
}

static void ResetCounter(Counter &c)
{
	InterlockedExchange(&c.calls, 0);
	InterlockedExchange64(&c.cpuTicks, 0);
}

static void AccumulateCounter(Counter &dst, const Counter &src)
{
	InterlockedAdd(&dst.calls, src.calls);
	InterlockedAdd64(&dst.cpuTicks, src.cpuTicks);
}

static double TicksToUs(LONG64 ticks)
{
	if (gPerfFrequency.QuadPart == 0)
		return 0.0;
	return static_cast<double>(ticks) * 1000000.0 / static_cast<double>(gPerfFrequency.QuadPart);
}

static const wchar_t *FormatUs(double us, wchar_t *buf, size_t bufSize)
{
	if (us < 1000.0)
		swprintf_s(buf, bufSize, L"%6.1f us", us);
	else if (us < 1000000.0)
		swprintf_s(buf, bufSize, L"%6.1f ms", us / 1000.0);
	else
		swprintf_s(buf, bufSize, L"%6.1f  s", us / 1000000.0);
	return buf;
}

static void ResetFrame()
{
	ResetCounter(gDrawInstanced);
	ResetCounter(gDrawIndexedInstanced);
	ResetCounter(gDispatch);
	ResetCounter(gSetPipelineState);
	ResetCounter(gSetDescriptorHeaps);
	ResetCounter(gSetComputeRootSignature);
	ResetCounter(gSetGraphicsRootSignature);
	ResetCounter(gSetComputeRootDescriptorTable);
	ResetCounter(gSetGraphicsRootDescriptorTable);
	ResetCounter(gSetComputeRootConstantBufferView);
	ResetCounter(gSetGraphicsRootConstantBufferView);
	ResetCounter(gSetComputeRootShaderResourceView);
	ResetCounter(gSetGraphicsRootShaderResourceView);
	ResetCounter(gSetComputeRootUnorderedAccessView);
	ResetCounter(gSetGraphicsRootUnorderedAccessView);
	ResetCounter(gIASetIndexBuffer);
	ResetCounter(gIASetVertexBuffers);
	ResetCounter(gIASetPrimitiveTopology);
	ResetCounter(gResourceBarrier);
	ResetCounter(gExecuteCommandLists);
	ResetCounter(gPresent);
	ResetCounter(gResetCommandList);
	ResetCounter(gCloseCommandList);
	ResetCounter(gFastForwardHits);
	ResetCounter(gIaDrawOverrideChecks);
	ResetCounter(gIaHashStateHits);
	ResetCounter(gIaHashStateMisses);
	ResetCounter(gIaTextureMayHits);
	ResetCounter(gIaTextureMayMisses);
	ResetCounter(gIaPrepareCalls);
	ResetCounter(gIaApplied);
	ResetCounter(gIaCandidateTests);
	ResetCounter(gIaCandidateHits);
	ResetCounter(gIaHuntIaUpdates);
	ResetCounter(gIaReplacementDraws);
	ResetCounter(gIaReplacementIndexedDraws);
	ResetCounter(gIaReplacementDispatches);
	ResetCounter(gPreSkinCsTests);
	ResetCounter(gPreSkinUavTests);
	ResetCounter(gPreSkinUavHits);
	ResetCounter(gPreSkinApplied);
	ResetCounter(gPreSkinDispatchResized);
}

void Init()
{
	QueryPerformanceFrequency(&gPerfFrequency);
	InterlockedExchange(&gCollectCounters, DX12DiagnosticsLoggingEnabled() ? 1 : 0);
}

void BeginFrame()
{
	if (gMode != Mode::SUMMARY)
		return;

	QueryPerformanceCounter(&gFrameStartTime);
}

void EndFrame()
{
	InterlockedIncrement(&gFramePresentCount);

	if (gMode == Mode::SUMMARY) {
		FlushFrame();
	} else if (DX12DiagnosticsLoggingEnabled()) {
		LogFrameStats();
	}

	if (ShouldCollectCounters())
		ResetFrame();
}

void Toggle()
{
	if (gMode == Mode::NONE) {
		gMode = Mode::SUMMARY;
		InterlockedExchange(&gCollectCounters, 1);
		DX12Log("DX12 profiling enabled (SUMMARY mode) - press F11 to toggle\n");
		DX12LogJsonFunc("DX12Profiling", "\"status\":\"enabled\",\"mode\":\"SUMMARY\"");
	} else {
		if (gMode == Mode::SUMMARY)
			FlushFrame();
		gMode = Mode::NONE;
		InterlockedExchange(&gCollectCounters, DX12DiagnosticsLoggingEnabled() ? 1 : 0);
		if (!ShouldCollectCounters())
			ResetFrame();
		DX12Log("DX12 profiling disabled\n");
		DX12LogJsonFunc("DX12Profiling", "\"status\":\"disabled\"");
	}
}

bool IsActive()
{
	return gMode != Mode::NONE;
}

void RecordIaDrawOverrideCheck()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaDrawOverrideChecks.calls);
}

void RecordIaHashStateResult(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(hit ? &gIaHashStateHits.calls : &gIaHashStateMisses.calls);
}

void RecordIaTextureMayMatchResult(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(hit ? &gIaTextureMayHits.calls : &gIaTextureMayMisses.calls);
}

void RecordIaPrepareCall()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaPrepareCalls.calls);
}

void RecordIaApplied()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaApplied.calls);
}

void RecordIaCandidateTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaCandidateTests.calls);
	if (hit)
		InterlockedIncrement(&gIaCandidateHits.calls);
}

void RecordIaHuntIaUpdate()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaHuntIaUpdates.calls);
}

void RecordIaReplacementDraw(bool indexed)
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaReplacementDraws.calls);
	if (indexed)
		InterlockedIncrement(&gIaReplacementIndexedDraws.calls);
}

void RecordIaReplacementDispatch()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gIaReplacementDispatches.calls);
}

void RecordPreSkinCsTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gPreSkinCsTests.calls);
	(void)hit;
}

void RecordPreSkinUavTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gPreSkinUavTests.calls);
	if (hit)
		InterlockedIncrement(&gPreSkinUavHits.calls);
}

void RecordPreSkinApplied()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gPreSkinApplied.calls);
}

void RecordPreSkinDispatchResized()
{
	if (!ShouldCollectCounters())
		return;
	InterlockedIncrement(&gPreSkinDispatchResized.calls);
}

void LogFrameStats()
{
	LONG present = gFramePresentCount;

	LONG draws = gDrawInstanced.calls + gDrawIndexedInstanced.calls;
	LONG bindings =
		gSetDescriptorHeaps.calls +
		gSetComputeRootSignature.calls + gSetGraphicsRootSignature.calls +
		gSetComputeRootDescriptorTable.calls + gSetGraphicsRootDescriptorTable.calls +
		gSetComputeRootConstantBufferView.calls + gSetGraphicsRootConstantBufferView.calls +
		gSetComputeRootShaderResourceView.calls + gSetGraphicsRootShaderResourceView.calls +
		gSetComputeRootUnorderedAccessView.calls + gSetGraphicsRootUnorderedAccessView.calls +
		gIASetIndexBuffer.calls + gIASetVertexBuffers.calls + gIASetPrimitiveTopology.calls;
	LONG total = draws + gDispatch.calls + gSetPipelineState.calls + bindings +
		gResourceBarrier.calls;

	DX12LogDebugJsonFunc("DX12FrameStats",
		"\"present\":%ld,"
		"\"draws\":%ld,\"dispatch\":%ld,\"setPSO\":%ld,\"bindings\":%ld,"
		"\"rootTables\":%ld,\"rootCBV_SRV_UAV\":%ld,"
		"\"iaSet\":%ld,\"barriers\":%ld,"
		"\"execCL\":%ld,\"reset\":%ld,"
		"\"total\":%ld,\"ffHits\":%ld,"
		"\"iaOverrideChecks\":%ld,\"iaHashHits\":%ld,\"iaHashMisses\":%ld,"
		"\"iaMayHits\":%ld,\"iaMayMisses\":%ld,\"iaPrepareCalls\":%ld,\"iaApplied\":%ld,"
		"\"iaCandidateTests\":%ld,\"iaCandidateHits\":%ld,\"iaHuntIaUpdates\":%ld,"
		"\"iaReplacementDraws\":%ld,\"iaReplacementIndexedDraws\":%ld,\"iaReplacementDispatches\":%ld,"
		"\"preSkinCsTests\":%ld,\"preSkinUavTests\":%ld,\"preSkinUavHits\":%ld,"
		"\"preSkinApplied\":%ld,\"preSkinDispatchResized\":%ld",
		present,
		draws, gDispatch.calls, gSetPipelineState.calls, bindings,
		gSetComputeRootDescriptorTable.calls + gSetGraphicsRootDescriptorTable.calls,
		gSetComputeRootConstantBufferView.calls + gSetGraphicsRootConstantBufferView.calls +
			gSetComputeRootShaderResourceView.calls + gSetGraphicsRootShaderResourceView.calls +
			gSetComputeRootUnorderedAccessView.calls + gSetGraphicsRootUnorderedAccessView.calls,
		gIASetIndexBuffer.calls + gIASetVertexBuffers.calls + gIASetPrimitiveTopology.calls,
		gResourceBarrier.calls,
		gExecuteCommandLists.calls, gResetCommandList.calls,
		total, gFastForwardHits.calls,
		gIaDrawOverrideChecks.calls, gIaHashStateHits.calls, gIaHashStateMisses.calls,
		gIaTextureMayHits.calls, gIaTextureMayMisses.calls,
		gIaPrepareCalls.calls, gIaApplied.calls,
		gIaCandidateTests.calls, gIaCandidateHits.calls, gIaHuntIaUpdates.calls,
		gIaReplacementDraws.calls, gIaReplacementIndexedDraws.calls,
		gIaReplacementDispatches.calls,
		gPreSkinCsTests.calls, gPreSkinUavTests.calls, gPreSkinUavHits.calls,
		gPreSkinApplied.calls, gPreSkinDispatchResized.calls);
}

void FlushFrame()
{
	if (gMode != Mode::SUMMARY)
		return;

	LONG present = gFramePresentCount;

	Counter draws = {};
	AccumulateCounter(draws, gDrawInstanced);
	AccumulateCounter(draws, gDrawIndexedInstanced);

	Counter bindings = {};
	AccumulateCounter(bindings, gSetDescriptorHeaps);
	AccumulateCounter(bindings, gSetComputeRootSignature);
	AccumulateCounter(bindings, gSetGraphicsRootSignature);
	AccumulateCounter(bindings, gSetComputeRootDescriptorTable);
	AccumulateCounter(bindings, gSetGraphicsRootDescriptorTable);
	AccumulateCounter(bindings, gSetComputeRootConstantBufferView);
	AccumulateCounter(bindings, gSetGraphicsRootConstantBufferView);
	AccumulateCounter(bindings, gSetComputeRootShaderResourceView);
	AccumulateCounter(bindings, gSetGraphicsRootShaderResourceView);
	AccumulateCounter(bindings, gSetComputeRootUnorderedAccessView);
	AccumulateCounter(bindings, gSetGraphicsRootUnorderedAccessView);
	AccumulateCounter(bindings, gIASetIndexBuffer);
	AccumulateCounter(bindings, gIASetVertexBuffers);
	AccumulateCounter(bindings, gIASetPrimitiveTopology);

	Counter all = {};
	AccumulateCounter(all, draws);
	AccumulateCounter(all, gDispatch);
	AccumulateCounter(all, gSetPipelineState);
	AccumulateCounter(all, bindings);
	AccumulateCounter(all, gResourceBarrier);

	DX12LogDebugJsonFunc("DX12ProfilingFrame",
		"\"present\":%ld,"
		"\"drawInstanced\":%ld,\"drawIndexedInstanced\":%ld,\"drawTotal\":%ld,"
		"\"dispatch\":%ld,"
		"\"setPipelineState\":%ld,"
		"\"setDescriptorHeaps\":%ld,"
		"\"setRootSignatures\":%ld,"
		"\"setRootDescriptorTables\":%ld,"
		"\"setRootCBV\":%ld,\"setRootSRV\":%ld,\"setRootUAV\":%ld,"
		"\"iaSetIndexBuffer\":%ld,\"iaSetVertexBuffers\":%ld,\"iaSetPrimitiveTopology\":%ld,"
		"\"bindingsTotal\":%ld,"
		"\"resourceBarrier\":%ld,"
		"\"executeCommandLists\":%ld,\"resetCommandList\":%ld,\"present\":%ld,"
		"\"commandsTotal\":%ld,"
		"\"fastForwardHits\":%ld,"
		"\"cpuUs_total\":%.1f,\"cpuUs_draws\":%.1f,\"cpuUs_bindings\":%.1f,\"cpuUs_dispatch\":%.1f",
		present,
		gDrawInstanced.calls, gDrawIndexedInstanced.calls, draws.calls,
		gDispatch.calls,
		gSetPipelineState.calls,
		gSetDescriptorHeaps.calls,
		gSetComputeRootSignature.calls + gSetGraphicsRootSignature.calls,
		gSetComputeRootDescriptorTable.calls + gSetGraphicsRootDescriptorTable.calls,
		gSetComputeRootConstantBufferView.calls + gSetGraphicsRootConstantBufferView.calls,
		gSetComputeRootShaderResourceView.calls + gSetGraphicsRootShaderResourceView.calls,
		gSetComputeRootUnorderedAccessView.calls + gSetGraphicsRootUnorderedAccessView.calls,
		gIASetIndexBuffer.calls, gIASetVertexBuffers.calls, gIASetPrimitiveTopology.calls,
		bindings.calls,
		gResourceBarrier.calls,
		gExecuteCommandLists.calls, gResetCommandList.calls, gPresent.calls,
		all.calls,
		gFastForwardHits.calls,
		TicksToUs(all.cpuTicks),
		TicksToUs(draws.cpuTicks),
		TicksToUs(bindings.cpuTicks),
		TicksToUs(gDispatch.cpuTicks));

	wchar_t buf[256];
	gOverlayText[0] = L'\0';
	swprintf_s(gOverlayText, L"DX12 Profiling Frame %ld\n"
		L"  Draws: %ld (I %ld + II %ld)  %s\n"
		L"  Dispatch: %ld  %s\n"
		L"  SetPSO: %ld  %s\n"
		L"  Bindings: %ld  %s\n"
		L"    DH:%ld RS:%ld RT:%ld CBV:%ld SRV:%ld UAV:%ld\n"
		L"    IB:%ld VB:%ld Topo:%ld\n"
		L"  Barriers: %ld\n"
		L"  ExecCL:%ld Reset:%ld Present:%ld\n"
		L"  Total: %ld  FF:%ld(%.0f%%)",
		present,
		draws.calls, gDrawInstanced.calls, gDrawIndexedInstanced.calls,
		FormatUs(TicksToUs(draws.cpuTicks), buf, 256),
		gDispatch.calls,
		FormatUs(TicksToUs(gDispatch.cpuTicks), buf, 256),
		gSetPipelineState.calls,
		FormatUs(TicksToUs(gSetPipelineState.cpuTicks), buf, 256),
		bindings.calls,
		FormatUs(TicksToUs(bindings.cpuTicks), buf, 256),
		gSetDescriptorHeaps.calls,
		gSetComputeRootSignature.calls + gSetGraphicsRootSignature.calls,
		gSetComputeRootDescriptorTable.calls + gSetGraphicsRootDescriptorTable.calls,
		gSetComputeRootConstantBufferView.calls + gSetGraphicsRootConstantBufferView.calls,
		gSetComputeRootShaderResourceView.calls + gSetGraphicsRootShaderResourceView.calls,
		gSetComputeRootUnorderedAccessView.calls + gSetGraphicsRootUnorderedAccessView.calls,
		gIASetIndexBuffer.calls, gIASetVertexBuffers.calls, gIASetPrimitiveTopology.calls,
		gResourceBarrier.calls,
		gExecuteCommandLists.calls, gResetCommandList.calls, gPresent.calls,
		all.calls,
		gFastForwardHits.calls,
		all.calls > 0 ? (double)gFastForwardHits.calls * 100.0 / (double)all.calls : 0.0);
}

}
