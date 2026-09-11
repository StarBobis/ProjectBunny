#pragma once

// Frame-analysis runtime state (capture, marking, selection).
// Split out of Core/DX12CommandState.h; include that umbrella header from existing code.
#include "../DX12Diagnostics.h"

class DX12FrameAnalysisState
{
public:
	inline static SRWLOCK lock = SRWLOCK_INIT;
	inline static bool active = false;
	inline static bool captureRequested = false;
	inline static bool capturing = false;
	inline static bool dumpPending = false;
	inline static UINT captureFramesRemaining = 0;
	inline static std::atomic<LONG> fastStateFlags{ 0 };
	inline static wchar_t path[MAX_PATH] = {};
	inline static FILE *log = nullptr;
	inline static unsigned long long lineNo = 0;
	inline static constexpr UINT CaptureFramesPerDump = 1;
	static constexpr LONG FLAG_ACTIVE = 1 << 0;
	static constexpr LONG FLAG_CAPTURE_REQUESTED = 1 << 1;
	static constexpr LONG FLAG_CAPTURING = 1 << 2;

	static void PublishFastStateLocked()
	{
		LONG flags = 0;
		if (active)
			flags |= FLAG_ACTIVE;
		if (captureRequested)
			flags |= FLAG_CAPTURE_REQUESTED;
		if (capturing)
			flags |= FLAG_CAPTURING;
		fastStateFlags.store(flags, std::memory_order_release);
	}

	static LONG GetFastFlags()
	{
		return fastStateFlags.load(std::memory_order_acquire);
	}

	static bool Begin()
	{
		wchar_t modulePath[MAX_PATH];
		wchar_t subdir[MAX_PATH];
		__time64_t now;
		struct tm localTime;

		if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), modulePath, ARRAYSIZE(modulePath)))
			return false;
		PathRemoveFileSpecW(modulePath);

		_time64(&now);
		if (_localtime64_s(&localTime, &now) != 0)
			return false;
		wcsftime(subdir, ARRAYSIZE(subdir), L"FrameAnalysis-%Y-%m-%d-%H%M%S", &localTime);
		PathAppendW(modulePath, subdir);

		if (!CreateDirectoryW(modulePath, nullptr)) {
			DWORD error = GetLastError();
			DX12_LOG_TEXT(
				"Error creating frame analysis directory: %lu path=%S\n", error, modulePath);
			return false;
		}

		AcquireSRWLockExclusive(&lock);
		CloseLogLocked();
		wcsncpy_s(path, modulePath, _TRUNCATE);
		lineNo = 0;
		active = true;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		PublishFastStateLocked();
		ReleaseSRWLockExclusive(&lock);

		DX12_LOG_TEXT("Frame analysis directory: %S\n", modulePath);
		return true;
	}

	static void End()
	{
		AcquireSRWLockExclusive(&lock);
		active = false;
		captureRequested = false;
		capturing = false;
		dumpPending = false;
		captureFramesRemaining = 0;
		PublishFastStateLocked();
		CloseLogLocked();
		ReleaseSRWLockExclusive(&lock);
	}

	static void RequestCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && !capturing && !dumpPending) {
			captureRequested = true;
			PublishFastStateLocked();
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static void BeginCapture()
	{
		AcquireSRWLockExclusive(&lock);
		if (active && captureRequested && !capturing && !dumpPending) {
			captureRequested = false;
			capturing = true;
			captureFramesRemaining = CaptureFramesPerDump;
			PublishFastStateLocked();
		}
		ReleaseSRWLockExclusive(&lock);
	}

	static bool EndCapture()
	{
		// Fast gate: capture is inactive in the overwhelmingly common case, and
		// the locked path below would otherwise be an unconditional exclusive
		// lock on the per-Present boundary. The flag is published under the
		// same lock, so a zero read means the locked body would return false.
		if (!(GetFastFlags() & FLAG_ACTIVE))
			return false;
		AcquireSRWLockExclusive(&lock);
		if (active && capturing) {
			if (captureFramesRemaining > 1) {
				captureFramesRemaining--;
				ReleaseSRWLockExclusive(&lock);
				return false;
			}
			capturing = false;
			dumpPending = true;
			captureFramesRemaining = 0;
			PublishFastStateLocked();
			ReleaseSRWLockExclusive(&lock);
			return true;
		}
		ReleaseSRWLockExclusive(&lock);
		return false;
	}

	static bool IsActive()
	{
		return (GetFastFlags() & FLAG_ACTIVE) != 0;
	}

	static bool IsCaptureRequested()
	{
		return (GetFastFlags() & FLAG_CAPTURE_REQUESTED) != 0;
	}

	static bool IsCapturing()
	{
		return (GetFastFlags() & FLAG_CAPTURING) != 0;
	}

	// A draw must not be forwarded past the interpreter while a capture is
	// either running or already requested but not yet started. Both bits come
	// from the same published word, so testing them together is one load
	// instead of one load per query on the per-draw path.
	static bool CapturesDraws()
	{
		return (GetFastFlags() &
			(FLAG_CAPTURING | FLAG_CAPTURE_REQUESTED)) != 0;
	}

	static bool GetPath(wchar_t *outPath, size_t pathCount)
	{
		if (!outPath || pathCount == 0)
			return false;

		AcquireSRWLockShared(&lock);
		bool ok = path[0] != 0;
		if (ok)
			wcsncpy_s(outPath, pathCount, path, _TRUNCATE);
		ReleaseSRWLockShared(&lock);
		return ok;
	}

	static bool OpenLogLocked()
	{
		wchar_t filename[MAX_PATH];

		if (log)
			return true;
		if (!active || !path[0])
			return false;

		swprintf_s(filename, L"%s\\log.jsonl", path);
		log = _wfsopen(filename, L"w", _SH_DENYNO);
		if (!log) {
			DX12_LOG_TEXT("Error opening frame analysis log: %S\n", filename);
			return false;
		}

		fprintf(log,
			"{\"index\":0,\"func\":\"Header\",\"schema\":\"dx12-fa-log/3\",\"present_count_at_start\":%ld}\n",
			DX12RuntimeState::GetPresentCount());
		return true;
	}

	static void CloseLogLocked()
	{
		if (log) {
			fclose(log);
			log = nullptr;
		}
	}

	static void LogJsonFields(const char *fields)
	{
		AcquireSRWLockExclusive(&lock);
		if (!active || !OpenLogLocked()) {
			ReleaseSRWLockExclusive(&lock);
			return;
		}

		fprintf(log, "{\"index\":%llu,%s}\n", ++lineNo,
			fields ? fields : "\"func\":\"Unknown\"");
		fflush(log);
		ReleaseSRWLockExclusive(&lock);
	}
};


bool DX12ShaderDumpIsCapturingFrame();
bool DX12ShaderDumpIsCaptureRequested();
bool DX12ShaderDumpIsBusy();
bool DX12HuntIsEnabled();
bool DX12HuntNeedsHotPath();
bool DX12ModHasActiveShaderOverrides();
bool DX12ModNeedsPresentReplacement();
bool DX12ModPresentReplacementPendingForCurrentPresent();
bool DX12ModNeedsPreSkinningUavProbe();
bool DX12ModNeedsPreSkinningBindingTracking();
bool DX12ModNeedsShaderDescriptorTracking();
UINT64 DX12ModGetReloadGenerationFast();
bool DX12ModHasPotentialIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);
bool DX12ModHasPotentialHashedIaTextureOverrideTargetByCount(
	bool indexBuffer,
	uint32_t vertexSlot,
	uint32_t vertexCount,
	uint32_t indexCount);
