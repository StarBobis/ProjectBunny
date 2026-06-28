#pragma once

#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "IniDocument.h"

namespace Bunny {

enum class CommandListTargetKind
{
	Unknown,
	IndexBuffer,
	VertexBuffer,
	ConstantBuffer,
	ShaderResource,
	UnorderedAccessView
};

enum class CommandListShaderStage
{
	Unknown,
	Vertex,
	Pixel,
	Compute
};

struct CommandListTarget
{
	CommandListTargetKind kind = CommandListTargetKind::Unknown;
	CommandListShaderStage stage = CommandListShaderStage::Unknown;
	uint32_t slot = 0;
};

enum class CommandListActionKind
{
	Run,
	CheckTextureOverride,
	HandlingSkip,
	SetIndexBuffer,
	SetVertexBuffer,
	Draw,
	DrawIndexed,
	DrawFromCaller,
	DrawIndexedFromCaller,
	Dispatch
};

struct CommandListAction
{
	CommandListActionKind kind = CommandListActionKind::Run;
	std::wstring commandList;
	CommandListTarget target;
	std::wstring resource;
	std::wstring iniNamespace;
	uint32_t args[5] = {};
	uint32_t argCount = 0;
};

struct CommandListConfig
{
	std::wstring section;
	std::wstring originalSection;
	std::wstring sourcePath;
	std::wstring sourceDir;
	std::wstring iniNamespace;
	std::vector<CommandListAction> actions;
};

struct CommandListLinks
{
	std::vector<std::wstring> pre;
	std::vector<std::wstring> main;
	std::vector<std::wstring> post;
};

using CommandListMap = std::unordered_map<std::wstring, CommandListConfig>;

bool ParseCommandListTarget(const std::wstring &text, CommandListTarget *target);
bool ParseCommandListActionFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListAction *action);
void ParseCommandListLinksFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListLinks *links);
void ResolveCommandListLinks(
	CommandListLinks *links, const std::wstring &iniNamespace);
void ResolveCommandListActionReferences(
	CommandListAction *action, const std::wstring &iniNamespace);
void ParseCommandListSections(const IniDocument &ini, CommandListMap *commandLists);

}
