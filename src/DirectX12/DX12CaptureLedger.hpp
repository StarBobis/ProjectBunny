#pragma once

#include <Windows.h>

#include <vector>

#include "DX12BindingTrackerState.hpp"

class DX12CaptureLedgerSnapshot
{
public:
	std::vector<BindingEvent> events;
	std::vector<OutputBindingEvent> outputEvents;
	UINT64 droppedEvents = 0;
	UINT64 droppedOutputEvents = 0;
	UINT64 beginPresent = 0;
	UINT64 ledgerEpoch = 0;
	UINT64 captureSessionId = 0;
};

enum class DX12CaptureLedgerRecordResult
{
	NotActive = 0,
	Stored = 1,
	Duplicate = 2,
	InactiveSession = 3,
};

class DX12CaptureLedger
{
public:
	static SRWLOCK Lock;
	static bool Active;
	static UINT64 BeginPresent;
	static UINT64 LedgerEpoch;
	static UINT64 CaptureSessionId;
	static UINT64 EventSerial;
	static UINT64 OutputEventSerial;
	static UINT64 DroppedEvents;
	static UINT64 DroppedOutputEvents;
	static std::vector<BindingEvent> Events;
	static std::vector<OutputBindingEvent> OutputEvents;

	static void BeginCapture(UINT64 present);
	static void EndCapture();
	static void Clear();
	static bool IsActive();
	static UINT64 CurrentCaptureSessionId();
	static UINT64 LastCaptureSessionId();
	static bool IsSessionCurrent(UINT64 captureSessionId);
	static DX12CaptureLedgerRecordResult RecordEvent(BindingEvent *event);
	static DX12CaptureLedgerRecordResult TryStoreOutputEvent(OutputBindingEvent *event);
	static void GetSnapshot(DX12CaptureLedgerSnapshot *snapshot);
	static void ReleaseEventsLocked();
	static void LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot);
};
