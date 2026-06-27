#pragma once

#include <Windows.h>
#include <stdio.h>
#include <wchar.h>

namespace DX12Profiling {

enum class Mode {
	NONE = 0,
	SUMMARY,
};

struct Counter {
	volatile LONG calls = 0;
	volatile LONG64 cpuTicks = 0;
};

extern Counter gDrawInstanced;
extern Counter gDrawIndexedInstanced;
extern Counter gDispatch;
extern Counter gSetPipelineState;
extern Counter gSetDescriptorHeaps;
extern Counter gSetComputeRootSignature;
extern Counter gSetGraphicsRootSignature;
extern Counter gSetComputeRootDescriptorTable;
extern Counter gSetGraphicsRootDescriptorTable;
extern Counter gSetComputeRootConstantBufferView;
extern Counter gSetGraphicsRootConstantBufferView;
extern Counter gSetComputeRootShaderResourceView;
extern Counter gSetGraphicsRootShaderResourceView;
extern Counter gSetComputeRootUnorderedAccessView;
extern Counter gSetGraphicsRootUnorderedAccessView;
extern Counter gIASetIndexBuffer;
extern Counter gIASetVertexBuffers;
extern Counter gIASetPrimitiveTopology;
extern Counter gResourceBarrier;
extern Counter gExecuteCommandLists;
extern Counter gPresent;
extern Counter gResetCommandList;
extern Counter gCloseCommandList;
extern Counter gIaDrawOverrideChecks;
extern Counter gIaHashStateHits;
extern Counter gIaHashStateMisses;
extern Counter gIaTextureMayHits;
extern Counter gIaTextureMayMisses;
extern Counter gIaPrepareCalls;
extern Counter gIaApplied;
extern Counter gIaCandidateTests;
extern Counter gIaCandidateHits;
extern Counter gIaHuntIaUpdates;
extern Counter gIaReplacementDraws;
extern Counter gIaReplacementIndexedDraws;
extern Counter gIaReplacementDispatches;
extern Counter gPreSkinCsTests;
extern Counter gPreSkinUavTests;
extern Counter gPreSkinUavHits;
extern Counter gPreSkinApplied;
extern Counter gPreSkinDispatchResized;
extern Counter gAllDraws;
extern Counter gAllDispatches;
extern Counter gAllBindings;
extern Counter gAllCommands;
extern Counter gFastForwardHits;
extern Mode gMode;
extern volatile LONG gCollectCounters;
extern volatile LONG gFramePresentCount;
extern LARGE_INTEGER gFrameStartTime;
extern LARGE_INTEGER gPerfFrequency;
extern wchar_t gOverlayText[2048];

void Init();
void BeginFrame();
void EndFrame();
void Toggle();
bool IsActive();
inline bool ShouldCollectCounters()
{
	return gCollectCounters != 0;
}
void RecordIaDrawOverrideCheck();
void RecordIaHashStateResult(bool hit);
void RecordIaTextureMayMatchResult(bool hit);
void RecordIaPrepareCall();
void RecordIaApplied();
void RecordIaCandidateTest(bool hit);
void RecordIaHuntIaUpdate();
void RecordIaReplacementDraw(bool indexed);
void RecordIaReplacementDispatch();
void RecordPreSkinCsTest(bool hit);
void RecordPreSkinUavTest(bool hit);
void RecordPreSkinApplied();
void RecordPreSkinDispatchResized();
void LogFrameStats();
void FlushFrame();

class ScopedTimer {
public:
	explicit ScopedTimer(Counter &counter);
	~ScopedTimer();
private:
	Counter &mCounter;
	LARGE_INTEGER mStart;
};

}

#define DX12_PROFILE_SCOPE(counter) \
	DX12Profiling::ScopedTimer _profile_timer(DX12Profiling::g##counter)

#define DX12_PROFILE_FAST_FORWARD() do { \
	if (DX12Profiling::ShouldCollectCounters()) \
		InterlockedIncrement(&DX12Profiling::gFastForwardHits.calls); \
} while (0)
