#include "DX12ModRuntimeInternal.h"

// A named list called by a key has two independently evaluated bodies.
// Only compile the graph reachable from key bindings. Draw-time plans keep
// their own GPU execution context and never take the key frame mutex.
static void RemoveEmptyKeyBranches(std::vector<Bunny::CommandListAction> &actions)
{
	std::vector<size_t> starts;
	size_t out = 0, lastEffect = 0;
	for (auto &action : actions) {
		if (action.kind != Bunny::CommandListActionKind::ControlFlow) {
			lastEffect = out + 1;
		} else if (action.key.starts_with(L"if ")) {
			starts.push_back(out);
		} else if (action.key == L"endif" && !starts.empty()) {
			const auto start = starts.back();
			starts.pop_back();
			if (lastEffect <= start) {
				out = start;
				continue;
			}
		}
		if (&actions[out] != &action)
			actions[out] = std::move(action);
		++out;
	}
	actions.resize(out);
}

DX12KeyCommandLists BuildKeyCommandLists(const Bunny::IniDocument &ini,
	const std::vector<Bunny::MigotoKeyBindingUtils::Binding> &bindings)
{
	using namespace Bunny;
	using Kind = CommandListActionKind;
	DX12KeyCommandLists lists;
	std::vector<std::wstring> queue;
	for (const auto &binding : bindings)
		queue.insert(queue.end(), binding.runCommandLists.begin(), binding.runCommandLists.end());
	while (!queue.empty()) {
		const auto name = IniDocumentText::ToLower(queue.back());
		queue.pop_back();
		const auto *section = name.empty() ? nullptr : ini.FindSection(name.c_str());
		if (!section || !MigotoCommandListParserText::IsExplicitCommandListSection(name))
			continue;
		auto [it, inserted] = lists.try_emplace(name);
		if (!inserted)
			continue;
		auto &body = it->second;
		for (auto entry : section->entries) {
			const auto key = IniDocumentText::ToLower(entry.key);
			const bool post = key.starts_with(L"post ");
			const bool explicitPhase = post || key.starts_with(L"pre ");
			if (explicitPhase) {
				const auto prefix = post ? 5u : 4u;
				entry.key = IniDocumentText::Trim(entry.key.substr(prefix));
				entry.rawLine = IniDocumentText::Trim(entry.rawLine);
				entry.rawLine.erase(0, prefix);
			}
			CommandListAction action;
			if (!MigotoCommandListParser::ParseSectionAction(entry, action))
				continue;
			if (action.kind == Kind::Run) {
				queue.push_back(action.commandList);
				if (explicitPhase)
					action.key = key; // An explicit run calls both halves of the child.
			}
			if (!explicitPhase && (action.kind == Kind::ControlFlow || action.kind == Kind::Run))
				body.post.push_back(action);
			(post ? body.post : body.pre).push_back(std::move(action));
		}
		RemoveEmptyKeyBranches(body.pre);
		RemoveEmptyKeyBranches(body.post);
		body.vm.PushLocalScope(); // Locals belong to the named list across its two callbacks.
	}
	return lists;
}

void DX12CommandListExecutor::RunKeyCommandPhase(const std::wstring &name, bool post, unsigned depth)
{
	if (depth > 16 || IsRegisteredCustomShaderElement(name))
		return;
	auto it = gKeyCommandLists.find(Bunny::IniDocumentText::ToLower(name));
	if (it == gKeyCommandLists.end() || it->second.running)
		return;
	auto &body = it->second;
	const auto &actions = post ? body.post : body.pre;
	if (actions.empty())
		return;
	BoolScope executing(&body.running, true);
	auto &vm = body.vm;
	vm.branches.clear();
	for (const auto &action : actions) {
		if (ApplyDx12CommandVmAction(vm, action, false) || !vm.Active())
			continue;
		if (action.kind == Bunny::CommandListActionKind::Run) {
			if (action.key == L"run") {
				RunKeyCommandPhase(action.commandList, post, depth + 1);
			} else {
				RunKeyCommandPhase(action.commandList, false, depth + 1);
				RunKeyCommandPhase(action.commandList, true, depth + 1);
			}
		} else if (action.kind == Bunny::CommandListActionKind::ResourceAssignment) {
			if (!RegisterResourceAlias(action))
				DX12_LOG("DX12ResourceAliasDiag", "event=unsupported_copy target=%S (no GPU context in Key)", action.value.c_str());
		}
	}
}
