#include "MigotoCommandList.h"

namespace Bunny {

static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
{
	std::wstring lower = ToLower(value);
	std::wstring lowerPrefix = ToLower(prefix);
	return lower.rfind(lowerPrefix, 0) == 0;
}

static bool EqualsI(const std::wstring &value, const wchar_t *text)
{
	return ToLower(value) == ToLower(text);
}

static bool IsExplicitCommandListSection(const std::wstring &section)
{
	return StartsWithI(section, L"CommandList") ||
		StartsWithI(section, L"BuiltInCommandList") ||
		EqualsI(section, L"Present") ||
		EqualsI(section, L"Constants");
}

static bool ParseVertexBufferTarget(const std::wstring &text, uint32_t *slot)
{
	if (!slot || text.size() < 3 || text[0] != L'v' || text[1] != L'b')
		return false;

	uint32_t parsed = 0;
	for (size_t i = 2; i < text.size(); ++i) {
		if (text[i] < L'0' || text[i] > L'9')
			return false;
		parsed = parsed * 10 + static_cast<uint32_t>(text[i] - L'0');
	}

	*slot = parsed;
	return true;
}

static bool ParseTargetSlot(const std::wstring &text, size_t start, uint32_t *slot)
{
	if (!slot || start >= text.size())
		return false;

	uint32_t parsed = 0;
	for (size_t i = start; i < text.size(); ++i) {
		if (text[i] < L'0' || text[i] > L'9')
			return false;
		parsed = parsed * 10 + static_cast<uint32_t>(text[i] - L'0');
	}

	*slot = parsed;
	return true;
}

static bool ParseShaderStagePrefix(
	const std::wstring &text, size_t *prefixLength, CommandListShaderStage *stage)
{
	if (!prefixLength || !stage || text.size() < 3 || text[2] != L'-')
		return false;

	if (text[0] == L'v' && text[1] == L's') {
		*prefixLength = 3;
		*stage = CommandListShaderStage::Vertex;
		return true;
	}
	if (text[0] == L'p' && text[1] == L's') {
		*prefixLength = 3;
		*stage = CommandListShaderStage::Pixel;
		return true;
	}
	if (text[0] == L'c' && text[1] == L's') {
		*prefixLength = 3;
		*stage = CommandListShaderStage::Compute;
		return true;
	}
	return false;
}

bool ParseCommandListTarget(const std::wstring &text, CommandListTarget *target)
{
	if (!target)
		return false;
	*target = CommandListTarget();

	std::wstring value = ToLower(Trim(text));
	if (value == L"ib") {
		target->kind = CommandListTargetKind::IndexBuffer;
		return true;
	}

	uint32_t slot = 0;
	if (ParseVertexBufferTarget(value, &slot)) {
		target->kind = CommandListTargetKind::VertexBuffer;
		target->slot = slot;
		return true;
	}

	size_t prefixLength = 0;
	CommandListShaderStage stage = CommandListShaderStage::Unknown;
	if (ParseShaderStagePrefix(value, &prefixLength, &stage)) {
		if (value.rfind(L"cb", prefixLength) == prefixLength &&
		    ParseTargetSlot(value, prefixLength + 2, &slot)) {
			target->kind = CommandListTargetKind::ConstantBuffer;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
		if (value.rfind(L"t", prefixLength) == prefixLength &&
		    ParseTargetSlot(value, prefixLength + 1, &slot)) {
			target->kind = CommandListTargetKind::ShaderResource;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
		if (value.rfind(L"u", prefixLength) == prefixLength &&
		    ParseTargetSlot(value, prefixLength + 1, &slot) &&
		    (stage == CommandListShaderStage::Pixel ||
		     stage == CommandListShaderStage::Compute)) {
			target->kind = CommandListTargetKind::UnorderedAccessView;
			target->stage = stage;
			target->slot = slot;
			return true;
		}
	}
	return false;
}

static void AppendRunList(const std::wstring &value, std::vector<std::wstring> *lists)
{
	if (!lists)
		return;
	std::wstring list = Trim(value);
	if (!list.empty())
		lists->push_back(list);
}

static std::wstring ResolveCommandListReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	std::wstring ref = Trim(value);
	if (ref.empty())
		return L"";

	if (IsExplicitCommandListSection(ref))
		return MakeNamespacedSectionName(ref, iniNamespace);
	return ResolveNamespacedSectionReference(ref, L"CommandList", iniNamespace);
}

static std::wstring ResolveResourceReference(
	const std::wstring &value, const std::wstring &iniNamespace)
{
	return ResolveNamespacedSectionReference(value, L"Resource", iniNamespace);
}

static bool ParseUInt(const std::wstring &text, uint32_t *value)
{
	if (!value)
		return false;

	std::wstring trimmed = Trim(text);
	if (trimmed.empty())
		return false;

	wchar_t *end = nullptr;
	unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 0);
	if (!end || *Trim(end).c_str())
		return false;

	*value = static_cast<uint32_t>(parsed);
	return true;
}

static bool ParseDrawArgs(
	const std::wstring &value, uint32_t expectedArgs, CommandListAction *action)
{
	if (!action || expectedArgs > 5)
		return false;

	std::wstring text = value;
	size_t start = 0;
	uint32_t count = 0;
	while (start <= text.size()) {
		size_t comma = text.find(L',', start);
		std::wstring token = comma == std::wstring::npos ?
			text.substr(start) : text.substr(start, comma - start);
		if (count >= expectedArgs)
			return false;
		uint32_t parsed = 0;
		if (!ParseUInt(token, &parsed))
			return false;
		action->args[count++] = parsed;
		if (comma == std::wstring::npos)
			break;
		start = comma + 1;
	}

	if (count != expectedArgs)
		return false;
	action->argCount = count;
	return true;
}

bool ParseCommandListActionFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListAction *action)
{
	if (!action)
		return false;

	if (key == L"run") {
		action->kind = CommandListActionKind::Run;
		action->commandList = value;
		return !action->commandList.empty();
	}

	if (key == L"checktextureoverride") {
		action->kind = CommandListActionKind::CheckTextureOverride;
		return ParseCommandListTarget(value, &action->target);
	}

	if (key == L"handling" && ToLower(value) == L"skip") {
		action->kind = CommandListActionKind::HandlingSkip;
		return true;
	}

	if (key == L"draw") {
		if (ToLower(value) == L"from_caller") {
			action->kind = CommandListActionKind::DrawFromCaller;
			return true;
		}
		action->kind = CommandListActionKind::Draw;
		return ParseDrawArgs(value, 2, action);
	}

	if (key == L"drawindexed") {
		if (ToLower(value) == L"from_caller") {
			action->kind = CommandListActionKind::DrawIndexedFromCaller;
			return true;
		}
		action->kind = CommandListActionKind::DrawIndexed;
		return ParseDrawArgs(value, 3, action);
	}

	if (key == L"dispatch") {
		action->kind = CommandListActionKind::Dispatch;
		return ParseDrawArgs(value, 3, action);
	}

	CommandListTarget target;
	if (ParseCommandListTarget(key, &target)) {
		if (target.kind != CommandListTargetKind::IndexBuffer &&
		    target.kind != CommandListTargetKind::VertexBuffer)
			return false;
		action->kind = target.kind == CommandListTargetKind::IndexBuffer ?
			CommandListActionKind::SetIndexBuffer :
			CommandListActionKind::SetVertexBuffer;
		action->target = target;
		action->resource = value;
		return !action->resource.empty();
	}
	return false;
}

void ParseCommandListLinksFromEntry(
	const std::wstring &key, const std::wstring &value, CommandListLinks *links)
{
	if (!links)
		return;

	std::wstring lower = ToLower(Trim(key));
	if (lower == L"run") {
		AppendRunList(value, &links->main);
		return;
	}
	if (lower == L"pre" || lower == L"pre run" || lower == L"prerun") {
		AppendRunList(value, &links->pre);
		return;
	}
	if (lower == L"post" || lower == L"post run" || lower == L"postrun") {
		AppendRunList(value, &links->post);
		return;
	}
}

static void ResolveCommandListVector(
	std::vector<std::wstring> *lists, const std::wstring &iniNamespace)
{
	if (!lists)
		return;
	for (std::wstring &list : *lists)
		list = ResolveCommandListReference(list, iniNamespace);
}

void ResolveCommandListLinks(
	CommandListLinks *links, const std::wstring &iniNamespace)
{
	if (!links)
		return;
	ResolveCommandListVector(&links->pre, iniNamespace);
	ResolveCommandListVector(&links->main, iniNamespace);
	ResolveCommandListVector(&links->post, iniNamespace);
}

void ResolveCommandListActionReferences(
	CommandListAction *action, const std::wstring &iniNamespace)
{
	if (!action)
		return;
	action->iniNamespace = iniNamespace;
	if (action->kind == CommandListActionKind::Run)
		action->commandList = ResolveCommandListReference(action->commandList, iniNamespace);
	else if (action->kind == CommandListActionKind::SetIndexBuffer ||
	         action->kind == CommandListActionKind::SetVertexBuffer)
		action->resource = ResolveResourceReference(action->resource, iniNamespace);
}

void ParseCommandListSections(const IniDocument &ini, CommandListMap *commandLists)
{
	if (!commandLists)
		return;

	commandLists->clear();
	for (const IniSection &section : ini.Sections()) {
		if (!IsExplicitCommandListSection(section.name))
			continue;

		CommandListConfig config;
		config.section = section.name;
		config.originalSection = section.originalName.empty() ? section.name : section.originalName;
		config.sourcePath = section.sourcePath;
		config.sourceDir = section.sourceDir;
		config.iniNamespace = section.iniNamespace;

		for (const IniEntry &entry : section.entries) {
			if (!entry.hasEquals)
				continue;

			std::wstring key = ToLower(Trim(entry.key));
			std::wstring value = Trim(entry.value);
			CommandListAction action;
			if (ParseCommandListActionFromEntry(key, value, &action)) {
				ResolveCommandListActionReferences(&action, entry.iniNamespace);
				config.actions.push_back(action);
				continue;
			}
		}

		(*commandLists)[config.section] = config;
		if (section.iniNamespace.empty())
			(*commandLists)[config.originalSection] = config;
	}
}

}
