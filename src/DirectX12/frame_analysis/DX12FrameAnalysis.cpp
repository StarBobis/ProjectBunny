#include "DX12FrameAnalysis.h"

#include <Shlwapi.h>
#include <share.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "DX12Json.h"
#include "DX12State.h"

DX12FrameAnalysisState::DX12FrameAnalysisState() :
	mActive(false),
	mCaptureRequested(false),
	mCapturing(false),
	mDumpPending(false),
	mActiveFast(0),
	mCaptureRequestedFast(0),
	mCapturingFast(0),
	mLog(nullptr),
	mLineNo(0)
{
	InitializeSRWLock(&mLock);
	mPath[0] = 0;
}

DX12FrameAnalysisState::~DX12FrameAnalysisState()
{
	CloseLogLocked();
}

DX12FrameAnalysisState &DX12FrameAnalysisState::Get()
{
	static DX12FrameAnalysisState state;
	return state;
}

bool DX12FrameAnalysisState::Begin()
{
	wchar_t path[MAX_PATH];
	wchar_t subdir[MAX_PATH];
	__time64_t now;
	struct tm localTime;

	if (!GetModuleFileNameW(DX12GetModule(), path, ARRAYSIZE(path)))
		return false;
	PathRemoveFileSpecW(path);

	_time64(&now);
	if (_localtime64_s(&localTime, &now) != 0)
		return false;
	wcsftime(subdir, ARRAYSIZE(subdir), L"FrameAnalysis-%Y-%m-%d-%H%M%S", &localTime);
	PathAppendW(path, subdir);

	if (!CreateDirectoryW(path, nullptr)) {
		DWORD error = GetLastError();
		DX12Log("Error creating frame analysis directory: %lu path=%S\n", error, path);
		return false;
	}

	AcquireSRWLockExclusive(&mLock);
	CloseLogLocked();
	wcsncpy_s(mPath, path, _TRUNCATE);
	mLineNo = 0;
	mActive = true;
	mCaptureRequested = false;
	mCapturing = false;
	mDumpPending = false;
	InterlockedExchange(&mActiveFast, 1);
	InterlockedExchange(&mCaptureRequestedFast, 0);
	InterlockedExchange(&mCapturingFast, 0);
	ReleaseSRWLockExclusive(&mLock);

	DX12Log("Frame analysis directory: %S\n", path);
	return true;
}

void DX12FrameAnalysisState::End()
{
	AcquireSRWLockExclusive(&mLock);
	mActive = false;
	mCaptureRequested = false;
	mCapturing = false;
	mDumpPending = false;
	InterlockedExchange(&mActiveFast, 0);
	InterlockedExchange(&mCaptureRequestedFast, 0);
	InterlockedExchange(&mCapturingFast, 0);
	CloseLogLocked();
	ReleaseSRWLockExclusive(&mLock);
}

void DX12FrameAnalysisState::RequestCapture()
{
	AcquireSRWLockExclusive(&mLock);
	if (mActive && !mCapturing && !mDumpPending) {
		mCaptureRequested = true;
		InterlockedExchange(&mCaptureRequestedFast, 1);
	}
	ReleaseSRWLockExclusive(&mLock);
}

void DX12FrameAnalysisState::BeginCapture()
{
	AcquireSRWLockExclusive(&mLock);
	if (mActive && mCaptureRequested && !mCapturing && !mDumpPending) {
		mCaptureRequested = false;
		mCapturing = true;
		InterlockedExchange(&mCaptureRequestedFast, 0);
		InterlockedExchange(&mCapturingFast, 1);
	}
	ReleaseSRWLockExclusive(&mLock);
}

bool DX12FrameAnalysisState::EndCapture()
{
	AcquireSRWLockExclusive(&mLock);
	if (mActive && mCapturing) {
		mCapturing = false;
		mDumpPending = true;
		InterlockedExchange(&mCapturingFast, 0);
		ReleaseSRWLockExclusive(&mLock);
		return true;
	}
	ReleaseSRWLockExclusive(&mLock);
	return false;
}

bool DX12FrameAnalysisState::IsActive()
{
	return InterlockedCompareExchange(&mActiveFast, 0, 0) != 0;
}

bool DX12FrameAnalysisState::IsCaptureRequested()
{
	return InterlockedCompareExchange(&mCaptureRequestedFast, 0, 0) != 0;
}

bool DX12FrameAnalysisState::IsCapturing()
{
	return InterlockedCompareExchange(&mCapturingFast, 0, 0) != 0;
}

bool DX12FrameAnalysisState::GetPath(wchar_t *path, size_t pathCount)
{
	if (!path || pathCount == 0)
		return false;

	AcquireSRWLockShared(&mLock);
	bool ok = mPath[0] != 0;
	if (ok)
		wcsncpy_s(path, pathCount, mPath, _TRUNCATE);
	ReleaseSRWLockShared(&mLock);
	return ok;
}

bool DX12FrameAnalysisState::OpenLogLocked()
{
	wchar_t filename[MAX_PATH];

	if (mLog)
		return true;
	if (!mActive || !mPath[0])
		return false;

	swprintf_s(filename, L"%s\\log.jsonl", mPath);
	mLog = _wfsopen(filename, L"w", _SH_DENYNO);
	if (!mLog) {
		DX12Log("Error opening frame analysis log: %S\n", filename);
		return false;
	}

	fprintf(mLog, "{\"index\":0,\"func\":\"Header\",\"schema\":\"dx12-fa-log/3\",\"present_count_at_start\":%ld}\n", DX12GetPresentCount());
	return true;
}

void DX12FrameAnalysisState::CloseLogLocked()
{
	if (mLog) {
		fclose(mLog);
		mLog = nullptr;
	}
}

void DX12FrameAnalysisState::LogJsonFields(const char *fields)
{
	AcquireSRWLockExclusive(&mLock);
	if (!mActive || !OpenLogLocked()) {
		ReleaseSRWLockExclusive(&mLock);
		return;
	}

	fprintf(mLog, "{\"index\":%llu,%s}\n", ++mLineNo, fields ? fields : "\"func\":\"Unknown\"");
	fflush(mLog);
	ReleaseSRWLockExclusive(&mLock);
}

bool DX12FrameAnalysisBegin()
{
	return DX12FrameAnalysisState::Get().Begin();
}

void DX12FrameAnalysisEnd()
{
	DX12FrameAnalysisState::Get().End();
}

void DX12FrameAnalysisRequestCapture()
{
	DX12FrameAnalysisState::Get().RequestCapture();
}

void DX12FrameAnalysisBeginCapture()
{
	DX12FrameAnalysisState::Get().BeginCapture();
}

bool DX12FrameAnalysisEndCapture()
{
	return DX12FrameAnalysisState::Get().EndCapture();
}

bool DX12FrameAnalysisIsActive()
{
	return DX12FrameAnalysisState::Get().IsActive();
}

bool DX12FrameAnalysisIsCaptureRequested()
{
	return DX12FrameAnalysisState::Get().IsCaptureRequested();
}

bool DX12FrameAnalysisIsCapturing()
{
	return DX12FrameAnalysisState::Get().IsCapturing();
}

bool DX12FrameAnalysisGetPath(wchar_t *path, size_t pathCount)
{
	return DX12FrameAnalysisState::Get().GetPath(path, pathCount);
}

void DX12FrameAnalysisLogEvent(const char *fmt, ...)
{
	va_list args;
	char buffer[1024];
	char fields[4096];

	if (!DX12FrameAnalysisIsActive())
		return;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	strcpy_s(fields, sizeof(fields), "\"func\":\"Event\"");
	DX12JsonAppendLogFieldsFromText(fields, sizeof(fields), buffer);
	DX12FrameAnalysisLogJsonFields(fields);
}

void DX12FrameAnalysisLogInfo(const char *fmt, ...)
{
	va_list args;
	char buffer[1024];
	char fields[4096];

	if (!DX12FrameAnalysisIsActive())
		return;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	strcpy_s(fields, sizeof(fields), "\"func\":\"LogInfo\"");
	DX12JsonAppendLogFieldsFromText(fields, sizeof(fields), buffer);
	DX12FrameAnalysisLogJsonFields(fields);
}

void DX12FrameAnalysisLogJsonFunc(const char *func, const char *fmt, ...)
{
	if (!DX12FrameAnalysisIsActive())
		return;

	char funcJson[512];
	char extra[4096];
	char fields[4608];
	DX12JsonEscapeString(funcJson, sizeof(funcJson), func ? func : "Unknown");
	extra[0] = '\0';

	if (fmt && fmt[0]) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(extra, sizeof(extra), fmt, args);
		va_end(args);
	}

	if (extra[0])
		sprintf_s(fields, sizeof(fields), "\"func\":%s,%s", funcJson, extra);
	else
		sprintf_s(fields, sizeof(fields), "\"func\":%s", funcJson);
	DX12FrameAnalysisLogJsonFields(fields);
}

void DX12FrameAnalysisLogJsonFields(const char *fields)
{
	DX12FrameAnalysisState::Get().LogJsonFields(fields);
}
