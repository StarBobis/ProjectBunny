#pragma once

// Frame/present profiling counters and tick-to-us formatting helpers.
// Split out of Core/DX12HotPath.h; include that umbrella header from existing code.
#include "../DX12CommandState.h"

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
	static void RecordIaApplied();
	static void RecordIaHuntIaUpdate();
	static void RecordIaReplacementDraw(bool indexed);
	static void RecordIaReplacementDispatch();
	static void RecordPreSkinCsTest(bool hit);
	static void RecordPreSkinUavTest(bool hit);
	static void RecordPreSkinApplied();
	static void FlushFrame();
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
