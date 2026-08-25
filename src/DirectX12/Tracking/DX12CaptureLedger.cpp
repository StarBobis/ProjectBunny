// DX12CaptureLedger: capture-session ledger event storage.
// Split from DX12BindingTracking.cpp; implementation unit for its module.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


SRWLOCK DX12CaptureLedger::Lock = SRWLOCK_INIT;
std::atomic<UINT64> DX12CaptureLedger::ActiveSessionId{0};
UINT64 DX12CaptureLedger::BeginPresent = 0;
UINT64 DX12CaptureLedger::LedgerEpoch = 0;
std::atomic<UINT64> DX12CaptureLedger::CaptureSessionId{0};
UINT64 DX12CaptureLedger::EventSerial = 0;
UINT64 DX12CaptureLedger::OutputEventSerial = 0;
UINT64 DX12CaptureLedger::DroppedEvents = 0;
UINT64 DX12CaptureLedger::DroppedOutputEvents = 0;
std::vector<BindingEvent> DX12CaptureLedger::Events;
std::vector<OutputBindingEvent> DX12CaptureLedger::OutputEvents;

void DX12CaptureLedger::BeginCapture(UINT64 present)
{
	AcquireSRWLockExclusive(&Lock);
	DX12CaptureLedger::ReleaseEventsLocked();
	Events.clear();
	OutputEvents.clear();
	OutputKeys.clear();
	DroppedEvents = 0;
	DroppedOutputEvents = 0;
	EventSerial = 0;
	OutputEventSerial = 0;
	BeginPresent = present;
	++LedgerEpoch;
	const UINT64 captureSessionId =
		CaptureSessionId.load(std::memory_order_relaxed) + 1;
	CaptureSessionId.store(captureSessionId, std::memory_order_relaxed);
	// Publish last: a lock-free reader that observes a non-zero
	// ActiveSessionId must see the fully-initialized session above.
	ActiveSessionId.store(captureSessionId, std::memory_order_release);
	const UINT64 epoch = LedgerEpoch;
	ReleaseSRWLockExclusive(&Lock);

	DX12FrameAnalysis::LogJsonFunc("CaptureLedgerBegin",
		"\"present\":%llu,\"epoch\":%llu,\"captureSessionId\":%llu",
		static_cast<unsigned long long>(present),
		static_cast<unsigned long long>(epoch),
		static_cast<unsigned long long>(captureSessionId));
}

void DX12CaptureLedger::EndCapture()
{
	AcquireSRWLockExclusive(&Lock);
	ActiveSessionId.store(0, std::memory_order_release);
	const size_t events = Events.size();
	const size_t outputs = OutputEvents.size();
	const UINT64 droppedEvents = DroppedEvents;
	const UINT64 droppedOutputs = DroppedOutputEvents;
	const UINT64 epoch = LedgerEpoch;
	const UINT64 captureSessionId =
		CaptureSessionId.load(std::memory_order_relaxed);
	ReleaseSRWLockExclusive(&Lock);

	DX12FrameAnalysis::LogJsonFunc("CaptureLedgerEnd",
		"\"epoch\":%llu,\"captureSessionId\":%llu,\"events\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(epoch), events, outputs,
		static_cast<unsigned long long>(captureSessionId),
		static_cast<unsigned long long>(droppedEvents),
		static_cast<unsigned long long>(droppedOutputs));
}

void DX12CaptureLedger::Clear()
{
	AcquireSRWLockExclusive(&Lock);
	DX12CaptureLedger::ReleaseEventsLocked();
	Events.clear();
	OutputEvents.clear();
	OutputKeys.clear();
	DroppedEvents = 0;
	DroppedOutputEvents = 0;
	EventSerial = 0;
	OutputEventSerial = 0;
	ActiveSessionId.store(0, std::memory_order_release);
	ReleaseSRWLockExclusive(&Lock);
}

bool DX12CaptureLedger::IsActive()
{
	return ActiveSessionId.load(std::memory_order_acquire) != 0;
}

UINT64 DX12CaptureLedger::CurrentCaptureSessionId()
{
	return ActiveSessionId.load(std::memory_order_acquire);
}

UINT64 DX12CaptureLedger::LastCaptureSessionId()
{
	return CaptureSessionId.load(std::memory_order_acquire);
}


DX12CaptureLedgerRecordResult DX12CaptureLedger::RecordEvent(BindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (ActiveSessionId.load(std::memory_order_relaxed) == 0) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (event->captureSessionId == 0 ||
		event->captureSessionId !=
			CaptureSessionId.load(std::memory_order_relaxed)) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::InactiveSession;
	}
	event->serial = ++EventSerial;
	Events.push_back(std::move(*event));
	ReleaseSRWLockExclusive(&Lock);
	return DX12CaptureLedgerRecordResult::Stored;
}

DX12CaptureLedgerRecordResult DX12CaptureLedger::TryStoreOutputEvent(OutputBindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (ActiveSessionId.load(std::memory_order_relaxed) == 0) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (event->captureSessionId == 0 ||
		event->captureSessionId !=
			CaptureSessionId.load(std::memory_order_relaxed)) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::InactiveSession;
	}
	event->serial = ++OutputEventSerial;
	DX12CaptureOutputKey key;
	if (event->drawId != 0) {
		key.commandList = event->commandList;
		key.cpuHandle = event->cpuHandle;
		key.kind = event->kind;
		key.slot = event->slot;
		key.drawId = event->drawId;
		key.dispatchId = event->dispatchId;
		if (OutputKeys.find(key) != OutputKeys.end()) {
			ReleaseSRWLockExclusive(&Lock);
			return DX12CaptureLedgerRecordResult::Duplicate;
		}
	}
	OutputEvents.push_back(std::move(*event));
	if (key.drawId != 0)
		OutputKeys.insert(std::move(key));
	ReleaseSRWLockExclusive(&Lock);
	return DX12CaptureLedgerRecordResult::Stored;
}

void DX12CaptureLedger::GetSnapshot(DX12CaptureLedgerSnapshot *snapshot)
{
	if (!snapshot)
		return;
	AcquireSRWLockShared(&Lock);
	snapshot->events = Events;
	snapshot->outputEvents = OutputEvents;
	snapshot->droppedEvents = DroppedEvents;
	snapshot->droppedOutputEvents = DroppedOutputEvents;
	snapshot->beginPresent = BeginPresent;
	snapshot->ledgerEpoch = LedgerEpoch;
	snapshot->captureSessionId =
		CaptureSessionId.load(std::memory_order_relaxed);
	ReleaseSRWLockShared(&Lock);
}

void DX12CaptureLedger::ReleaseEventsLocked()
{
	for (BindingEvent &event : Events)
		DX12BindingTracker::ReleaseEventIaResources(&event);
}

void DX12CaptureLedger::LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot)
{
	size_t draws = 0;
	size_t dispatches = 0;
	for (const BindingEvent &event : snapshot.events) {
		if (event.kind == "draw" || event.kind == "draw_indexed")
			draws++;
		else if (event.kind == "dispatch")
			dispatches++;
	}
	DX12FrameAnalysis::LogJsonFunc(stage ? stage : "CaptureLedgerSnapshot",
		"\"epoch\":%llu,\"captureSessionId\":%llu,\"begin_present\":%llu,\"events\":%zu,\"draws\":%zu,\"dispatches\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(snapshot.ledgerEpoch),
		static_cast<unsigned long long>(snapshot.captureSessionId),
		static_cast<unsigned long long>(snapshot.beginPresent),
		snapshot.events.size(), draws, dispatches, snapshot.outputEvents.size(),
		static_cast<unsigned long long>(snapshot.droppedEvents),
		static_cast<unsigned long long>(snapshot.droppedOutputEvents));
}
