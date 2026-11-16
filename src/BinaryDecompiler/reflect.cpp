#include "internal_includes/reflect.h"
#include "internal_includes/debug.h"
#include "internal_includes/decode.h"
#include <stdlib.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

//F-128: D3D structures are nested far below this; the limit keeps a malformed
//type graph from exhausting the thread stack before the cycle check runs.
constexpr unsigned kMaxShaderTypeDepth = 64;

size_t ReflectionBudget(size_t chunkSize)
{
    //Type traversal budget for DAG expansion. Strings are deliberately not
    //budgeted: legal RDEF data can re-read the same shared struct member names
    //once per variable, which is proportional to the output type tree, not to
    //the bytes stored once in the chunk.
    const size_t minimum = 1024;
    const size_t maximum = static_cast<size_t>(-1);
    if (chunkSize > (maximum - minimum) / 2)
        return maximum;
    return std::max(minimum, chunkSize * 2);
}

struct ReflectionReader
{
    const uint8_t* base = nullptr;
    size_t size = 0;
    size_t typeBudget = 0;

    bool HasBytes(size_t offset, size_t count) const
    {
        return base != nullptr && offset <= size && count <= size - offset;
    }

    bool ReadU32(size_t offset, uint32_t* value) const
    {
        if (!HasBytes(offset, sizeof(*value)))
            return false;
        std::memcpy(value, base + offset, sizeof(*value));
        return true;
    }

    bool ReadU16(size_t offset, uint16_t* value) const
    {
        if (!HasBytes(offset, sizeof(*value)))
            return false;
        std::memcpy(value, base + offset, sizeof(*value));
        return true;
    }

    bool ReadString(size_t offset, std::string* value)
    {
        value->clear();
        if (!base || offset >= size)
            return false;

        //Scan only the actual remaining payload. No chunk-wide budget: a legal
        //shader may reference the same long name many times while building a
        //separate type tree for each variable.
        const size_t scanBytes = size - offset;
        const void* terminator = std::memchr(base + offset, 0, scanBytes);
        if (!terminator)
            return false;

        const size_t length = static_cast<const uint8_t*>(terminator) - (base + offset);
        value->assign(reinterpret_cast<const char*>(base + offset), length);
        return true;
    }
};

ReflectionReader MakeReader(const ReflectionChunk& chunk)
{
    ReflectionReader reader;
    reader.base = chunk.pui8Data;
    reader.size = chunk.uiSize;
    reader.typeBudget = ReflectionBudget(chunk.uiSize);
    return reader;
}

//count records of recordBytes each must fit between offset and the chunk end.
//The division form cannot overflow for a 32-bit count and a size_t offset.
bool CheckTable(const ReflectionReader& reader, size_t offset, uint32_t count, size_t recordBytes)
{
    if (offset > reader.size)
        return false;
    return count <= (reader.size - offset) / recordBytes;
}

bool ReadSignatures(ReflectionReader& reader,
                    InOutSignature** ppsSignatures,
                    uint32_t* pui32NumSignatures,
                    bool streams,
                    bool minPrec,
                    bool patchConstants)
{
    uint32_t elementCount = 0;
    uint32_t key = 0;
    if (!reader.ReadU32(0, &elementCount) || !reader.ReadU32(sizeof(uint32_t), &key))
        return false;

    const size_t recordBytes = 6 * sizeof(uint32_t) +
        (streams ? sizeof(uint32_t) : 0) +
        (minPrec ? sizeof(uint32_t) : 0);
    if (!CheckTable(reader, 2 * sizeof(uint32_t), elementCount, recordBytes))
        return false;

    InOutSignature* psSignatures = new InOutSignature[elementCount];
    *ppsSignatures = psSignatures;
    *pui32NumSignatures = elementCount;

    size_t recordOffset = 2 * sizeof(uint32_t);
    for (uint32_t i = 0; i < elementCount; ++i, recordOffset += recordBytes)
    {
        InOutSignature* psCurrentSignature = psSignatures + i;
        size_t cursor = recordOffset;
        uint32_t value = 0;

        psCurrentSignature->ui32Stream = 0;
        psCurrentSignature->eMinPrec = MIN_PRECISION_DEFAULT;

        if (streams)
        {
            if (!reader.ReadU32(cursor, &psCurrentSignature->ui32Stream))
                return false;
            cursor += sizeof(uint32_t);
        }

        uint32_t ui32SemanticNameOffset = 0;
        if (!reader.ReadU32(cursor, &ui32SemanticNameOffset))
            return false;
        cursor += sizeof(uint32_t);
        if (!reader.ReadU32(cursor, &psCurrentSignature->ui32SemanticIndex))
            return false;
        cursor += sizeof(uint32_t);
        if (!reader.ReadU32(cursor, &value))
            return false;
        psCurrentSignature->eSystemValueType = (SPECIAL_NAME)value;
        cursor += sizeof(uint32_t);
        if (!reader.ReadU32(cursor, &value))
            return false;
        psCurrentSignature->eComponentType = (INOUT_COMPONENT_TYPE)value;
        cursor += sizeof(uint32_t);
        if (!reader.ReadU32(cursor, &psCurrentSignature->ui32Register))
            return false;
        cursor += sizeof(uint32_t);
        uint32_t ui32ComponentMasks = 0;
        if (!reader.ReadU32(cursor, &ui32ComponentMasks))
            return false;
        psCurrentSignature->ui32Mask = ui32ComponentMasks & 0x7F;
        psCurrentSignature->ui32ReadWriteMask = (ui32ComponentMasks & 0x7F00) >> 8;
        cursor += sizeof(uint32_t);

        if (minPrec)
        {
            if (!reader.ReadU32(cursor, &value))
                return false;
            psCurrentSignature->eMinPrec = (MIN_PRECISION)value;
            cursor += sizeof(uint32_t);
        }

        //Massage some special inputs/outputs to match the types of GLSL counterparts
        if (patchConstants && psCurrentSignature->eSystemValueType == NAME_RENDER_TARGET_ARRAY_INDEX)
            psCurrentSignature->eComponentType = INOUT_COMPONENT_SINT32;

        if (!reader.ReadString(ui32SemanticNameOffset, &psCurrentSignature->SemanticName))
            return false;
    }

    return true;
}

bool ReadInputSignatures(ReflectionReader& reader, ShaderInfo* psShaderInfo, bool extended)
{
    return ReadSignatures(reader, &psShaderInfo->psInputSignatures,
        &psShaderInfo->ui32NumInputSignatures, extended, extended, false);
}

bool ReadOutputSignatures(ReflectionReader& reader, ShaderInfo* psShaderInfo,
                          bool minPrec, bool streams)
{
    return ReadSignatures(reader, &psShaderInfo->psOutputSignatures,
        &psShaderInfo->ui32NumOutputSignatures, streams, minPrec, false);
}

bool ReadPatchConstantSignatures(ReflectionReader& reader, ShaderInfo* psShaderInfo,
                                 bool minPrec, bool streams)
{
    return ReadSignatures(reader, &psShaderInfo->psPatchConstantSignatures,
        &psShaderInfo->ui32NumPatchConstantSignatures, streams, minPrec, true);
}

static bool ReadResourceBinding(ReflectionReader& reader, size_t offset, ResourceBinding* psBinding)
{
    uint32_t values[8];
    size_t cursor = offset;
    for (unsigned i = 0; i < 8; ++i, cursor += sizeof(uint32_t))
    {
        if (!reader.ReadU32(cursor, &values[i]))
            return false;
    }

    if (!reader.ReadString(values[0], &psBinding->Name))
        return false;

    psBinding->eType = (ResourceType) values[1];
    psBinding->ui32ReturnType = values[2];
    psBinding->eDimension = (REFLECT_RESOURCE_DIMENSION) values[3];
    psBinding->ui32NumSamples = values[4];
    psBinding->ui32BindPoint = values[5];
    psBinding->ui32BindCount = values[6];
    psBinding->ui32Flags = values[7];

    return true;
}

//Read D3D11_SHADER_TYPE_DESC
static bool ReadShaderVariableType(ReflectionReader& reader,
                                   uint32_t ui32TypeOffset,
                                   ShaderVarType* varType,
                                   unsigned uiDepth,
                                   const uint32_t* pui32Ancestors,
                                   unsigned ui32AncestorCount)
{
    if (uiDepth > kMaxShaderTypeDepth || reader.typeBudget == 0)
        return false;
    --reader.typeBudget;

    const size_t typeOffset = ui32TypeOffset;
    if (!reader.HasBytes(typeOffset, 16))
        return false;

    for (unsigned i = 0; i < ui32AncestorCount; ++i)
    {
        if (pui32Ancestors[i] == ui32TypeOffset)
            return false;
    }

    uint16_t header[6];
    for (unsigned i = 0; i < 6; ++i)
    {
        if (!reader.ReadU16(typeOffset + i * sizeof(uint16_t), &header[i]))
            return false;
    }
    uint32_t ui32MemberInfoOffset = 0;
    if (!reader.ReadU32(typeOffset + 3 * sizeof(uint32_t), &ui32MemberInfoOffset))
        return false;

    varType->Class = (SHADER_VARIABLE_CLASS) header[0];
    varType->Type = (SHADER_VARIABLE_TYPE) header[1];
    varType->Rows = header[2];
    varType->Columns = header[3];
    varType->Elements = header[4];
    varType->MemberCount = header[5];
    varType->Members = 0;

    if (varType->ParentCount)
    {
        varType->FullName = varType->Parent->FullName +
            "." +
            varType->Name;
    }

    const uint32_t ui32MemberCount = varType->MemberCount;
    if (!ui32MemberCount)
        return true;

    if (ui32AncestorCount > kMaxShaderTypeDepth)
        return false;
    if (!CheckTable(reader, ui32MemberInfoOffset, ui32MemberCount, 3 * sizeof(uint32_t)))
        return false;

    uint32_t aui32ChildAncestors[kMaxShaderTypeDepth + 1];
    for (unsigned i = 0; i < ui32AncestorCount; ++i)
        aui32ChildAncestors[i] = pui32Ancestors[i];
    aui32ChildAncestors[ui32AncestorCount] = ui32TypeOffset;

    ShaderVarType* psMembers = new ShaderVarType[ui32MemberCount];
    varType->Members = psMembers;

    size_t memberOffset = ui32MemberInfoOffset;
    for (uint32_t i = 0; i < ui32MemberCount; ++i, memberOffset += 3 * sizeof(uint32_t))
    {
        uint32_t ui32NameOffset = 0;
        uint32_t ui32MemberTypeOffset = 0;
        uint32_t ui32MemberFieldOffset = 0;
        if (!reader.ReadU32(memberOffset, &ui32NameOffset))
            return false;
        if (!reader.ReadU32(memberOffset + sizeof(uint32_t), &ui32MemberTypeOffset))
            return false;
        if (!reader.ReadU32(memberOffset + 2 * sizeof(uint32_t), &ui32MemberFieldOffset))
            return false;

        psMembers[i].Parent = varType;
        psMembers[i].ParentCount = varType->ParentCount + 1;
        psMembers[i].Offset = ui32MemberFieldOffset;

        if (!reader.ReadString(ui32NameOffset, &psMembers[i].Name))
            return false;

        if (!ReadShaderVariableType(reader, ui32MemberTypeOffset, &psMembers[i],
                uiDepth + 1, aui32ChildAncestors, ui32AncestorCount + 1))
            return false;
    }

    return true;
}

static bool ReadConstantBuffer(ShaderInfo* psShaderInfo,
    ReflectionReader& reader, size_t offset, ConstantBuffer* psBuffer)
{
    uint32_t header[6];
    size_t cursor = offset;
    for (unsigned i = 0; i < 6; ++i, cursor += sizeof(uint32_t))
    {
        if (!reader.ReadU32(cursor, &header[i]))
            return false;
    }

    const uint32_t ui32NameOffset = header[0];
    const uint32_t ui32VarCount = header[1];
    const uint32_t ui32VarOffset = header[2];

    const size_t variableRecordBytes = psShaderInfo->ui32MajorVersion >= 5 ?
        10 * sizeof(uint32_t) : 6 * sizeof(uint32_t);
    if (!CheckTable(reader, ui32VarOffset, ui32VarCount, variableRecordBytes))
        return false;

    if (!reader.ReadString(ui32NameOffset, &psBuffer->Name))
        return false;

    psBuffer->asVars.resize(ui32VarCount);

    size_t varCursor = ui32VarOffset;
    for (uint32_t i = 0; i < ui32VarCount; ++i, varCursor += variableRecordBytes)
    {
        //D3D11_SHADER_VARIABLE_DESC
        // DarkStarSword: Changed this to resize first, then get a pointer.
        // The way we originally did this filling out the structure then inserting
        // it with push_back(), was problematic, as the inserted struct was a copy,
        // not the original, so the Parent pointers of all members were invalid.
        ShaderVar * const psVar = &psBuffer->asVars[i];

        uint32_t ui32Flags = 0;
        uint32_t ui32TypeOffset = 0;
        uint32_t ui32DefaultValueOffset = 0;

        uint32_t ui32VarNameOffset = 0;
        if (!reader.ReadU32(varCursor, &ui32VarNameOffset))
            return false;
        if (!reader.ReadU32(varCursor + sizeof(uint32_t), &psVar->ui32StartOffset))
            return false;
        if (!reader.ReadU32(varCursor + 2 * sizeof(uint32_t), &psVar->ui32Size))
            return false;
        if (!reader.ReadU32(varCursor + 3 * sizeof(uint32_t), &ui32Flags))
            return false;
        if (!reader.ReadU32(varCursor + 4 * sizeof(uint32_t), &ui32TypeOffset))
            return false;
        if (!reader.ReadU32(varCursor + 5 * sizeof(uint32_t), &ui32DefaultValueOffset))
            return false;

        if (!reader.ReadString(ui32VarNameOffset, &psVar->Name))
            return false;

		psVar->sType.Name = psVar->Name;
		psVar->sType.FullName = psVar->Name;
		psVar->sType.Parent = 0;
		psVar->sType.ParentCount = 0;
		psVar->sType.Offset = 0;

        if (!ReadShaderVariableType(reader, ui32TypeOffset, &psVar->sType, 0, nullptr, 0))
            return false;

		psVar->haveDefaultValue = false;

        if(ui32DefaultValueOffset)
        {
			uint32_t i = 0;
			const uint32_t ui32NumDefaultValues = psVar->ui32Size / 4;

			//Always a sequence of 4-bytes at the moment.
			//bool const becomes 0 or 0xFFFFFFFF int, int & float are 4-bytes.
			if (psVar->ui32Size % 4 != 0)
				return false;
			if (!CheckTable(reader, ui32DefaultValueOffset, psVar->ui32Size, 1))
				return false;

			psVar->haveDefaultValue = true;
			psVar->pui32DefaultValues.resize(ui32NumDefaultValues);

			for(i=0; i<ui32NumDefaultValues;++i)
			{
				if (!reader.ReadU32(size_t(ui32DefaultValueOffset) + i * sizeof(uint32_t),
						&psVar->pui32DefaultValues[i]))
					return false;
			}
        }
    }

	psBuffer->ui32TotalSizeInBytes = header[3];
	psBuffer->iUnsized = 0;

    return true;
}

static bool ReadResources(ReflectionReader& reader, ShaderInfo* psShaderInfo)
{
    uint32_t header[6];
    size_t cursor = 0;
    for (unsigned i = 0; i < 6; ++i, cursor += sizeof(uint32_t))
    {
        if (!reader.ReadU32(cursor, &header[i]))
            return false;
    }

	const uint32_t ui32NumConstantBuffers = header[0];
    const uint32_t ui32ConstantBufferOffset = header[1];
    const uint32_t ui32NumResourceBindings = header[2];
    const uint32_t ui32ResourceBindingOffset = header[3];

    if (!CheckTable(reader, ui32ResourceBindingOffset, ui32NumResourceBindings, 8 * sizeof(uint32_t)))
        return false;
    if (!CheckTable(reader, ui32ConstantBufferOffset, ui32NumConstantBuffers, 6 * sizeof(uint32_t)))
        return false;

    //Resources
    ResourceBinding* psResBindings = new ResourceBinding[ui32NumResourceBindings];

    psShaderInfo->ui32NumResourceBindings = ui32NumResourceBindings;
    psShaderInfo->psResourceBindings = psResBindings;

    size_t bindingOffset = ui32ResourceBindingOffset;
    for (uint32_t i = 0; i < ui32NumResourceBindings; ++i, bindingOffset += 8 * sizeof(uint32_t))
    {
        if (!ReadResourceBinding(reader, bindingOffset, psResBindings + i))
            return false;
		ASSERT(psResBindings[i].ui32BindPoint < MAX_RESOURCE_BINDINGS);
	}

    //Constant buffers
    ConstantBuffer* psConstantBuffers = new ConstantBuffer[ui32NumConstantBuffers];

    psShaderInfo->ui32NumConstantBuffers = ui32NumConstantBuffers;
    psShaderInfo->psConstantBuffers = psConstantBuffers;

    size_t constantBufferOffset = ui32ConstantBufferOffset;
    for (uint32_t i = 0; i < ui32NumConstantBuffers; ++i, constantBufferOffset += 6 * sizeof(uint32_t))
    {
        if (!ReadConstantBuffer(psShaderInfo, reader, constantBufferOffset, psConstantBuffers + i))
            return false;
    }

	//Map resource bindings to constant buffers
	if(psShaderInfo->ui32NumConstantBuffers)
	{
		for(uint32_t i = 0; i < ui32NumResourceBindings; ++i)
		{
			ResourceGroup eRGroup;
			uint32_t cbufIndex = 0;

			eRGroup = ResourceTypeToResourceGroup(psResBindings[i].eType);

			//Find the constant buffer whose name matches the resource at the given resource binding point
			for(cbufIndex=0; cbufIndex < psShaderInfo->ui32NumConstantBuffers; cbufIndex++)
			{
				if(psConstantBuffers[cbufIndex].Name == psResBindings[i].Name)
				{
					psShaderInfo->aui32ResourceMap[eRGroup][psResBindings[i].ui32BindPoint] = cbufIndex;
				}
			}
		}
	}

    return true;
}

static bool ReadClassType(ReflectionReader& reader, size_t offset, ClassType* psClassType)
{
    uint32_t ui32NameOffset = 0;
    uint16_t ui16ID = 0;
    uint16_t ui16ConstBufStride = 0;
    uint16_t ui16Texture = 0;
    uint16_t ui16Sampler = 0;

    if (!reader.ReadU32(offset, &ui32NameOffset))
        return false;
    if (!reader.ReadU16(offset + 4, &ui16ID))
        return false;
    if (!reader.ReadU16(offset + 6, &ui16ConstBufStride))
        return false;
    if (!reader.ReadU16(offset + 8, &ui16Texture))
        return false;
    if (!reader.ReadU16(offset + 10, &ui16Sampler))
        return false;

    psClassType->ui16ID = ui16ID;
    psClassType->ui16ConstBufStride = ui16ConstBufStride;
    psClassType->ui16Texture = ui16Texture;
    psClassType->ui16Sampler = ui16Sampler;

    return reader.ReadString(ui32NameOffset, &psClassType->Name);
}

static bool ReadClassInstance(ReflectionReader& reader, size_t offset, ClassInstance* psClassInstance)
{
    uint16_t ui16NameHigh = 0;
    uint16_t ui16NameLow = 0;
    if (!reader.ReadU16(offset, &ui16NameHigh) || !reader.ReadU16(offset + 2, &ui16NameLow))
        return false;
    //Preserve the existing word order; only the signed-left-shift UB is fixed.
    uint32_t ui32NameOffset = (static_cast<uint32_t>(ui16NameHigh) << 16) | ui16NameLow;

    uint16_t ui16ID = 0;
    uint16_t ui16ConstBuf = 0;
    uint16_t ui16ConstBufOffset = 0;
    uint16_t ui16Texture = 0;
    uint16_t ui16Sampler = 0;
    if (!reader.ReadU16(offset + 4, &ui16ID))
        return false;
    if (!reader.ReadU16(offset + 6, &ui16ConstBuf))
        return false;
    if (!reader.ReadU16(offset + 8, &ui16ConstBufOffset))
        return false;
    if (!reader.ReadU16(offset + 10, &ui16Texture))
        return false;
    if (!reader.ReadU16(offset + 12, &ui16Sampler))
        return false;

    psClassInstance->ui16ID = ui16ID;
    psClassInstance->ui16ConstBuf = ui16ConstBuf;
    psClassInstance->ui16ConstBufOffset = ui16ConstBufOffset;
    psClassInstance->ui16Texture = ui16Texture;
    psClassInstance->ui16Sampler = ui16Sampler;

    return reader.ReadString(ui32NameOffset, &psClassInstance->Name);
}


static bool ReadInterfaces(ReflectionReader& reader, ShaderInfo* psShaderInfo)
{
    uint32_t header[7];
    size_t cursor = 0;
    for (unsigned i = 0; i < 7; ++i, cursor += sizeof(uint32_t))
    {
        if (!reader.ReadU32(cursor, &header[i]))
            return false;
    }

    const uint32_t ui32ClassInstanceCount = header[0];
    const uint32_t ui32ClassTypeCount = header[1];
    const uint32_t ui32InterfaceSlotRecordCount = header[2];
    const uint32_t ui32ClassInstanceOffset = header[4];
    const uint32_t ui32ClassTypeOffset = header[5];
    const uint32_t ui32InterfaceSlotOffset = header[6];

    if (!CheckTable(reader, ui32ClassTypeOffset, ui32ClassTypeCount, 12))
        return false;
    //D3D11_CLASS_INSTANCE_DESC: DWORD name offset plus five WORD fields.
    if (!CheckTable(reader, ui32ClassInstanceOffset, ui32ClassInstanceCount, 14))
        return false;
    if (!CheckTable(reader, ui32InterfaceSlotOffset, ui32InterfaceSlotRecordCount, 16))
        return false;

    ClassType* psClassTypes = new ClassType[ui32ClassTypeCount];
    //F-104: publish before filling so FreeShaderInfo can release a partially
    //constructed array when a string allocation below throws.
    psShaderInfo->ui32NumClassTypes = ui32ClassTypeCount;
    psShaderInfo->psClassTypes = psClassTypes;
    size_t classTypeOffset = ui32ClassTypeOffset;
    for (uint32_t i = 0; i < ui32ClassTypeCount; ++i, classTypeOffset += 12)
    {
        if (!ReadClassType(reader, classTypeOffset, psClassTypes + i))
            return false;
        psClassTypes[i].ui16ID = (uint16_t)i;
    }

    ClassInstance* psClassInstances = new ClassInstance[ui32ClassInstanceCount];
    psShaderInfo->ui32NumClassInstances = ui32ClassInstanceCount;
    psShaderInfo->psClassInstances = psClassInstances;
    size_t classInstanceOffset = ui32ClassInstanceOffset;
    for (uint32_t i = 0; i < ui32ClassInstanceCount; ++i, classInstanceOffset += 14)
    {
        if (!ReadClassInstance(reader, classInstanceOffset, psClassInstances + i))
            return false;
    }

    //Slots map function table to $ThisPointer cbuffer variable index
    size_t slotOffset = ui32InterfaceSlotOffset;
    for (uint32_t i = 0; i < ui32InterfaceSlotRecordCount; ++i, slotOffset += 16)
    {
        uint32_t ui32SlotSpan = 0;
        uint32_t ui32Count = 0;
        uint32_t ui32TypeIDOffset = 0;
        uint32_t ui32TableIDOffset = 0;
        if (!reader.ReadU32(slotOffset, &ui32SlotSpan))
            return false;
        if (!reader.ReadU32(slotOffset + sizeof(uint32_t), &ui32Count))
            return false;
        if (!reader.ReadU32(slotOffset + 2 * sizeof(uint32_t), &ui32TypeIDOffset))
            return false;
        if (!reader.ReadU32(slotOffset + 3 * sizeof(uint32_t), &ui32TableIDOffset))
            return false;

        if (!CheckTable(reader, ui32TypeIDOffset, ui32Count, sizeof(uint16_t)))
            return false;
        if (!CheckTable(reader, ui32TableIDOffset, ui32Count, sizeof(uint32_t)))
            return false;

        for (uint32_t k = 0; k < ui32Count; ++k)
        {
            uint16_t ui16TypeID = 0;
            uint32_t ui32TableID = 0;
            if (!reader.ReadU16(size_t(ui32TypeIDOffset) + k * sizeof(uint16_t), &ui16TypeID))
                return false;
            if (!reader.ReadU32(size_t(ui32TableIDOffset) + k * sizeof(uint32_t), &ui32TableID))
                return false;
            psShaderInfo->aui32TableIDToTypeID[ui32TableID] = ui16TypeID;
        }

        (void)ui32SlotSpan;
    }

    return true;
}

} // namespace

void GetConstantBufferFromBindingPoint(const ResourceGroup eGroup, const uint32_t ui32BindPoint, const ShaderInfo* psShaderInfo, ConstantBuffer** ppsConstBuf)
{
	if(psShaderInfo->ui32MajorVersion > 3)
	{
		*ppsConstBuf = psShaderInfo->psConstantBuffers + psShaderInfo->aui32ResourceMap[eGroup].at(ui32BindPoint);
	}
	else
	{
		ASSERT(psShaderInfo->ui32NumConstantBuffers == 1);
		*ppsConstBuf = psShaderInfo->psConstantBuffers;
	}
}

int GetResourceFromBindingPoint(const ResourceGroup eGroup, uint32_t const ui32BindPoint, const ShaderInfo* psShaderInfo, ResourceBinding** ppsOutBinding)
{
    uint32_t i;
    const uint32_t ui32NumBindings = psShaderInfo->ui32NumResourceBindings;
    ResourceBinding* psBindings = psShaderInfo->psResourceBindings;

    for(i=0; i<ui32NumBindings; ++i)
    {
        if(ResourceTypeToResourceGroup(psBindings[i].eType) == eGroup)
        {
			if(ui32BindPoint >= psBindings[i].ui32BindPoint && ui32BindPoint < (psBindings[i].ui32BindPoint + psBindings[i].ui32BindCount))
			{
				*ppsOutBinding = psBindings + i;
				return 1;
			}
        }
    }
    return 0;
}

int GetInterfaceVarFromOffset(uint32_t ui32Offset, ShaderInfo* psShaderInfo, ShaderVar** ppsShaderVar)
{
    uint32_t i;
    ConstantBuffer* psThisPointerConstBuffer = psShaderInfo->psThisPointerConstBuffer;

    const uint32_t ui32NumVars = (uint32_t)psThisPointerConstBuffer->asVars.size();

    for(i=0; i<ui32NumVars; ++i)
    {
        if(ui32Offset >= psThisPointerConstBuffer->asVars[i].ui32StartOffset && 
            ui32Offset < (psThisPointerConstBuffer->asVars[i].ui32StartOffset + psThisPointerConstBuffer->asVars[i].ui32Size))
	    {
		    *ppsShaderVar = &psThisPointerConstBuffer->asVars[i];
		    return 1;
	    }
    }
    return 0;
}

// Added to calculate structure sizes rather than assume 4 bytes -DSS
uint32_t ShaderVarSize(ShaderVarType* psType, uint32_t* singularSize)
{
	uint32_t thisSize = 0;
	uint32_t m = 0;

	if(psType->Class == SVC_STRUCT)
	{
		for(m=0; m < psType->MemberCount; ++m)
			thisSize += ShaderVarSize(psType->Members + m, NULL);
	}
	else
		thisSize = psType->Columns * psType->Rows * 4;

	if(singularSize)
		*singularSize = thisSize;

	if(psType->Elements)
	{
#if 0
		// DarkStarSword: This assumption they are making is incorrect for
		// StructuredBuffers, and this can cause a variable following an array
		// to be misattributed as being part of the array. This assumption was
		// based on the alignment constraints enforced on constant buffers, so
		// upstreaming a patch for this would need to take into account the
		// kind of buffer being queried (we do not currently use this for
		// constant buffers). Our structured_buffers.hlsl test case includes a
		// couple of cases specifically for this issue in StructuredBuffers.


		// Everything smaller than vec4 in an array takes the space of vec4, except for the last one
		if (thisSize < 4 * 4)
		{
			thisSize = (4 * 4 * (psType->Elements - 1)) + thisSize;
		}
		else
		{
			thisSize *= psType->Elements;
		}
#else
		thisSize *= psType->Elements;
#endif
	}
	return thisSize;
}

// Manually added from latest James-Jones HLSLCrossCompiler for StructuredBuffer support -DSS
static int IsOffsetInType(ShaderVarType* psType,
						  uint32_t parentOffset,
						  uint32_t offsetToFind,
						  int32_t* pi32Index,
						  int32_t* pi32Rebase)
{
	uint32_t thisOffset = parentOffset + psType->Offset;
	// DarkStarSword: Changed this line to calculate arrays and nested struct sizes properly:
	uint32_t thisSize = ShaderVarSize(psType, NULL);

	if((offsetToFind >= thisOffset) &&
		offsetToFind < (thisOffset + thisSize))
	{

        if(psType->Class == SVC_MATRIX_ROWS || 
            psType->Class == SVC_MATRIX_COLUMNS)
        {
			//Matrices are treated as arrays of vectors.
			pi32Index[0] = (offsetToFind - thisOffset) / 16;
        }
		//Check for array of scalars or vectors (both take up 16 bytes per element)
		else if ((psType->Class == SVC_SCALAR || psType->Class == SVC_VECTOR) && psType->Elements > 1)
		{
			pi32Index[0] = (offsetToFind - thisOffset) / 16;
		}
		else if(psType->Class == SVC_VECTOR && psType->Columns > 1)
		{
			//Check for vector starting at a non-vec4 offset.

			// cbuffer $Globals
			// {
			//
			//   float angle;                       // Offset:    0 Size:     4
			//   float2 angle2;                     // Offset:    4 Size:     8
			//
			// }

			//cb0[0].x = angle
			//cb0[0].yzyy = angle2.xyxx

			//Rebase angle2 so that .y maps to .x, .z maps to .y

			pi32Rebase[0] = thisOffset % 16;
		}

		return 1;
	}
	return 0;
}

// Moved out of GetShaderVarFromOffset to handle nested structs -DSS
int GetShaderVarFromNestedStructOffset(ShaderVarType* psType,
									   const uint32_t parentOffset,
									   uint32_t ui32ByteOffset,
									   ShaderVarType** ppsShaderVar,
									   int32_t* pi32Index,
									   int32_t* pi32Rebase)
{
	uint32_t singularSize = 0;
	uint32_t thisSize = ShaderVarSize(psType, &singularSize);
	uint32_t thisOffset = parentOffset + psType->Offset;
	uint32_t m = 0;

	if(ui32ByteOffset < thisOffset ||
	   ui32ByteOffset >= thisOffset + thisSize)
		return 0;

	// We know we are somewhere inside the struct, but if this is an array of
	// structs and we are looking for an offset in a subsequent struct index we
	// need to adjust the offset to fit within the first struct index:
	ui32ByteOffset = (ui32ByteOffset - thisOffset) % singularSize + thisOffset;

	for(m=0; m < psType->MemberCount; ++m)
	{
		ShaderVarType* psMember = psType->Members + m;

		if(psMember->Class == SVC_STRUCT)
		{
			if(GetShaderVarFromNestedStructOffset(psMember, thisOffset, ui32ByteOffset, ppsShaderVar, pi32Index, pi32Rebase))
				return 1;
		}
		else
		{
			if(IsOffsetInType(psMember, thisOffset, ui32ByteOffset, pi32Index, pi32Rebase))
			{
				ppsShaderVar[0] = psMember;
				return 1;
			}
		}
	}
	return 0;
}

// Manually added from latest James-Jones HLSLCrossCompiler for StructuredBuffer support -DSS
int GetShaderVarFromOffset(const uint32_t ui32Vec4Offset,
						   const uint32_t* pui32Swizzle,
						   ConstantBuffer* psCBuf,
						   ShaderVarType** ppsShaderVar,
						   int32_t* pi32Index,
						   int32_t* pi32Rebase)
{
    uint32_t i;
    uint32_t ui32ByteOffset = ui32Vec4Offset * 16;

    const uint32_t ui32NumVars = (uint32_t)psCBuf->asVars.size();

	if(psCBuf->iUnsized && ui32NumVars == 1 && psCBuf->asVars[0].sType.Class != SVC_STRUCT)
	{
		ppsShaderVar[0] = &psCBuf->asVars[0].sType;
		return 1;
	}

    //Swizzle can point to another variable. In the example below
	//cbUIUpdates.g_uMaxFaces would be cb1[2].z. The scalars are combined
	//into vectors. psCBuf->ui32NumVars will be 3.

	// cbuffer cbUIUpdates
	// {
	//
	//   float g_fLifeSpan;                 // Offset:    0 Size:     4
	//   float g_fLifeSpanVar;              // Offset:    4 Size:     4 [unused]
	//   float g_fRadiusMin;                // Offset:    8 Size:     4 [unused]
	//   float g_fRadiusMax;                // Offset:   12 Size:     4 [unused]
	//   float g_fGrowTime;                 // Offset:   16 Size:     4 [unused]
	//   float g_fStepSize;                 // Offset:   20 Size:     4
	//   float g_fTurnRate;                 // Offset:   24 Size:     4
	//   float g_fTurnSpeed;                // Offset:   28 Size:     4 [unused]
	//   float g_fLeafRate;                 // Offset:   32 Size:     4
	//   float g_fShrinkTime;               // Offset:   36 Size:     4 [unused]
	//   uint g_uMaxFaces;                  // Offset:   40 Size:     4
	//
	// }

	// Name                                 Type  Format         Dim Slot Elements
	// ------------------------------ ---------- ------- ----------- ---- --------
	// cbUIUpdates                       cbuffer      NA          NA    1        1

    if(pui32Swizzle[0] == OPERAND_4_COMPONENT_Y)
    {
        ui32ByteOffset += 4;
    }
    else
    if(pui32Swizzle[0] == OPERAND_4_COMPONENT_Z)
    {
        ui32ByteOffset += 8;
    }
    else
    if(pui32Swizzle[0] == OPERAND_4_COMPONENT_W)
    {
        ui32ByteOffset += 12;
    }

    for(i=0; i<ui32NumVars; ++i)
    {
		if(psCBuf->asVars[i].sType.Class == SVC_STRUCT)
		{
			if(GetShaderVarFromNestedStructOffset(&psCBuf->asVars[i].sType, psCBuf->asVars[i].ui32StartOffset, ui32ByteOffset, ppsShaderVar, pi32Index, pi32Rebase))
				return 1;
		}
		else
		{
			if(IsOffsetInType(&psCBuf->asVars[i].sType, psCBuf->asVars[i].ui32StartOffset, ui32ByteOffset, pi32Index, pi32Rebase))
			{
				ppsShaderVar[0] = &psCBuf->asVars[i].sType;
				return 1;
			}
		}
    }
    return 0;
}

ResourceGroup ResourceTypeToResourceGroup(ResourceType eType)
{
	switch(eType)
	{
	case RTYPE_CBUFFER:
		return RGROUP_CBUFFER;

	case RTYPE_SAMPLER:
		return RGROUP_SAMPLER;

	case RTYPE_TEXTURE:
	case RTYPE_BYTEADDRESS:
	case RTYPE_STRUCTURED:
		return RGROUP_TEXTURE;

	case RTYPE_UAV_RWTYPED:
	case RTYPE_UAV_RWSTRUCTURED:
	case RTYPE_UAV_RWBYTEADDRESS:
	case RTYPE_UAV_APPEND_STRUCTURED:
	case RTYPE_UAV_CONSUME_STRUCTURED:
	case RTYPE_UAV_RWSTRUCTURED_WITH_COUNTER:
		return RGROUP_UAV;

	case RTYPE_TBUFFER:
		// DarkStarSword: Removed this assert - tbuffers appear in several of
		// our game example test cases and do indeed map to 't' registers.
		//ASSERT(0); // Need to find out which group this belongs to
		return RGROUP_TEXTURE;
	}

	ASSERT(0);
	return RGROUP_CBUFFER;
}

bool LoadShaderInfo(const uint32_t ui32MajorVersion,
    const uint32_t ui32MinorVersion,
    const ReflectionChunks* psChunks,
    ShaderInfo* psInfo)
{
    psInfo->eTessOutPrim = TESSELLATOR_OUTPUT_UNDEFINED;
    psInfo->eTessPartitioning = TESSELLATOR_PARTITIONING_UNDEFINED;

    psInfo->ui32MajorVersion = ui32MajorVersion;
    psInfo->ui32MinorVersion = ui32MinorVersion;

    if(psChunks->inputs.present())
    {
        ReflectionReader reader = MakeReader(psChunks->inputs);
        if (!ReadInputSignatures(reader, psInfo, false))
            return false;
    }
    if(psChunks->inputs11.present())
    {
        ReflectionReader reader = MakeReader(psChunks->inputs11);
        if (!ReadInputSignatures(reader, psInfo, true))
            return false;
    }
    if(psChunks->resources.present())
    {
        ReflectionReader reader = MakeReader(psChunks->resources);
        if (!ReadResources(reader, psInfo))
            return false;
    }
    if(psChunks->interfaces.present())
    {
        ReflectionReader reader = MakeReader(psChunks->interfaces);
        if (!ReadInterfaces(reader, psInfo))
            return false;
    }
    if(psChunks->outputs.present())
    {
        ReflectionReader reader = MakeReader(psChunks->outputs);
        if (!ReadOutputSignatures(reader, psInfo, false, false))
            return false;
    }
    if(psChunks->outputs11.present())
    {
        ReflectionReader reader = MakeReader(psChunks->outputs11);
        if (!ReadOutputSignatures(reader, psInfo, true, true))
            return false;
    }
	if(psChunks->outputsWithStreams.present())
	{
        ReflectionReader reader = MakeReader(psChunks->outputsWithStreams);
        if (!ReadOutputSignatures(reader, psInfo, false, true))
            return false;
	}
	if(psChunks->patchConstants.present())
	{
        ReflectionReader reader = MakeReader(psChunks->patchConstants);
        if (!ReadPatchConstantSignatures(reader, psInfo, false, false))
            return false;
	}

    {
        uint32_t i;
        for(i=0; i<psInfo->ui32NumConstantBuffers;++i)
        {
            std::string cbufName = psInfo->psConstantBuffers[i].Name;
            std::string cbufThisPointer = "$ThisPointer";
            if(cbufName.compare(cbufThisPointer) == 0)
            {
                psInfo->psThisPointerConstBuffer = &psInfo->psConstantBuffers[i];
            }
        }
    }

    return true;
}

void FreeShaderInfo(ShaderInfo* psShaderInfo)
{
    //F-101: the decode owner may see a partially constructed ShaderInfo. A
    //null info adds nothing to release; an all-zero/partially-null info is
    //already safe because delete[] accepts null and the counts are reset below.
    if (!psShaderInfo)
        return;

    delete[] psShaderInfo->psInputSignatures; psShaderInfo->psInputSignatures = 0;
    delete[] psShaderInfo->psResourceBindings; psShaderInfo->psResourceBindings = 0;
    delete[] psShaderInfo->psConstantBuffers; psShaderInfo->psConstantBuffers = 0;
    delete[] psShaderInfo->psClassTypes; psShaderInfo->psClassTypes = 0;
    delete[] psShaderInfo->psClassInstances; psShaderInfo->psClassInstances = 0;
    delete[] psShaderInfo->psOutputSignatures; psShaderInfo->psOutputSignatures = 0;
    delete[] psShaderInfo->psPatchConstantSignatures; psShaderInfo->psPatchConstantSignatures = 0;

    psShaderInfo->ui32NumInputSignatures = 0;
    psShaderInfo->ui32NumResourceBindings = 0;
    psShaderInfo->ui32NumConstantBuffers = 0;
    psShaderInfo->ui32NumClassTypes = 0;
    psShaderInfo->ui32NumClassInstances = 0;
    psShaderInfo->ui32NumOutputSignatures = 0;
	psShaderInfo->ui32NumPatchConstantSignatures = 0;
}

struct ConstantTableD3D9
{
    uint32_t size;
    uint32_t creator;
    uint32_t version;
    uint32_t constants;
    uint32_t constantInfos;
    uint32_t flags;
    uint32_t target;
};

// These enums match those in d3dx9shader.h.
enum RegisterSet
{
    RS_BOOL,
    RS_INT4,
    RS_FLOAT4,
    RS_SAMPLER,
};

enum TypeClass
{
    CLASS_SCALAR,
    CLASS_VECTOR,
    CLASS_MATRIX_ROWS,
    CLASS_MATRIX_COLUMNS,
    CLASS_OBJECT,
    CLASS_STRUCT,
};

enum Type
{
    PT_VOID,
    PT_BOOL,
    PT_INT,
    PT_FLOAT,
    PT_STRING,
    PT_TEXTURE,
    PT_TEXTURE1D,
    PT_TEXTURE2D,
    PT_TEXTURE3D,
    PT_TEXTURECUBE,
    PT_SAMPLER,
    PT_SAMPLER1D,
    PT_SAMPLER2D,
    PT_SAMPLER3D,
    PT_SAMPLERCUBE,
    PT_PIXELSHADER,
    PT_VERTEXSHADER,
    PT_PIXELFRAGMENT,
    PT_VERTEXFRAGMENT,
    PT_UNSUPPORTED,
};
typedef struct ConstantInfoD3D9_TAG
{
    uint32_t name;
    uint16_t registerSet;
    uint16_t registerIndex;
    uint16_t registerCount;
    uint16_t reserved;
    uint32_t typeInfo;
    uint32_t defaultValue;
} ConstantInfoD3D9;

typedef struct TypeInfoD3D9_TAG
{
    uint16_t typeClass;
    uint16_t type;
    uint16_t rows;
    uint16_t columns;
    uint16_t elements;
    uint16_t structMembers;
    uint32_t structMemberInfos;
} TypeInfoD3D9;

typedef struct StructMemberInfoD3D9_TAG
{
    uint32_t name;
    uint32_t typeInfo;
} StructMemberInfoD3D9;

void LoadD3D9ConstantTable(const char* data,
    ShaderInfo* psInfo)
{
    ConstantTableD3D9* ctab;
    uint32_t constNum;
    ConstantInfoD3D9* cinfos;
    ConstantBuffer* psConstantBuffer;
    uint32_t ui32ConstantBufferSize = 0;
	uint32_t numResourceBindingsNeeded = 0;

    ctab = (ConstantTableD3D9*)data;

    cinfos = (ConstantInfoD3D9*) (data + ctab->constantInfos);

    psInfo->ui32NumConstantBuffers++;

    //Only 1 Constant Table in d3d9
    ASSERT(psInfo->ui32NumConstantBuffers==1);

    psConstantBuffer = new ConstantBuffer[1];

    psInfo->psConstantBuffers = psConstantBuffer;

    psConstantBuffer->Name = "$Globals";

	//Determine how many resource bindings to create
	for(constNum = 0; constNum < ctab->constants; ++constNum)
	{
		if(cinfos[constNum].registerSet == RS_SAMPLER)
		{
			++numResourceBindingsNeeded;
		}
	}

	psInfo->psResourceBindings = new ResourceBinding[numResourceBindingsNeeded];

    for(constNum = 0; constNum < ctab->constants; ++constNum)
    {
		TypeInfoD3D9* typeInfo = (TypeInfoD3D9*) (data + cinfos[constNum].typeInfo);
		ShaderVar var;

		if(cinfos[constNum].registerSet != RS_SAMPLER)
		{
	        var.Name = data + cinfos[constNum].name;
    	    var.ui32Size = cinfos[constNum].registerCount * 16;
       		var.ui32StartOffset = cinfos[constNum].registerIndex * 16;
			var.haveDefaultValue = false;

			if(ui32ConstantBufferSize < (var.ui32Size + var.ui32StartOffset))
			{
				ui32ConstantBufferSize = var.ui32Size + var.ui32StartOffset;
			}

			var.sType.Rows = typeInfo->rows;
			var.sType.Columns = typeInfo->columns;
			var.sType.Elements = typeInfo->elements;
			var.sType.MemberCount = typeInfo->structMembers;
			var.sType.Members = 0;
			var.sType.Offset = 0;
			var.sType.FullName = var.Name;
			var.sType.Parent = 0;
			var.sType.ParentCount = 0;

			switch(typeInfo->typeClass)
			{
				case CLASS_SCALAR:
				{
					var.sType.Class = SVC_SCALAR;
					break;
				}
				case CLASS_VECTOR:
				{
					var.sType.Class = SVC_VECTOR;
					break;
				}
				case CLASS_MATRIX_ROWS:
				{
					var.sType.Class = SVC_MATRIX_ROWS;
					break;
				}
				case CLASS_MATRIX_COLUMNS:
				{
					var.sType.Class = SVC_MATRIX_COLUMNS;
					break;
				}
				case CLASS_OBJECT:
				{
					var.sType.Class = SVC_OBJECT;
					break;
				}
				case CLASS_STRUCT:
				{
					var.sType.Class = SVC_STRUCT;
					break;
				}
			}

			switch(cinfos[constNum].registerSet)
			{
				case RS_BOOL:
				{
					var.sType.Type = SVT_BOOL;
					break;
				}
				case RS_INT4:
				{
					var.sType.Type = SVT_INT;
					break;
				}
				case RS_FLOAT4:
				{
					var.sType.Type = SVT_FLOAT;
					break;
				}
			}
			psConstantBuffer->asVars.push_back(var);
		}
		else
		{
			// Create a resource if it is sampler in order to replicate the d3d10+
			// method of separating samplers from general constants.
			uint32_t ui32ResourceIndex = psInfo->ui32NumResourceBindings++;
			ResourceBinding* res = &psInfo->psResourceBindings[ui32ResourceIndex];

			res->Name = data + cinfos[constNum].name;

			res->ui32BindPoint = cinfos[constNum].registerIndex;
			res->ui32BindCount = cinfos[constNum].registerCount;
			res->ui32Flags = 0;
			res->ui32NumSamples = 1;
			res->ui32ReturnType = 0;

			res->eType = RTYPE_TEXTURE;

			switch(typeInfo->type)
			{
			case PT_SAMPLER:
			case PT_SAMPLER1D:
				res->eDimension = REFLECT_RESOURCE_DIMENSION_TEXTURE1D;
				break;
			case PT_SAMPLER2D:
				res->eDimension = REFLECT_RESOURCE_DIMENSION_TEXTURE2D;
				break;
			case PT_SAMPLER3D:
				res->eDimension = REFLECT_RESOURCE_DIMENSION_TEXTURE3D;
				break;
			case PT_SAMPLERCUBE:
				res->eDimension = REFLECT_RESOURCE_DIMENSION_TEXTURECUBE;
				break;
			}
		}
    }
    psConstantBuffer->ui32TotalSizeInBytes = ui32ConstantBufferSize;
	psConstantBuffer->iUnsized = 0;
}
