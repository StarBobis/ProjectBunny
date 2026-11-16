#ifndef DECODE_H
#define DECODE_H

#include "structs.h"

#include <cstddef>
#include <memory>

//F-104: both entry points need the real buffer size. The old single argument
//overloads are deleted so a caller that forgets the size fails to compile
//instead of reading past a short buffer.
Shader* DecodeDXBC(const void* data, size_t byteSize);
Shader* DecodeDXBC(uint32_t* data) = delete;

//You don't need to call this directly because DecodeDXBC
//will call DecodeDX9BC if the shader looks
//like it is SM1/2/3.
Shader* DecodeDX9BC(const uint32_t* pui32Tokens, size_t byteSize);
Shader* DecodeDX9BC(const uint32_t* pui32Tokens) = delete;

//F-104: owns a Shader until it is handed back to the caller. FreeShaderInfo
//releases the reflection arrays and clears the pointers, so the following
//~Shader only deletes the empty ShaderInfo.
struct ShaderOwnerDeleter
{
    void operator()(Shader* shader) const noexcept;
};
using ShaderPtr = std::unique_ptr<Shader, ShaderOwnerDeleter>;

void UpdateOperandReferences(Shader* psShader, Instruction* psInst);

#endif
