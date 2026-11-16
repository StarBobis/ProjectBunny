#include <d3d11_1.h>
#include <D3D11ShaderTracing.h>
#include <Shlobj.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <mutex>

#include "util.h"
#include "bounded_path_format.h"
#include "d3dcWrapper.h"

#define COMPILER_DLL_VERSION "46"
#define COMPILER_DLL_VERSIONL L"46"

// Standalone DLL needs its own log file reference, as log.h just exports them.
bool gLogDebug = false;
FILE *LogFile = 0;

static bool gInitialized = false;

static bool EXPORT_SHADERS = false;
static wchar_t SHADER_PATH[MAX_PATH] = { 0 };

using namespace std;



void InitializeDLL()
{
	if (!gInitialized)
	{
		gInitialized = true;
		wchar_t dir[MAX_PATH];
		DWORD moduleLength = GetModuleFileName(0, dir, MAX_PATH);
		wchar_t *moduleBase = moduleLength && moduleLength < static_cast<DWORD>(MAX_PATH) ? wcsrchr(dir, L'\\') : NULL;
		if (!moduleBase) {
			OutputDebugStringW(L"[Bunny] D3DCompiler: d3dx.ini path rejected (module path missing or too long)\n");
			dir[0] = 0;
		}
		else
		{
			moduleBase[1] = 0;
			if (!AppendPathBounded(dir, _countof(dir), L"d3dx.ini")) {
				OutputDebugStringW(L"[Bunny] D3DCompiler: d3dx.ini path rejected (too long)\n");
				dir[0] = 0;
			}
		}
		LogFile = GetPrivateProfileInt(L"Logging", L"calls", 0, dir) ? (FILE *)-1 : 0;
		if (LogFile) fopen_s(&LogFile, "D3DCompiler_" COMPILER_DLL_VERSION "_log.txt", "w");

		LogInfo("\nD3DCompiler_" COMPILER_DLL_VERSION " starting init  -  %s\n\n", LogTime().c_str());

		// Unbuffered logging to remove need for fflush calls, and r/w access to make it easy
		// to open active files.
		int unbuffered = -1;
		if (LogFile && GetPrivateProfileInt(L"Logging", L"unbuffered", 0, dir))
		{
			unbuffered = setvbuf(LogFile, NULL, _IONBF, 0);
			LogInfo("  unbuffered=1  return: %d\n", unbuffered);
		}

		// Set the CPU affinity based upon d3dx.ini setting.  Useful for debugging and shader hunting in AC3.
		if (GetPrivateProfileInt(L"Logging", L"force_cpu_affinity", 0, dir))
		{
			DWORD one = 0x01;
			BOOL result = SetProcessAffinityMask(GetCurrentProcess(), one);
			LogInfo("CPU Affinity forced to 1- no multithreading: %s\n", result ? "true" : "false");
		}
		
		wchar_t val[MAX_PATH];
		GetPrivateProfileString(L"Rendering", L"storage_directory", 0, SHADER_PATH, MAX_PATH, dir);
		if (SHADER_PATH[0])
		{
			DWORD storageModuleLength = GetModuleFileName(0, val, MAX_PATH);
			wchar_t *storageBase = storageModuleLength && storageModuleLength < static_cast<DWORD>(MAX_PATH) ? wcsrchr(val, L'\\') : NULL;
			if (!storageBase) {
				LogInfo("D3DCompiler: storage_directory rejected (module path missing or too long)\n");
				SHADER_PATH[0] = 0;
			}
			else
			{
				storageBase[1] = 0;
				// F-200 #10/#11: the composed storage path is all-or-nothing;
				// a truncated one would export into a different directory.
				if (!AppendPathBounded(val, _countof(val), SHADER_PATH) ||
					!CopyPathBounded(SHADER_PATH, _countof(SHADER_PATH), val)) {
					LogInfo("D3DCompiler: storage_directory rejected (path too long)\n");
					SHADER_PATH[0] = 0;
				}
				else
				{
					// Create directory?
					CreateDirectory(SHADER_PATH, 0);
				}
			}
		}
		EXPORT_SHADERS = GetPrivateProfileInt(L"Rendering", L"export_shaders", 0, dir) == 1;
	}

	LogInfo("  DLL initialized.\n");
}

void DestroyDLL()
{
	if (LogFile)
	{
		LogInfo("Destroying DLL...\n");
		fclose(LogFile);
	}
}


static HMODULE hC46 = 0;
static HMODULE hD3D11 = 0;

typedef HRESULT (WINAPI *tD3DCompileFromMemory)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize,
           _In_opt_ LPCSTR pSourceName,
           _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D10_SHADER_MACRO* pDefines,
           _In_opt_ ID3D10Include* pInclude,
           _In_ LPCSTR pEntrypoint,
           _In_ LPCSTR pTarget,
           _In_ UINT Flags1,
           _In_ UINT Flags2,
           _Out_ ID3D10Blob** ppCode,
           _Out_opt_ ID3D10Blob** ppErrorMsgs);
static tD3DCompileFromMemory _D3DCompileFromMemory;
struct SOutputContext
{
	// Unknown.
};
typedef HRESULT (WINAPI *tD3DDisassembleCode)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ UINT Flags,
               _Out_ SOutputContext *context,
               _Out_ ID3D10Blob** ppDisassembly);
static tD3DDisassembleCode _D3DDisassembleCode;
typedef HRESULT (WINAPI *tD3DDisassembleEffect)(_In_ interface ID3D10Effect *pEffect, 
                       _In_ UINT Flags,
                       _Out_ ID3D10Blob** ppDisassembly);
static tD3DDisassembleEffect _D3DDisassembleEffect;
typedef HRESULT (WINAPI *tD3DGetCodeDebugInfo)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                _In_ SIZE_T SrcDataSize,
                _Out_ ID3D10Blob** ppDebugInfo);
static tD3DGetCodeDebugInfo _D3DGetCodeDebugInfo;
typedef HRESULT (WINAPI *tD3DPreprocessFromMemory)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
              _In_ SIZE_T SrcDataSize,
              _In_opt_ LPCSTR pSourceName,
              _In_opt_ CONST D3D10_SHADER_MACRO* pDefines,
              _In_opt_ ID3D10Include* pInclude,
              _Out_ ID3D10Blob** ppCodeText,
              _Out_opt_ ID3D10Blob** ppErrorMsgs);
static tD3DPreprocessFromMemory _D3DPreprocessFromMemory;
typedef HRESULT (WINAPI *tD3DReflectCode)(GUID *interfaceId,
           _In_ INT unknown,
		   _In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize);
static tD3DReflectCode _D3DReflectCode;
typedef HRESULT (WINAPI *tD3DReturnFailure1)(int a, int b, int c);
static tD3DReturnFailure1 _D3DReturnFailure1;
typedef void (WINAPI *tDebugSetMute)();
static tDebugSetMute _DebugSetMute;
typedef HRESULT (WINAPI *tD3DAssemble)(LPCVOID data, SIZE_T datasize, LPCSTR filename,
                           const D3D_SHADER_MACRO *defines, ID3DInclude *include,
                           UINT flags,
                           ID3DBlob **shader, ID3DBlob **error_messages);
static tD3DAssemble _D3DAssemble;
typedef HRESULT (WINAPI *tD3DDisassemble11Trace)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                      _In_ SIZE_T SrcDataSize,
                      _In_ ID3D11ShaderTrace* pTrace,
                      _In_ UINT StartStep,
                      _In_ UINT NumSteps,
                      _In_ UINT Flags,
                      _Out_ interface ID3D10Blob** ppDisassembly);
static tD3DDisassemble11Trace _D3DDisassemble11Trace;
typedef HRESULT (WINAPI *tD3DReadFileToBlob)(_In_ LPCWSTR pFileName,
                  _Out_ ID3DBlob** ppContents);
static tD3DReadFileToBlob _D3DReadFileToBlob;
typedef HRESULT (WINAPI *tD3DWriteBlobToFile)(_In_ ID3DBlob* pBlob,
                   _In_ LPCWSTR pFileName,
                   _In_ BOOL bOverwrite);
static tD3DWriteBlobToFile _D3DWriteBlobToFile;
typedef HRESULT (WINAPI *tD3DCompile)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize,
           _In_opt_ LPCSTR pSourceName,
           _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
           _In_opt_ ID3DInclude* pInclude,
           _In_ LPCSTR pEntrypoint,
           _In_ LPCSTR pTarget,
           _In_ UINT Flags1,
           _In_ UINT Flags2,
           _Out_ ID3DBlob** ppCode,
           _Out_opt_ ID3DBlob** ppErrorMsgs);
static tD3DCompile _D3DCompile;
typedef HRESULT (WINAPI *tD3DCompile2)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
            _In_ SIZE_T SrcDataSize,
            _In_opt_ LPCSTR pSourceName,
            _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
            _In_opt_ ID3DInclude* pInclude,
            _In_ LPCSTR pEntrypoint,
            _In_ LPCSTR pTarget,
            _In_ UINT Flags1,
            _In_ UINT Flags2,
            _In_ UINT SecondaryDataFlags,
            _In_reads_bytes_opt_(SecondaryDataSize) LPCVOID pSecondaryData,
            _In_ SIZE_T SecondaryDataSize,
            _Out_ ID3DBlob** ppCode,
            _Out_opt_ ID3DBlob** ppErrorMsgs);
static tD3DCompile2 _D3DCompile2;
typedef HRESULT (WINAPI *tD3DCompileFromFile)(_In_ LPCWSTR pFileName,
                   _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
                   _In_opt_ ID3DInclude* pInclude,
                   _In_ LPCSTR pEntrypoint,
                   _In_ LPCSTR pTarget,
                   _In_ UINT Flags1,
                   _In_ UINT Flags2,
                   _Out_ ID3DBlob** ppCode,
                   _Out_opt_ ID3DBlob** ppErrorMsgs);
static tD3DCompileFromFile _D3DCompileFromFile;
typedef HRESULT (WINAPI *tD3DPreprocess)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
              _In_ SIZE_T SrcDataSize,
              _In_opt_ LPCSTR pSourceName,
              _In_opt_ CONST D3D_SHADER_MACRO* pDefines,
              _In_opt_ ID3DInclude* pInclude,
              _Out_ ID3DBlob** ppCodeText,
              _Out_opt_ ID3DBlob** ppErrorMsgs);
static tD3DPreprocess _D3DPreprocess;
typedef HRESULT (WINAPI *tD3DGetDebugInfo)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                _In_ SIZE_T SrcDataSize,
                _Out_ ID3DBlob** ppDebugInfo);
static tD3DGetDebugInfo _D3DGetDebugInfo;
typedef HRESULT (WINAPI *tD3DReflect)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize,
	   _In_ REFIID pInterface,
           _Out_ void** ppReflector);
static tD3DReflect _D3DReflect;
typedef HRESULT (WINAPI *tD3DDisassemble)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ UINT Flags,
               _In_opt_ LPCSTR szComments,
               _Out_ ID3DBlob** ppDisassembly);
static tD3DDisassemble _D3DDisassemble;
typedef HRESULT (WINAPI *tD3DDisassembleRegion)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                     _In_ SIZE_T SrcDataSize,
                     _In_ UINT Flags,
                     _In_opt_ LPCSTR szComments,
                     _In_ SIZE_T StartByteOffset,
                     _In_ SIZE_T NumInsts,
                     _Out_opt_ SIZE_T* pFinishByteOffset,
                     _Out_ ID3DBlob** ppDisassembly);
static tD3DDisassembleRegion _D3DDisassembleRegion;
typedef HRESULT (WINAPI *tD3DDisassemble10Effect)(_In_ interface ID3D10Effect *pEffect, 
                       _In_ UINT Flags,
                       _Out_ ID3DBlob** ppDisassembly);
static tD3DDisassemble10Effect _D3DDisassemble10Effect;
typedef HRESULT (WINAPI *tD3DGetTraceInstructionOffsets)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                              _In_ SIZE_T SrcDataSize,
                              _In_ UINT Flags,
                              _In_ SIZE_T StartInstIndex,
                              _In_ SIZE_T NumInsts,
                              _Out_writes_to_opt_(NumInsts, min(NumInsts, *pTotalInsts)) SIZE_T* pOffsets,
                              _Out_opt_ SIZE_T* pTotalInsts);
static tD3DGetTraceInstructionOffsets _D3DGetTraceInstructionOffsets;
typedef HRESULT (WINAPI *tD3DGetInputSignatureBlob)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                         _In_ SIZE_T SrcDataSize,
                         _Out_ ID3DBlob** ppSignatureBlob);
static tD3DGetInputSignatureBlob _D3DGetInputSignatureBlob;
typedef HRESULT (WINAPI *tD3DGetOutputSignatureBlob)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                          _In_ SIZE_T SrcDataSize,
                          _Out_ ID3DBlob** ppSignatureBlob);
static tD3DGetOutputSignatureBlob _D3DGetOutputSignatureBlob;
typedef HRESULT (WINAPI *tD3DGetInputAndOutputSignatureBlob)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                                  _In_ SIZE_T SrcDataSize,
                                  _Out_ ID3DBlob** ppSignatureBlob);
static tD3DGetInputAndOutputSignatureBlob _D3DGetInputAndOutputSignatureBlob;
typedef HRESULT (WINAPI *tD3DStripShader)(_In_reads_bytes_(BytecodeLength) LPCVOID pShaderBytecode,
               _In_ SIZE_T BytecodeLength,
               _In_ UINT uStripFlags,
               _Out_ ID3DBlob** ppStrippedBlob);
static tD3DStripShader _D3DStripShader;
typedef HRESULT (WINAPI *tD3DGetBlobPart)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ D3D_BLOB_PART Part,
               _In_ UINT Flags,
               _Out_ ID3DBlob** ppPart);
static tD3DGetBlobPart _D3DGetBlobPart;
typedef HRESULT (WINAPI *tD3DSetBlobPart)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ D3D_BLOB_PART Part,
               _In_ UINT Flags,
	       _In_reads_bytes_(PartSize) LPCVOID pPart,
               _In_ SIZE_T PartSize,
               _Out_ ID3DBlob** ppNewShader);
static tD3DSetBlobPart _D3DSetBlobPart;
typedef HRESULT (WINAPI *tD3DCompressShaders)(_In_ UINT uNumShaders,
                   _In_reads_(uNumShaders) D3D_SHADER_DATA* pShaderData,
                   _In_ UINT uFlags,
                   _Out_ ID3DBlob** ppCompressedData);
static tD3DCompressShaders _D3DCompressShaders;
typedef HRESULT (WINAPI *tD3DDecompressShaders)(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                     _In_ SIZE_T SrcDataSize,
                     _In_ UINT uNumShaders,	      
                     _In_ UINT uStartIndex,
                     _In_reads_opt_(uNumShaders) UINT* pIndices,
                     _In_ UINT uFlags,
                     _Out_writes_(uNumShaders) ID3DBlob** ppShaders,
		     _Out_opt_ UINT* pTotalShaders);
static tD3DDecompressShaders _D3DDecompressShaders;
typedef HRESULT (WINAPI *tD3DCreateBlob)(_In_ SIZE_T Size,
              _Out_ ID3DBlob** ppBlob);
static tD3DCreateBlob _D3DCreateBlob;

// DirectX 11 bridge.
typedef int (WINAPI *tD3D11CoreGetLayeredDeviceSize)(int a, int b);
static tD3D11CoreGetLayeredDeviceSize _D3D11CoreGetLayeredDeviceSize;
struct D3D11BridgeData
{
	UINT64 BinaryHash;
	char *HLSLFileName;
};

static HRESULT CompilerUnavailable()
{
	return HRESULT_FROM_WIN32(hC46 ? ERROR_PROC_NOT_FOUND : ERROR_MOD_NOT_FOUND);
}

template <typename T>
static void ResolveC46(T &target, const char *name)
{
	target = reinterpret_cast<T>(GetProcAddress(hC46, name));
	if (!target)
		LogInfo("  D3DCompiler provider is missing export %s\n", name);
}

static void InitC46()
{
	static std::once_flag once;
	std::call_once(once, [] {
		InitializeDLL();

		wchar_t exePath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		wchar_t *slash = (length > 0 && length < MAX_PATH) ? wcsrchr(exePath, L'\\') : nullptr;
		if (!slash)
		{
			LogInfo("InitC46: cannot locate executable directory "
				"(GetModuleFileName=%lu, GetLastError=%lu); "
				"compiler provider initialization is latched for this process\n",
				length, GetLastError());
			return;
		}
		slash[1] = 0;

		wchar_t providerPath[MAX_PATH] = {};
		if (wcscpy_s(providerPath, _countof(providerPath), exePath) != 0 ||
			wcscat_s(providerPath, _countof(providerPath),
				L"D3DCompiler_" COMPILER_DLL_VERSIONL L"_org.dll") != 0)
		{
			LogInfo("InitC46: provider path is too long (exe dir length=%Iu, GetLastError=%lu); "
				"compiler provider initialization is latched for this process\n",
				wcslen(exePath), GetLastError());
			return;
		}

		char providerPathUtf8[MAX_PATH * 4] = {};
		const int providerPathLength = WideCharToMultiByte(CP_UTF8, 0, providerPath, -1,
			providerPathUtf8, static_cast<int>(sizeof(providerPathUtf8)), nullptr, nullptr);
		if (providerPathLength > 0)
		{
			for (char *p = providerPathUtf8; *p; ++p)
			{
				if (static_cast<unsigned char>(*p) >= 0x80)
					*p = '?';
			}
		}
		const char *providerPathForLog = providerPathLength > 0 ? providerPathUtf8 : "<unencodable provider path>";
		hC46 = LoadLibraryW(providerPath);
		if (!hC46)
		{
			const DWORD error = GetLastError();
			LogInfo("LoadLibrary on %s failed (path_chars=%Iu, GetLastError=%lu); "
				"compiler provider initialization is latched for this process [f107-latch-v2]\n",
				providerPathForLog, wcslen(providerPath), error);
			return;
		}
		LogInfo("Loaded compiler provider: %s\n", providerPathForLog);

		wchar_t d3d11Path[MAX_PATH] = {};
		if (wcscpy_s(d3d11Path, _countof(d3d11Path), exePath) != 0 ||
			wcscat_s(d3d11Path, _countof(d3d11Path), L"d3d11.dll") != 0)
		{
			LogInfo("InitC46: d3d11 path is too long (exe dir length=%Iu)\n", wcslen(exePath));
		}
		else
		{
			hD3D11 = LoadLibraryW(d3d11Path);
			if (!hD3D11)
				LogInfo("LoadLibrary on d3d11.dll wrapper failed (GetLastError=%lu)\n", GetLastError());
		}

		// d3d11 bridge
		_D3D11CoreGetLayeredDeviceSize = (tD3D11CoreGetLayeredDeviceSize) GetProcAddress(hD3D11, "D3D11CoreGetLayeredDeviceSize");
		// Version 39 (not exported; a missing name only affects dead code)
		_D3DCompileFromMemory = (tD3DCompileFromMemory) GetProcAddress(hC46, "D3DCompileFromMemory");
		_D3DDisassembleCode = (tD3DDisassembleCode) GetProcAddress(hC46, "D3DDisassembleCode");
		_D3DDisassembleEffect = (tD3DDisassembleEffect) GetProcAddress(hC46, "D3DDisassembleEffect");
		_D3DGetCodeDebugInfo = (tD3DGetCodeDebugInfo) GetProcAddress(hC46, "D3DGetCodeDebugInfo");
		_D3DPreprocessFromMemory = (tD3DPreprocessFromMemory) GetProcAddress(hC46, "D3DPreprocessFromMemory");
		_D3DReflectCode = (tD3DReflectCode) GetProcAddress(hC46, "D3DReflectCode");
		// Version 46 (public exports; every one is guarded by its caller)
		ResolveC46(_D3DAssemble, "D3DAssemble");
		ResolveC46(_D3DCompile, "D3DCompile");
		ResolveC46(_D3DCompile2, "D3DCompile2");
		ResolveC46(_D3DCompileFromFile, "D3DCompileFromFile");
		ResolveC46(_D3DCompressShaders, "D3DCompressShaders");
		ResolveC46(_D3DCreateBlob, "D3DCreateBlob");
		ResolveC46(_D3DDecompressShaders, "D3DDecompressShaders");
		ResolveC46(_D3DDisassemble, "D3DDisassemble");
		ResolveC46(_D3DDisassemble10Effect, "D3DDisassemble10Effect");
		ResolveC46(_D3DDisassemble11Trace, "D3DDisassemble11Trace");
		ResolveC46(_D3DDisassembleRegion, "D3DDisassembleRegion");
		ResolveC46(_D3DGetBlobPart, "D3DGetBlobPart");
		ResolveC46(_D3DGetDebugInfo, "D3DGetDebugInfo");
		ResolveC46(_D3DGetInputAndOutputSignatureBlob, "D3DGetInputAndOutputSignatureBlob");
		ResolveC46(_D3DGetInputSignatureBlob, "D3DGetInputSignatureBlob");
		ResolveC46(_D3DGetOutputSignatureBlob, "D3DGetOutputSignatureBlob");
		ResolveC46(_D3DGetTraceInstructionOffsets, "D3DGetTraceInstructionOffsets");
		ResolveC46(_D3DPreprocess, "D3DPreprocess");
		ResolveC46(_D3DReadFileToBlob, "D3DReadFileToBlob");
		ResolveC46(_D3DReflect, "D3DReflect");
		ResolveC46(_D3DReturnFailure1, "D3DReturnFailure1");
		ResolveC46(_D3DSetBlobPart, "D3DSetBlobPart");
		ResolveC46(_D3DStripShader, "D3DStripShader");
		ResolveC46(_D3DWriteBlobToFile, "D3DWriteBlobToFile");
		ResolveC46(_DebugSetMute, "DebugSetMute");
	});
}

HRESULT WINAPI D3DReturnFailure1(int a, int b, int c)
{
	InitC46();
	if (!_D3DReturnFailure1)
		return CompilerUnavailable();
	return (*_D3DReturnFailure1)(a, b, c);
}
extern "C" void WINAPI DebugSetMute()
{
	InitC46();
	if (!_DebugSetMute)
		return;
	(*_DebugSetMute)();
}
HRESULT WINAPI D3DAssemble(LPCVOID data, SIZE_T datasize, LPCSTR filename,
                           const D3D_SHADER_MACRO *defines, ID3DInclude *include,
                           UINT flags,
                           ID3DBlob **shader, ID3DBlob **error_messages)
{
	InitC46();
	if (!_D3DAssemble)
	{
		if (shader) *shader = nullptr;
		if (error_messages) *error_messages = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DAssemble called\n"); 
	
	return (*_D3DAssemble)(data, datasize, filename, defines, include, flags, shader, error_messages);
}
HRESULT WINAPI D3DDisassemble11Trace(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                      _In_ SIZE_T SrcDataSize,
                      _In_ ID3D11ShaderTrace* pTrace,
                      _In_ UINT StartStep,
                      _In_ UINT NumSteps,
                      _In_ UINT Flags,
                      _Out_ interface ID3D10Blob** ppDisassembly)
{
	InitC46();
	if (!_D3DDisassemble11Trace)
	{
		if (ppDisassembly) *ppDisassembly = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DDisassemble11Trace called\n"); 
	
	return (*_D3DDisassemble11Trace)(pSrcData, SrcDataSize, pTrace, StartStep, NumSteps, Flags, ppDisassembly);
}
HRESULT WINAPI D3DReadFileToBlob(_In_ LPCWSTR pFileName,
                  _Out_ ID3DBlob** ppContents)
{
	InitC46();
	if (!_D3DReadFileToBlob)
	{
		if (ppContents) *ppContents = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DReadFileToBlob called\n"); 
	
	return (*_D3DReadFileToBlob)(pFileName, ppContents);
}
HRESULT WINAPI D3DWriteBlobToFile(_In_ ID3DBlob* pBlob,
                   _In_ LPCWSTR pFileName,
                   _In_ BOOL bOverwrite)
{
	InitC46();
	if (!_D3DWriteBlobToFile)
		return CompilerUnavailable();
	LogInfo("D3DWriteBlobToFile called\n"); 
	
	return (*_D3DWriteBlobToFile)(pBlob, pFileName, bOverwrite);
}
HRESULT WINAPI D3DCompile(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize,
           _In_opt_ LPCSTR pSourceName,
           _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
           _In_opt_ ID3DInclude* pInclude,
           _In_ LPCSTR pEntrypoint,
           _In_ LPCSTR pTarget,
           _In_ UINT Flags1,
           _In_ UINT Flags2,
           _Out_ ID3DBlob** ppCode,
           _Out_opt_ ID3DBlob** ppErrorMsgs)
{
	InitC46();
	if (!_D3DCompile)
	{
		if (ppCode) *ppCode = nullptr;
		if (ppErrorMsgs) *ppErrorMsgs = nullptr;
		return CompilerUnavailable();
	}
	bool wrapperCall = (pSourceName && !strcmp(pSourceName, "wrapper1349"));
	if (LogFile && !wrapperCall) 
	{
		LogInfo("D3DCompile called with\n"); 
		LogInfo("  SourceName = %s\n", pSourceName);
		LogInfo("  Entrypoint = %s\n", pEntrypoint);
		LogInfo("  Target = %s\n", pTarget);
		LogInfo("  ppErrorMsgs = %p\n", ppErrorMsgs);
		
	}

	// Compile original shader.
	ID3DBlob *errorBlob = 0;
	if (!ppErrorMsgs)
	{
		ppErrorMsgs = &errorBlob;
	}
	HRESULT ret = (*_D3DCompile)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, pEntrypoint,
		pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
	UINT64 binaryHash;
	char shaderName[MAX_PATH];
	const bool needsShaderName = SHADER_PATH[0] != 0 || LogFile != nullptr;
	if (ret == S_OK && ppCode && *ppCode && !wrapperCall && needsShaderName)
	{
		// Calculate CRC and name.
		UINT64 sourceHash = fnv_64_buf(pSrcData, SrcDataSize);
		binaryHash = fnv_64_buf((*ppCode)->GetBufferPointer(), (*ppCode)->GetBufferSize());
		sprintf(shaderName, "%08lx%08lx-%s_%08lx%08lx.txt", (UINT32)(binaryHash >> 32), (UINT32)binaryHash, pTarget, (UINT32)(sourceHash >> 32), (UINT32)sourceHash);
		LogInfo("    Filename = %s\n", shaderName);
		LogInfo("    Compiled bytecode size = %d, bytecode handle = %p\n", (*ppCode)->GetBufferSize(), (*ppCode)->GetBufferPointer());
		
		// F-200: every optional name below is either complete or the operation
		// is skipped. A truncated path is never handed to FindFirstFile or
		// _wfopen_s, and the native compile result is left untouched.
		if (SHADER_PATH[0])
		{
			wchar_t target[MAX_PATH];
			size_t targetLength = 0;
			if (!pTarget || mbstowcs_s(&targetLength, target, _countof(target), pTarget, _TRUNCATE) != 0)
			{
				LogInfo("D3DCompiler: target name too long, optional shader files rejected\n");
			}
			else if (EXPORT_SHADERS)
			{
				wchar_t val[MAX_PATH];
				if (!FormatPathBounded(val, _countof(val), L"%ls\\%08lx%08lx-%ls_%08lx%08lx.txt", SHADER_PATH,
					(UINT32)(binaryHash >> 32), (UINT32)binaryHash, target, (UINT32)(sourceHash >> 32), (UINT32)sourceHash))
				{
					LogInfo("D3DCompiler: 路径过长被拒 (export path too long; storage_directory path_len=%Iu, max_path=%d)\n",
						wcslen(SHADER_PATH), MAX_PATH);
				}
				else
				{
					FILE *f;
					_wfopen_s(&f, val, L"wb");
					if (f)
					{
						fwrite(pSrcData, 1, SrcDataSize, f);
						fclose(f);
					}
				}
			}
			else
			{
				wchar_t val[MAX_PATH];
				if (!FormatPathBounded(val, _countof(val), L"%ls\\%08lx%08lx-%ls_%08lx%08lx*replace.txt", SHADER_PATH,
					(UINT32)(binaryHash >> 32), (UINT32)binaryHash, target, (UINT32)(sourceHash >> 32), (UINT32)sourceHash))
				{
					LogInfo("D3DCompiler: 路径过长被拒 (replacement search path too long; storage_directory path_len=%Iu, max_path=%d)\n",
						wcslen(SHADER_PATH), MAX_PATH);
				}
				else
				{
					WIN32_FIND_DATA findFileData;
					HANDLE hFind = FindFirstFile(val, &findFileData);
					if (hFind != INVALID_HANDLE_VALUE)
					{
						FindClose(hFind);
						if (!FormatPathBounded(val, _countof(val), L"%ls\\%ls", SHADER_PATH, findFileData.cFileName))
						{
							LogInfo("D3DCompiler: 路径过长被拒 (replacement file path too long; storage_directory path_len=%Iu, max_path=%d)\n",
								wcslen(SHADER_PATH), MAX_PATH);
						}
						else
						{
							FILE *f;
							_wfopen_s(&f, val, L"rb");
							if (LogFile)
							{
								char path[MAX_PATH];
								wcstombs(path, val, MAX_PATH);
								LogInfo("    Replacement shader found. Loading replacement HLSL code from file \"%s\".\n", path);
								
							}
							if (f)
							{
								fseek(f, 0, SEEK_END);
								SrcDataSize = ftell(f);
								fseek(f, 0, SEEK_SET);
								pSrcData = new char[SrcDataSize];
								fread((void *) pSrcData, 1, SrcDataSize, f);
								fclose(f);
								LogInfo("    Source code loaded. Size = %d\n", SrcDataSize);
								

								// Compile replacement.
								if (*ppErrorMsgs) (*ppErrorMsgs)->Release();
								if (*ppCode) (*ppCode)->Release();
								ret = (*_D3DCompile)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, pEntrypoint,
									pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
								delete[] static_cast<const char*>(pSrcData);
							}
						}
					}
				}
			}
		}
	}
	if (LogFile && !wrapperCall)
	{
		LogInfo("  Result = %x\n", ret);
		if (*ppErrorMsgs)
		{
			LPVOID errMsg = (*ppErrorMsgs)->GetBufferPointer();
			SIZE_T errSize = (*ppErrorMsgs)->GetBufferSize();
			LogInfo("  Compile errors:\n");
			LogInfo("--------------------------------------------- BEGIN ---------------------------------------------\n");
			fwrite(errMsg, 1, errSize, LogFile);
			LogInfo("\n---------------------------------------------- END ----------------------------------------------\n");
		}
	}
	if (errorBlob) errorBlob->Release();

	// ToDo: No longer connected to d3d11 here
	// Send to DirectX 
	//if (ret == S_OK && _D3D11CoreGetLayeredDeviceSize && !wrapperCall)
	//{
	//	D3D11BridgeData data;
	//	data.BinaryHash = binaryHash;
	//	data.HLSLFileName = shaderName;
	//	int success = (*_D3D11CoreGetLayeredDeviceSize)(0x77aa128b, (int) &data);
	//	if (LogFile && success != 0xaa77125b)
	//	{
	//		LogInfo("    sending code hash to d3d11.dll wrapper failed\n");
	//	}
	//}
	return ret;
}
HRESULT WINAPI D3DCompile2(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
            _In_ SIZE_T SrcDataSize,
            _In_opt_ LPCSTR pSourceName,
            _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
            _In_opt_ ID3DInclude* pInclude,
            _In_ LPCSTR pEntrypoint,
            _In_ LPCSTR pTarget,
            _In_ UINT Flags1,
            _In_ UINT Flags2,
            _In_ UINT SecondaryDataFlags,
            _In_reads_bytes_opt_(SecondaryDataSize) LPCVOID pSecondaryData,
            _In_ SIZE_T SecondaryDataSize,
            _Out_ ID3DBlob** ppCode,
            _Out_opt_ ID3DBlob** ppErrorMsgs)
{
	InitC46();
	if (!_D3DCompile2)
	{
		if (ppCode) *ppCode = nullptr;
		if (ppErrorMsgs) *ppErrorMsgs = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DCompile2 called\n"); 
	
	return (*_D3DCompile2)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, pEntrypoint,
		pTarget, Flags1, Flags2, SecondaryDataFlags, pSecondaryData, SecondaryDataSize, ppCode, ppErrorMsgs);
}
HRESULT WINAPI D3DCompileFromFile(_In_ LPCWSTR pFileName,
                   _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
                   _In_opt_ ID3DInclude* pInclude,
                   _In_ LPCSTR pEntrypoint,
                   _In_ LPCSTR pTarget,
                   _In_ UINT Flags1,
                   _In_ UINT Flags2,
                   _Out_ ID3DBlob** ppCode,
                   _Out_opt_ ID3DBlob** ppErrorMsgs)
{
	InitC46();
	if (!_D3DCompileFromFile)
	{
		if (ppCode) *ppCode = nullptr;
		if (ppErrorMsgs) *ppErrorMsgs = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DCompileFromFile called\n"); 
	
	return (*_D3DCompileFromFile)(pFileName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2,
		ppCode, ppErrorMsgs);
}
HRESULT WINAPI D3DPreprocess(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
              _In_ SIZE_T SrcDataSize,
              _In_opt_ LPCSTR pSourceName,
              _In_opt_ CONST D3D_SHADER_MACRO* pDefines,
              _In_opt_ ID3DInclude* pInclude,
              _Out_ ID3DBlob** ppCodeText,
              _Out_opt_ ID3DBlob** ppErrorMsgs)
{
	InitC46();
	if (!_D3DPreprocess)
	{
		if (ppCodeText) *ppCodeText = nullptr;
		if (ppErrorMsgs) *ppErrorMsgs = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DPreprocess called\n"); 
	
	return (*_D3DPreprocess)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, ppCodeText, ppErrorMsgs);
}
HRESULT WINAPI D3DGetDebugInfo(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                _In_ SIZE_T SrcDataSize,
                _Out_ ID3DBlob** ppDebugInfo)
{
	InitC46();
	if (!_D3DGetDebugInfo)
	{
		if (ppDebugInfo) *ppDebugInfo = nullptr;
		return CompilerUnavailable();
	}
	return (*_D3DGetDebugInfo)(pSrcData, SrcDataSize, ppDebugInfo);
}
HRESULT WINAPI D3DReflect(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize,
	   _In_ REFIID pInterface,
           _Out_ void** ppReflector)
{
	InitC46();
	if (!_D3DReflect)
	{
		if (ppReflector) *ppReflector = nullptr;
		return CompilerUnavailable();
	}
	//LogInfo("D3DReflect called\n"); 
	//
	return (*_D3DReflect)(pSrcData, SrcDataSize, pInterface, ppReflector);
}
HRESULT WINAPI D3DDisassemble(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ UINT Flags,
               _In_opt_ LPCSTR szComments,
               _Out_ ID3DBlob** ppDisassembly)
{
	InitC46();
	if (!_D3DDisassemble)
	{
		if (ppDisassembly) *ppDisassembly = nullptr;
		return CompilerUnavailable();
	}
	//LogInfo("D3DDisassemble called\n"); 
	//
	return (*_D3DDisassemble)(pSrcData, SrcDataSize, Flags, szComments, ppDisassembly);
}
HRESULT WINAPI D3DDisassembleRegion(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                     _In_ SIZE_T SrcDataSize,
                     _In_ UINT Flags,
                     _In_opt_ LPCSTR szComments,
                     _In_ SIZE_T StartByteOffset,
                     _In_ SIZE_T NumInsts,
                     _Out_opt_ SIZE_T* pFinishByteOffset,
                     _Out_ ID3DBlob** ppDisassembly)
{
	InitC46();
	if (!_D3DDisassembleRegion)
	{
		if (ppDisassembly) *ppDisassembly = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DDisassembleRegion called\n"); 
	
	return (*_D3DDisassembleRegion)(pSrcData, SrcDataSize, Flags, szComments, StartByteOffset,
		NumInsts, pFinishByteOffset, ppDisassembly);
}
HRESULT WINAPI D3DDisassemble10Effect(_In_ interface ID3D10Effect *pEffect, 
                       _In_ UINT Flags,
                       _Out_ ID3DBlob** ppDisassembly)
{
	InitC46();
	if (!_D3DDisassemble10Effect)
	{
		if (ppDisassembly) *ppDisassembly = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DDisassemble10Effect called\n"); 
	
	return (*_D3DDisassemble10Effect)(pEffect, Flags, ppDisassembly);
}
HRESULT WINAPI D3DGetTraceInstructionOffsets(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                              _In_ SIZE_T SrcDataSize,
                              _In_ UINT Flags,
                              _In_ SIZE_T StartInstIndex,
                              _In_ SIZE_T NumInsts,
                              _Out_writes_to_opt_(NumInsts, min(NumInsts, *pTotalInsts)) SIZE_T* pOffsets,
                              _Out_opt_ SIZE_T* pTotalInsts)
{
	InitC46();
	if (!_D3DGetTraceInstructionOffsets)
	{
		if (pTotalInsts) *pTotalInsts = 0;
		return CompilerUnavailable();
	}
	LogInfo("D3DGetTraceInstructionOffsets called\n"); 
	
	return (*_D3DGetTraceInstructionOffsets)(pSrcData, SrcDataSize, Flags, StartInstIndex, NumInsts,
		pOffsets, pTotalInsts);
}
HRESULT WINAPI D3DGetInputSignatureBlob(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                         _In_ SIZE_T SrcDataSize,
                         _Out_ ID3DBlob** ppSignatureBlob)
{
	InitC46();
	if (!_D3DGetInputSignatureBlob)
	{
		if (ppSignatureBlob) *ppSignatureBlob = nullptr;
		return CompilerUnavailable();
	}
	//LogInfo("D3DGetInputSignatureBlob called\n"); 
	//
	return (*_D3DGetInputSignatureBlob)(pSrcData, SrcDataSize, ppSignatureBlob);
}
HRESULT WINAPI D3DGetOutputSignatureBlob(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                          _In_ SIZE_T SrcDataSize,
                          _Out_ ID3DBlob** ppSignatureBlob)
{
	InitC46();
	if (!_D3DGetOutputSignatureBlob)
	{
		if (ppSignatureBlob) *ppSignatureBlob = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DGetOutputSignatureBlob called\n"); 
	
	return (*_D3DGetOutputSignatureBlob)(pSrcData, SrcDataSize, ppSignatureBlob);
}
HRESULT WINAPI D3DGetInputAndOutputSignatureBlob(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                                  _In_ SIZE_T SrcDataSize,
                                  _Out_ ID3DBlob** ppSignatureBlob)
{
	InitC46();
	if (!_D3DGetInputAndOutputSignatureBlob)
	{
		if (ppSignatureBlob) *ppSignatureBlob = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DGetInputAndOutputSignatureBlob called\n"); 
	
	return (*_D3DGetInputAndOutputSignatureBlob)(pSrcData, SrcDataSize, ppSignatureBlob);
}
HRESULT WINAPI D3DStripShader(_In_reads_bytes_(BytecodeLength) LPCVOID pShaderBytecode,
               _In_ SIZE_T BytecodeLength,
               _In_ UINT uStripFlags,
               _Out_ ID3DBlob** ppStrippedBlob)
{
	InitC46();
	if (!_D3DStripShader)
	{
		if (ppStrippedBlob) *ppStrippedBlob = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DStripShader called\n"); 
	
	return (*_D3DStripShader)(pShaderBytecode, BytecodeLength, uStripFlags, ppStrippedBlob);
}
HRESULT WINAPI D3DGetBlobPart(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ D3D_BLOB_PART Part,
               _In_ UINT Flags,
               _Out_ ID3DBlob** ppPart)
{
	InitC46();
	if (!_D3DGetBlobPart)
	{
		if (ppPart) *ppPart = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DGetBlobPart called\n"); 
	
	return (*_D3DGetBlobPart)(pSrcData, SrcDataSize, Part, Flags, ppPart);
}
HRESULT WINAPI D3DSetBlobPart(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ D3D_BLOB_PART Part,
               _In_ UINT Flags,
	       _In_reads_bytes_(PartSize) LPCVOID pPart,
               _In_ SIZE_T PartSize,
               _Out_ ID3DBlob** ppNewShader)
{
	InitC46();
	if (!_D3DSetBlobPart)
	{
		if (ppNewShader) *ppNewShader = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DSetBlobPart called\n"); 
	
	return (*_D3DSetBlobPart)(pSrcData, SrcDataSize, Part, Flags, pPart, PartSize, ppNewShader);
}
HRESULT WINAPI D3DCompressShaders(_In_ UINT uNumShaders,
                   _In_reads_(uNumShaders) D3D_SHADER_DATA* pShaderData,
                   _In_ UINT uFlags,
                   _Out_ ID3DBlob** ppCompressedData)
{
	InitC46();
	if (!_D3DCompressShaders)
	{
		if (ppCompressedData) *ppCompressedData = nullptr;
		return CompilerUnavailable();
	}
	LogInfo("D3DCompressShaders called\n"); 
	
	return (*_D3DCompressShaders)(uNumShaders, pShaderData, uFlags, ppCompressedData);
}
HRESULT WINAPI D3DDecompressShaders(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                     _In_ SIZE_T SrcDataSize,
                     _In_ UINT uNumShaders,	      
                     _In_ UINT uStartIndex,
                     _In_reads_opt_(uNumShaders) UINT* pIndices,
                     _In_ UINT uFlags,
                     _Out_writes_(uNumShaders) ID3DBlob** ppShaders,
		     _Out_opt_ UINT* pTotalShaders)
{
	InitC46();
	if (!_D3DDecompressShaders)
	{
		if (pTotalShaders) *pTotalShaders = 0;
		return CompilerUnavailable();
	}
	LogInfo("D3DDecompressShaders called\n"); 
	
	return (*_D3DDecompressShaders)(pSrcData, SrcDataSize, uNumShaders, uStartIndex, pIndices,
		uFlags, ppShaders, pTotalShaders);
}
HRESULT WINAPI D3DCreateBlob(_In_ SIZE_T Size,
              _Out_ ID3DBlob** ppBlob)
{
	InitC46();
	if (!_D3DCreateBlob)
	{
		if (ppBlob) *ppBlob = nullptr;
		return CompilerUnavailable();
	}
	LogDebug("D3DCreateBlob called\n");	// Too chatty for LogInfo
	
	return (*_D3DCreateBlob)(Size, ppBlob);
}

HRESULT WINAPI D3DCompileFromMemory(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize,
           _In_opt_ LPCSTR pSourceName,
           _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D10_SHADER_MACRO* pDefines,
           _In_opt_ ID3D10Include* pInclude,
           _In_ LPCSTR pEntrypoint,
           _In_ LPCSTR pTarget,
           _In_ UINT Flags1,
           _In_ UINT Flags2,
           _Out_ ID3D10Blob** ppCode,
           _Out_opt_ ID3D10Blob** ppErrorMsgs)
{
	InitC46();
	LogInfo("D3DCompileFromMemory called\n"); 
	
	return (*_D3DCompileFromMemory)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
		pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
}
HRESULT WINAPI D3DDisassembleCode(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
               _In_ SIZE_T SrcDataSize,
               _In_ UINT Flags,
               _Out_ SOutputContext *context,
               _Out_ ID3D10Blob** ppDisassembly)
{
	InitC46();
	LogInfo("D3DDisassembleCode called\n"); 
	
	return (*_D3DDisassembleCode)(pSrcData, SrcDataSize, Flags, context, ppDisassembly);
}
HRESULT WINAPI D3DDisassembleEffect(_In_ interface ID3D10Effect *pEffect, 
                       _In_ UINT Flags,
                       _Out_ ID3D10Blob** ppDisassembly)
{
	InitC46();
	LogInfo("D3DDisassembleEffect called\n"); 
	
	return (*_D3DDisassembleEffect)(pEffect, Flags, ppDisassembly);
}
HRESULT WINAPI D3DGetCodeDebugInfo(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
                _In_ SIZE_T SrcDataSize,
                _Out_ ID3D10Blob** ppDebugInfo)
{
	InitC46();
	LogInfo("D3DGetCodeDebugInfo called\n"); 
	
	return (*_D3DGetCodeDebugInfo)(pSrcData, SrcDataSize, ppDebugInfo);
}
HRESULT WINAPI D3DPreprocessFromMemory(_In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
              _In_ SIZE_T SrcDataSize,
              _In_opt_ LPCSTR pSourceName,
              _In_opt_ CONST D3D10_SHADER_MACRO* pDefines,
              _In_opt_ ID3D10Include* pInclude,
              _Out_ ID3D10Blob** ppCodeText,
              _Out_opt_ ID3D10Blob** ppErrorMsgs)
{
	InitC46();
	LogInfo("D3DPreprocessFromMemory called\n"); 
	
	return (*_D3DPreprocessFromMemory)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
		ppCodeText, ppErrorMsgs);
}
HRESULT WINAPI D3DReflectCode(GUID *interfaceId,
           _In_ INT unknown,
		   _In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
           _In_ SIZE_T SrcDataSize)
{
	InitC46();
	LogInfo("D3DReflectCode called\n"); 
	
	return (*_D3DReflectCode)(interfaceId, unknown, pSrcData, SrcDataSize);
}


// We need to get control as early as possible in order to pre-load our 
// d3d11 and nvapi64 dlls.  This will get called at static load time to
// get us hooked into place.  
//
// Be Careful of the restrictions on this entry point. No LogInfo e.g.

BOOL WINAPI DllMain(
	_In_  HINSTANCE hinstDLL,
	_In_  DWORD fdwReason,
	_In_  LPVOID lpvReserved)
{
	bool result = true;

	//bool waitfordebugger = false;
	//waitfordebugger = true;
	//do
	//{
	//	Sleep(250);
	//} while (!IsDebuggerPresent());
	//__debugbreak();

	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		wchar_t localPath[MAX_PATH] = L"d3d11.dll";
		hD3D11 = LoadLibrary(localPath);
		break;
	}

	case DLL_PROCESS_DETACH:
		break;

	case DLL_THREAD_ATTACH:
		// Do thread-specific initialization.
		break;

	case DLL_THREAD_DETACH:
		// Do thread-specific cleanup.
		break;
	}

	return result;
}
