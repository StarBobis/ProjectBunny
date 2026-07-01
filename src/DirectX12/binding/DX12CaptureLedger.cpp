#include "DX12CaptureLedger.hpp"

#include "DX12BindingTracker.hpp"
#include "DX12FrameAnalysis.hpp"

SRWLOCK DX12CaptureLedger::Lock = SRWLOCK_INIT;
bool DX12CaptureLedger::Active = false;
UINT64 DX12CaptureLedger::BeginPresent = 0;
UINT64 DX12CaptureLedger::LedgerEpoch = 0;
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
	BeginPresent = present;
	++LedgerEpoch;
	Active = true;
	const UINT64 epoch = LedgerEpoch;
	ReleaseSRWLockExclusive(&Lock);

	DX12FrameAnalysis::LogJsonFunc("CaptureLedgerBegin",
		"\"present\":%llu,\"epoch\":%llu",
		static_cast<unsigned long long>(present),
		static_cast<unsigned long long>(epoch));
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
	ReleaseSRWLockExclusive(&Lock);

	DX12FrameAnalysis::LogJsonFunc("CaptureLedgerEnd",
		"\"epoch\":%llu,\"events\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(epoch), events, outputs,
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

DX12CaptureLedgerRecordResult DX12CaptureLedger::RecordEvent(BindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (!Active) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (Events.size() >= MaxCaptureLedgerEvents) {
		DroppedEvents++;
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::Full;
	}
	Events.push_back(std::move(*event));
	ReleaseSRWLockExclusive(&Lock);
	return DX12CaptureLedgerRecordResult::Stored;
}

DX12CaptureLedgerRecordResult DX12CaptureLedger::RecordOutputEvent(OutputBindingEvent *event)
{
	if (!event)
		return DX12CaptureLedgerRecordResult::NotActive;

	AcquireSRWLockExclusive(&Lock);
	if (!Active) {
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::NotActive;
	}
	if (OutputEvents.size() >= MaxCaptureLedgerOutputEvents) {
		DroppedOutputEvents++;
		ReleaseSRWLockExclusive(&Lock);
		return DX12CaptureLedgerRecordResult::Full;
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
	ReleaseSRWLockShared(&Lock);
}

bool DX12CaptureLedger::TryGetView(DX12CaptureLedgerView *view)
{
	if (!view)
		return false;

	AcquireSRWLockShared(&Lock);
	view->events = &Events;
	view->outputEvents = &OutputEvents;
	view->droppedEvents = DroppedEvents;
	view->droppedOutputEvents = DroppedOutputEvents;
	view->beginPresent = BeginPresent;
	view->ledgerEpoch = LedgerEpoch;
	const bool hasContent =
		!Events.empty() || !OutputEvents.empty() ||
		DroppedEvents != 0 || DroppedOutputEvents != 0;
	ReleaseSRWLockShared(&Lock);
	return hasContent;
}

bool DX12CaptureLedger::HasOutputEvent(const OutputBindingEvent &event)
{
	AcquireSRWLockShared(&Lock);
	for (const OutputBindingEvent &existing : OutputEvents) {
		const bool sameResolvedDraw = event.drawId != 0 && existing.drawId == event.drawId;
		if (existing.commandList == event.commandList &&
			existing.cpuHandle == event.cpuHandle &&
			existing.kind == event.kind &&
			existing.slot == event.slot &&
			sameResolvedDraw &&
			existing.dispatchId == event.dispatchId) {
			ReleaseSRWLockShared(&Lock);
			return true;
		}
	}
	ReleaseSRWLockShared(&Lock);
	return false;
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
		"\"epoch\":%llu,\"begin_present\":%llu,\"events\":%zu,\"draws\":%zu,\"dispatches\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(snapshot.ledgerEpoch),
		static_cast<unsigned long long>(snapshot.beginPresent),
		snapshot.events.size(), draws, dispatches, snapshot.outputEvents.size(),
		static_cast<unsigned long long>(snapshot.droppedEvents),
		static_cast<unsigned long long>(snapshot.droppedOutputEvents));
}

void DX12CaptureLedger::LogViewHealth(const char *stage, const DX12CaptureLedgerView &view)
{
	size_t eventCount = view.events ? view.events->size() : 0;
	size_t outputCount = view.outputEvents ? view.outputEvents->size() : 0;
	size_t draws = 0;
	size_t dispatches = 0;
	if (view.events) {
		for (const BindingEvent &event : *view.events) {
			if (event.kind == "draw" || event.kind == "draw_indexed")
				draws++;
			else if (event.kind == "dispatch")
				dispatches++;
		}
	}
	DX12FrameAnalysis::LogJsonFunc(stage ? stage : "CaptureLedgerView",
		"\"epoch\":%llu,\"begin_present\":%llu,\"events\":%zu,\"draws\":%zu,\"dispatches\":%zu,\"outputs\":%zu,\"dropped_events\":%llu,\"dropped_outputs\":%llu",
		static_cast<unsigned long long>(view.ledgerEpoch),
		static_cast<unsigned long long>(view.beginPresent),
		eventCount, draws, dispatches, outputCount,
		static_cast<unsigned long long>(view.droppedEvents),
		static_cast<unsigned long long>(view.droppedOutputEvents));
}
