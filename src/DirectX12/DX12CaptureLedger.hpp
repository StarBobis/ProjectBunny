#pragma once

#include <Windows.h>

#include <vector>

#include "DX12BindingTrackerState.hpp"

constexpr UINT MaxCaptureLedgerEvents = MaxTrackedEvents * 8;
constexpr UINT MaxCaptureLedgerOutputEvents = MaxTrackedOutputEvents * 8;

class DX12CaptureLedgerSnapshot
{
public:
	std::vector<BindingEvent> events;
	std::vector<OutputBindingEvent> outputEvents;
	UINT64 droppedEvents = 0;
	UINT64 droppedOutputEvents = 0;
	UINT64 beginPresent = 0;
	UINT64 ledgerEpoch = 0;
};

class DX12CaptureLedger
{
public:
	static SRWLOCK Lock;
	static bool Active;
	static UINT64 BeginPresent;
	static UINT64 LedgerEpoch;
	static UINT64 DroppedEvents;
	static UINT64 DroppedOutputEvents;
	static std::vector<BindingEvent> Events;
	static std::vector<OutputBindingEvent> OutputEvents;

	static void BeginCapture(UINT64 present);
	static void EndCapture();
	static void Clear();
	static bool IsActive();
	static void RecordEvent(const BindingEvent &event);
	static void RecordOutputEvent(const OutputBindingEvent &event);
	static void GetSnapshot(DX12CaptureLedgerSnapshot *snapshot);
	static void ReleaseEventsLocked();
	static void LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot);
};
