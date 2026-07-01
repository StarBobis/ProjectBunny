#include "DX12CaptureLedger.hpp"

#include "DX12BindingTracker.hpp"
#include "DX12FrameAnalysis.hpp"

SRWLOCK DX12CaptureLedger::Lock = SRWLOCK_INIT;
bool DX12CaptureLedger::Active = false;
UINT64 DX12CaptureLedger::BeginPresent = 0;
UINT64 DX12CaptureLedger::LedgerEpoch = 0;
UINT64 DX12CaptureLedger::CaptureSessionId = 0;
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
	DroppedEvents = 0;
	DroppedOutputEvents = 0;
	EventSerial = 0;
	OutputEventSerial = 0;
	BeginPresent = present;
	++LedgerEpoch;
	++CaptureSessionId;
	Active = true;
	const UINT64 epoch = LedgerEpoch;
	const UINT64 captureSessionId = CaptureSessionId;
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
	Active = false;
	const size_t events = Events.size();
	const size_t outputs = OutputEvents.size();
	const UINT64 droppedEvents = DroppedEvents;
	const UINT64 droppedOutputs = DroppedOutputEvents;
	const UINT64 epoch = LedgerEpoch;
	const UINT64 captureSessionId = CaptureSessionId;
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
	DroppedEvents = 0;
	DroppedOutputEvents = 0;
	EventSerial = 0;
	OutputEventSerial = 0;
	Active = false;
	ReleaseSRWLockExclusive(&Lock);
}

bool DX12CaptureLedger::IsActive()
{
	AcquireSRWLockShared(&Lock);
	const bool active = Active;
	ReleaseSRWLockShared(&Lock);
	return active;
}

UINT64 DX12CaptureLedger::CurrentCaptureSessionId()
{
	AcquireSRWLockShared(&Lock);
	const UINT64 captureSessionId = Active ? CaptureSessionId : 0;
	ReleaseSRWLockShared(&Lock);
	return captureSessionId;
}

bool DX12CaptureLedger::IsSessionCurrent(UINT64 captureSessionId)
{
	if (captureSessionId == 0)
		return false;
	AcquireSRWLockShared(&Lock);
	const bool current = Active && CaptureSessionId == captureSessionId;
	ReleaseSRWLockShared(&Lock);
	return current;
}

DX12CaptureLedgerRecordResult DX12CaptureLedger::RecordEvent(BindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (!Active) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (event->captureSessionId == 0 || event->captureSessionId != CaptureSessionId) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::InactiveSession;
	}
	if (Events.size() >= MaxCaptureLedgerEvents) {
		DroppedEvents++;
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::Full;
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
	if (!Active) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (event->captureSessionId == 0 || event->captureSessionId != CaptureSessionId) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::InactiveSession;
	}
	if (OutputEvents.size() >= MaxCaptureLedgerOutputEvents) {
		DroppedOutputEvents++;
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::Full;
	}
	event->serial = ++OutputEventSerial;
	for (const OutputBindingEvent &existing : OutputEvents) {
		const bool sameResolvedDraw =
			event->drawId != 0 && existing.drawId == event->drawId;
		if (existing.commandList == event->commandList &&
			existing.cpuHandle == event->cpuHandle &&
			existing.kind == event->kind &&
			existing.slot == event->slot &&
			sameResolvedDraw &&
			existing.dispatchId == event->dispatchId) {
			ReleaseSRWLockExclusive(&Lock);
			return DX12CaptureLedgerRecordResult::Duplicate;
		}
	}
	OutputEvents.push_back(std::move(*event));
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
	snapshot->captureSessionId = CaptureSessionId;
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
