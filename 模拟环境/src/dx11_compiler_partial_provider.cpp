#include <Windows.h>
#include <d3dcompiler.h>

// F-107 fixture: a provider that exports only D3DCompile. It mirrors the real
// compiler's failure contract by clearing the output pointers and returning a
// failure HRESULT. The wrapper must forward this call and degrade every export
// the provider does not have without touching caller-owned output arrays.
HRESULT WINAPI D3DCompile(
    _In_reads_bytes_(SrcDataSize) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSize,
    _In_opt_ LPCSTR pSourceName,
    _In_reads_opt_(_Inexpressible_(pDefines->Name != NULL)) CONST D3D_SHADER_MACRO* pDefines,
    _In_opt_ ID3DInclude* pInclude,
    _In_opt_ LPCSTR pEntrypoint,
    _In_ LPCSTR pTarget,
    _In_ UINT Flags1,
    _In_ UINT Flags2,
    _Out_ ID3DBlob** ppCode,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorMsgs)
{
    UNREFERENCED_PARAMETER(pSrcData);
    UNREFERENCED_PARAMETER(SrcDataSize);
    UNREFERENCED_PARAMETER(pSourceName);
    UNREFERENCED_PARAMETER(pDefines);
    UNREFERENCED_PARAMETER(pInclude);
    UNREFERENCED_PARAMETER(pEntrypoint);
    UNREFERENCED_PARAMETER(pTarget);
    UNREFERENCED_PARAMETER(Flags1);
    UNREFERENCED_PARAMETER(Flags2);
    if (ppCode) *ppCode = nullptr;
    if (ppErrorMsgs) *ppErrorMsgs = nullptr;
    return E_FAIL;
}
