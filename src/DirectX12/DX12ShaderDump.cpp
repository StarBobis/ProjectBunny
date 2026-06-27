#include "DX12ShaderDump.h"

#include <d3dcompiler.h>
#include <dxcapi.h>
#include <Shlwapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DX12BindingTracker.h"
#include "DX12FrameAnalysis.h"
#include "DX12Json.h"
#include "DX12ResourceDump.h"
#include "DX12ResourceTracker.h"
#include "DX12State.h"

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
static UINT64 gPsoSerial = 0;
static volatile LONG gShaderDumpRunning = 0;
static volatile LONG gShaderDumpCaptureRequested = 0;
static volatile LONG gShaderDumpCapturing = 0;

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

static bool GetDumpDirectory(wchar_t *path, size_t pathCount)
{
	return DX12FrameAnalysisGetPath(path, pathCount);
}

static bool EnsureDirectory(const wchar_t *path)
{
	return CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool GetShaderFixesDirectory(wchar_t *path, size_t pathCount)
{
	if (!path || pathCount == 0)
		return false;
	if (!GetModuleFileNameW(DX12GetModule(), path, static_cast<DWORD>(pathCount)))
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
		DX12Log("Failed to load d3dcompiler_47.dll for shader disassembly, error=%lu\n",
			GetLastError());
		return false;
	}

	gD3DDisassemble = reinterpret_cast<PFN_D3D_DISASSEMBLE>(
		GetProcAddress(gD3DCompiler, "D3DDisassemble"));
	if (!gD3DDisassemble) {
		DX12Log("Failed to find D3DDisassemble in d3dcompiler_47.dll\n");
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
		DX12Log("Failed to load dxcompiler.dll for DXIL disassembly, error=%lu\n",
			GetLastError());
		return false;
	}

	gDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
		GetProcAddress(gDXCompiler, "DxcCreateInstance"));
	if (!gDxcCreateInstance) {
		DX12Log("Failed to find DxcCreateInstance in dxcompiler.dll\n");
		return false;
	}

	HRESULT hr = gDxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&gDxcCompiler));
	if (FAILED(hr) || !gDxcCompiler) {
		DX12Log("Failed to create IDxcCompiler3 for DXIL disassembly hr=0x%lx\n", hr);
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
		DX12Log("D3DDisassemble failed for %016llx-%s.bin hr=0x%lx\n",
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
		DX12Log("DXIL Disassemble failed for %016llx-%s.bin hr=0x%lx\n",
			static_cast<unsigned long long>(record.hash), record.stage.c_str(), hr);
		return false;
	}

	HRESULT status = S_OK;
	result->GetStatus(&status);
	if (FAILED(status)) {
		DX12Log("DXIL Disassemble status failed for %016llx-%s.bin status=0x%lx\n",
			static_cast<unsigned long long>(record.hash), record.stage.c_str(), status);
		result->Release();
		return false;
	}

	IDxcBlobUtf8 *disassembly = nullptr;
	hr = result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr);
	if (FAILED(hr) || !disassembly) {
		DX12Log("DXIL Disassemble output missing for %016llx-%s.bin hr=0x%lx\n",
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
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
		DX12JsonAppendRawField(fields, sizeof(fields), "entries", std::to_string(analyses.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("ShaderAnalysisWritten", "%s", fields + 1);
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
	DX12FrameAnalysisLogJsonFunc("PsoSummaryBuildShaderAnalyses",
		"\"shaders\":%zu", shaders.size());
	for (const ShaderRecord &shader : shaders) {
		ShaderAnalysisRecord analysis;
		BuildShaderAnalysisRecord(dir, shader, &analysis);
		analysesByKey.emplace(MakeShaderKey(shader.stage.c_str(), shader.hash), std::move(analysis));
	}
	DX12FrameAnalysisLogJsonFunc("PsoSummaryShaderAnalysesReady",
		"\"entries\":%zu", analysesByKey.size());

	std::vector<DX12RootSignatureSummary> rootSignatures;
	std::vector<DX12DescriptorSummary> descriptors;
	std::vector<DX12PsoRootSummary> psoRoots;
	DX12GetResourceMetadataSnapshot(&rootSignatures, &descriptors, &psoRoots);
	DX12FrameAnalysisLogJsonFunc("PsoSummaryMetadataReady",
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
	DX12FrameAnalysisLogJsonFunc("PsoSummaryLookupReady",
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
	DX12FrameAnalysisLogJsonFunc("PsoSummaryDescriptorInventory",
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
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
		DX12FrameAnalysisLogJsonFunc("PsoSummaryFileOpened", "%s", fields + 1);
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
	DX12FrameAnalysisLogJsonFunc("PsoSummaryRowsWritten",
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
	DX12FrameAnalysisLogJsonFunc("PsoSummaryRootUsageWritten",
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
		DX12JsonAppendWStringField(fields, sizeof(fields), "path", path);
		DX12JsonAppendRawField(fields, sizeof(fields), "psos", std::to_string(psos.size()).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "roots", std::to_string(rootSignatures.size()).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "descriptors", std::to_string(descriptors.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("PsoResourceSummaryWritten", "%s", fields + 1);
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
		DX12Log("PSO resource summary skipped after exception=0x%lx\n",
			GetExceptionCode());
	}
	return ok;
}

static bool TryDumpBindingTrace(const wchar_t *dir)
{
	bool ok = true;
	__try {
		DX12DumpBindingTrace(dir);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
		DX12Log("Binding trace skipped after exception=0x%lx\n", GetExceptionCode());
	}
	return ok;
}

static bool TryDumpCurrentFrameResources(const wchar_t *dir)
{
	bool ok = true;
	__try {
		DX12DumpCurrentFrameResources(dir);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
		DX12Log("Current-frame resource files skipped after exception=0x%lx\n",
			GetExceptionCode());
	}
	return ok;
}

static bool TryDumpCurrentFrameResourceTable(const wchar_t *dir)
{
	bool ok = true;
	__try {
		DX12DumpCurrentFrameResourceTable(dir);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
		DX12Log("Current-frame resource table skipped after exception=0x%lx\n",
			GetExceptionCode());
	}
	return ok;
}

static bool TryDumpResourceMetadata(const wchar_t *dir)
{
	bool ok = true;
	__try {
		DX12DumpResourceMetadata(dir);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
		DX12Log("Resource metadata skipped after exception=0x%lx\n",
			GetExceptionCode());
	}
	return ok;
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
	const size_t shadersInPso = pso.shaders.size();
	gPsos.push_back(std::move(pso));
	RecordPsoShaderSummaryLocked(info);
	if (pipelineState)
		gPsoShaderInfo[pipelineState] = info;
	UINT64 shaderCount = gShaders.size();
	UINT64 psoCount = gPsos.size();
	ReleaseSRWLockExclusive(&gDumpLock);

	DX12FrameAnalysisLogJsonFunc("ID3D12Device::CreateGraphicsPipelineState",
		"\"pso\":%llu,\"shaders\":%llu,\"cachedShaders\":%llu,\"cachedPsos\":%llu",
		static_cast<unsigned long long>(psoIndex),
		static_cast<unsigned long long>(shadersInPso),
		static_cast<unsigned long long>(shaderCount),
		static_cast<unsigned long long>(psoCount));
	if (!DX12ShaderDumpIsBusy())
		DX12SetOverlayStatus(L"3DMigoto DX12 hook alive | cached shaders ready");
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

	DX12FrameAnalysisLogJsonFunc("ID3D12Device::CreateComputePipelineState",
		"\"pso\":%llu,\"cachedShaders\":%llu,\"cachedPsos\":%llu",
		static_cast<unsigned long long>(psoIndex),
		static_cast<unsigned long long>(shaderCount),
		static_cast<unsigned long long>(psoCount));
	if (!DX12ShaderDumpIsBusy())
		DX12SetOverlayStatus(L"3DMigoto DX12 hook alive | cached shaders ready");
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

	while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc->SizeInBytes) {
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type =
			*reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(stream + offset);
		size_t payloadSize = PipelineStateStreamPayloadSize(type);
		size_t payloadAlignment = PipelineStateStreamPayloadAlignment(type);
		size_t payloadOffset = AlignUp(offset + sizeof(type), payloadAlignment);
		if (payloadSize == 0 || payloadOffset + payloadSize > desc->SizeInBytes) {
			DX12FrameAnalysisLogJsonFunc("PipelineStateStreamParseStopped",
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
		}

		offset = AlignUp(payloadOffset + payloadSize, alignof(void*));
	}

	shadersInPso = pso.shaders.size();
	DX12RecordPsoRootSignature(psoIndex, pso.kind.c_str(), pipelineState, rootSignature);
	gPsos.push_back(std::move(pso));
	RecordPsoShaderSummaryLocked(info);
	if (pipelineState)
		gPsoShaderInfo[pipelineState] = info;
	UINT64 shaderCount = gShaders.size();
	UINT64 psoCount = gPsos.size();
	ReleaseSRWLockExclusive(&gDumpLock);

	DX12FrameAnalysisLogJsonFunc("ID3D12Device2::CreatePipelineState",
		"\"pso\":%llu,\"shaders\":%llu,\"cachedShaders\":%llu,\"cachedPsos\":%llu",
		static_cast<unsigned long long>(psoIndex),
		static_cast<unsigned long long>(shadersInPso),
		static_cast<unsigned long long>(shaderCount),
		static_cast<unsigned long long>(psoCount));
	if (!DX12ShaderDumpIsBusy())
		DX12SetOverlayStatus(L"3DMigoto DX12 hook alive | cached shaders ready");
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
		DX12Log("Failed to resolve DX12 frame analysis directory\n");
		DX12SetOverlayStatus(L"F8 frame analysis failed: cannot create directory");
		return;
	}

	DX12SetOverlayStatus(L"F8 frame analysis dumping");

	std::vector<ShaderRecord> shaders;
	std::vector<PsoRecord> psos;
	SnapshotShadersAndPsos(&shaders, &psos);

	DX12LogJsonFunc("FrameAnalysisDumpBegin",
		"\"shaders\":%zu,\"psos\":%zu", shaders.size(), psos.size());
	{
		char fields[512] = "";
		DX12JsonAppendWStringField(fields, sizeof(fields), "dir", dir);
		DX12JsonAppendRawField(fields, sizeof(fields), "shaders", std::to_string(shaders.size()).c_str());
		DX12JsonAppendRawField(fields, sizeof(fields), "psos", std::to_string(psos.size()).c_str());
		DX12FrameAnalysisLogJsonFunc("FrameAnalysisSummary", "%s", fields + 1);
	}
	bool bindingTraceOk = TryDumpBindingTrace(dir);
	bool resourceTableOk = TryDumpCurrentFrameResourceTable(dir);
	bool metadataOk = TryDumpResourceMetadata(dir);
	bool psoSummaryOk = TryWritePsoResourceSummaryFile(dir, shaders, psos);
	bool resourceFilesOk = TryDumpCurrentFrameResources(dir);

	DX12SetOverlayStatus(L"F8 frame analysis complete");
	DX12LogJsonFunc("FrameAnalysisDumpComplete",
		"\"shaders\":%zu,\"psos\":%zu,\"bindingTrace\":%u,\"resourceTable\":%u,\"metadata\":%u,\"psoSummary\":%u,\"resourceFiles\":%u",
		shaders.size(), psos.size(),
		bindingTraceOk ? 1 : 0,
		resourceTableOk ? 1 : 0,
		metadataOk ? 1 : 0,
		psoSummaryOk ? 1 : 0,
		resourceFilesOk ? 1 : 0);
	DX12FrameAnalysisLogJsonFunc("FrameAnalysisComplete",
		"\"shaders\":%zu,\"psos\":%zu,\"bindingTrace\":%u,\"resourceTable\":%u,\"metadata\":%u,\"psoSummary\":%u,\"resourceFiles\":%u",
		shaders.size(), psos.size(),
		bindingTraceOk ? 1 : 0,
		resourceTableOk ? 1 : 0,
		metadataOk ? 1 : 0,
		psoSummaryOk ? 1 : 0,
		resourceFilesOk ? 1 : 0);
}

static void DX12DumpShaderRecords(
	const std::vector<ShaderRecord> &shaders, const std::vector<PsoRecord> &psos,
	const wchar_t *statusPrefix)
{
	wchar_t dir[MAX_PATH];
	if (!GetShaderFixesDirectory(dir, ARRAYSIZE(dir))) {
		DX12Log("Failed to resolve DX12 ShaderFixes directory\n");
		DX12SetOverlayStatus(L"F9 shader dump failed: cannot create ShaderFixes");
		return;
	}

	DX12SetOverlayStatus(L"F9 shader dump running");
	DX12Log("F9 shader dump begin: dir=%S shaders=%zu psos=%zu mode=%S\n",
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
	DX12SetOverlayStatus(status);
	DX12Log("F9 shader dump complete: dir=%S shaders=%u/%zu skippedShaders=%u asm=%u/%zu skippedAsm=%u psos=%zu psoSummary=%u\n",
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
		DX12Log("F9 shader dump failed after exception=0x%lx\n", GetExceptionCode());
		DX12SetOverlayStatus(L"F9 shader dump failed");
	}
	delete args;
	InterlockedExchange(&gShaderDumpRunning, 0);
	return 0;
}

void DX12RequestShaderDump()
{
	if (DX12ShaderDumpIsBusy()) {
		DX12Log("F9 shader dump ignored: already armed/running\n");
		DX12SetOverlayStatus(L"F9 shader dump still running");
		return;
	}

	InterlockedExchange(&gShaderDumpCaptureRequested, 1);
	DX12SetOverlayStatus(L"F9 armed: waiting for next frame");
}

bool DX12ShaderDumpIsCaptureRequested()
{
	return InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 0) != 0;
}

void DX12ShaderDumpBeginCapture()
{
	if (InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 1) == 1) {
		InterlockedExchange(&gShaderDumpCapturing, 1);
		DX12SetOverlayStatus(L"F9 capturing next frame shaders");
	}
}

bool DX12ShaderDumpEndCapture()
{
	if (InterlockedCompareExchange(&gShaderDumpCapturing, 0, 1) == 1)
		return true;
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
		DX12SetOverlayStatus(L"F9 shader dump still running");
		return;
	}

	std::vector<DX12PsoShaderInfo> frameInfos;
	DX12GetCurrentFrameShaderInfos(&frameInfos);
	ShaderDumpThreadArgs *args = new (std::nothrow) ShaderDumpThreadArgs();
	if (!args) {
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12SetOverlayStatus(L"F9 shader dump failed: out of memory");
		return;
	}
	SnapshotFrameShadersAndPsos(frameInfos, &args->shaders, &args->psos);
	DX12Log("F9 captured frame shaders: framePsos=%zu shaders=%zu psos=%zu\n",
		frameInfos.size(), args->shaders.size(), args->psos.size());
	if (args->shaders.empty() && args->psos.empty()) {
		delete args;
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12SetOverlayStatus(L"F9 not ready: no frame shaders captured");
		return;
	}
	DX12SetOverlayStatus(L"F9 dumping captured frame shaders");

	HANDLE thread = CreateThread(nullptr, 0, DX12ShaderDumpThreadProc, args, 0, nullptr);
	if (!thread) {
		InterlockedExchange(&gShaderDumpRunning, 0);
		delete args;
		DX12Log("F9 shader dump failed: CreateThread error=%lu\n", GetLastError());
		DX12SetOverlayStatus(L"F9 shader dump failed: cannot start thread");
		return;
	}
	CloseHandle(thread);
}
