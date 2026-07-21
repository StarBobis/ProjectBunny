


struct ShaderRecord
{
	std::string stage;
	uint64_t hash = 0;
	std::vector<uint8_t> bytecode;
	UINT64 firstPsoIndex = 0;
	UINT64 useCount = 0;
};

struct PsoRecord
{
	std::string kind;
	UINT64 index = 0;
	std::vector<std::string> shaders;
};

static SRWLOCK gDumpLock = SRWLOCK_INIT;
static std::unordered_map<std::string, ShaderRecord> gShaders;
static std::vector<PsoRecord> gPsos;
static std::unordered_map<ID3D12PipelineState*, DX12PsoShaderInfo> gPsoShaderInfo;
static std::unordered_map<UINT64, DX12PsoShaderSummary> gPsoShaderSummaryByIndex;
static std::unordered_map<UINT64, DX12PsoInputLayoutInfo> gPsoInputLayoutByIndex;
static UINT64 gPsoSerial = 0;
static volatile LONG gShaderDumpRunning = 0;
static volatile LONG gShaderDumpCaptureRequested = 0;
static volatile LONG gShaderDumpCapturing = 0;
static volatile LONG gShaderDumpStateFlags = 0;
static constexpr LONG DX12_SHADER_DUMP_STATE_CAPTURE_REQUESTED = 1 << 0;
static constexpr LONG DX12_SHADER_DUMP_STATE_CAPTURING = 1 << 1;
static constexpr LONG DX12_SHADER_DUMP_STATE_RUNNING = 1 << 2;

typedef HRESULT(WINAPI *PFN_D3D_DISASSEMBLE)(
	LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);

static HMODULE gD3DCompiler = nullptr;
static PFN_D3D_DISASSEMBLE gD3DDisassemble = nullptr;

static HMODULE gDXCompiler = nullptr;
static DxcCreateInstanceProc gDxcCreateInstance = nullptr;
static IDxcCompiler3 *gDxcCompiler = nullptr;

static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
		(static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
		(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
		(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

static bool ReadU32LE(const uint8_t *data, size_t size, size_t offset, uint32_t *value)
{
	if (!value || offset + sizeof(uint32_t) > size)
		return false;
	memcpy(value, data + offset, sizeof(uint32_t));
	return true;
}

static bool ShaderHasChunk(const std::vector<uint8_t> &bytecode, uint32_t fourCC)
{
	if (bytecode.size() < 32 || memcmp(bytecode.data(), "DXBC", 4))
		return false;

	uint32_t chunkCount = 0;
	if (!ReadU32LE(bytecode.data(), bytecode.size(), 28, &chunkCount))
		return false;

	for (uint32_t i = 0; i < chunkCount; ++i) {
		uint32_t chunkOffset = 0;
		if (!ReadU32LE(bytecode.data(), bytecode.size(), 32 + sizeof(uint32_t) * i, &chunkOffset))
			return false;
		if (chunkOffset + sizeof(uint32_t) > bytecode.size())
			continue;
		uint32_t chunkFourCC = 0;
		memcpy(&chunkFourCC, bytecode.data() + chunkOffset, sizeof(chunkFourCC));
		if (chunkFourCC == fourCC)
			return true;
	}

	return false;
}

static const char *ShaderKindPrefix(uint32_t kind)
{
	switch (kind) {
	case 0:
		return "ps";
	case 1:
		return "vs";
	case 2:
		return "gs";
	case 3:
		return "hs";
	case 4:
		return "ds";
	case 5:
		return "cs";
	default:
		return nullptr;
	}
}

static bool FormatShaderModel(uint32_t kind, uint32_t major, uint32_t minor, std::string *model)
{
	if (!model)
		return false;
	const char *prefix = ShaderKindPrefix(kind);
	if (!prefix)
		return false;
	char text[16] = {};
	sprintf_s(text, "%s_%u_%u", prefix, major, minor);
	*model = text;
	return true;
}

static bool GetDxbcShaderModel(const void *data, size_t size, std::string *model)
{
	if (!data || size < 32 || !model)
		return false;

	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	if (memcmp(bytes, "DXBC", 4))
		return false;

	uint32_t chunkCount = 0;
	if (!ReadU32LE(bytes, size, 28, &chunkCount))
		return false;

	for (uint32_t i = 0; i < chunkCount; ++i) {
		uint32_t chunkOffset = 0;
		if (!ReadU32LE(bytes, size, 32 + sizeof(uint32_t) * i, &chunkOffset))
			return false;
		if (chunkOffset + 12 > size)
			continue;
		uint32_t chunkFourCC = 0;
		memcpy(&chunkFourCC, bytes + chunkOffset, sizeof(chunkFourCC));
		if (chunkFourCC != MakeFourCC('S', 'H', 'D', 'R') &&
		    chunkFourCC != MakeFourCC('S', 'H', 'E', 'X'))
			continue;
		uint32_t versionToken = 0;
		if (!ReadU32LE(bytes, size, chunkOffset + 8, &versionToken))
			continue;
		const uint32_t kind = (versionToken >> 16) & 0xffff;
		const uint32_t major = (versionToken >> 4) & 0xf;
		const uint32_t minor = versionToken & 0xf;
		return FormatShaderModel(kind, major, minor, model);
	}
	return false;
}

static bool GetDxilShaderModel(const void *data, size_t size, std::string *model)
{
	if (!data || size < 32 || !model)
		return false;

	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	if (memcmp(bytes, "DXBC", 4))
		return false;

	uint32_t chunkCount = 0;
	if (!ReadU32LE(bytes, size, 28, &chunkCount))
		return false;

	for (uint32_t i = 0; i < chunkCount; ++i) {
		uint32_t chunkOffset = 0;
		if (!ReadU32LE(bytes, size, 32 + sizeof(uint32_t) * i, &chunkOffset))
			return false;
		if (chunkOffset + 16 > size)
			continue;
		uint32_t chunkFourCC = 0;
		memcpy(&chunkFourCC, bytes + chunkOffset, sizeof(chunkFourCC));
		if (chunkFourCC != MakeFourCC('D', 'X', 'I', 'L'))
			continue;
		uint32_t programVersion = 0;
		if (!ReadU32LE(bytes, size, chunkOffset + 8, &programVersion))
			continue;
		const uint32_t kind = (programVersion >> 16) & 0xffff;
		const uint32_t major = (programVersion >> 4) & 0xf;
		const uint32_t minor = programVersion & 0xf;
		return FormatShaderModel(kind, major, minor, model);
	}
	return false;
}

static bool GetShaderModel(const D3D12_SHADER_BYTECODE &bytecode, std::string *model)
{
	if (!bytecode.pShaderBytecode || bytecode.BytecodeLength == 0 || !model)
		return false;
	return GetDxilShaderModel(
		bytecode.pShaderBytecode, bytecode.BytecodeLength, model) ||
		GetDxbcShaderModel(
			bytecode.pShaderBytecode, bytecode.BytecodeLength, model);
}

static bool ShaderIsDXIL(const ShaderRecord &record)
{
	return ShaderHasChunk(record.bytecode, MakeFourCC('D', 'X', 'I', 'L'));
}

static uint64_t Fnv1a64(const void *data, size_t size)
{
	const uint8_t *bytes = static_cast<const uint8_t*>(data);
	uint64_t hash = 14695981039346656037ull;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

static std::string MakeShaderKey(const char *stage, uint64_t hash)
{
	char key[64];
	sprintf_s(key, "%s_%016llx", stage, static_cast<unsigned long long>(hash));
	return key;
}

static void RecordPsoShaderSummaryLocked(const DX12PsoShaderInfo &info)
{
	DX12PsoShaderSummary summary;
	summary.psoIndex = info.psoIndex;
	summary.hasVS = info.hasVS;
	summary.hasPS = info.hasPS;
	summary.hasCS = info.hasCS;
	summary.vs = info.vs;
	summary.ps = info.ps;
	summary.cs = info.cs;
	summary.vsModel = info.vsModel;
	summary.psModel = info.psModel;
	summary.csModel = info.csModel;
	gPsoShaderSummaryByIndex[info.psoIndex] = summary;
}

static UINT AlignInputElementOffset(UINT offset, DXGI_FORMAT format)
{
	const UINT alignment = DX12PsoInputLayoutUtils::FormatAlignment(format);
	if (!alignment || offset % alignment == 0)
		return offset;
	return offset + alignment - (offset % alignment);
}

static void RecordPsoInputLayoutLocked(
	UINT64 psoIndex, const D3D12_INPUT_LAYOUT_DESC &inputLayout)
{
	DX12PsoInputLayoutInfo layout;
	layout.psoIndex = psoIndex;
	if (!psoIndex || !inputLayout.pInputElementDescs || !inputLayout.NumElements) {
		gPsoInputLayoutByIndex[psoIndex] = layout;
		return;
	}

	UINT slotOffsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	layout.elements.reserve(inputLayout.NumElements);
	for (UINT i = 0; i < inputLayout.NumElements; ++i) {
		const D3D12_INPUT_ELEMENT_DESC &src = inputLayout.pInputElementDescs[i];
		DX12PsoInputElementInfo dst;
		dst.semanticName = src.SemanticName ? src.SemanticName : "";
		dst.semanticIndex = src.SemanticIndex;
		dst.format = src.Format;
		dst.inputSlot = src.InputSlot;
		dst.alignedByteOffset = src.AlignedByteOffset;
		dst.appendAlignedElement = src.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT;
		dst.inputSlotClass = src.InputSlotClass;
		dst.instanceDataStepRate = src.InstanceDataStepRate;

		const bool validSlot = src.InputSlot < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
		if (dst.appendAlignedElement)
			dst.resolvedByteOffset = validSlot ? AlignInputElementOffset(slotOffsets[src.InputSlot], src.Format) : 0;
		else
			dst.resolvedByteOffset = src.AlignedByteOffset;

		const UINT size = DX12PsoInputLayoutUtils::FormatSize(src.Format);
		if (validSlot && size)
			slotOffsets[src.InputSlot] = dst.resolvedByteOffset + size;
		layout.elements.push_back(std::move(dst));
	}
	gPsoInputLayoutByIndex[psoIndex] = std::move(layout);
}

static bool GetDumpDirectory(wchar_t *path, size_t pathCount)
{
	return DX12FrameAnalysis::GetPath(path, pathCount);
}

static bool EnsureDirectory(const wchar_t *path)
{
	return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool GetShaderFixesDirectory(wchar_t *path, size_t pathCount)
{
	if (!path || pathCount == 0)
		return false;
	if (!GetModuleFileNameW(DX12RuntimeState::GetModule(), path, static_cast<DWORD>(pathCount)))
		return false;
	PathRemoveFileSpecW(path);
	PathAppendW(path, L"ShaderFixes");
	return EnsureDirectory(path);
}

static void RecordShaderLocked(
	const D3D12_SHADER_BYTECODE &bytecode, const char *stage,
	UINT64 psoIndex, PsoRecord &pso, DX12PsoShaderInfo *info)
{
	if (!bytecode.pShaderBytecode || bytecode.BytecodeLength == 0)
		return;

	uint64_t hash = Fnv1a64(bytecode.pShaderBytecode, bytecode.BytecodeLength);
	std::string key = MakeShaderKey(stage, hash);
	pso.shaders.push_back(key);
	if (info) {
		std::string shaderModel;
		GetShaderModel(bytecode, &shaderModel);
		if (!strcmp(stage, "vs")) {
			info->hasVS = true;
			info->vs = hash;
			info->vsModel = shaderModel;
		} else if (!strcmp(stage, "ps")) {
			info->hasPS = true;
			info->ps = hash;
			info->psModel = shaderModel;
		} else if (!strcmp(stage, "cs")) {
			info->hasCS = true;
			info->cs = hash;
			info->csModel = shaderModel;
		}
	}

	auto it = gShaders.find(key);
	if (it == gShaders.end()) {
		ShaderRecord record;
		record.stage = stage;
		record.hash = hash;
		record.bytecode.resize(bytecode.BytecodeLength);
		memcpy(record.bytecode.data(), bytecode.pShaderBytecode, bytecode.BytecodeLength);
		record.firstPsoIndex = psoIndex;
		record.useCount = 1;
		gShaders.emplace(key, std::move(record));
	} else {
		it->second.useCount++;
	}
}

static bool WriteShaderFile(const wchar_t *dir, const ShaderRecord &record,
	bool overwriteExisting = true, bool *skippedExisting = nullptr)
{
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%016llx-%S.bin",
		dir, static_cast<unsigned long long>(record.hash), record.stage.c_str());

	if (skippedExisting)
		*skippedExisting = false;
	if (!overwriteExisting && PathFileExistsW(path)) {
		if (skippedExisting)
			*skippedExisting = true;
		return true;
	}

	FILE *file = _wfsopen(path, L"wb", _SH_DENYNO);
	if (!file)
		return false;
	fwrite(record.bytecode.data(), 1, record.bytecode.size(), file);
	fclose(file);
	return true;
}

static bool EnsureD3DDisassemble()
{
	if (gD3DDisassemble)
		return true;

	if (!gD3DCompiler) {
		wchar_t path[MAX_PATH];
		if (GetSystemDirectoryW(path, MAX_PATH)) {
			PathAppendW(path, L"d3dcompiler_47.dll");
			gD3DCompiler = LoadLibraryW(path);
		}
		if (!gD3DCompiler)
			gD3DCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
	}

	if (!gD3DCompiler) {
		DX12LogManager::Text("Failed to load d3dcompiler_47.dll for shader disassembly, error=%lu\n",
			GetLastError());
		return false;
	}

	gD3DDisassemble = reinterpret_cast<PFN_D3D_DISASSEMBLE>(
		GetProcAddress(gD3DCompiler, "D3DDisassemble"));
	if (!gD3DDisassemble) {
		DX12LogManager::Text("Failed to find D3DDisassemble in d3dcompiler_47.dll\n");
		return false;
	}

	return true;
}

static bool EnsureDXILDisassemble()
{
	if (gDxcCompiler)
		return true;

	if (!gDXCompiler) {
		const wchar_t *sdkPath =
			L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\dxcompiler.dll";
		if (PathFileExistsW(sdkPath))
			gDXCompiler = LoadLibraryW(sdkPath);
		if (!gDXCompiler)
			gDXCompiler = LoadLibraryW(L"dxcompiler.dll");
	}

	if (!gDXCompiler) {
		DX12LogManager::Text("Failed to load dxcompiler.dll for DXIL disassembly, error=%lu\n",
			GetLastError());
		return false;
	}

	gDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
		GetProcAddress(gDXCompiler, "DxcCreateInstance"));
	if (!gDxcCreateInstance) {
		DX12LogManager::Text("Failed to find DxcCreateInstance in dxcompiler.dll\n");
		return false;
	}

	HRESULT hr = gDxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&gDxcCompiler));
	if (FAILED(hr) || !gDxcCompiler) {
		DX12LogManager::Text("Failed to create IDxcCompiler3 for DXIL disassembly hr=0x%lx\n", hr);
		return false;
	}

	return true;
}

static bool WriteDXBCDisassemblyFile(const wchar_t *path, const ShaderRecord &record)
{
	if (!EnsureD3DDisassemble())
		return false;

	ID3DBlob *disassembly = nullptr;
	const UINT flags = D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS | D3D_DISASM_DISABLE_DEBUG_INFO;
	HRESULT hr = gD3DDisassemble(
		record.bytecode.data(), record.bytecode.size(), flags, nullptr, &disassembly);
	if (FAILED(hr) || !disassembly) {
		DX12LogManager::Text("D3DDisassemble failed for %016llx-%s.bin hr=0x%lx\n",
			static_cast<unsigned long long>(record.hash), record.stage.c_str(), hr);
		return false;
	}

	FILE *file = _wfsopen(path, L"wb", _SH_DENYNO);
	if (!file) {
		disassembly->Release();
		return false;
	}

	fwrite(disassembly->GetBufferPointer(), 1, disassembly->GetBufferSize(), file);
	fclose(file);
	disassembly->Release();
	return true;
}

static bool WriteDXILDisassemblyFile(const wchar_t *path, const ShaderRecord &record)
{
	if (!EnsureDXILDisassemble())
		return false;

	DxcBuffer buffer = {};
	buffer.Ptr = record.bytecode.data();
	buffer.Size = record.bytecode.size();
	buffer.Encoding = DXC_CP_ACP;

	IDxcResult *result = nullptr;
	HRESULT hr = gDxcCompiler->Disassemble(&buffer, IID_PPV_ARGS(&result));
	if (FAILED(hr) || !result) {
		DX12LogManager::Text("DXIL Disassemble failed for %016llx-%s.bin hr=0x%lx\n",
			static_cast<unsigned long long>(record.hash), record.stage.c_str(), hr);
		return false;
	}

	HRESULT status = S_OK;
	result->GetStatus(&status);
	if (FAILED(status)) {
		DX12LogManager::Text("DXIL Disassemble status failed for %016llx-%s.bin status=0x%lx\n",
			static_cast<unsigned long long>(record.hash), record.stage.c_str(), status);
		result->Release();
		return false;
	}

	IDxcBlobUtf8 *disassembly = nullptr;
	hr = result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr);
	if (FAILED(hr) || !disassembly) {
		DX12LogManager::Text("DXIL Disassemble output missing for %016llx-%s.bin hr=0x%lx\n",
			static_cast<unsigned long long>(record.hash), record.stage.c_str(), hr);
		result->Release();
		return false;
	}

	FILE *file = _wfsopen(path, L"wb", _SH_DENYNO);
	if (!file) {
		disassembly->Release();
		result->Release();
		return false;
	}

	fwrite(disassembly->GetStringPointer(), 1, disassembly->GetStringLength(), file);
	fclose(file);
	disassembly->Release();
	result->Release();
	return true;
}

static bool WriteShaderDisassemblyFile(const wchar_t *dir, const ShaderRecord &record,
	bool overwriteExisting = true, bool *skippedExisting = nullptr)
{
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%016llx-%S.asm.txt",
		dir, static_cast<unsigned long long>(record.hash), record.stage.c_str());

	if (skippedExisting)
		*skippedExisting = false;
	if (!overwriteExisting && PathFileExistsW(path)) {
		if (skippedExisting)
			*skippedExisting = true;
		return true;
	}

	if (ShaderIsDXIL(record))
		return WriteDXILDisassemblyFile(path, record);
	return WriteDXBCDisassemblyFile(path, record);
}

struct ShaderAnalysisRecord
{
	ShaderRecord shader;
	bool dxil = false;
	size_t asmSize = 0;
	UINT cbuffers = 0;
	UINT textures = 0;
	UINT samplers = 0;
	UINT uavs = 0;
	UINT samples = 0;
	UINT textureLoads = 0;
	UINT rawBufferLoads = 0;
	UINT cbufferLoads = 0;
	UINT storeOutputs = 0;
	bool hasDiscard = false;
	bool writesDepth = false;
	std::string tags;
};

struct StageAnalysisStats
{
	UINT shaders = 0;
	UINT dxil = 0;
	UINT dxbc = 0;
	UINT sampled = 0;
	UINT uav = 0;
	UINT discard = 0;
	UINT depth = 0;
	size_t bytecodeBytes = 0;
	size_t asmBytes = 0;
};

static bool ReadTextFile(const wchar_t *path, std::string *text)
{
	if (!text)
		return false;

	FILE *file = _wfsopen(path, L"rb", _SH_DENYNO);
	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	if (size < 0) {
		fclose(file);
		return false;
	}
	fseek(file, 0, SEEK_SET);

	text->resize(static_cast<size_t>(size));
	if (size > 0)
		fread(&(*text)[0], 1, static_cast<size_t>(size), file);
	fclose(file);
	return true;
}

static std::string LowerAscii(const std::string &text)
{
	std::string lower = text;
	for (char &ch : lower) {
		if (ch >= 'A' && ch <= 'Z')
			ch = static_cast<char>(ch - 'A' + 'a');
	}
	return lower;
}

static UINT CountSubstring(const std::string &text, const char *needle)
{
	if (!needle || !needle[0])
		return 0;

	UINT count = 0;
	size_t pos = 0;
	const size_t needleLen = strlen(needle);
	while ((pos = text.find(needle, pos)) != std::string::npos) {
		count++;
		pos += needleLen;
	}
	return count;
}

static UINT CountResourceBindingRows(const std::string &lowerText, const char *kind)
{
	UINT count = 0;
	bool inBindings = false;
	size_t pos = 0;
	while (pos < lowerText.size()) {
		size_t end = lowerText.find('\n', pos);
		if (end == std::string::npos)
			end = lowerText.size();
		std::string line = lowerText.substr(pos, end - pos);

		if (line.find("resource bindings:") != std::string::npos)
			inBindings = true;
		else if (inBindings && line.find(kind) != std::string::npos)
			count++;

		pos = end + 1;
	}
	return count;
}

static void AppendTag(std::string *tags, const char *tag)
{
	if (!tags || !tag || !tag[0])
		return;
	if (!tags->empty())
		*tags += "|";
	*tags += tag;
}

static void BuildShaderAsmPath(const wchar_t *dir, const ShaderRecord &shader, wchar_t *path, size_t pathCount)
{
	swprintf_s(path, pathCount, L"%s\\%016llx-%S.asm.txt",
		dir, static_cast<unsigned long long>(shader.hash), shader.stage.c_str());
}

static void BuildShaderAnalysisRecord(
	const wchar_t *dir, const ShaderRecord &shader, ShaderAnalysisRecord *analysis)
{
	if (!analysis)
		return;

	analysis->shader = shader;
	analysis->dxil = ShaderIsDXIL(shader);

	wchar_t asmPath[MAX_PATH];
	BuildShaderAsmPath(dir, shader, asmPath, ARRAYSIZE(asmPath));

	std::string text;
	if (!ReadTextFile(asmPath, &text))
		return;
	analysis->asmSize = text.size();

	std::string lower = LowerAscii(text);
	analysis->cbuffers = CountResourceBindingRows(lower, " cbuffer ");
	analysis->textures = CountResourceBindingRows(lower, " texture ");
	analysis->samplers = CountResourceBindingRows(lower, " sampler ");
	analysis->uavs =
		CountResourceBindingRows(lower, " uav ") +
		CountResourceBindingRows(lower, " rw") +
		CountSubstring(lower, "dx.op.bufferstore") +
		CountSubstring(lower, "dx.op.texturestore");
	analysis->samples =
		CountSubstring(lower, "dx.op.sample") +
		CountSubstring(lower, "\nsample") +
		CountSubstring(lower, "\nsample_l") +
		CountSubstring(lower, "\nsample_d");
	analysis->textureLoads =
		CountSubstring(lower, "dx.op.textureload") +
		CountSubstring(lower, "\nld ");
	analysis->rawBufferLoads = CountSubstring(lower, "dx.op.rawbufferload");
	analysis->cbufferLoads =
		CountSubstring(lower, "dx.op.cbufferload") +
		CountSubstring(lower, "cbufferloadlegacy");
	analysis->storeOutputs =
		CountSubstring(lower, "dx.op.storeoutput") +
		CountSubstring(lower, "\nmov o");
	analysis->hasDiscard =
		lower.find("dx.op.discard") != std::string::npos ||
		lower.find("\ndiscard") != std::string::npos;
	analysis->writesDepth =
		lower.find("sv_depth") != std::string::npos ||
		lower.find("depthoutput=1") != std::string::npos;

	AppendTag(&analysis->tags, analysis->dxil ? "dxil" : "dxbc");
	AppendTag(&analysis->tags, shader.stage.c_str());
	if (analysis->samples)
		AppendTag(&analysis->tags, "samples-texture");
	if (analysis->textureLoads || analysis->rawBufferLoads)
		AppendTag(&analysis->tags, "loads-resource");
	if (analysis->cbufferLoads || analysis->cbuffers)
		AppendTag(&analysis->tags, "uses-cbuffer");
	if (analysis->uavs)
		AppendTag(&analysis->tags, "writes-uav");
	if (analysis->storeOutputs)
		AppendTag(&analysis->tags, "writes-output");
	if (analysis->hasDiscard)
		AppendTag(&analysis->tags, "discard");
	if (analysis->writesDepth)
		AppendTag(&analysis->tags, "depth");
	if (lower.find("canvas") != std::string::npos || lower.find("atlas_texture") != std::string::npos)
		AppendTag(&analysis->tags, "ui-canvas-candidate");
	if (analysis->asmSize > 100000)
		AppendTag(&analysis->tags, "large");
}

static int StageIndex(const std::string &stage)
{
	if (stage == "vs")
		return 0;
	if (stage == "ps")
		return 1;
	if (stage == "cs")
		return 2;
	return 3;
}

static void WriteShaderAnalysisFile(
	const wchar_t *dir, const std::vector<ShaderRecord> &shaders,
	const std::vector<PsoRecord> &psos)
{
	std::vector<ShaderAnalysisRecord> analyses;
	analyses.reserve(shaders.size());
	for (const ShaderRecord &shader : shaders) {
		ShaderAnalysisRecord analysis;
		BuildShaderAnalysisRecord(dir, shader, &analysis);
		analyses.push_back(std::move(analysis));
	}

	std::sort(analyses.begin(), analyses.end(),
		[](const ShaderAnalysisRecord &a, const ShaderAnalysisRecord &b) {
			int stageA = StageIndex(a.shader.stage);
			int stageB = StageIndex(b.shader.stage);
			if (stageA != stageB)
				return stageA < stageB;
			return a.shader.hash < b.shader.hash;
		});

	StageAnalysisStats stageStats[4] = {};
	UINT dxilCount = 0;
	UINT dxbcCount = 0;
	UINT sampledCount = 0;
	UINT uavCount = 0;
	UINT discardCount = 0;
	UINT depthCount = 0;
	for (const ShaderAnalysisRecord &analysis : analyses) {
		int idx = StageIndex(analysis.shader.stage);
		if (idx < 0 || idx > 3)
			idx = 3;
		StageAnalysisStats &stats = stageStats[idx];
		stats.shaders++;
		stats.bytecodeBytes += analysis.shader.bytecode.size();
		stats.asmBytes += analysis.asmSize;
		if (analysis.dxil) {
			stats.dxil++;
			dxilCount++;
		} else {
			stats.dxbc++;
			dxbcCount++;
		}
		if (analysis.samples) {
			stats.sampled++;
			sampledCount++;
		}
		if (analysis.uavs) {
			stats.uav++;
			uavCount++;
		}
		if (analysis.hasDiscard) {
			stats.discard++;
			discardCount++;
		}
		if (analysis.writesDepth) {
			stats.depth++;
			depthCount++;
		}
	}

	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\ShaderAnalysis.txt", dir);
	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return;

	fprintf(file, "DX12 Shader Analysis\n");
	fprintf(file, "====================\n");
	fprintf(file, "shaders=%zu psos=%zu dxil=%u dxbc=%u sampled=%u uav=%u discard=%u depth=%u\n\n",
		analyses.size(), psos.size(), dxilCount, dxbcCount,
		sampledCount, uavCount, discardCount, depthCount);

	const char *stageNames[] = { "vs", "ps", "cs", "other" };
	fprintf(file, "Stage Summary\n");
	fprintf(file, "stage,shaders,dxil,dxbc,bytecode_bytes,asm_bytes,sampled,uav,discard,depth\n");
	for (int i = 0; i < 4; ++i) {
		const StageAnalysisStats &stats = stageStats[i];
		if (!stats.shaders)
			continue;
		fprintf(file, "%s,%u,%u,%u,%zu,%zu,%u,%u,%u,%u\n",
			stageNames[i], stats.shaders, stats.dxil, stats.dxbc,
			stats.bytecodeBytes, stats.asmBytes, stats.sampled,
			stats.uav, stats.discard, stats.depth);
	}

	std::vector<ShaderAnalysisRecord> bySize = analyses;
	std::sort(bySize.begin(), bySize.end(),
		[](const ShaderAnalysisRecord &a, const ShaderAnalysisRecord &b) {
			return a.asmSize > b.asmSize;
		});

	fprintf(file, "\nLargest ASM Files\n");
	fprintf(file, "hash,stage,asm_size,bytecode_size,tags\n");
	for (size_t i = 0; i < bySize.size() && i < 20; ++i) {
		const ShaderAnalysisRecord &analysis = bySize[i];
		fprintf(file, "%016llx,%s,%zu,%zu,%s\n",
			static_cast<unsigned long long>(analysis.shader.hash),
			analysis.shader.stage.c_str(),
			analysis.asmSize,
			analysis.shader.bytecode.size(),
			analysis.tags.c_str());
	}

	fprintf(file, "\nShader Details\n");
	fprintf(file,
		"hash,stage,container,size,asm_size,uses,first_pso,cbuffers,textures,samplers,uavs,"
		"samples,texture_loads,raw_buffer_loads,cbuffer_loads,store_outputs,discard,depth,tags,file,asm_file\n");
	for (const ShaderAnalysisRecord &analysis : analyses) {
		fprintf(file,
			"%016llx,%s,%s,%zu,%zu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s,%016llx-%s.bin,%016llx-%s.asm.txt\n",
			static_cast<unsigned long long>(analysis.shader.hash),
			analysis.shader.stage.c_str(),
			analysis.dxil ? "dxil" : "dxbc",
			analysis.shader.bytecode.size(),
			analysis.asmSize,
			static_cast<unsigned long long>(analysis.shader.useCount),
			static_cast<unsigned long long>(analysis.shader.firstPsoIndex),
			analysis.cbuffers,
			analysis.textures,
			analysis.samplers,
			analysis.uavs,
			analysis.samples,
			analysis.textureLoads,
			analysis.rawBufferLoads,
			analysis.cbufferLoads,
			analysis.storeOutputs,
			analysis.hasDiscard ? 1 : 0,
			analysis.writesDepth ? 1 : 0,
			analysis.tags.c_str(),
			static_cast<unsigned long long>(analysis.shader.hash),
			analysis.shader.stage.c_str(),
			static_cast<unsigned long long>(analysis.shader.hash),
			analysis.shader.stage.c_str());
	}
	fclose(file);

	{
		char fields[512] = "";
		DX12JsonUtils::AppendWStringField(fields, sizeof(fields), "path", path);
		DX12JsonUtils::AppendRawField(fields, sizeof(fields), "entries", std::to_string(analyses.size()).c_str());
		DX12FrameAnalysis::LogJsonFunc("ShaderAnalysisWritten", "%s", fields + 1);
	}
}

static void WritePsoResourceSummaryFile(
	const wchar_t *dir, const std::vector<ShaderRecord> &shaders,
	const std::vector<PsoRecord> &psos)
{
	if (!dir)
		return;

	std::unordered_map<std::string, ShaderAnalysisRecord> analysesByKey;
	analysesByKey.reserve(shaders.size());
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryBuildShaderAnalyses",
		"\"shaders\":%zu", shaders.size());
	for (const ShaderRecord &shader : shaders) {
		ShaderAnalysisRecord analysis;
		BuildShaderAnalysisRecord(dir, shader, &analysis);
		analysesByKey.emplace(MakeShaderKey(shader.stage.c_str(), shader.hash), std::move(analysis));
	}
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryShaderAnalysesReady",
		"\"entries\":%zu", analysesByKey.size());

	std::vector<DX12RootSignatureSummary> rootSignatures;
	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12PsoRootSummary> psoRoots;
	DX12GetResourceMetadataSnapshot(&rootSignatures, &descriptors, &psoRoots);
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryMetadataReady",
		"\"roots\":%zu,\"descriptors\":%zu,\"psoRoots\":%zu",
		rootSignatures.size(), descriptors.size(), psoRoots.size());

	std::unordered_map<ID3D12RootSignature*, DX12RootSignatureSummary> rootsByPtr;
	rootsByPtr.reserve(rootSignatures.size());
	for (const DX12RootSignatureSummary &root : rootSignatures) {
		if (root.rootSignature && rootsByPtr.find(root.rootSignature) == rootsByPtr.end())
			rootsByPtr.emplace(root.rootSignature, root);
	}

	std::unordered_map<UINT64, DX12PsoRootSummary> psoRootsByIndex;
	psoRootsByIndex.reserve(psoRoots.size());
	for (const DX12PsoRootSummary &root : psoRoots)
		psoRootsByIndex[root.psoIndex] = root;
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryLookupReady",
		"\"roots\":%zu,\"psoRoots\":%zu",
		rootsByPtr.size(), psoRootsByIndex.size());

	UINT64 totalCBV = 0;
	UINT64 totalSRV = 0;
	UINT64 totalUAV = 0;
	UINT64 totalRTV = 0;
	UINT64 totalDSV = 0;
	UINT64 totalSampler = 0;
	UINT64 totalTexture2D = 0;
	UINT64 totalBuffer = 0;
	for (const DX12DescriptorSummary &descriptor : descriptors) {
		if (descriptor.kind == "CBV")
			totalCBV++;
		else if (descriptor.kind == "SRV")
			totalSRV++;
		else if (descriptor.kind == "UAV")
			totalUAV++;
		else if (descriptor.kind == "RTV")
			totalRTV++;
		else if (descriptor.kind == "DSV")
			totalDSV++;
		else if (descriptor.kind == "Sampler")
			totalSampler++;

		if (descriptor.hasResourceDesc) {
			if (descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
				totalTexture2D++;
			else if (descriptor.resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
				totalBuffer++;
		}
	}
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryDescriptorInventory",
		"\"cbv\":%llu,\"srv\":%llu,\"uav\":%llu,\"rtv\":%llu,\"dsv\":%llu,\"sampler\":%llu,\"texture2d\":%llu,\"buffer\":%llu",
		static_cast<unsigned long long>(totalCBV),
		static_cast<unsigned long long>(totalSRV),
		static_cast<unsigned long long>(totalUAV),
		static_cast<unsigned long long>(totalRTV),
		static_cast<unsigned long long>(totalDSV),
		static_cast<unsigned long long>(totalSampler),
		static_cast<unsigned long long>(totalTexture2D),
		static_cast<unsigned long long>(totalBuffer));

	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\PsoResourceSummaryDX12.txt", dir);
	FILE *file = _wfsopen(path, L"w", _SH_DENYNO);
	if (!file)
		return;
	{
		char fields[512] = "";
		DX12JsonUtils::AppendWStringField(fields, sizeof(fields), "path", path);
		DX12FrameAnalysis::LogJsonFunc("PsoSummaryFileOpened", "%s", fields + 1);
	}

	fprintf(file, "DX12 PSO Resource Summary\n");
	fprintf(file, "=========================\n");
	fprintf(file,
		"psos=%zu shaders=%zu root_signatures=%zu descriptors=%zu cbv=%llu srv=%llu uav=%llu rtv=%llu dsv=%llu sampler=%llu texture2d=%llu buffer=%llu\n\n",
		psos.size(),
		shaders.size(),
		rootSignatures.size(),
		descriptors.size(),
		static_cast<unsigned long long>(totalCBV),
		static_cast<unsigned long long>(totalSRV),
		static_cast<unsigned long long>(totalUAV),
		static_cast<unsigned long long>(totalRTV),
		static_cast<unsigned long long>(totalDSV),
		static_cast<unsigned long long>(totalSampler),
		static_cast<unsigned long long>(totalTexture2D),
		static_cast<unsigned long long>(totalBuffer));

	fprintf(file, "PSO Summary\n");
	fprintf(file,
		"pso,kind,pipeline_state,root_signature,root_hash,root_size,shaders,shader_tags,cbuffers,textures,samplers,uavs,samples,texture_loads,raw_buffer_loads,cbuffer_loads,store_outputs,discard,depth,candidate\n");

	size_t psoRows = 0;
	for (const PsoRecord &pso : psos) {
		DX12PsoRootSummary psoRoot;
		auto psoRootIt = psoRootsByIndex.find(pso.index);
		if (psoRootIt != psoRootsByIndex.end())
			psoRoot = psoRootIt->second;

		DX12RootSignatureSummary root;
		auto rootIt = rootsByPtr.find(psoRoot.rootSignature);
		if (rootIt != rootsByPtr.end())
			root = rootIt->second;

		UINT cbuffers = 0;
		UINT textures = 0;
		UINT samplers = 0;
		UINT uavs = 0;
		UINT samples = 0;
		UINT textureLoads = 0;
		UINT rawBufferLoads = 0;
		UINT cbufferLoads = 0;
		UINT storeOutputs = 0;
		bool hasDiscard = false;
		bool writesDepth = false;
		bool hasVS = false;
		bool hasPS = false;
		bool hasCS = false;
		bool hasUiTag = false;
		bool hasLarge = false;
		std::string shaderList;
		std::string tagList;
		std::unordered_set<std::string> uniqueTags;

		for (size_t i = 0; i < pso.shaders.size(); ++i) {
			if (i)
				shaderList += ";";
			shaderList += pso.shaders[i];

			auto analysisIt = analysesByKey.find(pso.shaders[i]);
			if (analysisIt == analysesByKey.end())
				continue;
			const ShaderAnalysisRecord &analysis = analysisIt->second;
			cbuffers += analysis.cbuffers;
			textures += analysis.textures;
			samplers += analysis.samplers;
			uavs += analysis.uavs;
			samples += analysis.samples;
			textureLoads += analysis.textureLoads;
			rawBufferLoads += analysis.rawBufferLoads;
			cbufferLoads += analysis.cbufferLoads;
			storeOutputs += analysis.storeOutputs;
			hasDiscard = hasDiscard || analysis.hasDiscard;
			writesDepth = writesDepth || analysis.writesDepth;
			hasVS = hasVS || analysis.shader.stage == "vs";
			hasPS = hasPS || analysis.shader.stage == "ps";
			hasCS = hasCS || analysis.shader.stage == "cs";
			hasUiTag = hasUiTag || analysis.tags.find("ui-canvas-candidate") != std::string::npos;
			hasLarge = hasLarge || analysis.tags.find("large") != std::string::npos;

			size_t tagStart = 0;
			while (tagStart < analysis.tags.size()) {
				size_t tagEnd = analysis.tags.find('|', tagStart);
				if (tagEnd == std::string::npos)
					tagEnd = analysis.tags.size();
				std::string tag = analysis.tags.substr(tagStart, tagEnd - tagStart);
				if (!tag.empty() && uniqueTags.insert(tag).second) {
					if (!tagList.empty())
						tagList += "|";
					tagList += tag;
				}
				tagStart = tagEnd + 1;
			}
		}

		const char *candidate = "other";
		if (hasCS)
			candidate = "compute";
		else if (hasUiTag)
			candidate = "ui-candidate";
		else if (hasPS && (samples || textureLoads))
			candidate = hasDiscard ? "alpha/material-candidate" : "material/postprocess-candidate";
		else if (hasVS && hasPS)
			candidate = "draw-candidate";
		else if (hasLarge)
			candidate = "large-shader-candidate";

		fprintf(file,
			"%llu,%s,%p,%p,%016llx,%zu,%s,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s\n",
			static_cast<unsigned long long>(pso.index),
			pso.kind.c_str(),
			psoRoot.pipelineState,
			psoRoot.rootSignature,
			static_cast<unsigned long long>(root.hash),
			root.size,
			shaderList.c_str(),
			tagList.c_str(),
			cbuffers,
			textures,
			samplers,
			uavs,
			samples,
			textureLoads,
			rawBufferLoads,
			cbufferLoads,
			storeOutputs,
			hasDiscard ? 1 : 0,
			writesDepth ? 1 : 0,
			candidate);
		psoRows++;
	}
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryRowsWritten",
		"\"rows\":%zu", psoRows);

	fprintf(file, "\nRoot Signature Usage\n");
	fprintf(file, "root_signature,root_hash,root_size,pso_count,first_pso\n");
	std::unordered_map<ID3D12RootSignature*, UINT> rootUseCount;
	std::unordered_map<ID3D12RootSignature*, UINT64> rootFirstPso;
	for (const DX12PsoRootSummary &psoRoot : psoRoots) {
		rootUseCount[psoRoot.rootSignature]++;
		auto firstIt = rootFirstPso.find(psoRoot.rootSignature);
		if (firstIt == rootFirstPso.end() || psoRoot.psoIndex < firstIt->second)
			rootFirstPso[psoRoot.rootSignature] = psoRoot.psoIndex;
	}
	for (const auto &item : rootUseCount) {
		DX12RootSignatureSummary root;
		auto rootIt = rootsByPtr.find(item.first);
		if (rootIt != rootsByPtr.end())
			root = rootIt->second;
		fprintf(file, "%p,%016llx,%zu,%u,%llu\n",
			item.first,
			static_cast<unsigned long long>(root.hash),
			root.size,
			item.second,
			static_cast<unsigned long long>(rootFirstPso[item.first]));
	}
	DX12FrameAnalysis::LogJsonFunc("PsoSummaryRootUsageWritten",
		"\"rows\":%zu", rootUseCount.size());

	fprintf(file, "\nDescriptor Inventory\n");
	fprintf(file, "kind,count\n");
	fprintf(file, "CBV,%llu\n", static_cast<unsigned long long>(totalCBV));
	fprintf(file, "SRV,%llu\n", static_cast<unsigned long long>(totalSRV));
	fprintf(file, "UAV,%llu\n", static_cast<unsigned long long>(totalUAV));
	fprintf(file, "RTV,%llu\n", static_cast<unsigned long long>(totalRTV));
	fprintf(file, "DSV,%llu\n", static_cast<unsigned long long>(totalDSV));
	fprintf(file, "Sampler,%llu\n", static_cast<unsigned long long>(totalSampler));

	fclose(file);
	{
		char fields[512] = "";
		DX12JsonUtils::AppendWStringField(fields, sizeof(fields), "path", path);
		DX12JsonUtils::AppendRawField(fields, sizeof(fields), "psos", std::to_string(psos.size()).c_str());
		DX12JsonUtils::AppendRawField(fields, sizeof(fields), "roots", std::to_string(rootSignatures.size()).c_str());
		DX12JsonUtils::AppendRawField(fields, sizeof(fields), "descriptors", std::to_string(descriptors.size()).c_str());
		DX12FrameAnalysis::LogJsonFunc("PsoResourceSummaryWritten", "%s", fields + 1);
	}
}

static bool TryWritePsoResourceSummaryFile(
	const wchar_t *dir, const std::vector<ShaderRecord> &shaders,
	const std::vector<PsoRecord> &psos)
{
	bool ok = true;
	__try {
		WritePsoResourceSummaryFile(dir, shaders, psos);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
		DX12LogManager::Text("PSO resource summary skipped after exception=0x%lx\n",
			GetExceptionCode());
	}
	return ok;
}

static DX12FrameAnalysisDumpResult TryDumpBindingTrace(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	__try {
		result = DX12BindingFrameExport::DumpBindingTrace(dir);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		const DWORD exceptionCode = GetExceptionCode();
		result.ok = false;
		result.failed = 1;
		result.reason = "exception";
		DX12FrameAnalysis::LogJsonFunc("BindingTraceException",
			"\"exception\":\"0x%lx\",\"reason\":\"binding_trace_calls\"",
			exceptionCode);
		DX12LogManager::Text("Binding trace skipped after exception=0x%lx\n",
			exceptionCode);
	}
	return result;
}

static DX12FrameAnalysisDumpResult TryDumpCurrentFrameResources(const wchar_t *dir)
{
	DX12FrameAnalysisDumpResult result;
	__try {
		result = DX12ResourceDump::DumpCurrentFrameResources(dir);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		const DWORD exceptionCode = GetExceptionCode();
		result.ok = false;
		result.failed = 1;
		result.reason = "exception";
		DX12FrameAnalysis::LogJsonFunc("ResourceDumpException",
			"\"exception\":\"0x%lx\",\"reason\":\"current_frame_resource_files\"",
			exceptionCode);
		DX12LogManager::Text("Current-frame resource files skipped after exception=0x%lx\n",
			exceptionCode);
	}
	return result;
}

static size_t AlignUp(size_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static size_t PipelineStateStreamPayloadSize(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type)
{
	switch (type) {
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
		return sizeof(ID3D12RootSignature*);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
		return sizeof(D3D12_SHADER_BYTECODE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
		return sizeof(D3D12_STREAM_OUTPUT_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
		return sizeof(D3D12_BLEND_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
		return sizeof(UINT);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
		return sizeof(D3D12_RASTERIZER_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
		return sizeof(D3D12_DEPTH_STENCIL_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
		return sizeof(D3D12_INPUT_LAYOUT_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
		return sizeof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
		return sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
		return sizeof(D3D12_RT_FORMAT_ARRAY);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
		return sizeof(DXGI_FORMAT);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
		return sizeof(DXGI_SAMPLE_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
		return sizeof(UINT);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
		return sizeof(D3D12_CACHED_PIPELINE_STATE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
		return sizeof(D3D12_PIPELINE_STATE_FLAGS);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
		return sizeof(D3D12_DEPTH_STENCIL_DESC1);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
		return sizeof(D3D12_VIEW_INSTANCING_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
		return sizeof(D3D12_DEPTH_STENCIL_DESC2);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
		return sizeof(D3D12_RASTERIZER_DESC1);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
		return sizeof(D3D12_RASTERIZER_DESC2);
	default:
		return 0;
	}
}

static size_t PipelineStateStreamPayloadAlignment(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type)
{
	switch (type) {
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
		return alignof(ID3D12RootSignature*);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
		return alignof(D3D12_SHADER_BYTECODE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
		return alignof(D3D12_STREAM_OUTPUT_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
		return alignof(D3D12_BLEND_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
		return alignof(UINT);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
		return alignof(D3D12_RASTERIZER_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
		return alignof(D3D12_DEPTH_STENCIL_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
		return alignof(D3D12_INPUT_LAYOUT_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
		return alignof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
		return alignof(D3D12_PRIMITIVE_TOPOLOGY_TYPE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
		return alignof(D3D12_RT_FORMAT_ARRAY);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
		return alignof(DXGI_FORMAT);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
		return alignof(DXGI_SAMPLE_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
		return alignof(UINT);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
		return alignof(D3D12_CACHED_PIPELINE_STATE);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
		return alignof(D3D12_PIPELINE_STATE_FLAGS);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
		return alignof(D3D12_DEPTH_STENCIL_DESC1);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
		return alignof(D3D12_VIEW_INSTANCING_DESC);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
		return alignof(D3D12_DEPTH_STENCIL_DESC2);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
		return alignof(D3D12_RASTERIZER_DESC1);
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
		return alignof(D3D12_RASTERIZER_DESC2);
	default:
		return 1;
	}
}

static const char *PipelineStateStreamShaderStage(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type)
{
	switch (type) {
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
		return "vs";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
		return "ps";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
		return "ds";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
		return "hs";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
		return "gs";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
		return "cs";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
		return "as";
	case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
		return "ms";
	default:
		return nullptr;
	}
}

