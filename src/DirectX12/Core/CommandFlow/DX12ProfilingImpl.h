#pragma once

// DX12_PROFILE_* macros and out-of-line DX12Profiling member definitions.
// Split out of Core/DX12CommandFlow.h; include that umbrella header from existing code.
#include "../DX12HotPath.h"

class DX12Profiler;

#define DX12_PROFILE_FUNCTION(counter) \
	DX12Profiler::Scope _profile_timer( \
		DX12Profiler::Counter, &DX12Profiling::Get().counter)

#define DX12_PROFILE_FAST_FORWARD() do { \
	DX12Profiling::RecordFastForwardHit(); \
} while (0)

inline DX12Profiling &DX12Profiling::Get()
{
	static DX12Profiling profiling;
	return profiling;
}

inline void DX12Profiling::Counter::Reset()
{
	InterlockedExchange(&calls, 0);
	InterlockedExchange64(&cpuTicks, 0);
}

inline void DX12Profiling::Counter::AccumulateFrom(const Counter &src)
{
	InterlockedAdd(&calls, src.calls);
	InterlockedAdd64(&cpuTicks, src.cpuTicks);
}

inline void DX12Profiling::Counter::IncrementCalls()
{
	InterlockedIncrement(&calls);
}

inline void DX12Profiling::Start(State *state, Counter *counter)
{
	if (!state || !counter || !ShouldCollectCounters())
		return;
	state->counter = counter;
	if (!state->counted) {
		counter->IncrementCalls();
		state->counted = true;
	}
	state->startTime.QuadPart = 0;
	if (Get().collectTiming)
		QueryPerformanceCounter(&state->startTime);
}

inline void DX12Profiling::End(State *state)
{
	if (!state || !state->counter || state->startTime.QuadPart == 0)
		return;
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);
	InterlockedAdd64(
		&state->counter->cpuTicks,
		endTime.QuadPart - state->startTime.QuadPart);
	state->startTime.QuadPart = 0;
}

inline void DX12Profiling::Init()
{
	Get().InitState();
}

inline void DX12Profiling::BeginFrame()
{
	Get().BeginFrameState();
}

inline void DX12Profiling::EndFrame()
{
	Get().EndFrameState();
}

inline void DX12Profiling::Toggle()
{
	Get().ToggleState();
}

inline bool DX12Profiling::IsActive()
{
	return Get().IsActiveState();
}

inline bool DX12Profiling::ShouldCollectCounters()
{
	return Get().collectCounters != 0;
}

inline void DX12Profiling::RecordIaDrawOverrideCheck()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaDrawOverrideChecks.IncrementCalls();
}

inline void DX12Profiling::RecordIaHashStateResult(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	(hit ? profiling.IaHashStateHits : profiling.IaHashStateMisses).IncrementCalls();
}


inline void DX12Profiling::RecordIaApplied()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaApplied.IncrementCalls();
}


inline void DX12Profiling::RecordIaHuntIaUpdate()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaHuntIaUpdates.IncrementCalls();
}

inline void DX12Profiling::RecordIaReplacementDraw(bool indexed)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	profiling.IaReplacementDraws.IncrementCalls();
	if (indexed)
		profiling.IaReplacementIndexedDraws.IncrementCalls();
}

inline void DX12Profiling::RecordIaReplacementDispatch()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaReplacementDispatches.IncrementCalls();
}

inline void DX12Profiling::RecordPreSkinCsTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	Get().PreSkinCsTests.IncrementCalls();
	(void)hit;
}

inline void DX12Profiling::RecordPreSkinUavTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	profiling.PreSkinUavTests.IncrementCalls();
	if (hit)
		profiling.PreSkinUavHits.IncrementCalls();
}

inline void DX12Profiling::RecordPreSkinApplied()
{
	if (!ShouldCollectCounters())
		return;
	Get().PreSkinApplied.IncrementCalls();
}


inline void DX12Profiling::FlushFrame()
{
	Get().FlushFrameState();
}


inline void DX12Profiling::RecordFastForwardHit()
{
	if (!ShouldCollectCounters())
		return;
	Get().FastForwardHits.IncrementCalls();
}

inline void DX12Profiling::RecordBindingQueueTaskUs(LONG64 elapsedUs)
{
	if (!ShouldCollectCounters() || elapsedUs <= 0)
		return;
	DX12Profiling &profiling = Get();
	InterlockedIncrement(&profiling.BindingQueueTasks);
	InterlockedAdd64(&profiling.BindingQueueTaskUs, elapsedUs);
	LONG64 currentMax = profiling.BindingQueueMaxTaskUs;
	while (elapsedUs > currentMax &&
		InterlockedCompareExchange64(&profiling.BindingQueueMaxTaskUs, elapsedUs, currentMax) != currentMax) {
		currentMax = profiling.BindingQueueMaxTaskUs;
	}
}

inline void DX12Profiling::RecordBindingQueueDrainUs(LONG64 elapsedUs, LONG pending)
{
	if (!ShouldCollectCounters() || elapsedUs <= 0)
		return;
	DX12Profiling &profiling = Get();
	InterlockedIncrement(&profiling.BindingQueueDrains);
	InterlockedAdd64(&profiling.BindingQueueDrainUs, elapsedUs);
	LONG64 currentMaxUs = profiling.BindingQueueMaxDrainUs;
	while (elapsedUs > currentMaxUs &&
		InterlockedCompareExchange64(&profiling.BindingQueueMaxDrainUs, elapsedUs, currentMaxUs) != currentMaxUs) {
		currentMaxUs = profiling.BindingQueueMaxDrainUs;
	}
	LONG currentMaxPending = profiling.BindingQueueMaxPending;
	while (pending > currentMaxPending &&
		InterlockedCompareExchange(&profiling.BindingQueueMaxPending, pending, currentMaxPending) != currentMaxPending) {
		currentMaxPending = profiling.BindingQueueMaxPending;
	}
}

inline void DX12Profiling::InitState()
{
	QueryPerformanceFrequency(&perfFrequency);

	// Release hooks run for every D3D12 binding call. Keep the atomic profiling
	// counters off until the user explicitly enables profiling with F11.
	mode = Mode::NONE;
	InterlockedExchange(&collectCounters, 0);
	InterlockedExchange(&collectTiming, 0);
}

inline void DX12Profiling::BeginFrameState()
{
	if (mode != Mode::SUMMARY)
		return;

	QueryPerformanceCounter(&frameStartTime);
}

inline void DX12Profiling::EndFrameState()
{
	InterlockedIncrement(&framePresentCount);

	if (mode == Mode::SUMMARY) {
		FlushFrameState();
	} else if (kVerboseEnabled) {
		LogFrameStatsState();
	}

	if (ShouldCollectCounters())
		ResetFrame();
}

inline void DX12Profiling::ToggleState()
{
	if (mode == Mode::NONE) {
		mode = Mode::SUMMARY;
		InterlockedExchange(&collectCounters, 1);
		DX12LogManager::Text("DX12 profiling enabled (SUMMARY mode) - press F11 to toggle\n");
		DX12LogManager::Fields("DX12Profiling", "\"status\":\"enabled\",\"mode\":\"SUMMARY\"");
	} else {
		if (mode == Mode::SUMMARY)
			FlushFrameState();
		mode = Mode::NONE;
		InterlockedExchange(&collectCounters, 0);
		if (!ShouldCollectCounters())
			ResetFrame();
		DX12LogManager::Text("DX12 profiling disabled\n");
		DX12LogManager::Fields("DX12Profiling", "\"status\":\"disabled\"");
	}
}

inline bool DX12Profiling::IsActiveState() const
{
	return mode != Mode::NONE;
}

inline void DX12Profiling::ResetFrame()
{
	DrawInstanced.Reset();
	DrawIndexedInstanced.Reset();
	Dispatch.Reset();
	SetPipelineState.Reset();
	SetDescriptorHeaps.Reset();
	SetComputeRootSignature.Reset();
	SetGraphicsRootSignature.Reset();
	SetComputeRootDescriptorTable.Reset();
	SetGraphicsRootDescriptorTable.Reset();
	SetComputeRootConstantBufferView.Reset();
	SetGraphicsRootConstantBufferView.Reset();
	SetComputeRootShaderResourceView.Reset();
	SetGraphicsRootShaderResourceView.Reset();
	SetComputeRootUnorderedAccessView.Reset();
	SetGraphicsRootUnorderedAccessView.Reset();
	IASetIndexBuffer.Reset();
	IASetVertexBuffers.Reset();
	IASetPrimitiveTopology.Reset();
	ResourceBarrier.Reset();
	ExecuteCommandLists.Reset();
	Present.Reset();
	ResetCommandList.Reset();
	CloseCommandList.Reset();
	FastForwardHits.Reset();
	IaDrawOverrideChecks.Reset();
	IaHashStateHits.Reset();
	IaHashStateMisses.Reset();
	IaTextureMayHits.Reset();
	IaTextureMayMisses.Reset();
	IaPrepareCalls.Reset();
	IaApplied.Reset();
	IaCandidateTests.Reset();
	IaCandidateHits.Reset();
	IaHuntIaUpdates.Reset();
	IaReplacementDraws.Reset();
	IaReplacementIndexedDraws.Reset();
	IaReplacementDispatches.Reset();
	PreSkinCsTests.Reset();
	PreSkinUavTests.Reset();
	PreSkinUavHits.Reset();
	PreSkinApplied.Reset();
	PreSkinDispatchResized.Reset();
	InterlockedExchange64(&BindingQueueTaskUs, 0);
	InterlockedExchange64(&BindingQueueDrainUs, 0);
	InterlockedExchange64(&BindingQueueMaxTaskUs, 0);
	InterlockedExchange64(&BindingQueueMaxDrainUs, 0);
	InterlockedExchange(&BindingQueueTasks, 0);
	InterlockedExchange(&BindingQueueDrains, 0);
	InterlockedExchange(&BindingQueueMaxPending, 0);
}

inline double DX12Profiling::TicksToUs(LONG64 ticks) const
{
	if (perfFrequency.QuadPart == 0)
		return 0.0;
	return static_cast<double>(ticks) * 1000000.0 /
		static_cast<double>(perfFrequency.QuadPart);
}

inline const wchar_t *DX12Profiling::FormatUs(double us, wchar_t *buf, size_t bufSize) const
{
	if (us < 1000.0)
		swprintf_s(buf, bufSize, L"%6.1f us", us);
	else if (us < 1000000.0)
		swprintf_s(buf, bufSize, L"%6.1f ms", us / 1000.0);
	else
		swprintf_s(buf, bufSize, L"%6.1f  s", us / 1000000.0);
	return buf;
}

inline void DX12Profiling::LogFrameStatsState()
{
	LONG present = framePresentCount;

	LONG draws = DrawInstanced.calls + DrawIndexedInstanced.calls;
	LONG bindings =
		SetDescriptorHeaps.calls +
		SetComputeRootSignature.calls + SetGraphicsRootSignature.calls +
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls +
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls +
		SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls +
		SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls +
		IASetIndexBuffer.calls + IASetVertexBuffers.calls + IASetPrimitiveTopology.calls;
	LONG total = draws + Dispatch.calls + SetPipelineState.calls + bindings +
		ResourceBarrier.calls;

}

inline void DX12Profiling::FlushFrameState()
{
	if (mode != Mode::SUMMARY)
		return;

	LONG present = framePresentCount;

	Counter draws = {};
	draws.AccumulateFrom(DrawInstanced);
	draws.AccumulateFrom(DrawIndexedInstanced);

	Counter bindings = {};
	bindings.AccumulateFrom(SetDescriptorHeaps);
	bindings.AccumulateFrom(SetComputeRootSignature);
	bindings.AccumulateFrom(SetGraphicsRootSignature);
	bindings.AccumulateFrom(SetComputeRootDescriptorTable);
	bindings.AccumulateFrom(SetGraphicsRootDescriptorTable);
	bindings.AccumulateFrom(SetComputeRootConstantBufferView);
	bindings.AccumulateFrom(SetGraphicsRootConstantBufferView);
	bindings.AccumulateFrom(SetComputeRootShaderResourceView);
	bindings.AccumulateFrom(SetGraphicsRootShaderResourceView);
	bindings.AccumulateFrom(SetComputeRootUnorderedAccessView);
	bindings.AccumulateFrom(SetGraphicsRootUnorderedAccessView);
	bindings.AccumulateFrom(IASetIndexBuffer);
	bindings.AccumulateFrom(IASetVertexBuffers);
	bindings.AccumulateFrom(IASetPrimitiveTopology);

	Counter all = {};
	all.AccumulateFrom(draws);
	all.AccumulateFrom(Dispatch);
	all.AccumulateFrom(SetPipelineState);
	all.AccumulateFrom(bindings);
	all.AccumulateFrom(ResourceBarrier);


	wchar_t drawsBuf[32];
	wchar_t dispatchBuf[32];
	wchar_t setPsoBuf[32];
	wchar_t bindingsBuf[32];
	wchar_t queueTaskBuf[32];
	wchar_t queueDrainBuf[32];
	overlayText[0] = L'\0';
	swprintf_s(overlayText, L"DX12 Profiling Frame %ld\n"
		L"  Draws: %ld (I %ld + II %ld)  %s\n"
		L"  Dispatch: %ld  %s\n"
		L"  SetPSO: %ld  %s\n"
		L"  Bindings: %ld  %s\n"
		L"    DH:%ld RS:%ld RT:%ld CBV:%ld SRV:%ld UAV:%ld\n"
		L"    IB:%ld VB:%ld Topo:%ld\n"
		L"  Barriers: %ld\n"
		L"  ExecCL:%ld Reset:%ld Present:%ld\n"
		L"  BindingQueue: tasks=%ld %s drains=%ld %s maxPending=%ld\n"
		L"  Total: %ld  FF:%ld(%.0f%%)",
		present,
		draws.calls, DrawInstanced.calls, DrawIndexedInstanced.calls,
		FormatUs(TicksToUs(draws.cpuTicks), drawsBuf, ARRAYSIZE(drawsBuf)),
		Dispatch.calls,
		FormatUs(TicksToUs(Dispatch.cpuTicks), dispatchBuf, ARRAYSIZE(dispatchBuf)),
		SetPipelineState.calls,
		FormatUs(TicksToUs(SetPipelineState.cpuTicks), setPsoBuf, ARRAYSIZE(setPsoBuf)),
		bindings.calls,
		FormatUs(TicksToUs(bindings.cpuTicks), bindingsBuf, ARRAYSIZE(bindingsBuf)),
		SetDescriptorHeaps.calls,
		SetComputeRootSignature.calls + SetGraphicsRootSignature.calls,
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls,
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls,
		SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls,
		SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls,
		IASetIndexBuffer.calls, IASetVertexBuffers.calls, IASetPrimitiveTopology.calls,
		ResourceBarrier.calls,
		ExecuteCommandLists.calls, ResetCommandList.calls, Present.calls,
		BindingQueueTasks,
		FormatUs(static_cast<double>(BindingQueueTaskUs), queueTaskBuf, ARRAYSIZE(queueTaskBuf)),
		BindingQueueDrains,
		FormatUs(static_cast<double>(BindingQueueDrainUs), queueDrainBuf, ARRAYSIZE(queueDrainBuf)),
		BindingQueueMaxPending,
		all.calls,
		FastForwardHits.calls,
		all.calls > 0 ? (double)FastForwardHits.calls * 100.0 / (double)all.calls : 0.0);
}

