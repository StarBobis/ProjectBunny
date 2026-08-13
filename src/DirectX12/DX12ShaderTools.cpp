#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

static void DX12RefreshShaderDumpStateFlags()
{
	LONG flags = 0;
	if (InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 0) != 0)
		flags |= DX12_SHADER_DUMP_STATE_CAPTURE_REQUESTED;
	if (InterlockedCompareExchange(&gShaderDumpCapturing, 0, 0) != 0)
		flags |= DX12_SHADER_DUMP_STATE_CAPTURING;
	if (InterlockedCompareExchange(&gShaderDumpRunning, 0, 0) != 0)
		flags |= DX12_SHADER_DUMP_STATE_RUNNING;
	InterlockedExchange(&gShaderDumpStateFlags, flags);
}

static LONG DX12ReadShaderDumpStateFlags()
{
	return InterlockedCompareExchange(&gShaderDumpStateFlags, 0, 0);
}

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
	DX12RefreshShaderDumpStateFlags();
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
	DX12RefreshShaderDumpStateFlags();
	DX12HotPathState::Update();
	DX12OverlayStatusState::SetStatus(L"F9 armed: waiting for next frame");
}

bool DX12ShaderDumpIsCaptureRequested()
{
	return (DX12ReadShaderDumpStateFlags() &
		DX12_SHADER_DUMP_STATE_CAPTURE_REQUESTED) != 0;
}

void DX12ShaderDumpBeginCapture()
{
	if (InterlockedCompareExchange(&gShaderDumpCaptureRequested, 0, 1) == 1) {
		InterlockedExchange(&gShaderDumpCapturing, 1);
		DX12RefreshShaderDumpStateFlags();
		DX12HotPathState::Update();
		DX12OverlayStatusState::SetStatus(L"F9 capturing next frame shaders");
	}
}

bool DX12ShaderDumpEndCapture()
{
	if (InterlockedCompareExchange(&gShaderDumpCapturing, 0, 1) == 1) {
		DX12RefreshShaderDumpStateFlags();
		DX12HotPathState::Update();
		return true;
	}
	return false;
}

bool DX12ShaderDumpIsCapturingFrame()
{
	return (DX12ReadShaderDumpStateFlags() &
		DX12_SHADER_DUMP_STATE_CAPTURING) != 0;
}

bool DX12ShaderDumpIsBusy()
{
	return DX12ReadShaderDumpStateFlags() != 0;
}

void DX12DumpCapturedFrameShaders()
{
	if (InterlockedCompareExchange(&gShaderDumpRunning, 1, 0) != 0) {
		DX12OverlayStatusState::SetStatus(L"F9 shader dump still running");
		return;
	}
	DX12RefreshShaderDumpStateFlags();
	DX12HotPathState::Update();

	std::vector<DX12PsoShaderInfo> frameInfos;
	DX12BindingFrameExport::GetCurrentFrameShaderInfos(&frameInfos);
	ShaderDumpThreadArgs *args = new (std::nothrow) ShaderDumpThreadArgs();
	if (!args) {
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12RefreshShaderDumpStateFlags();
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
		DX12RefreshShaderDumpStateFlags();
		DX12HotPathState::Update();
		DX12OverlayStatusState::SetStatus(L"F9 not ready: no frame shaders captured");
		return;
	}
	DX12OverlayStatusState::SetStatus(L"F9 dumping captured frame shaders");

	HANDLE thread = CreateThread(nullptr, 0, DX12ShaderDumpThreadProc, args, 0, nullptr);
	if (!thread) {
		InterlockedExchange(&gShaderDumpRunning, 0);
		DX12RefreshShaderDumpStateFlags();
		DX12HotPathState::Update();
		delete args;
		DX12LogManager::Text("F9 shader dump failed: CreateThread error=%lu\n", GetLastError());
		DX12OverlayStatusState::SetStatus(L"F9 shader dump failed: cannot start thread");
		return;
	}
	CloseHandle(thread);
}

enum class HuntStage
{
	VS,
	PS,
	CS
};

enum class HuntCopyTarget
{
	None,
	VS,
	PS,
	CS,
	IB,
	VB
};

struct StageSelection
{
	std::set<UINT64> visited;
	size_t selected = 0;
	bool armed = false;
};

struct IaSelection
{
	std::set<uint32_t> visited;
	std::unordered_map<uint32_t, UINT64> lastSeenFrame;
	size_t selected = 0;
	uint32_t selectedHash = 0;
	bool armed = false;
};

struct HuntIaBuffer
{
	const char *role = "";
	D3D12_GPU_VIRTUAL_ADDRESS gpuVa = 0;
	UINT size = 0;
	UINT stride = 0;
	UINT slot = 0;
	UINT format = 0;
	bool resolved = false;
	ID3D12Resource *resource = nullptr;
	UINT64 resourceGpuVa = 0;
	UINT64 resourceBytes = 0;
	bool valid = false;
};

struct HuntIaState
{
	HuntIaBuffer indexBuffer;
	std::vector<HuntIaBuffer> vertexBuffers;
};

struct HuntCommandListVisits
{
	std::set<UINT64> vs;
	std::set<UINT64> ps;
	std::set<UINT64> cs;
	std::set<uint32_t> ib;
	std::set<uint32_t> vb;
	UINT64 draws = 0;
	UINT64 dispatches = 0;
	UINT64 indirects = 0;
};

static SRWLOCK gHuntLock = SRWLOCK_INIT;
static volatile LONG gHuntingActive = 0;
static std::unordered_map<ID3D12GraphicsCommandList*, HuntCommandListVisits> gCommandListVisits;
static StageSelection gVS;
static StageSelection gPS;
static StageSelection gCS;
static IaSelection gIB;
static IaSelection gVB;
static HuntCopyTarget gLastCopyTarget = HuntCopyTarget::None;
static UINT64 gPipelineStateBinds = 0;
static UINT64 gDrawCalls = 0;
static UINT64 gDispatchCalls = 0;
static UINT64 gIndirectCalls = 0;
static UINT64 gPresentFrames = 0;
static UINT64 gLastOverlayStatsFrame = 0;
static constexpr UINT64 kOverlayBufferHashLifetime = 1;

static HuntIaBuffer MakeIaView(
	const char *role, UINT slot, UINT64 gpuVa, UINT size, UINT stride, UINT format)
{
	HuntIaBuffer buffer;
	buffer.role = role ? role : "";
	buffer.slot = slot;
	if (!gpuVa || !size)
		return buffer;

	buffer.gpuVa = gpuVa;
	buffer.size = size;
	buffer.stride = stride;
	buffer.format = format;
	buffer.valid = true;
	return buffer;
}
static UINT64 SelectedHashLocked(const StageSelection &stage)
{
	if (stage.visited.empty())
		return 0;

	size_t index = stage.selected % stage.visited.size();
	auto it = stage.visited.begin();
	std::advance(it, index);
	return *it;
}

static void AddVisitedLocked(StageSelection &stage, UINT64 hash)
{
	if (!hash)
		return;
	stage.visited.insert(hash);
	DX12ResidentMaintenanceTelemetryUtils::RecordHuntVisitedWrite();
	if (!stage.visited.empty())
		stage.selected %= stage.visited.size();
}

static uint32_t IaRegionHash(const HuntIaBuffer &buffer, const DX12IaRegionDraw &draw)
{
	if (!buffer.valid)
		return 0;

	if (buffer.format) {
		D3D12_INDEX_BUFFER_VIEW view = {};
		view.BufferLocation = buffer.gpuVa;
		view.SizeInBytes = buffer.size;
		view.Format = static_cast<DXGI_FORMAT>(buffer.format);
		return DX12IaRegion::HashIndexBufferView(view, draw);
	}

	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = buffer.gpuVa;
	view.SizeInBytes = buffer.size;
	view.StrideInBytes = buffer.stride;
	return DX12IaRegion::HashVertexBufferView(buffer.slot, view, draw);
}

static void SyncIaSelectionPositionLocked(IaSelection &selection)
{
	if (selection.visited.empty()) {
		selection.selected = 0;
		if (!selection.armed)
			selection.selectedHash = 0;
		return;
	}
	if (selection.selectedHash) {
		auto it = selection.visited.find(selection.selectedHash);
		if (it != selection.visited.end()) {
			selection.selected = static_cast<size_t>(
				std::distance(selection.visited.begin(), it));
			return;
		}
		if (selection.armed)
			return;
	}
	if (!selection.armed)
		return;
	selection.selected %= selection.visited.size();
	auto it = selection.visited.begin();
	std::advance(it, selection.selected);
	selection.selectedHash = *it;
}

static void AddVisitedLocked(IaSelection &selection, uint32_t hash)
{
	if (!hash)
		return;
	selection.visited.insert(hash);
	DX12ResidentMaintenanceTelemetryUtils::RecordHuntVisitedWrite();
	selection.lastSeenFrame[hash] = gPresentFrames;
	SyncIaSelectionPositionLocked(selection);
}

static void PurgeStaleIaLocked(IaSelection &selection)
{
	for (auto it = selection.lastSeenFrame.begin(); it != selection.lastSeenFrame.end(); ) {
		const uint32_t hash = it->first;
		const UINT64 frame = it->second;
		if (gPresentFrames - frame > kOverlayBufferHashLifetime) {
			selection.visited.erase(hash);
			it = selection.lastSeenFrame.erase(it);
		} else {
			++it;
		}
	}
	if (selection.visited.empty()) {
		selection.selected = 0;
		return;
	}
	SyncIaSelectionPositionLocked(selection);
}

static void AddPsoVisitedLocked(const DX12PsoShaderInfo &info, bool draw, bool dispatch)
{
	if (draw) {
		AddVisitedLocked(gVS, info.hasVS ? info.vs : 0);
		AddVisitedLocked(gPS, info.hasPS ? info.ps : 0);
	}
	if (dispatch)
		AddVisitedLocked(gCS, info.hasCS ? info.cs : 0);
}

static size_t DisplayIndexLocked(const StageSelection &stage)
{
	return stage.armed && !stage.visited.empty() ? stage.selected + 1 : 0;
}

static size_t DisplayIndexLocked(const IaSelection &selection)
{
	if (!selection.armed || selection.visited.empty() || !selection.selectedHash)
		return 0;
	auto it = selection.visited.find(selection.selectedHash);
	if (it == selection.visited.end())
		return 0;
	return static_cast<size_t>(std::distance(selection.visited.begin(), it)) + 1;
}

static uint32_t SelectedIaHashLocked(const IaSelection &selection)
{
	return selection.selectedHash;
}

static void AdvanceStage(StageSelection &stage, HuntCopyTarget target, int delta)
{
	if (!DX12HuntIsEnabled())
		return;

	size_t selected = 0;
	size_t count = 0;
	UINT64 hash = 0;
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	stage.armed = true;
	gLastCopyTarget = target;
	if (!stage.visited.empty()) {
		const size_t size = stage.visited.size();
		if (delta < 0)
			stage.selected = (stage.selected + size - 1) % size;
		else
			stage.selected = (stage.selected + 1) % size;
	}
	selected = DisplayIndexLocked(stage);
	count = stage.visited.size();
	hash = SelectedHashLocked(stage);
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12LogManager::Fields("DX12HuntSelect",
		"target=%s,selected=%zu,count=%zu,hash=%016llx",
		target == HuntCopyTarget::VS ? "VS" :
		target == HuntCopyTarget::PS ? "PS" :
		target == HuntCopyTarget::CS ? "CS" : "",
		selected, count, static_cast<unsigned long long>(hash));
	DX12HuntRefreshOverlay();
}

static void AdvanceIa(IaSelection &selection, HuntCopyTarget target, int delta)
{
	if (!DX12HuntIsEnabled())
		return;

	size_t selected = 0;
	size_t count = 0;
	uint32_t hash = 0;
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	selection.armed = true;
	gLastCopyTarget = target;
	if (!selection.visited.empty()) {
		auto it = selection.selectedHash ?
			selection.visited.find(selection.selectedHash) : selection.visited.end();
		if (!selection.selectedHash) {
			it = delta < 0 ? std::prev(selection.visited.end()) : selection.visited.begin();
		} else if (it == selection.visited.end()) {
			it = selection.visited.lower_bound(selection.selectedHash);
			if (delta < 0) {
				if (it == selection.visited.begin())
					it = std::prev(selection.visited.end());
				else
					--it;
			} else if (it == selection.visited.end()) {
				it = selection.visited.begin();
			}
		} else if (delta < 0) {
			if (it == selection.visited.begin())
				it = std::prev(selection.visited.end());
			else
				--it;
		} else {
			++it;
			if (it == selection.visited.end())
				it = selection.visited.begin();
		}
		selection.selectedHash = *it;
		selection.selected = static_cast<size_t>(std::distance(selection.visited.begin(), it));
	}
	selected = DisplayIndexLocked(selection);
	count = selection.visited.size();
	hash = SelectedIaHashLocked(selection);
	ReleaseSRWLockExclusive(&gHuntLock);
	DX12LogManager::Fields("DX12HuntSelect",
		"target=%s,selected=%zu,count=%zu,hash=%08x",
		target == HuntCopyTarget::IB ? "IB" :
		target == HuntCopyTarget::VB ? "VB" : "",
		selected, count, hash);
	DX12HuntRefreshOverlay();
}

static HuntIaState BuildHuntIaState(const DX12ActiveIaState &ia)
{
	HuntIaState state;
	if (ia.hasIndexBuffer && ia.indexBuffer.BufferLocation && ia.indexBuffer.SizeInBytes) {
		state.indexBuffer = MakeIaView(
			"IB", 0, ia.indexBuffer.BufferLocation, ia.indexBuffer.SizeInBytes,
			0, static_cast<UINT>(ia.indexBuffer.Format));
	}
	for (UINT slot = 0; slot < ARRAYSIZE(ia.vertexBuffers); ++slot) {
		if (!ia.hasVertexBuffer[slot])
			continue;
		const D3D12_VERTEX_BUFFER_VIEW &view = ia.vertexBuffers[slot];
		HuntIaBuffer buffer;
		buffer.role = "VB";
		buffer.slot = slot;
		if (view.BufferLocation && view.SizeInBytes) {
			buffer = MakeIaView(
				"VB", slot, view.BufferLocation, view.SizeInBytes,
				view.StrideInBytes, 0);
		}
		if (state.vertexBuffers.size() <= slot)
			state.vertexBuffers.resize(slot + 1);
		state.vertexBuffers[slot] = buffer;
	}
	return state;
}


static bool PsoShouldSkip(ID3D12PipelineState *pipelineState, bool dispatch, UINT64 vs, UINT64 ps, UINT64 cs)
{
	DX12PsoShaderInfo info = {};
	if (!pipelineState || !DX12GetPipelineStateShaderInfo(pipelineState, &info))
		return false;

	if (dispatch)
		return cs && info.hasCS && info.cs == cs;

	return (vs && info.hasVS && info.vs == vs) ||
		(ps && info.hasPS && info.ps == ps);
}

void DX12HuntResetCommandList(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pipelineState)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	(void)pipelineState;
	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	gCommandListVisits.erase(commandList);
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntUnregisterCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return;

	bool removed = false;
	AcquireSRWLockExclusive(&gHuntLock);
	auto it = gCommandListVisits.find(commandList);
	if (it != gCommandListVisits.end()) {
		gCommandListVisits.erase(it);
		removed = true;
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (removed) {
		DX12LogManager::Fields("DX12ShaderHunt",
			"stage=unregister_command_list,commandList=%p",
			commandList);
	}
}

void DX12HuntObservePipelineState(ID3D12PipelineState *pipelineState)
{
	if (!DX12HuntIsEnabled())
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	if (!DX12HuntIsEnabled()) {
		ReleaseSRWLockExclusive(&gHuntLock);
		return;
	}
	gPipelineStateBinds++;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12PsoShaderInfo info = {};
	if (DX12HuntIsEnabled() && pipelineState && DX12GetPipelineStateShaderInfo(pipelineState, &info)) {
		AcquireSRWLockExclusive(&gHuntLock);
		if (DX12HuntIsEnabled())
			AddPsoVisitedLocked(info, true, true);
		ReleaseSRWLockExclusive(&gHuntLock);
	}
}

void DX12HuntRecordDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	const HuntIaState iaState = runtimeState ?
		BuildHuntIaState(runtimeState->ia) : HuntIaState();
	const bool hasIaState = runtimeState != nullptr;

	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);
	uint32_t ibHash = 0;
	std::vector<uint32_t> vbHashes;
	if (hasIaState) {
		if (indexed && iaState.indexBuffer.valid)
			ibHash = IaRegionHash(iaState.indexBuffer, draw);
		for (const HuntIaBuffer &buffer : iaState.vertexBuffers) {
			uint32_t hash = IaRegionHash(buffer, draw);
			if (hash)
				vbHashes.push_back(hash);
		}
	}

	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = gCommandListVisits[commandList];
		visits.draws++;
		if (hasInfo) {
			if (info.hasVS)
				visits.vs.insert(info.vs);
			if (info.hasPS)
				visits.ps.insert(info.ps);
		}
		if (ibHash)
			visits.ib.insert(ibHash);
		for (uint32_t hash : vbHashes)
			visits.vb.insert(hash);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntPublishCommandList(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	HuntCommandListVisits visits;
	AcquireSRWLockExclusive(&gHuntLock);
	auto it = gCommandListVisits.find(commandList);
	if (it != gCommandListVisits.end())
		visits = it->second;
	if (DX12HuntIsEnabled()) {
		gDrawCalls += visits.draws;
		gDispatchCalls += visits.dispatches;
		gIndirectCalls += visits.indirects;
		for (UINT64 hash : visits.vs)
			AddVisitedLocked(gVS, hash);
		for (UINT64 hash : visits.ps)
			AddVisitedLocked(gPS, hash);
		for (UINT64 hash : visits.cs)
			AddVisitedLocked(gCS, hash);
		for (uint32_t hash : visits.ib)
			AddVisitedLocked(gIB, hash);
		for (uint32_t hash : visits.vb)
			AddVisitedLocked(gVB, hash);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);

	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = gCommandListVisits[commandList];
		visits.dispatches++;
		if (hasInfo && info.hasCS)
			visits.cs.insert(info.cs);
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

void DX12HuntRecordExecuteIndirect(ID3D12GraphicsCommandList *commandList)
{
	if (!DX12HuntIsEnabled() || !commandList)
		return;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	DX12PsoShaderInfo info = {};
	const bool hasInfo = pipelineState &&
		DX12GetPipelineStateShaderInfo(pipelineState, &info);

	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		HuntCommandListVisits &visits = gCommandListVisits[commandList];
		visits.indirects++;
		if (hasInfo) {
			if (info.hasVS)
				visits.vs.insert(info.vs);
			if (info.hasPS)
				visits.ps.insert(info.ps);
			if (info.hasCS)
				visits.cs.insert(info.cs);
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);
}

bool DX12HuntShouldSkipDraw(
	ID3D12GraphicsCommandList *commandList, bool indexed,
	UINT vertexCount, UINT indexCount, UINT firstVertex, UINT firstIndex,
	INT baseVertex)
{
	if (!commandList)
		return false;
	DX12IaRegionDraw draw = DX12IaRegionDraw::FromDrawArguments(
		indexed, vertexCount, indexCount, firstVertex, firstIndex, baseVertex);

	bool enabled = false;
	bool vsArmed = false;
	bool psArmed = false;
	bool ibArmed = false;
	bool vbArmed = false;
	UINT64 selectedVS = 0;
	UINT64 selectedPS = 0;
	uint32_t selectedIB = 0;
	uint32_t selectedVB = 0;
	AcquireSRWLockShared(&gHuntLock);
	enabled = DX12HuntIsEnabled();
	if (enabled) {
		vsArmed = gVS.armed;
		psArmed = gPS.armed;
		ibArmed = gIB.armed;
		vbArmed = gVB.armed;
		selectedVS = SelectedHashLocked(gVS);
		selectedPS = SelectedHashLocked(gPS);
		selectedIB = SelectedIaHashLocked(gIB);
		selectedVB = SelectedIaHashLocked(gVB);
	}
	ReleaseSRWLockShared(&gHuntLock);
	if (!enabled)
		return false;

	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	const HuntIaState iaState = runtimeState ?
		BuildHuntIaState(runtimeState->ia) : HuntIaState();
	const bool hasIaState = runtimeState != nullptr;
	if ((vsArmed || psArmed) && PsoShouldSkip(
			pipelineState, false,
			vsArmed ? selectedVS : 0,
			psArmed ? selectedPS : 0,
			0))
		return true;

	if (!hasIaState)
		return false;
	if (indexed && ibArmed && selectedIB && iaState.indexBuffer.valid &&
	    IaRegionHash(iaState.indexBuffer, draw) == selectedIB)
		return true;
	if (vbArmed && selectedVB) {
		for (const HuntIaBuffer &buffer : iaState.vertexBuffers) {
			if (IaRegionHash(buffer, draw) == selectedVB)
				return true;
		}
	}
	return false;
}

bool DX12HuntShouldSkipDispatch(ID3D12GraphicsCommandList *commandList)
{
	if (!commandList)
		return false;
	bool enabled = false;
	bool csArmed = false;
	UINT64 selectedCS = 0;
	AcquireSRWLockShared(&gHuntLock);
	enabled = DX12HuntIsEnabled();
	if (enabled) {
		csArmed = gCS.armed;
		selectedCS = SelectedHashLocked(gCS);
	}
	ReleaseSRWLockShared(&gHuntLock);
	const DX12SimulateContext *runtimeState =
		DX12CommandListRuntime::GetStatePtr(commandList);
	ID3D12PipelineState *pipelineState = runtimeState ? runtimeState->pipelineState : nullptr;
	return enabled && csArmed && selectedCS &&
		PsoShouldSkip(pipelineState, true, 0, 0, selectedCS);
}

bool DX12HuntIsEnabled()
{
	return gHuntingActive != 0;
}

bool DX12HuntNeedsHotPath()
{
	return InterlockedCompareExchange(&gHuntingActive, 0, 0) != 0;
}

bool DX12HuntShouldDrawOverlay()
{
	return DX12HuntIsEnabled();
}

void DX12HuntToggle()
{
	size_t oldIa = 0;
	size_t oldIB = 0;
	size_t oldVB = 0;

	const bool enabled = InterlockedCompareExchange(&gHuntingActive, 0, 0) == 0;

	if (enabled) {
		AcquireSRWLockExclusive(&gHuntLock);
		oldIa = gCommandListVisits.size();
		oldIB = gIB.visited.size();
		oldVB = gVB.visited.size();
		gCommandListVisits.clear();
		gVS = StageSelection();
		gPS = StageSelection();
		gCS = StageSelection();
		gIB = IaSelection();
		gVB = IaSelection();
		gLastCopyTarget = HuntCopyTarget::None;
		gPipelineStateBinds = 0;
		gDrawCalls = 0;
		gDispatchCalls = 0;
		gIndirectCalls = 0;
		gLastOverlayStatsFrame = gPresentFrames;
		ReleaseSRWLockExclusive(&gHuntLock);
		InterlockedExchange(&gHuntingActive, 1);
		DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode::Hunting);
	} else {
		InterlockedExchange(&gHuntingActive, 0);
		DX12RuntimeState::SetRuntimeMode(DX12RuntimeMode::Mod);
	}

	DX12LogManager::Fields("DX12HuntToggle",
		"stage=state,enabled=%s,oldIa=%zu,oldIb=%zu,oldVb=%zu",
		enabled ? "true" : "false", oldIa, oldIB, oldVB);
	DX12HotPathState::Update();
	DX12LogManager::Fields("DX12HuntToggle",
		"stage=hotpath_update,enabled=%s",
		enabled ? "true" : "false");
	if (enabled) {
		DX12PrimeDescriptorHistoryForHunting();
		DX12LogManager::Fields("DX12HuntToggle", "stage=prime_descriptor_history,enabled=true");
		DX12HuntRefreshOverlay();
		DX12LogManager::Fields("DX12HuntToggle", "stage=refresh_overlay,enabled=true");
		DX12CommandListRuntime::BumpTrackingGeneration();
		DX12LogManager::Fields("DX12HuntToggle", "stage=bump_generation,enabled=true");
		DX12Overlay::Show();
		DX12LogManager::Fields("DX12HuntToggle", "stage=show_overlay,enabled=true");
	} else {
		DX12Overlay::Hide();
		DX12LogManager::Fields("DX12HuntToggle", "stage=hide_overlay,enabled=false");
	}
	DX12LogManager::Text("DX12 hunting toggled %s\n", enabled ? "on" : "off");
}

void DX12HuntOnPresent()
{
	if (!DX12HuntIsEnabled()) {
		return;
	}

	size_t ibCount = 0;
	size_t vbCount = 0;
	bool shouldLog = false;
	AcquireSRWLockExclusive(&gHuntLock);
	gPresentFrames++;
	PurgeStaleIaLocked(gIB);
	PurgeStaleIaLocked(gVB);
	ibCount = gIB.visited.size();
	vbCount = gVB.visited.size();
	if (gPresentFrames - gLastOverlayStatsFrame >= 120) {
		gLastOverlayStatsFrame = gPresentFrames;
		shouldLog = true;
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (shouldLog)
		DX12LogManager::Fields("DX12HuntIaStats",
			"frame=%llu,ib=%zu,vb=%zu",
			static_cast<unsigned long long>(gPresentFrames), ibCount, vbCount);
}

void DX12HuntPreviousVS() { AdvanceStage(gVS, HuntCopyTarget::VS, -1); }
void DX12HuntNextVS() { AdvanceStage(gVS, HuntCopyTarget::VS, 1); }
void DX12HuntPreviousPS() { AdvanceStage(gPS, HuntCopyTarget::PS, -1); }
void DX12HuntNextPS() { AdvanceStage(gPS, HuntCopyTarget::PS, 1); }
void DX12HuntPreviousCS() { AdvanceStage(gCS, HuntCopyTarget::CS, -1); }
void DX12HuntNextCS() { AdvanceStage(gCS, HuntCopyTarget::CS, 1); }
void DX12HuntPreviousIB() { AdvanceIa(gIB, HuntCopyTarget::IB, -1); }
void DX12HuntNextIB() { AdvanceIa(gIB, HuntCopyTarget::IB, 1); }
void DX12HuntPreviousVB() { AdvanceIa(gVB, HuntCopyTarget::VB, -1); }
void DX12HuntNextVB() { AdvanceIa(gVB, HuntCopyTarget::VB, 1); }

static bool CopyWideTextToClipboard(const wchar_t *text)
{
	if (!text || !text[0])
		return false;
	if (!OpenClipboard(nullptr))
		return false;
	EmptyClipboard();
	const size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!memory) {
		CloseClipboard();
		return false;
	}
	void *locked = GlobalLock(memory);
	if (!locked) {
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}
	memcpy(locked, text, bytes);
	GlobalUnlock(memory);
	if (!SetClipboardData(CF_UNICODETEXT, memory)) {
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}
	CloseClipboard();
	return true;
}

static const wchar_t *CopyTargetName(HuntCopyTarget target)
{
	switch (target) {
	case HuntCopyTarget::VS:
		return L"VS";
	case HuntCopyTarget::PS:
		return L"PS";
	case HuntCopyTarget::CS:
		return L"CS";
	case HuntCopyTarget::IB:
		return L"IB";
	case HuntCopyTarget::VB:
		return L"VB";
	default:
		return L"";
	}
}

void DX12HuntCopySelectedHash()
{
	if (!DX12HuntIsEnabled())
		return;

	wchar_t text[32] = {};
	HuntCopyTarget target = HuntCopyTarget::None;

	AcquireSRWLockShared(&gHuntLock);
	target = gLastCopyTarget;
	switch (target) {
	case HuntCopyTarget::VS:
		if (gVS.armed)
			swprintf_s(text, L"%016llx",
				static_cast<unsigned long long>(SelectedHashLocked(gVS)));
		break;
	case HuntCopyTarget::PS:
		if (gPS.armed)
			swprintf_s(text, L"%016llx",
				static_cast<unsigned long long>(SelectedHashLocked(gPS)));
		break;
	case HuntCopyTarget::CS:
		if (gCS.armed)
			swprintf_s(text, L"%016llx",
				static_cast<unsigned long long>(SelectedHashLocked(gCS)));
		break;
	case HuntCopyTarget::IB:
		if (gIB.armed)
			swprintf_s(text, L"%08x", SelectedIaHashLocked(gIB));
		break;
	case HuntCopyTarget::VB:
		if (gVB.armed)
			swprintf_s(text, L"%08x", SelectedIaHashLocked(gVB));
		break;
	default:
		break;
	}
	ReleaseSRWLockShared(&gHuntLock);

	if (!text[0] || wcspbrk(text, L"123456789abcdefABCDEF") == nullptr) {
		DX12OverlayStatusState::SetStatus(L"DX12 hunting: no selected hash to copy");
		DX12HuntRefreshOverlay();
		return;
	}

	wchar_t status[128];
	if (CopyWideTextToClipboard(text))
		swprintf_s(status, L"Copied %ls hash %ls", CopyTargetName(target), text);
	else
		swprintf_s(status, L"Copy %ls hash failed", CopyTargetName(target));
	DX12OverlayStatusState::SetStatus(status);
	DX12HuntRefreshOverlay();
}

void DX12HuntResetSelection()
{
	if (!DX12HuntIsEnabled())
		return;

	AcquireSRWLockExclusive(&gHuntLock);
	gVS.armed = false;
	gPS.armed = false;
	gCS.armed = false;
	gIB.armed = false;
	gVB.armed = false;
	gLastCopyTarget = HuntCopyTarget::None;
	ReleaseSRWLockExclusive(&gHuntLock);

	DX12OverlayStatusState::SetStatus(L"DX12 hunting selection reset");
	DX12HuntRefreshOverlay();
}

void DX12HuntRefreshOverlay()
{
	wchar_t status[1024];

	AcquireSRWLockShared(&gHuntLock);
	const UINT64 vs = SelectedHashLocked(gVS);
	const UINT64 ps = SelectedHashLocked(gPS);
	const UINT64 cs = SelectedHashLocked(gCS);
	const uint32_t ib = SelectedIaHashLocked(gIB);
	const uint32_t vb = SelectedIaHashLocked(gVB);
	swprintf_s(status,
		L"DX12 Hunting: %ls\n"
		L"VS %ls %zu/%zu %016llx   PS %ls %zu/%zu %016llx   CS %ls %zu/%zu %016llx\n"
		L"IB %ls %zu/%zu %08x   VB %ls %zu/%zu %08x\n"
		L"PSO binds %llu   Draw %llu Dispatch %llu Indirect %llu\n"
		L"Num0 toggle  Num+ reset  Num9 copy  4/5 VS  1/2 PS  Dec+1/2 CS  7/8 IB  Dec+7/8 VB",
		DX12HuntIsEnabled() ? L"on" : L"off",
		gVS.armed ? L"*" : L" ",
		DisplayIndexLocked(gVS), gVS.visited.size(),
		static_cast<unsigned long long>(gVS.armed ? vs : 0),
		gPS.armed ? L"*" : L" ",
		DisplayIndexLocked(gPS), gPS.visited.size(),
		static_cast<unsigned long long>(gPS.armed ? ps : 0),
		gCS.armed ? L"*" : L" ",
		DisplayIndexLocked(gCS), gCS.visited.size(),
		static_cast<unsigned long long>(gCS.armed ? cs : 0),
		gIB.armed ? L"*" : L" ",
		DisplayIndexLocked(gIB), gIB.visited.size(),
		gIB.armed ? ib : 0,
		gVB.armed ? L"*" : L" ",
		DisplayIndexLocked(gVB), gVB.visited.size(),
		gVB.armed ? vb : 0,
		static_cast<unsigned long long>(gPipelineStateBinds),
		static_cast<unsigned long long>(gDrawCalls),
		static_cast<unsigned long long>(gDispatchCalls),
		static_cast<unsigned long long>(gIndirectCalls));
	ReleaseSRWLockShared(&gHuntLock);

	DX12OverlayStatusState::SetStatus(status);
}

static void FormatStageItem(
	DX12HuntListItem *item, size_t displayIndex, UINT64 hash, const StageSelection &selection)
{
	if (!item)
		return;
	swprintf_s(item->label, L"%zu  %016llx", displayIndex,
		static_cast<unsigned long long>(hash));
	item->hash = hash;
	item->selected = selection.armed && SelectedHashLocked(selection) == hash;
}

static void FormatIaItem(
	DX12HuntListItem *item, size_t displayIndex, uint32_t hash, const IaSelection &selection)
{
	if (!item)
		return;
	swprintf_s(item->label, L"%zu  %08x", displayIndex, hash);
	item->hash = hash;
	item->selected = selection.armed && SelectedIaHashLocked(selection) == hash;
}

size_t DX12HuntGetListItems(
	DX12HuntListCategory category, DX12HuntListItem *items, size_t itemCount)
{
	size_t total = 0;
	AcquireSRWLockShared(&gHuntLock);
	switch (category) {
	case DX12_HUNT_LIST_VS:
		total = gVS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gVS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gVS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_PS:
		total = gPS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gPS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gPS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_CS:
		total = gCS.visited.size();
		if (items) {
			size_t index = 0;
			for (UINT64 hash : gCS.visited) {
				if (index >= itemCount)
					break;
				FormatStageItem(&items[index], index + 1, hash, gCS);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_IB:
		total = gIB.visited.size();
		if (items) {
			size_t index = 0;
			for (uint32_t hash : gIB.visited) {
				if (index >= itemCount)
					break;
				FormatIaItem(&items[index], index + 1, hash, gIB);
				++index;
			}
		}
		break;
	case DX12_HUNT_LIST_VB:
		total = gVB.visited.size();
		if (items) {
			size_t index = 0;
			for (uint32_t hash : gVB.visited) {
				if (index >= itemCount)
					break;
				FormatIaItem(&items[index], index + 1, hash, gVB);
				++index;
			}
		}
		break;
	default:
		break;
	}
	ReleaseSRWLockShared(&gHuntLock);
	return total;
}

template <typename Selection>
static bool SelectByIndexLocked(Selection &selection, size_t index)
{
	if (index >= selection.visited.size())
		return false;
	selection.armed = true;
	selection.selected = index;
	auto it = selection.visited.begin();
	std::advance(it, index);
	selection.selectedHash = *it;
	return true;
}

template <>
bool SelectByIndexLocked<StageSelection>(StageSelection &selection, size_t index)
{
	if (index >= selection.visited.size())
		return false;
	selection.armed = true;
	selection.selected = index;
	return true;
}

void DX12HuntSelectListItem(DX12HuntListCategory category, size_t index)
{
	if (!DX12HuntIsEnabled())
		return;

	bool selected = false;
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		switch (category) {
		case DX12_HUNT_LIST_VS:
			selected = SelectByIndexLocked(gVS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::VS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_PS:
			selected = SelectByIndexLocked(gPS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::PS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_CS:
			selected = SelectByIndexLocked(gCS, index);
			gLastCopyTarget = selected ? HuntCopyTarget::CS : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_IB:
			selected = SelectByIndexLocked(gIB, index);
			gLastCopyTarget = selected ? HuntCopyTarget::IB : gLastCopyTarget;
			break;
		case DX12_HUNT_LIST_VB:
			selected = SelectByIndexLocked(gVB, index);
			gLastCopyTarget = selected ? HuntCopyTarget::VB : gLastCopyTarget;
			break;
		default:
			break;
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (selected)
		DX12HuntRefreshOverlay();
}

void DX12HuntSelectListHash(DX12HuntListCategory category, UINT64 hash)
{
	if (!DX12HuntIsEnabled() || !hash)
		return;

	bool selected = false;
	AcquireSRWLockExclusive(&gHuntLock);
	if (DX12HuntIsEnabled()) {
		switch (category) {
		case DX12_HUNT_LIST_VS:
			if (gVS.visited.find(hash) != gVS.visited.end()) {
				gVS.armed = true;
				gVS.selected = static_cast<size_t>(std::distance(gVS.visited.begin(), gVS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::VS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_PS:
			if (gPS.visited.find(hash) != gPS.visited.end()) {
				gPS.armed = true;
				gPS.selected = static_cast<size_t>(std::distance(gPS.visited.begin(), gPS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::PS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_CS:
			if (gCS.visited.find(hash) != gCS.visited.end()) {
				gCS.armed = true;
				gCS.selected = static_cast<size_t>(std::distance(gCS.visited.begin(), gCS.visited.find(hash)));
				gLastCopyTarget = HuntCopyTarget::CS;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_IB:
			if (hash <= UINT32_MAX && gIB.visited.find(static_cast<uint32_t>(hash)) != gIB.visited.end()) {
				gIB.armed = true;
				gIB.selectedHash = static_cast<uint32_t>(hash);
				SyncIaSelectionPositionLocked(gIB);
				gLastCopyTarget = HuntCopyTarget::IB;
				selected = true;
			}
			break;
		case DX12_HUNT_LIST_VB:
			if (hash <= UINT32_MAX && gVB.visited.find(static_cast<uint32_t>(hash)) != gVB.visited.end()) {
				gVB.armed = true;
				gVB.selectedHash = static_cast<uint32_t>(hash);
				SyncIaSelectionPositionLocked(gVB);
				gLastCopyTarget = HuntCopyTarget::VB;
				selected = true;
			}
			break;
		default:
			break;
		}
	}
	ReleaseSRWLockExclusive(&gHuntLock);

	if (selected)
		DX12HuntRefreshOverlay();
}

static constexpr wchar_t kOverlayWindowClass[] = L"ProjectBunnyDX12ToolWindow";
static constexpr UINT kOverlayClientWidth = 860;
static constexpr UINT kOverlayClientHeight = 620;
static constexpr int kOverlayGap = 8;
static constexpr UINT_PTR kUiRefreshTimer = 1;
static constexpr UINT kUiRefreshMs = 100;
static constexpr DWORD kOverlayStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
// The tool window must never take activation from the game. Several DX12
// titles aggressively throttle or alter their render path while unfocused.
static constexpr DWORD kOverlayExStyle =
	WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
static constexpr COLORREF kWindowBg = RGB(245, 247, 250);
static constexpr COLORREF kPanelBg = RGB(255, 255, 255);
static constexpr COLORREF kTextColor = RGB(26, 31, 39);

enum UiControlId
{
	ID_HUNT_TOGGLE = 1001,
	ID_HUNT_RESET,
	ID_HUNT_COPY,
	ID_CATEGORY_VS,
	ID_CATEGORY_PS,
	ID_CATEGORY_CS,
	ID_CATEGORY_IB,
	ID_CATEGORY_VB,
	ID_HASH_LIST,
	ID_FRAME_ANALYSIS,
	ID_SHADER_DUMP,
	ID_MOD_RELOAD,
	ID_PROFILING,
	ID_STATUS_BAR,
};

static SRWLOCK gUiLock = SRWLOCK_INIT;
static SRWLOCK gThreadLock = SRWLOCK_INIT;
static HWND gGameWindow = nullptr;
static HWND gOverlayWindow = nullptr;
static HWND gStatusLabel = nullptr;
static HWND gHuntToggleButton = nullptr;
static HWND gProfilingButton = nullptr;
static HWND gHashList = nullptr;
static HWND gCategoryButtons[5] = {};
static HWND gMainLabels[4] = {};
static HFONT gUiFont = nullptr;
static HFONT gTitleFont = nullptr;
static HBRUSH gWindowBrush = nullptr;
static HBRUSH gPanelBrush = nullptr;
static std::vector<DX12HuntListItem> gListSnapshot;
static HANDLE gUiThread = nullptr;
static DWORD gUiThreadId = 0;
static volatile LONG gThreadStarted = 0;
static volatile LONG gShutdownRequested = 0;
static volatile LONG gLastGameWindowRefreshPresent = -120;
static bool gClassRegistered = false;
static DX12HuntListCategory gCurrentCategory = DX12_HUNT_LIST_VS;
static bool gUpdatingList = false;

static void SetSharedGameWindow(HWND hwnd)
{
	AcquireSRWLockExclusive(&gUiLock);
	gGameWindow = hwnd;
	ReleaseSRWLockExclusive(&gUiLock);
}

static HWND GetSharedGameWindow()
{
	AcquireSRWLockShared(&gUiLock);
	HWND hwnd = gGameWindow;
	ReleaseSRWLockShared(&gUiLock);
	return hwnd;
}

static void SetSharedOverlayWindow(HWND hwnd)
{
	AcquireSRWLockExclusive(&gUiLock);
	gOverlayWindow = hwnd;
	ReleaseSRWLockExclusive(&gUiLock);
	DX12OverlayStatusState::SetWindow(hwnd);
}

static bool GetGameWindowRect(HWND hwnd, RECT *rect)
{
	if (!hwnd || !rect || !IsWindow(hwnd))
		return false;
	if (!GetWindowRect(hwnd, rect))
		return false;
	return rect->right > rect->left && rect->bottom > rect->top;
}

static bool IsGameWindowDisplayable(HWND hwnd, RECT *rect)
{
	if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
		return false;

	BOOL cloaked = FALSE;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
		return false;

	RECT client = {};
	if (!GetClientRect(hwnd, &client) || client.right <= client.left || client.bottom <= client.top)
		return false;

	return GetGameWindowRect(hwnd, rect);
}

static bool GetOverlayWindowSize(int *width, int *height)
{
	RECT rect = { 0, 0, static_cast<LONG>(kOverlayClientWidth), static_cast<LONG>(kOverlayClientHeight) };
	if (!AdjustWindowRectEx(&rect, kOverlayStyle, FALSE, kOverlayExStyle))
		return false;
	if (width)
		*width = rect.right - rect.left;
	if (height)
		*height = rect.bottom - rect.top;
	return true;
}

static HWND AddControl(HWND parent, const wchar_t *className, const wchar_t *text,
	DWORD style, int id, int x, int y, int width, int height)
{
	HWND control = CreateWindowExW(
		0, className, text, WS_CHILD | WS_VISIBLE | style,
		x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
		DX12RuntimeState::GetModule(), nullptr);
	if (control && gUiFont)
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
	return control;
}

static HWND AddButton(HWND parent, const wchar_t *text, int id, int x, int y, int width = 130)
{
	return AddControl(parent, L"BUTTON", text, BS_PUSHBUTTON, id, x, y, width, 28);
}

static HWND AddLabel(HWND parent, const wchar_t *text, int x, int y, int width, int height = 22)
{
	return AddControl(parent, L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, x, y, width, height);
}

static HWND AddSectionLabel(HWND parent, const wchar_t *text, int x, int y, int width)
{
	HWND label = AddLabel(parent, text, x, y, width, 24);
	if (label && gTitleFont)
		SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(gTitleFont), TRUE);
	return label;
}

static HWND AddListBox(HWND parent, int id, int x, int y, int width, int height)
{
	return AddControl(parent, L"LISTBOX", L"",
		LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_BORDER | WS_VSCROLL,
		id, x, y, width, height);
}

static int CategoryIndex(DX12HuntListCategory category)
{
	return static_cast<int>(category);
}

static void UpdateCategoryButtons()
{
	static const wchar_t *labels[] = { L"VS", L"PS", L"CS", L"IB", L"VB" };
	for (int i = 0; i < ARRAYSIZE(gCategoryButtons); ++i) {
		if (!gCategoryButtons[i])
			continue;
		wchar_t text[16];
		swprintf_s(text, L"%ls%ls", i == CategoryIndex(gCurrentCategory) ? L"> " : L"", labels[i]);
		SetWindowTextW(gCategoryButtons[i], text);
	}
}

static void RefreshHashListSnapshot()
{
	if (!gHashList)
		return;

	const size_t total = DX12HuntGetListItems(gCurrentCategory, nullptr, 0);
	gListSnapshot.assign(total, DX12HuntListItem());
	if (total)
		DX12HuntGetListItems(gCurrentCategory, gListSnapshot.data(), gListSnapshot.size());

	gUpdatingList = true;
	SendMessageW(gHashList, WM_SETREDRAW, FALSE, 0);
	SendMessageW(gHashList, LB_RESETCONTENT, 0, 0);
	int selectedIndex = -1;
	for (size_t i = 0; i < gListSnapshot.size(); ++i) {
		const int row = static_cast<int>(SendMessageW(
			gHashList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(gListSnapshot[i].label)));
		if (row >= 0 && gListSnapshot[i].selected)
			selectedIndex = row;
	}
	if (selectedIndex >= 0)
		SendMessageW(gHashList, LB_SETCURSEL, selectedIndex, 0);
	SendMessageW(gHashList, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(gHashList, nullptr, TRUE);
	gUpdatingList = false;
	UpdateCategoryButtons();
}

static void ClearHashListSelection()
{
	if (gHashList)
		SendMessageW(gHashList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
	for (DX12HuntListItem &item : gListSnapshot)
		item.selected = false;
}

static void UpdateButtonText()
{
	if (gHuntToggleButton)
		SetWindowTextW(gHuntToggleButton, DX12HuntIsEnabled() ? L"Disable Hunting" : L"Enable Hunting");
	if (gProfilingButton)
		SetWindowTextW(gProfilingButton, DX12Profiling::IsActive() ? L"Disable Profiling" : L"Enable Profiling");
}

static void UpdateStatusText()
{
	if (!gStatusLabel)
		return;
	wchar_t status[1024] = {};
	DX12OverlayStatusState::GetStatus(status, ARRAYSIZE(status));
	if (!status[0])
		wcsncpy_s(status, L"3DMigoto DX12 hook alive", _TRUNCATE);
	SendMessageW(gStatusLabel, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(status));
	UpdateButtonText();
	UpdateCategoryButtons();
}

static void FollowGameWindow()
{
	HWND overlay = gOverlayWindow;
	HWND game = GetSharedGameWindow();
	RECT rect = {};
	if (!overlay || !IsGameWindowDisplayable(game, &rect)) {
		if (overlay)
			ShowWindow(overlay, SW_HIDE);
		return;
	}

	int width = 0;
	int height = 0;
	GetOverlayWindowSize(&width, &height);
	SetWindowPos(overlay, HWND_TOPMOST, rect.right + kOverlayGap, rect.top,
		width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void RunCommand(int id)
{
	switch (id) {
	case ID_HUNT_TOGGLE: DX12UiActions::RequestHuntToggle(); RefreshHashListSnapshot(); break;
	case ID_HUNT_RESET: DX12UiActions::RequestHuntResetSelection(); ClearHashListSelection(); break;
	case ID_HUNT_COPY: DX12UiActions::RequestHuntCopySelectedHash(); break;
	case ID_CATEGORY_VS: gCurrentCategory = DX12_HUNT_LIST_VS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_PS: gCurrentCategory = DX12_HUNT_LIST_PS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_CS: gCurrentCategory = DX12_HUNT_LIST_CS; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_IB: gCurrentCategory = DX12_HUNT_LIST_IB; RefreshHashListSnapshot(); break;
	case ID_CATEGORY_VB: gCurrentCategory = DX12_HUNT_LIST_VB; RefreshHashListSnapshot(); break;
	case ID_FRAME_ANALYSIS: DX12UiActions::RequestFrameAnalysis(); break;
	case ID_SHADER_DUMP: DX12UiActions::RequestShaderDump(); break;
	case ID_MOD_RELOAD: DX12UiActions::RequestModReload(); break;
	case ID_PROFILING: DX12UiActions::RequestProfilingToggle(); break;
	default: break;
	}
}

static void CreateUiResources(HWND hwnd)
{
	if (!gUiFont) {
		gUiFont = CreateFontW(
			-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	}
	if (!gTitleFont) {
		gTitleFont = CreateFontW(
			-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	}
	if (!gWindowBrush)
		gWindowBrush = CreateSolidBrush(kWindowBg);
	if (!gPanelBrush)
		gPanelBrush = CreateSolidBrush(kPanelBg);
	SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(gWindowBrush));
}

static HWND AddStatusBar(HWND parent)
{
	HWND status = CreateWindowExW(
		0, STATUSCLASSNAMEW, L"",
		WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
		0, 0, 0, 0, parent,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS_BAR)),
		DX12RuntimeState::GetModule(), nullptr);
	if (status && gUiFont) {
		SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(gUiFont), TRUE);
		SendMessageW(status, SB_SETMINHEIGHT, 24, 0);
		SendMessageW(status, WM_SIZE, 0, 0);
	}
	return status;
}

static void LayoutStatusBar(HWND hwnd)
{
	if (!gStatusLabel)
		return;
	SendMessageW(gStatusLabel, WM_SIZE, 0, 0);
	RECT client = {};
	if (!GetClientRect(hwnd, &client))
		return;
	int parts[] = { client.right - 180, -1 };
	SendMessageW(gStatusLabel, SB_SETPARTS, ARRAYSIZE(parts), reinterpret_cast<LPARAM>(parts));
	SendMessageW(gStatusLabel, SB_SETTEXT, 1, reinterpret_cast<LPARAM>(
		DX12RuntimeState::IsHuntingMode() ? L"Mode: Hunting" : L"Mode: Mod"));
}

static void BuildControls(HWND hwnd)
{
	CreateUiResources(hwnd);

	gStatusLabel = AddStatusBar(hwnd);

	gMainLabels[0] = AddSectionLabel(hwnd, L"Mode", 16, 16, 160);
	gHuntToggleButton = AddButton(hwnd, L"Enable Hunting", ID_HUNT_TOGGLE, 16, 48, 160);
	gProfilingButton = AddButton(hwnd, L"Enable Profiling", ID_PROFILING, 188, 48, 160);

	gMainLabels[1] = AddSectionLabel(hwnd, L"Categories", 16, 100, 160);
	gCategoryButtons[0] = AddButton(hwnd, L"> VS", ID_CATEGORY_VS, 16, 132, 92);
	gCategoryButtons[1] = AddButton(hwnd, L"PS", ID_CATEGORY_PS, 116, 132, 92);
	gCategoryButtons[2] = AddButton(hwnd, L"CS", ID_CATEGORY_CS, 216, 132, 92);
	gCategoryButtons[3] = AddButton(hwnd, L"IB", ID_CATEGORY_IB, 316, 132, 92);
	gCategoryButtons[4] = AddButton(hwnd, L"VB", ID_CATEGORY_VB, 416, 132, 92);

	gMainLabels[2] = AddSectionLabel(hwnd, L"Hashes", 16, 184, 420);
	gHashList = AddListBox(hwnd, ID_HASH_LIST, 16, 216, 560, 308);

	gMainLabels[3] = AddSectionLabel(hwnd, L"Actions", 604, 184, 200);
	AddButton(hwnd, L"Frame Dump", ID_FRAME_ANALYSIS, 604, 216, 180);
	AddButton(hwnd, L"Shader Dump", ID_SHADER_DUMP, 604, 252, 180);
	AddButton(hwnd, L"Reload Mods", ID_MOD_RELOAD, 604, 288, 180);
	AddButton(hwnd, L"Copy Hash", ID_HUNT_COPY, 604, 344, 180);
	AddButton(hwnd, L"Num+ Reset Hunting", ID_HUNT_RESET, 604, 380, 180);

	UpdateStatusText();
	LayoutStatusBar(hwnd);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_CREATE:
		BuildControls(hwnd);
		SetTimer(hwnd, kUiRefreshTimer, kUiRefreshMs, nullptr);
		return 0;
	case WM_TIMER:
		if (wparam == kUiRefreshTimer) {
			UpdateStatusText();
			LayoutStatusBar(hwnd);
			FollowGameWindow();
		}
		return 0;
	case WM_SIZE:
		LayoutStatusBar(hwnd);
		return 0;
	case WM_COMMAND:
		if (LOWORD(wparam) == ID_HASH_LIST && HIWORD(wparam) == LBN_SELCHANGE && !gUpdatingList) {
			const int selected = static_cast<int>(SendMessageW(gHashList, LB_GETCURSEL, 0, 0));
			if (selected >= 0 && static_cast<size_t>(selected) < gListSnapshot.size())
				DX12HuntSelectListHash(gCurrentCategory, gListSnapshot[selected].hash);
		} else if (HIWORD(wparam) == BN_CLICKED) {
			RunCommand(LOWORD(wparam));
		}
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, kUiRefreshTimer);
		SetSharedOverlayWindow(nullptr);
		PostQuitMessage(0);
		return 0;
	case WM_CTLCOLORSTATIC:
		SetTextColor(reinterpret_cast<HDC>(wparam), kTextColor);
		SetBkColor(reinterpret_cast<HDC>(wparam), kWindowBg);
		return reinterpret_cast<LRESULT>(gWindowBrush);
	case WM_CTLCOLORLISTBOX:
		SetTextColor(reinterpret_cast<HDC>(wparam), kTextColor);
		SetBkColor(reinterpret_cast<HDC>(wparam), kPanelBg);
		return reinterpret_cast<LRESULT>(gPanelBrush);
	default:
		return DefWindowProcW(hwnd, msg, wparam, lparam);
	}
}

static bool RegisterOverlayWindowClass()
{
	if (gClassRegistered)
		return true;
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = OverlayWndProc;
	wc.hInstance = DX12RuntimeState::GetModule();
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wc.lpszClassName = kOverlayWindowClass;
	if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;
	gClassRegistered = true;
	return true;
}

static DWORD WINAPI UiThreadProc(void*)
{
	INITCOMMONCONTROLSEX commonControls = {};
	commonControls.dwSize = sizeof(commonControls);
	commonControls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
	InitCommonControlsEx(&commonControls);

	if (!RegisterOverlayWindowClass()) {
		InterlockedExchange(&gThreadStarted, 0);
		return 0;
	}

	int width = 0;
	int height = 0;
	GetOverlayWindowSize(&width, &height);
	HWND hwnd = CreateWindowExW(
		kOverlayExStyle,
		kOverlayWindowClass,
		L"Hunting Window",
		kOverlayStyle,
		CW_USEDEFAULT, CW_USEDEFAULT, width, height,
		nullptr, nullptr, DX12RuntimeState::GetModule(), nullptr);
	if (!hwnd) {
		InterlockedExchange(&gThreadStarted, 0);
		return 0;
	}

	SetSharedOverlayWindow(hwnd);
	DX12LogManager::Fields("DX12NativeOverlay",
		"event=thread_started,hwnd=%p", hwnd);
	FollowGameWindow();

	MSG msg = {};
	while (InterlockedCompareExchange(&gShutdownRequested, 0, 0) == 0 &&
	       GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	if (IsWindow(hwnd))
		DestroyWindow(hwnd);
	DX12LogManager::Fields("DX12NativeOverlay", "event=thread_stopped");
	InterlockedExchange(&gThreadStarted, 0);
	return 0;
}

static void EnsureUiThread()
{
	AcquireSRWLockExclusive(&gThreadLock);
	if (InterlockedCompareExchange(&gThreadStarted, 1, 0) != 0) {
		ReleaseSRWLockExclusive(&gThreadLock);
		return;
	}
	if (gUiThread) {
		WaitForSingleObject(gUiThread, INFINITE);
		CloseHandle(gUiThread);
		gUiThread = nullptr;
		gUiThreadId = 0;
	}
	InterlockedExchange(&gShutdownRequested, 0);
	gUiThread = CreateThread(nullptr, 0, UiThreadProc, nullptr, 0, &gUiThreadId);
	if (!gUiThread) {
		gUiThreadId = 0;
		InterlockedExchange(&gThreadStarted, 0);
		DX12LogManager::Fields("DX12NativeOverlay", "event=thread_create_failed");
	}
	ReleaseSRWLockExclusive(&gThreadLock);
}

bool DX12NativeOverlayRender(IDXGISwapChain *swapChain)
{
	if (!swapChain)
		return false;
	const LONG present = DX12RuntimeState::GetPresentCount();
	HWND gameWindow = GetSharedGameWindow();
	const bool refreshGameWindow =
		!gameWindow ||
		!IsWindow(gameWindow) ||
		present >= InterlockedCompareExchange(&gLastGameWindowRefreshPresent, 0, 0) + 120;
	if (refreshGameWindow) {
		DXGI_SWAP_CHAIN_DESC desc = {};
		if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow && IsWindow(desc.OutputWindow))
			SetSharedGameWindow(desc.OutputWindow);
		InterlockedExchange(&gLastGameWindowRefreshPresent, present);
	}
	if (InterlockedCompareExchange(&gThreadStarted, 0, 0) == 0)
		EnsureUiThread();
	return true;
}

void DX12NativeOverlayInvalidate(IDXGISwapChain*)
{
}

void DX12NativeOverlayShutdown()
{
	AcquireSRWLockExclusive(&gThreadLock);
	InterlockedExchange(&gShutdownRequested, 1);
	if (gUiThreadId)
		PostThreadMessageW(gUiThreadId, WM_QUIT, 0, 0);
	if (gUiThread) {
		WaitForSingleObject(gUiThread, INFINITE);
		CloseHandle(gUiThread);
		gUiThread = nullptr;
	}
	gUiThreadId = 0;
	ReleaseSRWLockExclusive(&gThreadLock);
	SetSharedGameWindow(nullptr);
	SetSharedOverlayWindow(nullptr);
}

bool DX12NativeOverlayIsOverlayWindow(HWND hwnd)
{
	AcquireSRWLockShared(&gUiLock);
	const bool result = hwnd && hwnd == gOverlayWindow;
	ReleaseSRWLockShared(&gUiLock);
	return result;
}
