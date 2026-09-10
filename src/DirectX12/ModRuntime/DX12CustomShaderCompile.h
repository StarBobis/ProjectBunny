#pragma once

// DX12CustomShaderCompile.h - runtime HLSL compilation for [CustomShaderX]
// stage files (phase 2.2).
//
// DX11 compiled mod UI shaders to DXBC with d3dcompiler; D3D12 pipelines only
// accept DXIL, so the same source needs a shader model 6 compiler. Two
// backends are tried in order:
//   1. DXC (dxcompiler.dll + dxil.dll) loaded dynamically, found through the
//      normal DLL search (game directory / PATH) - a self-consistent pair is
//      the most reliable SM6 path;
//   2. d3dcompiler_47 with a vs_6_0/... profile - works where dxil.dll is
//      discoverable (d3dcompiler delegates SM6 to it).
// Compilation stays lazy (first custom draw after (re)load), is cached on
// file path + last-write time and degrades to a logged no-op when no backend
// is available.

#include <Windows.h>
#include <d3dcompiler.h>
#include <dxcapi.h>

#include <Shlwapi.h>

#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Config/DX12Config.h"

namespace Bunny {

// Owns compiled shader bytecode regardless of which compiler produced it
// (d3dcompiler hands out ID3DBlob, DXC hands out IDxcBlob). The PSO creator
// only needs the pointer/size pair; the owner keeps the buffer alive.
class DX12CustomShaderBytecode
{
public:
	const void *data = nullptr;
	size_t size = 0;
	IUnknown *owner = nullptr;

	DX12CustomShaderBytecode() = default;

	DX12CustomShaderBytecode(const DX12CustomShaderBytecode &) = delete;
	DX12CustomShaderBytecode &operator=(const DX12CustomShaderBytecode &) = delete;

	DX12CustomShaderBytecode(DX12CustomShaderBytecode &&other) noexcept
		: data(other.data), size(other.size), owner(other.owner)
	{
		other.data = nullptr;
		other.size = 0;
		other.owner = nullptr;
	}

	DX12CustomShaderBytecode &operator=(DX12CustomShaderBytecode &&other) noexcept
	{
		if (this != &other) {
			Release();
			data = other.data;
			size = other.size;
			owner = other.owner;
			other.data = nullptr;
			other.size = 0;
			other.owner = nullptr;
		}
		return *this;
	}

	~DX12CustomShaderBytecode()
	{
		Release();
	}

	explicit operator bool() const
	{
		return data != nullptr && size != 0;
	}

	// Duplicates the handle for cache storage / callers. Both share the same
	// underlying blob, whose lifetime is the longer of the two owners.
	DX12CustomShaderBytecode Duplicate() const
	{
		DX12CustomShaderBytecode copy;
		if (owner) {
			owner->AddRef();
			copy.owner = owner;
		}
		copy.data = data;
		copy.size = size;
		return copy;
	}

	static DX12CustomShaderBytecode FromD3DCompile(ID3DBlob *blob)
	{
		DX12CustomShaderBytecode bytecode;
		if (blob) {
			bytecode.data = blob->GetBufferPointer();
			bytecode.size = blob->GetBufferSize();
			bytecode.owner = blob; // ID3DBlob is an IUnknown; takes ownership
		}
		return bytecode;
	}

	static DX12CustomShaderBytecode FromDxc(IDxcBlob *blob)
	{
		DX12CustomShaderBytecode bytecode;
		if (blob) {
			bytecode.data = blob->GetBufferPointer();
			bytecode.size = blob->GetBufferSize();
			bytecode.owner = blob; // takes ownership
		}
		return bytecode;
	}

private:
	void Release()
	{
		if (owner) {
			owner->Release();
			owner = nullptr;
		}
		data = nullptr;
		size = 0;
	}
};

class DX12CustomShaderCompile
{
public:
	// Stage indices match DX12CustomShaderElementSpec stage ordering
	// (vs, hs, ds, gs, ps, cs).
	static const char *ProfileForStage(size_t stage)
	{
		static const char *const kProfiles[] = {
			"vs_6_0", "hs_6_0", "ds_6_0", "gs_6_0", "ps_6_0", "cs_6_0"
		};
		return stage < 6 ? kProfiles[stage] : "";
	}

	static const char *DefineForStage(size_t stage)
	{
		static const char *const kDefines[] = {
			"VERTEX_SHADER", "HULL_SHADER", "DOMAIN_SHADER",
			"GEOMETRY_SHADER", "PIXEL_SHADER", "COMPUTE_SHADER"
		};
		return stage < 6 ? kDefines[stage] : "";
	}

	// Resolves the raw stage file value the way DX11 does: absolute paths
	// stay put, relative paths are tried against the ini directory first and
	// then the loader root (config dir / working directory). Empty result
	// means no candidate existed.
	static std::wstring ResolveStageFilePath(
		const std::wstring &rawValue,
		const std::wstring &iniDir,
		const std::wstring &fallbackRoot)
	{
		std::wstring value = IniDocumentText::Trim(rawValue);
		if (value.empty())
			return L"";

		std::vector<std::wstring> candidates;
		if (IsAbsolutePath(value))
			candidates.push_back(value);
		else {
			if (!iniDir.empty())
				candidates.push_back(JoinPath(iniDir, value));
			if (!fallbackRoot.empty())
				candidates.push_back(JoinPath(fallbackRoot, value));
			candidates.push_back(value);
		}
		for (const std::wstring &candidate : candidates) {
			if (PathFileExistsW(candidate.c_str()))
				return candidate;
		}
		return L"";
	}

	// Compiles one stage file. Returns empty bytecode (with errorText set)
	// when no backend could compile it.
	static DX12CustomShaderBytecode CompileStage(
		const std::wstring &absolutePath,
		size_t stage,
		std::wstring *errorText)
	{
		if (absolutePath.empty() || stage >= 6) {
			SetError(errorText, L"empty shader path or invalid stage");
			return DX12CustomShaderBytecode();
		}
		const char *profile = ProfileForStage(stage);
		const char *define = DefineForStage(stage);

		// Cached entry: file time checked so edits hot-reload with the ini.
		{
			std::lock_guard<std::mutex> lock(gCacheMutex);
			CacheEntry *entry = LookupCacheLocked(absolutePath, profile);
			if (entry) {
				if (entry->bytecode && *entry->bytecode)
					return entry->bytecode->Duplicate();
				// Negative entry: this exact source already failed (no
				// backend or compile error). Without this, a mod with a
				// custom element would pay two file reads and two compiler
				// attempts per element per frame forever.
				SetError(errorText,
					L"cached compile failure for " + absolutePath);
				return DX12CustomShaderBytecode();
			}
		}

		std::string source;
		if (!ReadUtf8File(absolutePath, &source)) {
			SetError(errorText, L"unable to read shader file: " + absolutePath);
			std::lock_guard<std::mutex> lock(gCacheMutex);
			StoreCacheLocked(absolutePath, profile, DX12CustomShaderBytecode());
			return DX12CustomShaderBytecode();
		}

		// Backend 1: DXC - a self-consistent dxcompiler/dxil pair is the most
		// reliable SM6 path (d3dcompiler_47 picking up a foreign dxil.dll
		// from the search path is a known crash source).
		DX12CustomShaderBytecode bytecode =
			CompileWithDxc(source, define, profile);

		// Backend 2: d3dcompiler with an SM6 profile (works where dxil.dll
		// is discoverable next to the game/mod binaries).
		if (!bytecode)
			bytecode = CompileWithD3DCompiler(source, define, profile);

		if (!bytecode) {
			SetError(errorText,
				L"no SM6 compiler for " + WideFromAscii(profile) +
				L" (d3dcompiler needs dxil.dll, dxcompiler.dll not found)");
			// Cache the failure: the backend set does not change within a
			// process, and reload clears the cache for shader edits.
			std::lock_guard<std::mutex> lock(gCacheMutex);
			StoreCacheLocked(absolutePath, profile, DX12CustomShaderBytecode());
			return DX12CustomShaderBytecode();
		}

		{
			std::lock_guard<std::mutex> lock(gCacheMutex);
			StoreCacheLocked(absolutePath, profile, bytecode.Duplicate());
		}
		return bytecode;
	}

	// Discards cached bytecode; called on config reload so edited shaders
	// recompile with the new generation.
	static void ClearCache()
	{
		std::lock_guard<std::mutex> lock(gCacheMutex);
		gCache.clear();
	}

private:
	class CacheEntry
	{
	public:
		FILETIME lastWrite = {};
		DX12CustomShaderBytecode *bytecode = nullptr;

		~CacheEntry()
		{
			delete bytecode;
		}

		CacheEntry() = default;
		CacheEntry(const CacheEntry &) = delete;
		CacheEntry &operator=(const CacheEntry &) = delete;
	};

	struct CacheKey
	{
		std::wstring path;
		std::string profile;

		bool operator==(const CacheKey &other) const
		{
			return path == other.path && profile == other.profile;
		}
	};

	struct CacheKeyHash
	{
		size_t operator()(const CacheKey &key) const
		{
			return std::hash<std::wstring>()(key.path) ^
				(std::hash<std::string>()(key.profile) << 1);
		}
	};

	static inline std::mutex gCacheMutex;
	static inline std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> gCache;

	using DxcCreateInstanceFn = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
	static inline std::once_flag sDxcLoadOnce;
	static inline HMODULE sCompilerModule = nullptr;
	static inline HMODULE sDxilModule = nullptr;
	static inline DxcCreateInstanceFn sCreateInstance = nullptr;
	static inline IDxcUtils *sUtils = nullptr;
	static inline IDxcCompiler3 *sCompiler = nullptr;

	static DX12CustomShaderBytecode CompileWithD3DCompiler(
		const std::string &source, const char *define, const char *profile)
	{
		D3D_SHADER_MACRO defines[2] = {};
		defines[0].Name = define;
		defines[0].Definition = "1";
		ID3DBlob *blob = nullptr;
		ID3DBlob *errors = nullptr;
		const HRESULT hr = D3DCompile(
			source.data(), source.size(), nullptr,
			defines, nullptr, "main", profile,
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
		if (errors)
			errors->Release();
		if (FAILED(hr) || !blob)
			return DX12CustomShaderBytecode();
		return DX12CustomShaderBytecode::FromD3DCompile(blob);
	}

	// Minimal COM pointer RAII without WRL (the codebase avoids it).
	template <typename T>
	class DxcComPtr
	{
	public:
		T *p = nullptr;

		~DxcComPtr()
		{
			if (p)
				p->Release();
		}
	};

	static DX12CustomShaderBytecode CompileWithDxc(
		const std::string &source, const char *define, const char *profile)
	{
		// dxcompiler/dxil are loaded once per process and never unloaded:
		// IDxcBlob results outlive the compile call (they are cached and
		// released long after), so releasing them into an unloaded module
		// would crash. Load dxil from the SAME directory as dxcompiler (a
		// mismatched pair crashes), falling back to the plain DLL search.
		if (!EnsureDxcLoaded())
			return DX12CustomShaderBytecode();
		DxcComPtr<IDxcUtils> utils;
		DxcComPtr<IDxcCompiler3> compiler;
		if (FAILED(sCreateInstance(CLSID_DxcUtils, __uuidof(IDxcUtils),
				reinterpret_cast<void **>(&utils.p))) ||
		    FAILED(sCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3),
				reinterpret_cast<void **>(&compiler.p))))
			return DX12CustomShaderBytecode();
		return RunDxcCompiler(utils.p, compiler.p, source, define, profile);
	}

	static bool EnsureDxcLoaded()
	{
		std::call_once(sDxcLoadOnce, []() {
			// Prefer the dxcompiler deployed next to THIS module (the build
			// script drops the official pair beside d3d12.dll): a plain
			// LoadLibrary would never probe our directory, and could instead
			// bind a foreign dxcompiler found ahead of it on the search path.
			sCompilerModule = LoadLibraryW(JoinPath(SelfModuleDirectory(), L"dxcompiler.dll").c_str());
			if (!sCompilerModule)
				sCompilerModule = LoadLibraryW(L"dxcompiler.dll");
			if (!sCompilerModule)
				return;
			sDxilModule = LoadLibraryW(L"dxil.dll");
			wchar_t modulePath[MAX_PATH];
			if (GetModuleFileNameW(sCompilerModule, modulePath, MAX_PATH) &&
			    modulePath[0]) {
				PathRemoveFileSpecW(modulePath);
				const std::wstring coLocated = JoinPath(modulePath, L"dxil.dll");
				if (PathFileExistsW(coLocated.c_str())) {
					if (sDxilModule)
						FreeLibrary(sDxilModule);
					sDxilModule = LoadLibraryW(coLocated.c_str());
				}
			}
			sCreateInstance = reinterpret_cast<DxcCreateInstanceFn>(
				GetProcAddress(sCompilerModule, "DxcCreateInstance"));
			if (sCreateInstance) {
				IDxcUtils *utils = nullptr;
				IDxcCompiler3 *compiler = nullptr;
				if (SUCCEEDED(sCreateInstance(CLSID_DxcUtils, __uuidof(IDxcUtils),
						reinterpret_cast<void **>(&utils)))) {
					sUtils = utils; // leaked on purpose: process-lifetime singletons
				}
				if (SUCCEEDED(sCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3),
						reinterpret_cast<void **>(&compiler)))) {
					sCompiler = compiler;
				}
			}
		});
		return sCompilerModule != nullptr && sCreateInstance != nullptr;
	}

	static DX12CustomShaderBytecode RunDxcCompiler(
		IDxcUtils *utils, IDxcCompiler3 *compiler,
		const std::string &source, const char *define, const char *profile)
	{
		DxcBuffer sourceBuffer = {};
		sourceBuffer.Ptr = source.data();
		sourceBuffer.Size = source.size();
		sourceBuffer.Encoding = DXC_CP_UTF8;

		const std::wstring defineArg = L"-D" + WideFromAscii(define) + L"=1";
		const std::wstring profileArg = WideFromAscii(profile);
		const wchar_t *argv[] = {
			L"-E", L"main",
			L"-T", profileArg.c_str(),
			defineArg.c_str()
		};
		(void)utils; // reserved for include handlers / blob helpers

		DxcComPtr<IDxcResult> result;
		const HRESULT hr = compiler->Compile(
			&sourceBuffer, argv, static_cast<UINT32>(ARRAYSIZE(argv)),
			nullptr, __uuidof(IDxcResult),
			reinterpret_cast<void **>(&result.p));
		if (FAILED(hr) || !result.p)
			return DX12CustomShaderBytecode();

		HRESULT status = E_FAIL;
		if (FAILED(result.p->GetStatus(&status)) || FAILED(status))
			return DX12CustomShaderBytecode();

		DxcComPtr<IDxcBlob> objectBlob;
		if (FAILED(result.p->GetOutput(
				DXC_OUT_OBJECT, __uuidof(IDxcBlob),
				reinterpret_cast<void **>(&objectBlob.p), nullptr)))
			return DX12CustomShaderBytecode();
		// FromDxc takes ownership, so detach from the RAII wrapper first:
		// otherwise the wrapper's destructor would release the same blob a
		// second time and underflow its refcount.
		IDxcBlob *detached = objectBlob.p;
		objectBlob.p = nullptr;
		return DX12CustomShaderBytecode::FromDxc(detached);
	}

	static bool IsAbsolutePath(const std::wstring &path)
	{
		return path.size() >= 2 &&
			(path[1] == L':' || path[0] == L'\\' || path[0] == L'/');
	}

	static std::wstring JoinPath(const std::wstring &base, const std::wstring &path)
	{
		if (path.empty())
			return base;
		if (IsAbsolutePath(path))
			return path;
		wchar_t buffer[MAX_PATH];
		wcsncpy_s(buffer, base.c_str(), _TRUNCATE);
		PathAppendW(buffer, path.c_str());
		return buffer;
	}

	// Directory of the d3d12.dll image this code is compiled into (empty when
	// it cannot be determined). Used to find the DXC pair deployed alongside
	// the mod DLL regardless of the host process's DLL search path.
	static std::wstring SelfModuleDirectory()
	{
		HMODULE selfModule = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SelfModuleDirectory), &selfModule) ||
		    !selfModule)
			return L"";
		wchar_t modulePath[MAX_PATH];
		if (!GetModuleFileNameW(selfModule, modulePath, MAX_PATH) || !modulePath[0])
			return L"";
		PathRemoveFileSpecW(modulePath);
		return modulePath;
	}

	static bool ReadUtf8File(const std::wstring &path, std::string *out)
	{
		if (!out)
			return false;
		out->clear();
		FILE *file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
		if (!file)
			return false;
		fseek(file, 0, SEEK_END);
		const long size = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (size > 0) {
			out->resize(static_cast<size_t>(size));
			const size_t read = fread(&(*out)[0], 1, out->size(), file);
			fclose(file);
			if (read != out->size())
				return false;
		} else {
			fclose(file);
		}
		if (out->size() >= 3 &&
		    static_cast<unsigned char>((*out)[0]) == 0xef &&
		    static_cast<unsigned char>((*out)[1]) == 0xbb &&
		    static_cast<unsigned char>((*out)[2]) == 0xbf) {
			out->erase(0, 3);
		}
		return true;
	}

	static CacheEntry *LookupCacheLocked(
		const std::wstring &path, const char *profile)
	{
		auto found = gCache.find(CacheKey{ path, std::string(profile) });
		if (found == gCache.end())
			return nullptr;
		FILETIME lastWrite = {};
		HANDLE file = CreateFileW(
			path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			GetFileTime(file, nullptr, nullptr, &lastWrite);
			CloseHandle(file);
		}
		if (CompareFileTime(&found->second.lastWrite, &lastWrite) != 0) {
			// Source changed on disk: drop the stale bytecode and recompile.
			gCache.erase(found);
			return nullptr;
		}
		return &found->second;
	}

	static void StoreCacheLocked(
		const std::wstring &path, const char *profile,
		DX12CustomShaderBytecode bytecode)
	{
		FILETIME lastWrite = {};
		HANDLE file = CreateFileW(
			path.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			GetFileTime(file, nullptr, nullptr, &lastWrite);
			CloseHandle(file);
		}
		auto inserted = gCache.try_emplace(CacheKey{ path, std::string(profile) });
		CacheEntry &entry = inserted.first->second;
		entry.lastWrite = lastWrite;
		delete entry.bytecode;
		entry.bytecode = new DX12CustomShaderBytecode(std::move(bytecode));
	}

	static std::wstring WideFromAscii(const char *text)
	{
		if (!text || !*text)
			return L"";
		const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
		if (length <= 0)
			return L"";
		std::wstring wide(static_cast<size_t>(length - 1), L'\0');
		MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], length);
		return wide;
	}

	static void SetError(std::wstring *errorText, const std::wstring &message)
	{
		if (errorText)
			*errorText = message;
	}
};

} // namespace Bunny
