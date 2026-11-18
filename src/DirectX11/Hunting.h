// Hunting
//  This code is to implement the Hunting mechanism as a separate compilation from the main wrapper code.
//  It implements all the shader management based on user input via key presses from Input.

#include "HackerDevice.h"
#include <D3Dcompiler.h>

void TimeoutHuntingBuffers();
void ParseHuntingSection();
void DumpUsage(wchar_t *dir);

void RegisterVisitedIndexBufferNoLock(uint32_t hash);
void RegisterVisitedIndexBuffer(uint32_t hash);
void RegisterVisitedVertexBufferNoLock(uint32_t hash, uint32_t slot_id);
void RegisterVisitedVertexBuffer(uint32_t hash, uint32_t slot_id);
void PurgeStaleVisitedBufferHashes(HackerDevice* device);

// The source name and include paths use UTF-8 regardless of the game thread locale.
HRESULT CompileShaderWithUnicodePath(const void* source, SIZE_T size, const wchar_t* path,
    const D3D_SHADER_MACRO* defines, const char* entry, const char* target,
    UINT flags, UINT effectFlags, ID3DBlob** code, ID3DBlob** errors, bool forceLegacy = false);
