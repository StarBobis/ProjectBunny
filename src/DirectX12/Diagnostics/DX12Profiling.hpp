#pragma once

#include "Runtime/DX12RuntimeCore.hpp"

class DX12Profiling
{
public:
	enum class Mode {
		NONE = 0,
		SUMMARY,
	};

	class Counter
	{
	public:
		volatile LONG calls = 0;
		volatile LONG64 cpuTicks = 0;

		void Reset();
		void AccumulateFrom(const Counter &src);
		void IncrementCalls();
	};

	struct State
	{
		Counter *counter = nullptr;
		LARGE_INTEGER startTime = {};
		bool counted = false;
	};

	class ScopedTimer
	{
	public:
		State state;

		explicit ScopedTimer(Counter &counter);
		~ScopedTimer();
		void Pause();
		void Resume();
		void Finish();
	};

	Counter DrawInstanced;
	Counter DrawIndexedInstanced;
	Counter Dispatch;
	Counter SetPipelineState;
	Counter SetDescriptorHeaps;
	Counter SetComputeRootSignature;
	Counter SetGraphicsRootSignature;
	Counter SetComputeRootDescriptorTable;
	Counter SetGraphicsRootDescriptorTable;
	Counter SetComputeRootConstantBufferView;
	Counter SetGraphicsRootConstantBufferView;
	Counter SetComputeRootShaderResourceView;
	Counter SetGraphicsRootShaderResourceView;
	Counter SetComputeRootUnorderedAccessView;
	Counter SetGraphicsRootUnorderedAccessView;
	Counter IASetIndexBuffer;
	Counter IASetVertexBuffers;
	Counter IASetPrimitiveTopology;
	Counter ResourceBarrier;
	Counter ExecuteCommandLists;
	Counter Present;
	Counter ResetCommandList;
	Counter CloseCommandList;
	Counter IaDrawOverrideChecks;
	Counter IaHashStateHits;
	Counter IaHashStateMisses;
	Counter IaTextureMayHits;
	Counter IaTextureMayMisses;
	Counter IaPrepareCalls;
	Counter IaApplied;
	Counter IaCandidateTests;
	Counter IaCandidateHits;
	Counter IaHuntIaUpdates;
	Counter IaReplacementDraws;
	Counter IaReplacementIndexedDraws;
	Counter IaReplacementDispatches;
	Counter PreSkinCsTests;
	Counter PreSkinUavTests;
	Counter PreSkinUavHits;
	Counter PreSkinApplied;
	Counter PreSkinDispatchResized;
	Counter AllDraws;
	Counter AllDispatches;
	Counter AllBindings;
	Counter AllCommands;
	Counter FastForwardHits;
	volatile LONG64 BindingQueueTaskUs = 0;
	volatile LONG64 BindingQueueDrainUs = 0;
	volatile LONG64 BindingQueueMaxTaskUs = 0;
	volatile LONG64 BindingQueueMaxDrainUs = 0;
	volatile LONG BindingQueueTasks = 0;
	volatile LONG BindingQueueDrains = 0;
	volatile LONG BindingQueueMaxPending = 0;

	Mode mode = Mode::SUMMARY;
	volatile LONG collectCounters = 0;
	volatile LONG collectTiming = 0;
	volatile LONG framePresentCount = 0;
	LARGE_INTEGER frameStartTime = {};
	LARGE_INTEGER perfFrequency = {};
	wchar_t overlayText[2048] = L"";

	static DX12Profiling &Get();
	static void Init();
	static void BeginFrame();
	static void EndFrame();
	static void Toggle();
	static bool IsActive();
	static bool ShouldCollectCounters();
	static void Start(State *state, Counter *counter);
	static void End(State *state);
	static void RecordIaDrawOverrideCheck();
	static void RecordIaHashStateResult(bool hit);
	static void RecordIaTextureMayMatchResult(bool hit);
	static void RecordIaPrepareCall();
	static void RecordIaApplied();
	static void RecordIaCandidateTest(bool hit);
	static void RecordIaHuntIaUpdate();
	static void RecordIaReplacementDraw(bool indexed);
	static void RecordIaReplacementDispatch();
	static void RecordPreSkinCsTest(bool hit);
	static void RecordPreSkinUavTest(bool hit);
	static void RecordPreSkinApplied();
	static void RecordPreSkinDispatchResized();
	static void LogFrameStats();
	static void FlushFrame();
	static const wchar_t *OverlayText();
	static void RecordFastForwardHit();
	static void RecordBindingQueueTaskUs(LONG64 elapsedUs);
	static void RecordBindingQueueDrainUs(LONG64 elapsedUs, LONG pending);

	void InitState();
	void BeginFrameState();
	void EndFrameState();
	void ToggleState();
	bool IsActiveState() const;
	void ResetFrame();
	void LogFrameStatsState();
	void FlushFrameState();
	double TicksToUs(LONG64 ticks) const;
	const wchar_t *FormatUs(double us, wchar_t *buf, size_t bufSize) const;
};

#define DX12_PROFILE_FUNCTION(counter) \
	DX12Profiling::ScopedTimer _profile_timer(DX12Profiling::Get().counter)

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

inline DX12Profiling::ScopedTimer::ScopedTimer(Counter &counter) :
	state()
{
	DX12Profiling::Start(&state, &counter);
}

inline DX12Profiling::ScopedTimer::~ScopedTimer()
{
	Finish();
}

inline void DX12Profiling::ScopedTimer::Pause()
{
	DX12Profiling::End(&state);
}

inline void DX12Profiling::ScopedTimer::Resume()
{
	DX12Profiling::Start(&state, state.counter);
}

inline void DX12Profiling::ScopedTimer::Finish()
{
	DX12Profiling::End(&state);
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

inline void DX12Profiling::RecordIaTextureMayMatchResult(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	(hit ? profiling.IaTextureMayHits : profiling.IaTextureMayMisses).IncrementCalls();
}

inline void DX12Profiling::RecordIaPrepareCall()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaPrepareCalls.IncrementCalls();
}

inline void DX12Profiling::RecordIaApplied()
{
	if (!ShouldCollectCounters())
		return;
	Get().IaApplied.IncrementCalls();
}

inline void DX12Profiling::RecordIaCandidateTest(bool hit)
{
	if (!ShouldCollectCounters())
		return;
	DX12Profiling &profiling = Get();
	profiling.IaCandidateTests.IncrementCalls();
	if (hit)
		profiling.IaCandidateHits.IncrementCalls();
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

inline void DX12Profiling::RecordPreSkinDispatchResized()
{
	if (!ShouldCollectCounters())
		return;
	Get().PreSkinDispatchResized.IncrementCalls();
}

inline void DX12Profiling::LogFrameStats()
{
	Get().LogFrameStatsState();
}

inline void DX12Profiling::FlushFrame()
{
	Get().FlushFrameState();
}

inline const wchar_t *DX12Profiling::OverlayText()
{
	return Get().overlayText;
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
	InterlockedExchange(&collectCounters, 1);
#if defined(_DEBUG)
	InterlockedExchange(&collectTiming, 1);
#else
	InterlockedExchange(&collectTiming, 0);
#endif
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
	} else if (DX12LogManager::VerboseDiagnosticsEnabled()) {
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

	DX12LogManager::DebugFields("DX12FrameStats",
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
		draws, Dispatch.calls, SetPipelineState.calls, bindings,
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls,
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls +
			SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls +
			SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls,
		IASetIndexBuffer.calls + IASetVertexBuffers.calls + IASetPrimitiveTopology.calls,
		ResourceBarrier.calls,
		ExecuteCommandLists.calls, ResetCommandList.calls,
		total, FastForwardHits.calls,
		IaDrawOverrideChecks.calls, IaHashStateHits.calls, IaHashStateMisses.calls,
		IaTextureMayHits.calls, IaTextureMayMisses.calls,
		IaPrepareCalls.calls, IaApplied.calls,
		IaCandidateTests.calls, IaCandidateHits.calls, IaHuntIaUpdates.calls,
		IaReplacementDraws.calls, IaReplacementIndexedDraws.calls,
		IaReplacementDispatches.calls,
		PreSkinCsTests.calls, PreSkinUavTests.calls, PreSkinUavHits.calls,
		PreSkinApplied.calls, PreSkinDispatchResized.calls);
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

	DX12LogManager::DebugFields("DX12ProfilingFrame",
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
		"\"executeCommandLists\":%ld,\"resetCommandList\":%ld,\"presentCalls\":%ld,"
		"\"commandsTotal\":%ld,"
		"\"fastForwardHits\":%ld,"
		"\"cpuUs_total\":%.1f,\"cpuUs_draws\":%.1f,\"cpuUs_bindings\":%.1f,\"cpuUs_dispatch\":%.1f,"
		"\"bindingQueueTasks\":%ld,\"bindingQueueTaskUs\":%.1f,\"bindingQueueMaxTaskUs\":%.1f,"
		"\"bindingQueueDrains\":%ld,\"bindingQueueDrainUs\":%.1f,\"bindingQueueMaxDrainUs\":%.1f,"
		"\"bindingQueueMaxPending\":%ld",
		present,
		DrawInstanced.calls, DrawIndexedInstanced.calls, draws.calls,
		Dispatch.calls,
		SetPipelineState.calls,
		SetDescriptorHeaps.calls,
		SetComputeRootSignature.calls + SetGraphicsRootSignature.calls,
		SetComputeRootDescriptorTable.calls + SetGraphicsRootDescriptorTable.calls,
		SetComputeRootConstantBufferView.calls + SetGraphicsRootConstantBufferView.calls,
		SetComputeRootShaderResourceView.calls + SetGraphicsRootShaderResourceView.calls,
		SetComputeRootUnorderedAccessView.calls + SetGraphicsRootUnorderedAccessView.calls,
		IASetIndexBuffer.calls, IASetVertexBuffers.calls, IASetPrimitiveTopology.calls,
		bindings.calls,
		ResourceBarrier.calls,
		ExecuteCommandLists.calls, ResetCommandList.calls, Present.calls,
		all.calls,
		FastForwardHits.calls,
		TicksToUs(all.cpuTicks),
		TicksToUs(draws.cpuTicks),
		TicksToUs(bindings.cpuTicks),
		TicksToUs(Dispatch.cpuTicks),
		BindingQueueTasks,
		static_cast<double>(BindingQueueTaskUs),
		static_cast<double>(BindingQueueMaxTaskUs),
		BindingQueueDrains,
		static_cast<double>(BindingQueueDrainUs),
		static_cast<double>(BindingQueueMaxDrainUs),
		BindingQueueMaxPending);

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
