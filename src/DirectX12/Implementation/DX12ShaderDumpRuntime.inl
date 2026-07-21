static bool ShaderInfoContainsHash(const DX12PsoShaderInfo &info, const ShaderRecord &shader)
{
	if (info.hasVS && shader.stage == "vs" && shader.hash == info.vs)
		return true;
	if (info.hasPS && shader.stage == "ps" && shader.hash == info.ps)
		return true;
	if (info.hasCS && shader.stage == "cs" && shader.hash == info.cs)
		return true;
	return false;
}

static void SnapshotShadersAndPsos(std::vector<ShaderRecord> *shaders, std::vector<PsoRecord> *psos)
{
	if (!shaders || !psos)
		return;

	AcquireSRWLockShared(&gDumpLock);
	*psos = gPsos;
	shaders->clear();
	shaders->reserve(gShaders.size());
	for (const auto &item : gShaders)
		shaders->push_back(item.second);
	ReleaseSRWLockShared(&gDumpLock);
}

static void SnapshotFrameShadersAndPsos(
	const std::vector<DX12PsoShaderInfo> &frameInfos,
	std::vector<ShaderRecord> *shaders, std::vector<PsoRecord> *psos)
{
	if (!shaders || !psos)
		return;

	shaders->clear();
	psos->clear();
	std::unordered_set<UINT64> framePsoIndexes;
	std::unordered_set<std::string> shaderKeys;
	for (const DX12PsoShaderInfo &info : frameInfos)
		framePsoIndexes.insert(info.psoIndex);

	AcquireSRWLockShared(&gDumpLock);
	for (const PsoRecord &pso : gPsos) {
		if (framePsoIndexes.find(pso.index) == framePsoIndexes.end())
			continue;
		psos->push_back(pso);
		for (const std::string &key : pso.shaders)
			shaderKeys.insert(key);
	}

	shaders->reserve(shaderKeys.size());
	for (const std::string &key : shaderKeys) {
		auto it = gShaders.find(key);
		if (it != gShaders.end())
			shaders->push_back(it->second);
	}

	for (const DX12PsoShaderInfo &info : frameInfos) {
		for (const auto &item : gShaders) {
			if (!ShaderInfoContainsHash(info, item.second))
				continue;
			if (shaderKeys.insert(item.first).second)
				shaders->push_back(item.second);
		}
	}
	ReleaseSRWLockShared(&gDumpLock);
}

static void WriteShaderUsageFiles(
	const wchar_t *dir, const std::vector<ShaderRecord> &shaders,
	const std::vector<PsoRecord> &psos)
{
	wchar_t usagePath[MAX_PATH];
	swprintf_s(usagePath, L"%s\\ShaderUsage.txt", dir);
	FILE *usage = _wfsopen(usagePath, L"w", _SH_DENYNO);
	if (usage) {
		fprintf(usage, "hash,stage,size,uses,first_pso,file,asm_file\n");
		for (const ShaderRecord &shader : shaders) {
			fprintf(usage, "%016llx,%s,%zu,%llu,%llu,%016llx-%s.bin,%016llx-%s.asm.txt\n",
				static_cast<unsigned long long>(shader.hash),
				shader.stage.c_str(),
				shader.bytecode.size(),
				static_cast<unsigned long long>(shader.useCount),
				static_cast<unsigned long long>(shader.firstPsoIndex),
				static_cast<unsigned long long>(shader.hash),
				shader.stage.c_str(),
				static_cast<unsigned long long>(shader.hash),
				shader.stage.c_str());
		}
		fclose(usage);
	}

	wchar_t psoPath[MAX_PATH];
	swprintf_s(psoPath, L"%s\\pso_log.txt", dir);
	FILE *psoLog = _wfsopen(psoPath, L"w", _SH_DENYNO);
	if (psoLog) {
		for (const PsoRecord &pso : psos) {
			fprintf(psoLog, "pso=%llu kind=%s shaders=",
				static_cast<unsigned long long>(pso.index), pso.kind.c_str());
			for (size_t i = 0; i < pso.shaders.size(); ++i)
				fprintf(psoLog, "%s%s", i ? ";" : "", pso.shaders[i].c_str());
			fprintf(psoLog, "\n");
		}
		fclose(psoLog);
	}
}

void DX12DumpFrameAnalysis()
{
	wchar_t dir[MAX_PATH];
	if (!GetDumpDirectory(dir, ARRAYSIZE(dir))) {
		DX12LogManager::Text("Failed to resolve DX12 frame analysis directory\n");
		DX12OverlayStatusState::SetStatus(L"F8 frame analysis failed: cannot create directory");
		return;
	}

	DX12OverlayStatusState::SetStatus(L"F8 frame analysis dumping");

	std::vector<ShaderRecord> shaders;
	std::vector<PsoRecord> psos;
	SnapshotShadersAndPsos(&shaders, &psos);

	DX12LogManager::Fields("FrameAnalysisDumpBegin",
		"\"shaders\":%zu,\"psos\":%zu", shaders.size(), psos.size());
	{
		char fields[512] = "";
		DX12JsonUtils::AppendWStringField(fields, sizeof(fields), "dir", dir);
		DX12JsonUtils::AppendRawField(fields, sizeof(fields), "shaders", std::to_string(shaders.size()).c_str());
		DX12JsonUtils::AppendRawField(fields, sizeof(fields), "psos", std::to_string(psos.size()).c_str());
		DX12FrameAnalysis::LogJsonFunc("FrameAnalysisSummary", "%s", fields + 1);
	}
	DX12BeginFrameAnalysisMetadataCache();
	DX12FrameAnalysisDumpResult bindingTrace;
	DX12FrameAnalysisDumpResult resourceTable;
	DX12FrameAnalysisDumpResult metadata;
	DX12FrameAnalysisDumpResult resourceFiles;
	bool psoSummaryOk = false;
	{
		DX12FrameAnalysis::LogJsonFunc("FrameAnalysisSummarySkipped",
			"\"bindingTrace\":0,\"resourceTable\":1,\"resourceMetadata\":1,\"psoResourceSummary\":1");
		bindingTrace = TryDumpBindingTrace(dir);
		resourceFiles = TryDumpCurrentFrameResources(dir);
	}
	DX12EndFrameAnalysisMetadataCache();

	DX12OverlayStatusState::SetStatus(L"F8 frame analysis complete");
	DX12LogManager::Fields("FrameAnalysisDumpComplete",
		"\"shaders\":%zu,\"psos\":%zu,\"bindingTrace\":%u,\"resourceTable\":%u,\"metadata\":%u,\"psoSummary\":%u,\"resourceFiles\":%u,\"resourceFilesRan\":%u,\"resourceFilesCompletedWithoutFailures\":%u,\"bindingTraceWritten\":%zu,\"resourceTableWritten\":%zu,\"metadataWritten\":%zu,\"resourceFilesWritten\":%zu,\"resourceFilesSkipped\":%zu,\"resourceFilesFailed\":%zu",
		shaders.size(), psos.size(),
		bindingTrace.ok ? 1 : 0,
		resourceTable.ok ? 1 : 0,
		metadata.ok ? 1 : 0,
		psoSummaryOk ? 1 : 0,
		resourceFiles.ok ? 1 : 0,
		(resourceFiles.written || resourceFiles.skipped || resourceFiles.failed) ? 1 : 0,
		resourceFiles.ok ? 1 : 0,
		bindingTrace.written,
		resourceTable.written,
		metadata.written,
		resourceFiles.written,
		resourceFiles.skipped,
		resourceFiles.failed);
	DX12FrameAnalysis::LogJsonFunc("FrameAnalysisComplete",
		"\"shaders\":%zu,\"psos\":%zu,\"bindingTrace\":%u,\"resourceTable\":%u,\"metadata\":%u,\"psoSummary\":%u,\"resourceFiles\":%u,\"resourceFilesRan\":%u,\"resourceFilesCompletedWithoutFailures\":%u,\"bindingTraceWritten\":%zu,\"resourceTableWritten\":%zu,\"metadataWritten\":%zu,\"resourceFilesWritten\":%zu,\"resourceFilesSkipped\":%zu,\"resourceFilesFailed\":%zu",
		shaders.size(), psos.size(),
		bindingTrace.ok ? 1 : 0,
		resourceTable.ok ? 1 : 0,
		metadata.ok ? 1 : 0,
		psoSummaryOk ? 1 : 0,
		resourceFiles.ok ? 1 : 0,
		(resourceFiles.written || resourceFiles.skipped || resourceFiles.failed) ? 1 : 0,
		resourceFiles.ok ? 1 : 0,
		bindingTrace.written,
		resourceTable.written,
		metadata.written,
		resourceFiles.written,
		resourceFiles.skipped,
		resourceFiles.failed);
}

static void DX12DumpShaderRecords(
	const std::vector<ShaderRecord> &shaders, const std::vector<PsoRecord> &psos,
	const wchar_t *statusPrefix)
{
	wchar_t dir[MAX_PATH];
	if (!GetShaderFixesDirectory(dir, ARRAYSIZE(dir))) {
		DX12LogManager::Text("Failed to resolve DX12 ShaderFixes directory\n");
		DX12OverlayStatusState::SetStatus(L"F9 shader dump failed: cannot create ShaderFixes");
		return;
	}

	DX12OverlayStatusState::SetStatus(L"F9 shader dump running");
	DX12LogManager::Text("F9 shader dump begin: dir=%S shaders=%zu psos=%zu mode=%S\n",
		dir, shaders.size(), psos.size(), statusPrefix ? statusPrefix : L"frame");

	UINT writtenShaders = 0;
	UINT writtenDisassembly = 0;
	UINT skippedShaders = 0;
	UINT skippedDisassembly = 0;
	for (const ShaderRecord &shader : shaders) {
		bool skipped = false;
		if (WriteShaderFile(dir, shader, false, &skipped)) {
			if (skipped)
				skippedShaders++;
			else
				writtenShaders++;
		}
		skipped = false;
		if (WriteShaderDisassemblyFile(dir, shader, false, &skipped)) {
			if (skipped)
				skippedDisassembly++;
			else
				writtenDisassembly++;
		}
	}

	WriteShaderUsageFiles(dir, shaders, psos);
	WriteShaderAnalysisFile(dir, shaders, psos);
	bool psoSummaryOk = TryWritePsoResourceSummaryFile(dir, shaders, psos);

	wchar_t status[160];
	swprintf_s(status, L"F9 %s complete: %u bin / %u asm, PSO summary %s",
		statusPrefix ? statusPrefix : L"frame", writtenShaders, writtenDisassembly,
		psoSummaryOk ? L"ok" : L"failed");
	DX12OverlayStatusState::SetStatus(status);
	DX12LogManager::Text("F9 shader dump complete: dir=%S shaders=%u/%zu skippedShaders=%u asm=%u/%zu skippedAsm=%u psos=%zu psoSummary=%u\n",
		dir, writtenShaders, shaders.size(), skippedShaders,
		writtenDisassembly, shaders.size(), skippedDisassembly, psos.size(),
		psoSummaryOk ? 1 : 0);
}

void DX12DumpCachedShaders()
{
	std::vector<ShaderRecord> shaders;
	std::vector<PsoRecord> psos;
	SnapshotShadersAndPsos(&shaders, &psos);
	DX12DumpShaderRecords(shaders, psos, L"all cached shaders");
}

struct ShaderDumpThreadArgs
{
	std::vector<ShaderRecord> shaders;
	std::vector<PsoRecord> psos;
};

static DWORD WINAPI DX12ShaderDumpThreadProc(void *param)
{
	ShaderDumpThreadArgs *args = static_cast<ShaderDumpThreadArgs*>(param);
	__try {
		if (args)
			DX12DumpShaderRecords(args->shaders, args->psos, L"frame shaders");
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		DX12LogManager::Text("F9 shader dump failed after exception=0x%lx\n", GetExceptionCode());
		DX12OverlayStatusState::SetStatus(L"F9 shader dump failed");
	}
	delete args;
	InterlockedExchange(&gShaderDumpRunning, 0);
	DX12HotPathState::Update();
	return 0;
}

void DX12RequestShaderDump()
{
	if (DX12ShaderDumpIsBusy()) {
		DX12LogManager::Text("F9 shader dump ignored: already armed/running\n");
		DX12OverlayStatusState::SetStatus(L"F9 shader dump still running");
		return;
	}

	InterlockedExchange(&gShaderDumpCaptureRequested, 1);
	DX12HotPathState::Update();
	DX12OverlayStatusState::SetStatus(L"F9 armed: waiting for next frame");
}

bool DX12ShaderDumpIsCaptureRequested()
{
	return InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 0) != 0;
}

void DX12ShaderDumpBeginCapture()
{
	if (InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 1) == 1) {
		InterlockedExchange(&gShaderDumpCapturing, 1);
		DX12HotPathState::Update();
		DX12OverlayStatusState::SetStatus(L"F9 capturing next frame shaders");
	}
}

bool DX12ShaderDumpEndCapture()
{
	if (InterlockedCompareExchange(&gShaderDumpCapturing, 0, 1) == 1) {
		DX12HotPathState::Update();
		return true;
	}
	return false;
}

bool DX12ShaderDumpIsCapturingFrame()
{
	return InterlockedCompareExchange(&gShaderDumpCapturing, 0, 0) != 0;
}

bool DX12ShaderDumpIsBusy()
{
	return InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 0) != 0 ||
		InterlockedCompareExchange(&gShaderDumpCapturing, 0, 0) != 0 ||
		InterlockedCompareExchange(&gShaderDumpRunning, 0, 0) != 0;
}

void DX12DumpCapturedFrameShaders()
{
	if (InterlockedCompareExchange(&gShaderDumpRunning, 1, 0) != 0) {
		DX12OverlayStatusState::SetStatus(L"F9 shader dump still running");
		return;
	}
	DX12HotPathState::Update();

	std::vector<DX12PsoShaderInfo> frameInfos;
	DX12BindingFrameExport::GetCurrentFrameShaderInfos(&frameInfos);
	ShaderDumpThreadArgs *args = new (std::nothrow) ShaderDumpThreadArgs();
	if (!args) {
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12HotPathState::Update();
		DX12OverlayStatusState::SetStatus(L"F9 shader dump failed: out of memory");
		return;
	}
	SnapshotFrameShadersAndPsos(frameInfos, &args->shaders, &args->psos);
	DX12LogManager::Text("F9 captured frame shaders: framePsos=%zu shaders=%zu psos=%zu\n",
		frameInfos.size(), args->shaders.size(), args->psos.size());
	if (args->shaders.empty() && args->psos.empty()) {
		delete args;
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12HotPathState::Update();
		DX12OverlayStatusState::SetStatus(L"F9 not ready: no frame shaders captured");
		return;
	}
	DX12OverlayStatusState::SetStatus(L"F9 dumping captured frame shaders");

	HANDLE thread = CreateThread(nullptr, 0, DX12ShaderDumpThreadProc, args, 0, nullptr);
	if (!thread) {
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12HotPathState::Update();
		delete args;
		DX12LogManager::Text("F9 shader dump failed: CreateThread error=%lu\n", GetLastError());
		DX12OverlayStatusState::SetStatus(L"F9 shader dump failed: cannot start thread");
		return;
	}
	CloseHandle(thread);
}
