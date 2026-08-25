// Capture output ledger: dedup keys and record results.
// Split from DX12Tracking.h; keep declarations in the layer that owns them.
#pragma once

#include "DX12BindingTrackerQuery.h"

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

class DX12CaptureOutputKey
{
public:
	ID3D12GraphicsCommandList *commandList = nullptr;
	SIZE_T cpuHandle = 0;
	std::string kind;
	UINT slot = 0;
	UINT64 drawId = 0;
	UINT64 dispatchId = 0;

	bool operator==(const DX12CaptureOutputKey &other) const
	{
		return commandList == other.commandList &&
			cpuHandle == other.cpuHandle &&
			kind == other.kind &&
			slot == other.slot &&
			drawId == other.drawId &&
			dispatchId == other.dispatchId;
	}
};

class DX12CaptureOutputKeyHash
{
public:
	size_t operator()(const DX12CaptureOutputKey &key) const
	{
		size_t hash = std::hash<void*>()(
			static_cast<void*>(key.commandList));
		hash = Mix(hash, key.cpuHandle);
		hash = Mix(hash, std::hash<std::string>()(key.kind));
		hash = Mix(hash, key.slot);
		hash = Mix(hash, key.drawId);
		return Mix(hash, key.dispatchId);
	}

private:
	static size_t Mix(size_t hash, UINT64 value)
	{
		return hash ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull +
			(hash << 6) + (hash >> 2));
	}
};

class DX12CaptureLedger
{
public:
	static SRWLOCK Lock;
	// Lock-free mirror of the capture session state: 0 means no capture is
	// active, otherwise the id of the currently active capture session.
	// Maintained under Lock by the writers; read without Lock on the hot
	// submit/recording paths where it is only an advisory gate - RecordEvent
	// and TryStoreOutputEvent re-validate the session under Lock.
	static std::atomic<UINT64> ActiveSessionId;
	static UINT64 BeginPresent;
	static UINT64 LedgerEpoch;
	static std::atomic<UINT64> CaptureSessionId;
	static UINT64 EventSerial;
	static UINT64 OutputEventSerial;
	static UINT64 DroppedEvents;
	static UINT64 DroppedOutputEvents;
	static std::vector<BindingEvent> Events;
	static std::vector<OutputBindingEvent> OutputEvents;
	static std::unordered_set<DX12CaptureOutputKey, DX12CaptureOutputKeyHash>
		OutputKeys;

	static void BeginCapture(UINT64 present);
	static void EndCapture();
	static void Clear();
	static bool IsActive();
	static UINT64 CurrentCaptureSessionId();
	static UINT64 LastCaptureSessionId();
	static DX12CaptureLedgerRecordResult RecordEvent(BindingEvent *event);
	static DX12CaptureLedgerRecordResult TryStoreOutputEvent(OutputBindingEvent *event);
	static void GetSnapshot(DX12CaptureLedgerSnapshot *snapshot);
	static void ReleaseEventsLocked();
	static void LogSnapshotHealth(const char *stage, const DX12CaptureLedgerSnapshot &snapshot);
};


