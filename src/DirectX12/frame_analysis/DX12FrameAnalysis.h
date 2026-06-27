#pragma once

#include <Windows.h>
#include <stdio.h>

class DX12FrameAnalysisState
{
public:
	static DX12FrameAnalysisState &Get();

	bool Begin();
	void End();
	void RequestCapture();
	void BeginCapture();
	bool EndCapture();
	bool IsActive();
	bool IsCaptureRequested();
	bool IsCapturing();
	bool GetPath(wchar_t *path, size_t pathCount);
	void LogJsonFields(const char *fields);

private:
	DX12FrameAnalysisState();
	~DX12FrameAnalysisState();
	DX12FrameAnalysisState(const DX12FrameAnalysisState&) = delete;
	DX12FrameAnalysisState &operator=(const DX12FrameAnalysisState&) = delete;

	bool OpenLogLocked();
	void CloseLogLocked();

	SRWLOCK mLock;
	bool mActive;
	bool mCaptureRequested;
	bool mCapturing;
	bool mDumpPending;
	volatile LONG mActiveFast;
	volatile LONG mCaptureRequestedFast;
	volatile LONG mCapturingFast;
	wchar_t mPath[MAX_PATH];
	FILE *mLog;
	unsigned long long mLineNo;
};

bool DX12FrameAnalysisBegin();
void DX12FrameAnalysisEnd();
void DX12FrameAnalysisRequestCapture();
void DX12FrameAnalysisBeginCapture();
bool DX12FrameAnalysisEndCapture();
bool DX12FrameAnalysisIsActive();
bool DX12FrameAnalysisIsCaptureRequested();
bool DX12FrameAnalysisIsCapturing();
bool DX12FrameAnalysisGetPath(wchar_t *path, size_t pathCount);
void DX12FrameAnalysisLogEvent(const char *fmt, ...);
void DX12FrameAnalysisLogInfo(const char *fmt, ...);
void DX12FrameAnalysisLogJsonFunc(const char *func, const char *fmt, ...);
void DX12FrameAnalysisLogJsonFields(const char *fields);
