#ifndef REFLECT_H
#define REFLECT_H

#include "hlslcc.h"

#include <cstddef>

ResourceGroup ResourceTypeToResourceGroup(ResourceType);

int GetResourceFromBindingPoint(const ResourceGroup eGroup, const uint32_t ui32BindPoint, const ShaderInfo* psShaderInfo, ResourceBinding** ppsOutBinding);

void GetConstantBufferFromBindingPoint(const ResourceGroup eGroup, const uint32_t ui32BindPoint, const ShaderInfo* psShaderInfo, ConstantBuffer** ppsConstBuf);

int GetInterfaceVarFromOffset(uint32_t ui32Offset, ShaderInfo* psShaderInfo, ShaderVar** ppsShaderVar);

uint32_t ShaderVarSize(ShaderVarType* psType, uint32_t* singularSize);

int GetShaderVarFromOffset(const uint32_t ui32Vec4Offset,
						   const uint32_t* pui32Swizzle,
						   ConstantBuffer* psCBuf,
						   ShaderVarType** ppsShaderVar,
						   int32_t* pi32Index,
						   int32_t* pi32Rebase);

//F-128: every reflection chunk carries the payload byte count that DecodeDXBC
//already validated against the real container. The reader stops at uiSize
//instead of trusting the counts and string offsets inside the payload.
struct ReflectionChunk
{
    const uint8_t* pui8Data = nullptr;
    size_t uiSize = 0;

    bool present() const { return pui8Data != nullptr; }
};

struct ReflectionChunks
{
    ReflectionChunk inputs;
    ReflectionChunk outputs;
    ReflectionChunk resources;
    ReflectionChunk interfaces;
    ReflectionChunk inputs11;
    ReflectionChunk outputs11;
    ReflectionChunk outputsWithStreams;
    ReflectionChunk patchConstants;
};

bool LoadShaderInfo(const uint32_t ui32MajorVersion,
    const uint32_t ui32MinorVersion,
    const ReflectionChunks* psChunks,
    ShaderInfo* psInfo);

void LoadD3D9ConstantTable(const char* data,
    ShaderInfo* psInfo);

void FreeShaderInfo(ShaderInfo* psShaderInfo);

#if 0
//--- Utility functions ---

//Returns 0 if not found, 1 otherwise.
int GetResourceFromName(const char* name, ShaderInfo* psShaderInfo, ResourceBinding* psBinding);

//These call into OpenGL and modify the uniforms of the currently bound program.
void SetResourceValueF(ResourceBinding* psBinding, float* value);
void SetResourceValueI(ResourceBinding* psBinding, int* value);
void SetResourceValueStr(ResourceBinding* psBinding, char* value); //Used for interfaces/subroutines. Also for constant buffers?

void CreateUniformBufferObjectFromResource(ResourceBinding* psBinding, uint32_t* ui32GLHandle);
//------------------------
#endif

#endif

