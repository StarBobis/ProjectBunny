void DX12RecordGraphicsPipelineState(
	ID3D12PipelineState *pipelineState, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc)
{
	if (!desc)
		return;

	AcquireSRWLockExclusive(&gDumpLock);
	UINT64 psoIndex = ++gPsoSerial;
	PsoRecord pso;
	DX12PsoShaderInfo info = {};
	info.psoIndex = psoIndex;
	pso.kind = "graphics";
	pso.index = psoIndex;
	DX12RecordPsoRootSignature(psoIndex, pso.kind.c_str(), pipelineState, desc->pRootSignature);
	RecordShaderLocked(desc->VS, "vs", psoIndex, pso, &info);
	RecordShaderLocked(desc->PS, "ps", psoIndex, pso, &info);
	RecordShaderLocked(desc->DS, "ds", psoIndex, pso, nullptr);
	RecordShaderLocked(desc->HS, "hs", psoIndex, pso, nullptr);
	RecordShaderLocked(desc->GS, "gs", psoIndex, pso, nullptr);
	RecordPsoInputLayoutLocked(psoIndex, desc->InputLayout);
	const size_t shadersInPso = pso.shaders.size();
	gPsos.push_back(std::move(pso));
	RecordPsoShaderSummaryLocked(info);
	if (pipelineState)
		gPsoShaderInfo[pipelineState] = info;
	UINT64 shaderCount = gShaders.size();
	UINT64 psoCount = gPsos.size();
	ReleaseSRWLockExclusive(&gDumpLock);

	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateGraphicsPipelineState",
		"\"pso\":%llu,\"shaders\":%llu,\"cachedShaders\":%llu,\"cachedPsos\":%llu",
		static_cast<unsigned long long>(psoIndex),
		static_cast<unsigned long long>(shadersInPso),
		static_cast<unsigned long long>(shaderCount),
		static_cast<unsigned long long>(psoCount));
	if (!DX12ShaderDumpIsBusy())
		DX12OverlayStatusState::SetStatus(L"3DMigoto DX12 hook alive | cached shaders ready");
}

void DX12RecordComputePipelineState(
	ID3D12PipelineState *pipelineState, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
	if (!desc)
		return;

	AcquireSRWLockExclusive(&gDumpLock);
	UINT64 psoIndex = ++gPsoSerial;
	PsoRecord pso;
	DX12PsoShaderInfo info = {};
	info.psoIndex = psoIndex;
	pso.kind = "compute";
	pso.index = psoIndex;
	DX12RecordPsoRootSignature(psoIndex, pso.kind.c_str(), pipelineState, desc->pRootSignature);
	RecordShaderLocked(desc->CS, "cs", psoIndex, pso, &info);
	gPsos.push_back(std::move(pso));
	RecordPsoShaderSummaryLocked(info);
	if (pipelineState)
		gPsoShaderInfo[pipelineState] = info;
	UINT64 shaderCount = gShaders.size();
	UINT64 psoCount = gPsos.size();
	ReleaseSRWLockExclusive(&gDumpLock);

	DX12FrameAnalysis::LogJsonFunc("ID3D12Device::CreateComputePipelineState",
		"\"pso\":%llu,\"cachedShaders\":%llu,\"cachedPsos\":%llu",
		static_cast<unsigned long long>(psoIndex),
		static_cast<unsigned long long>(shaderCount),
		static_cast<unsigned long long>(psoCount));
	if (!DX12ShaderDumpIsBusy())
		DX12OverlayStatusState::SetStatus(L"3DMigoto DX12 hook alive | cached shaders ready");
}

void DX12RecordPipelineStateStream(
	ID3D12PipelineState *pipelineState, const D3D12_PIPELINE_STATE_STREAM_DESC *desc)
{
	if (!desc || !desc->pPipelineStateSubobjectStream || desc->SizeInBytes == 0)
		return;

	const uint8_t *stream = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
	size_t offset = 0;
	size_t shadersInPso = 0;

	AcquireSRWLockExclusive(&gDumpLock);
	UINT64 psoIndex = ++gPsoSerial;
	PsoRecord pso;
	DX12PsoShaderInfo info = {};
	info.psoIndex = psoIndex;
	pso.kind = "stream";
	pso.index = psoIndex;
	ID3D12RootSignature *rootSignature = nullptr;
	D3D12_INPUT_LAYOUT_DESC inputLayout = {};
	bool hasInputLayout = false;

	while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc->SizeInBytes) {
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type =
			*reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(stream + offset);
		size_t payloadSize = PipelineStateStreamPayloadSize(type);
		size_t payloadAlignment = PipelineStateStreamPayloadAlignment(type);
		size_t payloadOffset = AlignUp(offset + sizeof(type), payloadAlignment);
		if (payloadSize == 0 || payloadOffset + payloadSize > desc->SizeInBytes) {
			DX12FrameAnalysis::LogJsonFunc("PipelineStateStreamParseStopped",
				"\"pso\":%llu,\"type\":%d,\"offset\":%zu,\"size\":%zu",
				static_cast<unsigned long long>(psoIndex), static_cast<int>(type),
				offset, desc->SizeInBytes);
			break;
		}

		const char *stage = PipelineStateStreamShaderStage(type);
		if (stage) {
			const D3D12_SHADER_BYTECODE *bytecode =
				reinterpret_cast<const D3D12_SHADER_BYTECODE*>(stream + payloadOffset);
			RecordShaderLocked(*bytecode, stage, psoIndex, pso, &info);
		} else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE) {
			rootSignature = *reinterpret_cast<ID3D12RootSignature *const*>(stream + payloadOffset);
		} else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT) {
			inputLayout = *reinterpret_cast<const D3D12_INPUT_LAYOUT_DESC*>(stream + payloadOffset);
			hasInputLayout = true;
		}

		offset = AlignUp(payloadOffset + payloadSize, alignof(void*));
	}

	shadersInPso = pso.shaders.size();
	DX12RecordPsoRootSignature(psoIndex, pso.kind.c_str(), pipelineState, rootSignature);
	if (hasInputLayout)
		RecordPsoInputLayoutLocked(psoIndex, inputLayout);
	else
		RecordPsoInputLayoutLocked(psoIndex, D3D12_INPUT_LAYOUT_DESC());
	gPsos.push_back(std::move(pso));
	RecordPsoShaderSummaryLocked(info);
	if (pipelineState)
		gPsoShaderInfo[pipelineState] = info;
	UINT64 shaderCount = gShaders.size();
	UINT64 psoCount = gPsos.size();
	ReleaseSRWLockExclusive(&gDumpLock);

	DX12FrameAnalysis::LogJsonFunc("ID3D12Device2::CreatePipelineState",
		"\"pso\":%llu,\"shaders\":%llu,\"cachedShaders\":%llu,\"cachedPsos\":%llu",
		static_cast<unsigned long long>(psoIndex),
		static_cast<unsigned long long>(shadersInPso),
		static_cast<unsigned long long>(shaderCount),
		static_cast<unsigned long long>(psoCount));
	if (!DX12ShaderDumpIsBusy())
		DX12OverlayStatusState::SetStatus(L"3DMigoto DX12 hook alive | cached shaders ready");
}

bool DX12GetPipelineStateShaderInfo(ID3D12PipelineState *pipelineState, DX12PsoShaderInfo *info)
{
	if (!pipelineState || !info)
		return false;

	AcquireSRWLockShared(&gDumpLock);
	auto it = gPsoShaderInfo.find(pipelineState);
	if (it == gPsoShaderInfo.end()) {
		ReleaseSRWLockShared(&gDumpLock);
		return false;
	}
	*info = it->second;
	ReleaseSRWLockShared(&gDumpLock);
	return true;
}

bool DX12GetPsoShaderSummary(UINT64 psoIndex, DX12PsoShaderSummary *summary)
{
	if (!psoIndex || !summary)
		return false;

	AcquireSRWLockShared(&gDumpLock);
	auto it = gPsoShaderSummaryByIndex.find(psoIndex);
	if (it == gPsoShaderSummaryByIndex.end()) {
		ReleaseSRWLockShared(&gDumpLock);
		return false;
	}
	*summary = it->second;
	ReleaseSRWLockShared(&gDumpLock);
	return true;
}

bool DX12GetPsoInputLayout(UINT64 psoIndex, DX12PsoInputLayoutInfo *layout)
{
	if (!psoIndex || !layout)
		return false;

	AcquireSRWLockShared(&gDumpLock);
	auto it = gPsoInputLayoutByIndex.find(psoIndex);
	if (it == gPsoInputLayoutByIndex.end()) {
		ReleaseSRWLockShared(&gDumpLock);
		return false;
	}
	*layout = it->second;
	ReleaseSRWLockShared(&gDumpLock);
	return true;
}

