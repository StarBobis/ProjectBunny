// Native WARP work with the production readback, shadow and overlay implementations.
#include "DX12Core.h"
#include "DX12Hooks.h"
#include "DX12Tracking.h"
#include "DX12Runtime.h"
#include "DX12Resources.h"
#include "DX12Presentation.h"
#include "ModRuntime/DX12ModRuntimeInternal.h"
#include "Presentation/DX12SwapChainQueues.h"
#include "tests/IniUnaryMathCases.h"
#include "tests/IniArithmeticCases.h"
#include "tests/IniBitwiseCases.h"
#include "tests/IniXInputCases.h"
#include "tests/IniKeyCycleCases.h"
#include "tests/IniKeyTransitionCases.h"
#include "tests/IniInputDelayCases.h"
#include "tests/IniForegroundWindowCases.h"
#include "tests/IniInputScopeCases.h"
#include "tests/IniLogicalCases.h"
#include "tests/IniEarlySettingsCases.h"
#include "tests/IniFileConditionCases.h"
#include <bit>
#include <xmmintrin.h>
#include <wrl/client.h>
#include <d3d12sdklayers.h>
#include <thread>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>

namespace
{
using Microsoft::WRL::ComPtr;

void Require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "Failed assertion: %s\n", message);
		fflush(stderr);
		throw std::runtime_error(message);
	}
}

void TestFileConditions()
{
	using Vm = MigotoCommandVmUtils;
	Vm::SetGlobal(L"$ini_expression_regression_value", 1);
	const auto samples = GetExpressionRandomSampleCount();
	for (const auto &test : migoto::tests::fileConditions) {
		Bunny::IniDocument doc;
		const std::wstring text = std::wstring(L"ConDiTion = ") + test.expression + L"\n[Constants]\nglobal $loaded = 3\n";
		Require(doc.Parse(text.c_str()) == test.validConstant && (doc.FindSection(L"Constants") != nullptr) == test.loads,
			"DX12 file guard determines both file contents and validation status");
	}
	for (bool hunting : {false, true}) {
		Bunny::IniDocument doc;
		Bunny::IniFileParseResult parsed;
		Require(doc.AppendParse(L"condition=!hunting\n[Constants]\n", L"hunt.ini", L"", L"",
			migoto::InputDisableScope::ALL, &parsed, hunting) && parsed.conditionError.empty() == !hunting &&
			(doc.FindSection(L"Constants") != nullptr) == !hunting, "Hunting operands use the load-time runtime state");
	}
	const auto dir = std::filesystem::current_path() /
		(L"file-conditions-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	const auto mods = dir / L"Mods";
	Require(std::filesystem::create_directories(mods), "create file condition fixture");
	const auto root = dir / L"main.ini";
	const auto gated = dir / L"gated.ini";
	const auto child = dir / L"child.ini";
	const auto sibling = mods / L"sibling.ini";
	const auto saved = dir / L"user.ini";
	std::ofstream(root) << "[Include]\ninclude=gated.ini\ninclude_recursive=Mods\nuser_config=user.ini\n"
		"[Constants]\nglobal $loaded=0\nglobal $child=0\nglobal $sibling=0\n";
	std::ofstream(child) << "[Constants]\n$child=8\n";
	std::ofstream(sibling) << "condition=abs(-2) == 2\n[Constants]\n$sibling=3\n";
	std::ofstream(saved) << "[Constants]\n$unknown=12\n";
	auto gate = [&](const char *conditions) {
		std::ofstream(gated) << "namespace=Guarded\n" << conditions <<
			"[Include]\ninclude=child.ini\n[Constants]\n$loaded=5\n[KeyOnly]\nkey=F23\n$loaded=7\n";
	};
	auto load = [&](bool warn, bool active) {
		Bunny::MigotoIniLoadResult tree;
		Require(Bunny::MigotoIniLoader::LoadWithIncludes(root.c_str(), &tree, {}, DX12HuntIsEnabled()) &&
			tree.errors.empty() && tree.includesComplete == !warn && tree.warnings.size() == static_cast<size_t>(warn),
			"file guard diagnostic and include completeness");
		if (warn)
			Require(tree.warnings.front().find(L"gated.ini:2:") != std::wstring::npos, "diagnostic identifies the condition source line");
		bool errors = false;
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, &errors, DX12ModLoadReason::UserReload) && !errors,
			"load siblings after checking the file's condition");
		Vm::State vm;
		Require(vm.Evaluate(L"$loaded") == (active ? 5 : 0) && vm.Evaluate(L"$child") == (active ? 8 : 0) &&
			vm.Evaluate(L"$sibling") == 3 && gKeyBindings.size() == (active ? 1u : 0u),
			"variables, nested includes, keys and independent sibling follow the file guard on reload");
	};
	gate("condition=1\ncondition=-0\ncondition=random(1)\n");
	load(false, false); // A decisive earlier line skips the rest of the file.
	gate("condition=0 || -1\n");
	load(false, true);
	std::ofstream(saved) << "[Constants]\n$unknown=12\n";
	gate("condition=random(1)\n");
	load(true, false);
	load(true, false);
	{
		std::ifstream input(saved);
		std::string bytes{std::istreambuf_iterator<char>(input), {}};
		Require(bytes.find("$unknown=12") != bytes.npos, "invalid guard cannot confirm cleanup for a missing declaration");
	}
	const bool initiallyHunting = DX12HuntIsEnabled();
	gate("condition=!hunting\n");
	load(initiallyHunting, !initiallyHunting);
	DX12HuntToggle();
	load(!initiallyHunting, initiallyHunting);
	DX12HuntToggle();
	gate("condition=nan\n");
	load(false, true);
	const std::string invalidUser = "condition=1 +\n[Constants]\n$unknown=12\n";
	std::ofstream(saved, std::ios::binary) << invalidUser;
	bool errors = false;
	Require(!DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, &errors, DX12ModLoadReason::UserReload) && errors &&
		Vm::State{}.Evaluate(L"$loaded") == 5, "invalid persist-file guard retains the live configuration");
	std::ofstream(root, std::ios::app) << "[IncludeAlias]\ninclude=user.ini\n";
	errors = false;
	Require(!DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, &errors, DX12ModLoadReason::UserReload) && errors &&
		Vm::State{}.Evaluate(L"$loaded") == 5, "an earlier include cannot hide an invalid persist-file guard");
	{
		std::ifstream input(saved, std::ios::binary);
		std::string bytes{std::istreambuf_iterator<char>(input), {}};
		Require(bytes == invalidUser, "failed guarded reload preserves the user settings file byte for byte");
	}
	Require(GetExpressionRandomSampleCount() == samples, "file guard parsing, reload and recovery never sample random");
	for (const auto &path : {saved, sibling, child, gated, root})
		std::filesystem::remove(path);
	std::filesystem::remove(mods);
	std::filesystem::remove(dir);
}

void TestShaderRegexExclusions()
{
	Bunny::IniDocument ini;
	Require(ini.Parse(
		L"[ShaderRegexExcept]\nshader_model=ps_5_0\nexclude_hash=0XAbCd\n"
		L"exclude_hash=abcd\nexclude_hash=ffffffffffffffff\nhandling=skip\n"
		L"[ShaderRegexOther]\nshader_model=ps_5_0\n"
		L"[ShaderRegexInvalid]\nexclude_hash=1234567890abcdef0\nshader_model=ps_5_0\n"),
		"parse shader regex groups");
	std::vector<std::wstring> warnings;
	Bunny::MigotoShaderRegexParser::ParseSections(ini, &gShaderRegexes, warnings);
	Require(warnings.size() == 1 && gShaderRegexes.size() == 2, "discard invalid group only");
	const auto &excluded = gShaderRegexes.at(L"ShaderRegexExcept").excludedHashes;
	Require(excluded.size() == 2 && excluded.contains(UINT64_MAX), "full width and duplicate exclusions");

	std::unordered_set<std::wstring> seen;
	std::vector<Bunny::ShaderOverrideConfig> matches;
	AppendShaderRegexForShaderLocked(0xabcd, "ps_5_0", &matches, &seen);
	Require(matches.size() == 1 && matches.front().section == L"ShaderRegexOther",
		"exclusion is local to one regex group");
	AppendShaderRegexForShaderLocked(0xfedc, "ps_5_0", &matches, &seen);
	Require(matches.size() == 2 && matches.back().handlingSkip,
		"excluded shader cannot suppress a later eligible shader in the same PSO");

	Require(ini.Parse(L"[ShaderRegexExcept]\nshader_model=ps_5_0\n"), "reload regex");
	Bunny::MigotoShaderRegexParser::ParseSections(ini, &gShaderRegexes, warnings);
	seen.clear();
	matches.clear();
	AppendShaderRegexForShaderLocked(0xabcd, "ps_5_0", &matches, &seen);
	Require(matches.size() == 1, "reload drops obsolete exclusions");
	gShaderRegexes.clear();
}

void TestLockedVariables()
{
	using Vm = MigotoCommandVmUtils;
	using Keys = Bunny::MigotoKeyBindingUtils;
	const auto dir = std::filesystem::current_path() /
		(L"variables-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create isolated variable fixture");
	const auto root = dir / L"main.ini";
	const auto user = dir / L"saved.ini";
	const auto write = [&](const wchar_t *file, const char *contents) {
		std::ofstream output(dir / file, std::ios::binary);
		output << contents;
		output.close();
		Require(output.good(), "write variable fixture");
	};
	write(L"main.ini", "[Include]\ninclude=a.ini\ninclude=b.ini\ninclude=c.ini\nuser_config=saved.ini\n"
		"[Constants]\nglobal persist $root=3\n");
	write(L"a.ini", "namespace=Mod A\n[Constants]\n$variant=random(99)\n"
		"persist locked global $variant=1.5\nglobal $variant=9\npost $variant=random(99)\n"
		"global locked $only_locked=3\nglobal $bad=1+\nglobal $bad=6\n"
		"global locked $future=$later\nglobal $future=2\nglobal $later=10\nglobal $probe\n"
		"if $root == 3\n$probe=$future+$bad\nendif\n"
		"[CommandListShadow]\nlocal $variant=4\n$variant=$variant+1\n$probe=$variant\n"
		"post $variant=6\npost $probe=$variant\npost $\\mod a\\variant=random(99)\n"
		"[KeyMode]\nkey=F2\ntype=cycle\ncondition=$probe==6\n"
		"$variant=random(9),2,3\n$probe=6,7\n");
	write(L"b.ini", "namespace=Skin\\\xe7\x8c\xab-mod\n[Constants]\nglobal persist $variant=20\n"
		"global persist $root=4\nglobal $step=2\n"
		"[KeyChange]\nkey=F3\ntype=hold\ncondition=$variant==40\n$variant=$step+3\n");
	write(L"c.ini", "namespace=C\n[Constants]\npersist $variant=30\n");
	write(L"saved.ini", "[Constants]\n$variant=40\n$only_locked=random(99)\n$root=50\n");
	Bunny::CommandListMap lists;
	std::vector<Keys::Binding> bindings;
	auto reload = [&] {
		Bunny::MigotoIniLoadResult config;
		Require(Bunny::MigotoIniLoader::LoadWithIncludes(root.c_str(), &config) &&
			config.errors.empty() && config.warnings.empty(), "load included variable configs and user save");
		Bunny::MigotoCommandListParser::ParseSections(config.document, &lists);
		Bunny::DX12ConstantsVmUtils::Initialize(lists, config.userConfigPath);
		Keys::ParseSections(config.document, &bindings);
	};
	const auto before = GetExpressionRandomSampleCount();
	reload();
	Vm::State vm;
	const std::wstring b = L"Skin\\\u732b-mod";
	Require(vm.Evaluate(L"$\\MOD A\\variant") == 1.5 && vm.Evaluate(L"$variant", b) == 40 &&
		vm.Evaluate(L"$\\c\\variant") == 40 && vm.Evaluate(L"$root") == 50 &&
		vm.Evaluate(L"$root", b) == 4 && vm.Evaluate(L"$probe", L"Mod A") == 8,
		"first valid declaration wins, legacy save fans out and root resolves independently");
	Require(!Vm::PersistDirty() && bindings.size() == 2 && bindings[0].variables.size() == 1 &&
		Keys::CycleValueCount(bindings[0]) == 2, "locked targets create no cycle steps or dirty writes");
	const auto &shadow = lists.at(L"CommandList\\Mod A\\Shadow");
	vm.PushLocalScope();
	for (const auto &action : shadow.actions)
		Require(ApplyDx12CommandVmAction(vm, action, true), "execute parsed variable action");
	Require(vm.Evaluate(L"$variant", L"Mod A") == 5 && vm.Evaluate(L"$variant", b) == 40,
		"a local shadows only the originating namespace");
	vm.FlushPostAssignments();
	vm.PopLocalScope();
	Require(vm.Evaluate(L"$\\mod a\\probe") == 6 && vm.Evaluate(L"$\\mod a\\variant") == 1.5 &&
		Keys::ConditionActive(bindings[0]) && Keys::ConditionActive(bindings[1]),
		"post preserves the local and explicit global resolves through the lock");
	Keys::FrameState keyState;
	auto ignoreCommand = [](const std::wstring &, bool) {};
	Keys::ProcessBindings(bindings, keyState, [](int vk) { return vk == VK_F2 || vk == VK_F3; }, {}, ignoreCommand);
	Require(vm.Evaluate(L"$\\mod a\\probe") == 7 && vm.Evaluate(L"$\\skin\\\u732b-mod\\variant") == 5 &&
		vm.Evaluate(L"$\\c\\variant") == 40 && Vm::PersistDirty(), "key conditions, cycle and hold use owner");
	Keys::ProcessBindings(bindings, keyState, [](int) { return false; }, {}, ignoreCommand);
	Require(vm.Evaluate(L"$variant", b) == 40, "hold release restores its own saved value");
	vm.Assign(L"$variant", L"44", b);
	Vm::SetGlobal(L"$\\mod a\\variant", 0);
	Require(vm.Evaluate(L"$\\mod a\\variant") == 1.5 && GetExpressionRandomSampleCount() == before,
		"locked immediate, post, save and key assignments do not evaluate their right side");
	Require(Vm::SavePersistedVariables(Vm::MakePersistTarget(user.wstring())), "write qualified save");
	std::wstring saved;
	Require(Bunny::MigotoIniLoaderPath::ReadFileText(user.wstring(), &saved) &&
		saved.find(L"$\\skin\\\u732b-mod\\variant = 44") != std::wstring::npos &&
		saved.find(L"$\\c\\variant = 40") != std::wstring::npos &&
		saved.find(L"mod a") == std::wstring::npos, "UTF-8 save contains only mutable persistent targets");
	write(L"a.ini", "namespace=Mod A\n[Constants]\nglobal persist $variant=12\n");
	reload();
	Require(!Vm::PersistDirty() && vm.Evaluate(L"$variant", L"Mod A") == 12 &&
		vm.Evaluate(L"$variant", b) == 44 && vm.Evaluate(L"$root", b) == 4 &&
		vm.Evaluate(L"$\\c\\variant") == 40, "new save round trips and reload replaces flags");
	vm.Assign(L"$variant", L"13", L"Mod A");
	Require(vm.Evaluate(L"$variant", L"Mod A") == 13 && Vm::PersistDirty(), "reloaded declaration is writable");
	Vm::ClearGlobals();
	for (const auto *file : {L"main.ini", L"a.ini", L"b.ini", L"c.ini", L"saved.ini"})
		std::filesystem::remove(dir / file);
	std::filesystem::remove(dir);
}

void TestInactiveUserSettings()
{
	using Vm = MigotoCommandVmUtils;
	const auto dir = std::filesystem::current_path() /
		(L"inactive-settings-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create inactive settings fixture");
	const auto root = dir / L"main.ini";
	const auto settings = dir / L"saved.ini";
	const auto second = dir / L"second.ini";
	const auto mod = dir / L"mod.ini";
	auto configure = [&](bool enabled, bool switchOwner = false) {
		std::ofstream ini(root);
		ini << "[Include]\nuser_config=" << (switchOwner ? "second.ini" : "saved.ini") << "\n";
		if (enabled)
			ini << "include=mod.ini\n";
		ini << "[System]\nclear_unknown_settings=0\n[Constants]\n"
			"global persist $active=3\nglobal locked $fixed=17\nglobal $ordinary=8\n"
			"$from_mod=23\n";
		Require(ini.good(), "write inactive settings config");
	};
	auto fileText = [](const std::filesystem::path &path) {
		std::ifstream input(path, std::ios::binary);
		Require(input.good(), "read saved configuration");
		return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	};
	std::ofstream(mod) << "namespace=Mod X\n[Constants]\nglobal persist $choice=7\n";
	std::ofstream(settings) << "[Constants]\n$active=4\n$fixed=91\n$ordinary=2\n"
		"$\\Mod X\\choice = -0B101\n$infinity=+inf\n$bad=3+4\n$side_effect=random(9)\n"
		"$invalid-name=5\n$poolabsent[02]=21\n$poolabsent[4x]=12\n";
	configure(false);
	const auto randomBefore = GetExpressionRandomSampleCount();
	Require(DX12ModRuntimeLoad(root.c_str()), "initial load with inactive mod");
	Vm::State vm;
	Require(vm.Evaluate(L"$active") == 4 && vm.Evaluate(L"$ordinary") == 8 &&
		vm.Evaluate(L"$fixed") == 17 && vm.Evaluate(L"$\\mod x\\choice") == 0 &&
		GetExpressionRandomSampleCount() == randomBefore, "inactive settings have no live VM effects");
	vm.Assign(L"$active", L"11");
	const auto original = fileText(settings);
	HANDLE blocked = CreateFileW(settings.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(blocked != INVALID_HANDLE_VALUE, "lock settings against replacement");
	const bool reloadFailed = !DX12ModRuntimeReload();
	CloseHandle(blocked);
	Require(reloadFailed && Vm::PersistDirty() && vm.Evaluate(L"$active") == 11 &&
		fileText(settings) == original, "replacement failure retains original file and live values for reload retry");
	Require(DX12ModRuntimeReload() && !Vm::PersistDirty(), "successful retry saves before rebuilding");
	const auto saved = fileText(settings);
	Require(saved.find("$\\mod x\\choice = -5") != saved.npos &&
		saved.find("$poolabsent[2] = 21") != saved.npos && saved.find("$ordinary = 2") != saved.npos &&
		saved.find("$infinity = inf") != saved.npos && saved.find("$active = 11") != saved.npos &&
		saved.find("$fixed") == saved.npos && saved.find("$from_mod") == saved.npos &&
		saved.find("$side_effect") == saved.npos && saved.find("$bad") == saved.npos &&
		saved.find("invalid-name") == saved.npos && saved.find("[4x]") == saved.npos,
		"save keeps only valid numeric user-file settings and current persistent variables");
	configure(true);
	Require(DX12ModRuntimeReload() && vm.Evaluate(L"$\\mod x\\choice") == -5,
		"re-enabled mod recovers its saved value");
	std::ofstream(second) << "[Constants]\n$active=6\n$second_only=31\n";
	configure(false, true);
	blocked = CreateFileW(second.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(blocked != INVALID_HANDLE_VALUE, "deny reads of new settings owner");
	const bool readFailed = !DX12ModRuntimeReload();
	CloseHandle(blocked);
	Require(readFailed && vm.Evaluate(L"$\\mod x\\choice") == -5,
		"unreadable settings keep previous configuration");
	vm.Assign(L"$active", L"13");
	DX12ModSavePersistStateOnExit();
	const auto previousOwner = fileText(settings);
	Require(previousOwner.find("$active = 13") != previousOwner.npos &&
		fileText(second).find("$active=6") != std::string::npos, "failed load retains previous save owner");
	Require(DX12ModRuntimeReload() && vm.Evaluate(L"$active") == 6, "read new owner after retry");
	vm.Assign(L"$active", L"12");
	DX12ModSavePersistStateOnExit();
	const auto other = fileText(second);
	Require(other.find("$second_only = 31") != other.npos && other.find("$active = 12") != other.npos &&
		other.find("choice") == other.npos && other.find("$ordinary") == other.npos &&
		fileText(settings) == previousOwner, "owner switch isolates saved unknowns");
	std::ofstream(second) << "[Constants]\n$active=22\n";
	DX12ModSavePersistStateOnExit();
	Require(DX12ModRuntimeReload() && vm.Evaluate(L"$active") == 22,
		"clean exit and reload preserve externally edited user_config");
	for (const auto &path : {mod, settings, second, root})
		std::filesystem::remove(path);
	std::filesystem::remove(dir);
	Vm::ClearGlobals();
}

void TestSettingsHotkeys()
{
	using Keys = DX12HuntingHotkeys;
	auto &hotkeys = Keys::Instance();
	auto install = [&](const wchar_t *config, std::initializer_list<int> held, size_t expectedWarnings) {
		Bunny::IniDocument ini;
		Require(ini.Parse(config), "parse settings hotkeys");
		std::vector<std::wstring> warnings;
		auto bindings = Keys::Parse(ini, warnings);
		Require(warnings.size() == expectedWarnings, "report invalid settings chords");
		hotkeys.Install(std::move(bindings), [&](int vk) { return std::find(held.begin(), held.end(), vk) != held.end(); });
	};
	auto poll = [&](std::initializer_list<int> held) {
		return hotkeys.Poll([&](int vk) { return std::find(held.begin(), held.end(), vk) != held.end(); }, 1000);
	};
	install(L"[Hunting]\n", {}, 0);
	Require(poll({VK_CONTROL, VK_MENU, VK_F10}) == 0 &&
		poll({VK_DECIMAL, VK_F10}) == 0 && poll({VK_RWIN, VK_F10}) == 0 &&
		poll({VK_F10}) == static_cast<LONG>(DX12UiActionBits::ModReload), "legacy configs keep the unmodified F10 binding only");
	install(L"[Hunting]\nreload_config=\nwipe_user_config=no_shift bogus VK_F10\n", {}, 1);
	Require(poll({VK_F10}) == 0, "empty or malformed chord is disabled as a whole");
	install(L"[Hunting]\nreload_config=Prnt Scrn\nwipe_user_config=no_modifiers\n", {}, 1);
	Require(poll({}) == 0 && poll({VK_SNAPSHOT}) == static_cast<LONG>(DX12UiActionBits::ModReload),
		"legacy key with spaces works and a wipe requires a positive key");
	const auto duplicate = L"[Hunting]\nreload_config=ctrl no_0x41 F7\nwipe_user_config=ctrl no_0x41 F7\n";
	install(duplicate, {VK_CONTROL, VK_F7}, 0);
	Require(poll({VK_CONTROL, VK_F7}) == 0 &&
		poll({VK_CONTROL, 'A', VK_F7}) == 0 &&
		poll({VK_CONTROL, VK_F7}) == (static_cast<LONG>(DX12UiActionBits::ModReload) | static_cast<LONG>(DX12UiActionBits::WipeUserConfig)), "rebind while held waits for release before dispatching shared chords");
	install(duplicate, {}, 0);
	Require(poll({VK_CONTROL, VK_F7}) == 0, "unchanged held binding survives reinstall");
	poll({});
}

class RecordedControllers
{
public:
	inline static migoto::XInputSnapshot hardware;
	inline static std::array<unsigned, XUSER_MAX_COUNT> reads{};

	static DWORD WINAPI Read(DWORD port, XINPUT_STATE *state)
	{
		++reads.at(port);
		const auto &slot = hardware.slots.at(port);
		if (!slot.connected)
			return ERROR_DEVICE_NOT_CONNECTED;
		*state = slot.state;
		return ERROR_SUCCESS;
	}
};

// Run in a fresh process because [Input] input is evaluated only once.
void TestEarlyConfig()
{
	using Window = migoto::tests::InputTestWindow;
	const auto dir = std::filesystem::current_path() /
		(L"early-options-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create early settings fixture");
	const auto root = dir / L"main.ini";
	const auto saved = dir / L"user.ini";
	const std::string original = "[Constants]\n$unloaded=13\n";
	std::ofstream(saved, std::ios::binary) << original;
	auto bytes = [&] {
		std::ifstream file(saved, std::ios::binary);
		Require(file.good(), "read actual persistent settings");
		return std::string(std::istreambuf_iterator<char>(file), {});
	};
	std::ofstream(root) << "[Include]\nuser_config=user.ini\n[System]\n" << migoto::tests::earlySystemInput
		<< "[Hunting]\nnext_pixelshader=K\n" << migoto::tests::earlyRepeat
		<< "[Constants]\n" << migoto::tests::earlyDeclaration;
	const auto samples = GetExpressionRandomSampleCount();
	auto load = [&](DX12ModLoadReason reason) {
		bool warnings = false;
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, &warnings, nullptr, reason) && warnings,
			"load configuration and diagnose invalid early settings");
		Require(GetExpressionRandomSampleCount() == samples, "early bindings reject Mod variables before random sampling");
	};
	load(DX12ModLoadReason::Initial);
	const auto input = DX12UiActions::PollKeyboard([](int vk) { return vk == 'K'; }, 1000, Window::Absent());
	Require(input && input->blocked == migoto::InputDisableScope::NONE && DX12UiActions::PendingActions ==
		static_cast<LONG>(DX12UiActionBits::HuntNextPS), "startup input and foreground defaults allow the real Hunting action");
	InterlockedExchange(&DX12UiActions::PendingActions, 0);
	load(DX12ModLoadReason::UserReload);
	Require(bytes() == original, "first reload with an invalid cleanup option preserves every saved byte");
	load(DX12ModLoadReason::UserReload);
	Require(bytes().find("$unloaded") == std::string::npos, "second identical reload applies the DX11 cleanup default");

	// A literal zero is authoritative over a duplicate root key. An included
	// global can override it. Exercise both against the same real settings file.
	std::ofstream(saved, std::ios::binary) << original;
	const auto include = dir / L"options.ini";
	std::ofstream(include) << "[System]\n";
	std::ofstream(root) << "[Include]\ninclude=options.ini\nuser_config=user.ini\n[System]\n"
		"clear_unknown_settings=0\nclear_unknown_settings=1\n[Constants]\n" << migoto::tests::earlyDeclaration;
	auto reload = [&] { return DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload); };
	Require(reload(), "load duplicate root cleanup settings");
	const auto preserved = bytes();
	Require(preserved.find("$unloaded") != std::string::npos && reload() && bytes() == preserved,
		"first root value preserves unknown settings across reloads");
	std::ofstream(include) << "[System]\nclear_unknown_settings=ON\n";
	Require(reload() && bytes().find("$unloaded") == std::string::npos,
		"included global enables cleanup of the previously confirmed settings");
	std::ofstream(saved, std::ios::binary) << original;
	Require(reload() && bytes() == original, "arm a new unknown-value snapshot");
	std::ofstream file(root);
	file << "[Include]\nuser_config=user.ini\n";
	const float last = migoto::tests::WriteEarlyReloadOptions(file);
	file.close();
	const auto before = GetExpressionRandomSampleCount();
	Require(reload() && GetExpressionRandomSampleCount() == before + 6 &&
		MigotoCommandVmUtils::State{}.Evaluate(L"$early_sample") == last && bytes().find("$unloaded") == std::string::npos,
		"reload samples cleanup before Constants and retains startup-only input");
	const auto allowShortcuts = MigotoCommandVmUtils::runtimeShortcuts.load();
	Require(allowShortcuts && allowShortcuts(), "both Rendering switches receive their own random sample");
	Window window;
	Require(window.Valid(), "create window to check the sampled foreground option");
	auto poll = [&](unsigned tick, migoto::InputWindowSource windowSource) {
		InterlockedExchange(&DX12UiActions::PendingActions, 0);
		DX12UiActions::PollKeyboard([](int key) { return key == VK_F23; }, tick, windowSource);
		return InterlockedExchange(&DX12UiActions::PendingActions, 0);
	};
	const auto next = static_cast<LONG>(DX12UiActionBits::HuntNextPS);
	Require(!poll(3000, Window::Absent()) && poll(3000, Window::Own()) == next &&
		!poll(3199, Window::Own()) && poll(3200, Window::Own()) == next,
		"foreground and Hunting repeat rate use their expected random samples");

	std::filesystem::remove(include);
	std::filesystem::remove(saved);
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
}

void TestConfiguredInputScopes()
{
	using Vm = MigotoCommandVmUtils;
	using Keys = Bunny::MigotoKeyBindingUtils;
	const auto dir = std::filesystem::current_path() /
		(L"scope-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create exclusive scope directory");
	std::filesystem::create_directory(dir / "Mods");
	std::filesystem::create_directory(dir / "More");
	for (const auto &file : migoto::tests::scopeFiles)
		std::ofstream(dir / file.path) << file.body;
	const auto root = dir / L"main.ini";
	bool started = false;
	auto load = [&](const char *initial, const char *mode) {
		std::ofstream(root) << migoto::tests::ScopeConfig("", initial, mode)
			<< "[Hunting]\nanalyse_frame=F23\n";
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr,
			started ? DX12ModLoadReason::UserReload : DX12ModLoadReason::Initial), "load real input scope settings and includes");
		started = true;
	};
	load("off", "mods"); // First configuration published in this process.
	Bunny::MigotoIniLoadResult sources;
	Require(Bunny::MigotoIniLoader::LoadWithIncludes(root.c_str(), &sources) &&
		std::any_of(sources.warnings.begin(), sources.warnings.end(), [](const std::wstring &message) {
			return message.find(L"duplicate [Key\\DirectAlias\\Direct]") != std::wstring::npos;
		}), "duplicate namespaced Key is diagnosed at its source");
	Vm::State vm;
	LONG actions = 0;
	auto frame = [&](std::string_view keys, bool hunt = false) {
		auto read = [&](int vk) {
			return vk == VK_F23 ? hunt : vk >= 'A' && vk <= 'Z' && keys.find(static_cast<char>(vk)) != keys.npos;
		};
		const auto sample = DX12UiActions::PollKeyboard(read, GetTickCount64());
		Require(sample.has_value(), "foreground check is disabled for scope fixture");
		actions = InterlockedExchange(&DX12UiActions::PendingActions, 0);
		Keys::ProcessBindings(gKeyBindings, gKeyFrame, read, sample->controllers,
			[](const std::wstring &command, bool post) { DX12CommandListExecutor::RunKeyCommandPhase(command, post); },
			GetTickCount64, sample->blocked);
	};
	migoto::tests::CheckScopeFrames(frame, load, [&](int rootValue, int direct, int held, int cycle, int child, int deep, const char *why) {
		Require(vm.Evaluate(L"$root_value") == rootValue && vm.Evaluate(L"$direct_value") == direct &&
			vm.Evaluate(L"$held_value") == held && vm.Evaluate(L"$cycle_value") == cycle &&
			vm.Evaluate(L"$child_value") == child && vm.Evaluate(L"$deep_value") == deep, why);
	});

	load("off", "all");
	frame("T", true);
	Require(actions == static_cast<LONG>(DX12UiActionBits::InputToggled), "same-frame all-mode disable gates Hunting before polling");
	const auto settings = dir / L"d3dx_user.ini";
	HANDLE guard = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(guard != INVALID_HANDLE_VALUE, "lock settings during input policy replacement");
	std::ofstream(root) << migoto::tests::ScopeConfig("", "on", "mods");
	const bool reloaded = DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload);
	CloseHandle(guard);
	std::filesystem::remove(settings);
	Require(!reloaded, "abort reload on locked settings");
	frame("R", true);
	Require(!actions && vm.Evaluate(L"$root_value") == 0, "failed reload keeps all mode and its blocked tool keys");
	frame("T", true);
	Require(actions == (static_cast<LONG>(DX12UiActionBits::InputToggled) | static_cast<LONG>(DX12UiActionBits::FrameAnalysis)),
		"toggle re-enables a held Hunting binding in the same frame");
	// Reload while the toggle remains held. DX11 registers a fresh toggle listener.
	load("on", "invalid");
	frame("THR");
	Require(vm.Evaluate(L"$root_value") == 1 && vm.Evaluate(L"$held_value") == 3 &&
		actions == static_cast<LONG>(DX12UiActionBits::InputToggled), "invalid mode defaults to mods and reload resets the toggle edge");
	frame("");
	frame("T");
	frame("");
	bool warnings = true;
	std::ofstream(root) << "[Input]\ninput=bad\ntoggle_input=T\n";
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, &warnings) && !warnings, "reload skips evaluation of startup-only input");
	std::ofstream(root) << "[Input]\ninput=bad\ntoggle_input=no_such_key\n";
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, &warnings) && warnings, "malformed toggle diagnosed");
	frame("T");
	Require(!actions, "removing a toggle cannot leave its old binding active");
	std::ofstream(root) << "[Constants]\n";
	Require(DX12ModRuntimeLoad(root.c_str()), "restore default input configuration");
	for (const auto &file : migoto::tests::scopeFiles)
		std::filesystem::remove(dir / file.path);
	std::filesystem::remove(dir / "Mods");
	std::filesystem::remove(dir / "More");
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
}

void TestControllerInputs()
{
	migoto::test::CheckXInputButtons([](const std::wstring &name, const migoto::XInputSnapshot &pads) {
		migoto::InputCombination combination;
		Require(combination.Parse(name), "parse controller chord");
		return combination.Active([](int) { return false; }, pads);
	}, Require);
	migoto::InputCombination combo;
	for (const wchar_t *invalid : {L"CTRL XB5_A", L"XB_BAD F1", L"XB_RIGHT_TRIGGE",
		L"XB_RIGHT_TRIGGER>foo", L"ctrl XB_RIGHT_TRIGGER>42junk", L"XB_LEFT_TRIGGER>",
		L"XB2_LEFT_TRIGGER>999999999999999999999999"}) {
		Require(!combo.Parse(invalid) && combo.Empty(), "invalid port, button or threshold disables whole chord");
	}
	Require(!combo.Parse(L"no_XB1_B", true) && combo.Empty(), "reset requires a positive keyboard or pad button");
	Require(combo.Parse(L"ctrl XB2_LEFT_TRIGGER > 128 no_XB3_B") && combo.Ports() == 6, "mixed devices and threshold");
	migoto::XInputSnapshot pads;
	pads.slots[1].connected = true;
	pads.slots[1].state.Gamepad.bLeftTrigger = 129;
	auto ctrl = [](int vk) { return vk == VK_CONTROL; };
	Require(!combo.Active(ctrl, pads), "absent inverted port does not qualify");
	pads.slots[2].connected = true;
	Require(combo.Active(ctrl, pads) && !combo.Active([](int) { return false; }, pads), "all mixed chord inputs required");
	pads.slots[2].state.Gamepad.wButtons = XINPUT_GAMEPAD_B;
	Require(!combo.Active(ctrl, pads), "inverted controller cancels mixed chord");

	using Source = RecordedControllers;
	Source::hardware = {};
	Source::reads = {};
	migoto::XInputPoller poller;
	for (unsigned second = 4; second < 8; ++second) {
		poller.Update(second, 15, Source::Read);
		poller.Update(second, 15, Source::Read);
	}
	Require(Source::reads == std::array<unsigned,4>{1,1,1,1}, "disconnected ports staggered over four seconds");
	Source::hardware.slots[1].connected = true;
	poller.Update(9, 2, Source::Read);
	poller.Update(9, 2, Source::Read);
	Require(poller.snapshot.slots[1].connected && Source::reads[1] == 3, "connected port sampled each frame");
	Source::hardware.slots[1].connected = false;
	poller.Update(9, 2, Source::Read);
	poller.Update(9, 2, Source::Read);
	Require(!poller.snapshot.slots[1].connected && Source::reads[1] == 4, "disconnect detected then probes throttled");
	Source::reads = {};
	poller.Update(10, 0, Source::Read, true);
	Require(Source::reads == std::array<unsigned,4>{}, "keyboard-only configuration makes no controller calls");
}

void TestSharedModTriggers()
{
	using Keys = Bunny::MigotoKeyBindingUtils;
	using Vm = MigotoCommandVmUtils;
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[KeyCycle]\nkey=Z\nkey=XB2_A\nkey=XB6_Y Q\nback=XB2_B\nback=VK_ESC\ntype=cycle\n"
		L"$mode=0,1,2\nrun=CommandListZero,CommandListOne,CommandListTwo\n[KeyHeld]\nkey=CTRL H\nkey=XB2_LEFT_TRIGGER > 128\ntype=hold\n$held=9\n"
		L"[KeyToggle]\nkey=T\nkey=XB_Y\ntype=toggle\n$value=10\n"
		L"[KeyMod]\nkey=no_modifiers VK_F1\ntype=activate\n$plain=1\n"
		L"[KeyTrace]\nkey=XB2_X\ncondition=random(1)>=0\n$traced=1\n"), "parse multi-trigger Mod");
	std::vector<Keys::Binding> bindings;
	std::vector<std::wstring> warnings;
	Keys::ParseSections(ini, &bindings, &warnings);
	Require(warnings.size() == 1 && bindings.size() == 5, "bad alternate leaves other triggers usable");
	Vm::State vm;
	vm.Assign(L"$mode", L"0");
	vm.Assign(L"$held", L"4");
	vm.Assign(L"$value", L"3");
	migoto::XInputSnapshot pads;
	pads.slots[1].connected = true;
	auto &pad = pads.slots[1].state.Gamepad;
	Keys::FrameState keyState;
	std::vector<std::wstring> actions;
	auto frame = [&](std::initializer_list<int> keys) {
		std::array<bool,256> sampled{};
		actions.clear();
		Keys::ProcessBindings(bindings, keyState, [&](int vk) {
			Require(!sampled.at(vk), "multiple Mod triggers share the same keyboard poll");
			sampled[vk] = true;
			return std::find(keys.begin(), keys.end(), vk) != keys.end();
		}, pads, [&](const std::wstring &name, bool post) { if (!post) actions.push_back(name); });
	};
	frame({'Z'});
	Require(vm.Evaluate(L"$mode") == 1, "keyboard advances shared cycle");
	pad.wButtons = XINPUT_GAMEPAD_A;
	frame({'Z'});
	frame({'Z'});
	Require(vm.Evaluate(L"$mode") == 2, "a second held device has its own edge on the same cycle");
	pad.wButtons = XINPUT_GAMEPAD_B;
	frame({VK_ESCAPE});
	Require(vm.Evaluate(L"$mode") == 0, "both reverse triggers step the same cycle");
	pad.wButtons = 0;
	frame({});
	pad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
	frame({});
	Require(actions.size() == 2 && !_wcsicmp(actions.front().c_str(), L"CommandListTwo") &&
		!_wcsicmp(actions.back().c_str(), L"CommandListZero"), "cycle-back dispatch precedes forward bindings");
	pad.wButtons = 0;
	frame({VK_LCONTROL, 'H'});
	pad.bLeftTrigger = 129;
	frame({VK_LCONTROL, 'H'});
	Require(vm.Evaluate(L"$held") == 9, "overlapping hold keys activate the same override");
	frame({});
	Require(vm.Evaluate(L"$held") == 4, "release restores first saved value even with another trigger down");
	pad.bLeftTrigger = 0;
	frame({});
	frame({'T'});
	Require(vm.Evaluate(L"$value") == 10, "keyboard turns on shared toggle");
	pad.wButtons = XINPUT_GAMEPAD_Y;
	frame({});
	Require(vm.Evaluate(L"$value") == 3, "controller turns off same toggle");
	frame({VK_LWIN, VK_F1});
	Require(vm.Evaluate(L"$plain") == 0, "no_modifiers excludes Windows keys");
	frame({VK_F1});
	Require(vm.Evaluate(L"$plain") == 1, "unmodified legacy virtual key");
	const auto previousSink = Keys::g_keyPressSink;
	Keys::SetKeyPressSink([](const wchar_t *, const wchar_t *, int, const wchar_t *,
		const wchar_t *, int, const wchar_t *) {});
	const auto samples = GetExpressionRandomSampleCount();
	pad.wButtons = XINPUT_GAMEPAD_X;
	frame({});
	Keys::SetKeyPressSink(previousSink);
	Require(vm.Evaluate(L"$traced") == 1 && GetExpressionRandomSampleCount() == samples + 1,
		"tracing a controller edge does not evaluate its condition twice");
	Vm::ClearGlobals();
}

void TestKeyTransitionClock()
{
	using Keys = Bunny::MigotoKeyBindingUtils;
	using Vm = MigotoCommandVmUtils;
	Bunny::IniDocument doc;
	Require(doc.Parse(L"[KeyCycle]\nkey=A\nback=B\ntype=cycle\n$v=10,20,30\ntransition=100,200,0\n"
		L"transition_type=linear,cosine,linear\n[KeyHold]\nkey=H\ntype=hold\n$v=50\ntransition=5*20\n"
		L"release_transition=200\nrelease_transition_type=cosine\n"
		L"[KeyInvalid]\nkey=I\n$v=11\ntransition_type=bad\ntransition=missing\n"), "parse transition settings");
	Vm::ClearGlobals();
	Vm::State vm;
	vm.Assign(L"$v", L"0");
	std::vector<Keys::Binding> bindings;
	std::vector<std::wstring> warnings;
	Keys::ParseSections(doc, &bindings, &warnings);
	Require(warnings.size() == 2, "diagnose invalid duration and curve");
	Keys::FrameState state;
	auto frame = [&](ULONGLONG tick, int pressed) {
		Keys::ProcessBindings(bindings, state, [&](int vk) { return vk == pressed; }, {},
			[](const std::wstring &, bool) {}, [tick] { return tick; });
		return vm.Evaluate(L"$v");
	};
	Require(frame(100, 'A') == 0 && frame(125, 0) == 2.5, "linear transition publishes an intermediate float");
	Require(frame(150, 'A') == 2.5 && state.pending.at(L"$v").target == 20,
		"smart cycle matches the target and retargets from the last published value");
	const double expected = 2.5 + (20.0 - 2.5) * (1.0 - std::cos(3.141592653589793 / 4)) / 2.0;
	Require(std::abs(frame(200, 0) - expected) < 1e-5 && frame(201, 'A') == 30 && state.pending.empty(),
		"per-step cosine curve and immediate completion");
	Require(frame(300, 'H') == 30 && frame(350, 'H') == 40 && frame(375, 0) == 40, "hold and timed release start from visible value");
	Require(frame(475, 0) == 35 && frame(500, 'H') == 35 && frame(600, 'H') == 50 &&
		frame(610, 0) == 50 && frame(810, 0) == 30 && state.saves.empty() && state.pending.empty(),
		"repress during release saves its destination and restores the original on final release");
	Require(frame(820, 'I') == 11 && state.pending.empty(), "invalid options fall back to immediate linear activation");
	Vm::ClearGlobals();
}

void TestHuntingKeyBindings()
{
	using Keys = DX12HuntingHotkeys;
	using A = DX12UiActionBits;
	auto &hotkeys = Keys::Instance();
	auto install = [&](const std::wstring &body, size_t expectedWarnings, int held = 0) {
		Bunny::IniDocument doc;
		Require(doc.Parse((L"[Hunting]\n" + body).c_str()), "parse Hunting settings");
		std::vector<std::wstring> warnings;
		auto bindings = Keys::Parse(doc, warnings);
		Require(warnings.size() == expectedWarnings, "invalid Hunting setting reported once");
		hotkeys.Install(std::move(bindings), [&](int vk) { return vk == held; });
	};
	auto poll = [&](ULONGLONG tick, std::initializer_list<int> keys) {
		std::array<bool, 256> sampled{};
		return hotkeys.Poll([&](int vk) {
			Require(!sampled.at(vk), "each physical key read at most once per Present poll");
			sampled[vk] = true;
			return std::find(keys.begin(), keys.end(), vk) != keys.end();
		}, tick);
	};
	auto mask = [](A action) { return static_cast<LONG>(action); };
	install(L"", 0);
	Require(!poll(1000, {}), "release all previous bindings");
	Require(poll(1001, {VK_NUMPAD2}) == mask(A::HuntNextPS) && !poll(1100, {VK_NUMPAD2}) &&
		poll(1167, {VK_NUMPAD2}) == mask(A::HuntNextPS), "legacy navigation repeats at six per second");
	Require(!poll(1168, {}) && poll(1169, {VK_NUMPAD2}) == mask(A::HuntNextPS),
		"fresh presses stay immediate in legacy files");
	Require(poll(1170, {VK_NUMPAD2, VK_DECIMAL}) == mask(A::HuntNextCS) &&
		!poll(1171, {VK_NUMPAD2, VK_DECIMAL, VK_LWIN}), "Decimal selects CS and Win suppresses defaults");
	Require(poll(1200, {VK_F9, VK_F11, VK_F12}) ==
		(mask(A::ShaderDump) | mask(A::ProfHudCycle) | mask(A::ProfHudFreeze)), "legacy diagnostic keys");
	poll(1300, {});

	const std::wstring custom = L"next_pixelshader=R\nmark_pixelshader=R\nprevious_vertexshader=\n"
		L"next_vertexshader=bogus F9\nanalyse_frame=ctrl alt F2\nrepeat_rate=2 * 2\n";
	install(custom, 1, 'R');
	Require(!poll(2000, {'R'}) && !poll(4000, {'R'}) && !poll(4100, {}),
		"new held repeatable chord waits for a physical release");
	Require(!poll(4200, {VK_NUMPAD4, VK_NUMPAD5}) &&
		poll(4250, {VK_CONTROL, VK_MENU, VK_F2}) == mask(A::FrameAnalysis),
		"unbound and malformed navigation disabled, frame analysis accepts custom chord");
	Require(poll(4300, {'R'}) == (mask(A::HuntNextPS) | mask(A::HuntMarkPS)) &&
		!poll(4549, {'R'}) && poll(4550, {'R'}) == mask(A::HuntNextPS),
		"four repeats per second from static expression, mark only on first press");
	install(custom, 1);
	Require(!poll(4600, {'R'}) && poll(4800, {'R'}) == mask(A::HuntNextPS),
		"reload preserves held repeat deadline and mark edge");
	Require(!poll(5050, {}) && poll(5060, {'R'}) == mask(A::HuntMarkPS) &&
		poll(5300, {'R'}) == mask(A::HuntNextPS), "explicit rate throttles releases like DX11");
	install(L"next_pixelshader=P\nrepeat_rate=0\n", 0);
	Require(poll(6000, {'P'}) == mask(A::HuntNextPS) && !poll(8000, {'P'}) &&
		!poll(8001, {}) && poll(8002, {'P'}) == mask(A::HuntNextPS), "zero rate disables held repeats");
	install(L"next_pixelshader=Z\nrepeat_rate=$undeclared\n", 1);
	Require(poll(9000, {'Z'}) == mask(A::HuntNextPS) && !poll(9165, {'Z'}) &&
		poll(9166, {'Z'}) == mask(A::HuntNextPS), "invalid rate falls back to legacy cadence");
	install(L"next_pixelshader=Y\nrepeat_rate=99999999999999999999999\n", 0);
	Require(poll(10000, {'Y'}) == mask(A::HuntNextPS) && poll(10000, {'Y'}) == mask(A::HuntNextPS),
		"saturated literal gives a zero-millisecond interval, at most once per poll");
	install(L"next_pixelshader=Y\nrepeat_rate=nan\n", 0);
	Require(!poll(12000, {'Y'}), "static NaN becomes zero, a reload does not invent a press");
	poll(12001, {});

	// Assign each supported action a distinct key. This catches swapped
	// previous/next stages as well as ignored config names.
	constexpr std::pair<const wchar_t *, A> actions[] = {
		{L"previous_vertexbuffer", A::HuntPreviousVB}, {L"next_vertexbuffer", A::HuntNextVB},
		{L"mark_vertexbuffer", A::HuntMarkVB}, {L"previous_indexbuffer", A::HuntPreviousIB},
		{L"next_indexbuffer", A::HuntNextIB}, {L"mark_indexbuffer", A::HuntMarkIB},
		{L"previous_computeshader", A::HuntPreviousCS}, {L"next_computeshader", A::HuntNextCS},
		{L"mark_computeshader", A::HuntMarkCS}, {L"previous_pixelshader", A::HuntPreviousPS},
		{L"next_pixelshader", A::HuntNextPS}, {L"mark_pixelshader", A::HuntMarkPS},
		{L"previous_vertexshader", A::HuntPreviousVS}, {L"next_vertexshader", A::HuntNextVS},
		{L"mark_vertexshader", A::HuntMarkVS}, {L"toggle_hunting", A::HuntToggle},
		{L"done_hunting", A::HuntReset}, {L"analyse_frame", A::FrameAnalysis},
		{L"monitor_performance", A::ProfHudCycle}, {L"freeze_performance_monitor", A::ProfHudFreeze},
	};
	std::wstring body = L"repeat_rate=0\n";
	for (size_t i = 0; i < std::size(actions); ++i) {
		body += actions[i].first;
		body += L'=';
		body += static_cast<wchar_t>(L'A' + i);
		body += L'\n';
	}
	install(body, 0);
	for (size_t i = 0; i < std::size(actions); ++i)
		Require(poll(13000, {'A' + static_cast<int>(i)}) == mask(actions[i].second),
			"configured Hunting name dispatches its own action");
	poll(13001, {});
}

void TestHuntingKeyDispatch(ID3D12PipelineState *pipeline, UINT64 csHash)
{
	const auto dir = std::filesystem::current_path() /
		(L"hunting-keys-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create isolated Hunting config");
	const auto root = dir / L"main.ini";
	std::ofstream(root) << "[Hunting]\ntoggle_hunting=F2\nnext_computeshader=alt F3\ndone_hunting=F4\n"
		"analyse_frame=F5\nrepeat_rate=0\n";
	Require(!DX12HuntIsEnabled() && DX12ModRuntimeLoad(root.c_str()), "load live Hunting bindings");
	auto poll = [](std::initializer_list<int> keys) {
		DX12UiActions::PollKeyboard([&](int vk) { return std::find(keys.begin(), keys.end(), vk) != keys.end(); },
			GetTickCount64());
		return DX12UiActions::Consume();
	};
	poll({});
	Require(!poll({VK_NUMPAD0}) && poll({VK_F5}) && !DX12HuntIsEnabled() && !DX12FrameAnalysis::IsActive(),
		"frame request while hunting is off respects the existing gate");
	Require(poll({VK_F2}) && DX12HuntIsEnabled() && !poll({VK_F2}), "rebound toggle uses one edge");
	DX12HuntObservePipelineState(pipeline);
	Require(!poll({VK_F3}) && poll({VK_MENU, VK_F3}), "select registered WARP compute shader");
	wchar_t overlay[1024]{}, expected[64]{};
	swprintf_s(expected, L"CS %016llx", static_cast<unsigned long long>(csHash));
	DX12HuntComposeOverlayText(overlay, std::size(overlay));
	Require(wcsstr(overlay, expected) != nullptr, "Hunting overlay contains the selected shader hash");
	Require(poll({VK_F4}), "rebound done_hunting dispatched");
	DX12HuntComposeOverlayText(overlay, std::size(overlay));
	Require(wcsstr(overlay, expected) == nullptr && wcsstr(overlay, L"CS:0/1"), "reset disarms real selection");
	std::ofstream(root, std::ios::app) << "toggle_hunting=F6\n";
	Require(DX12ModRuntimeReload(), "publish rebound key on reload");
	poll({});
	Require(!poll({VK_F2}) && poll({VK_F6}) && !DX12HuntIsEnabled(), "new binding replaces the old one");
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
}

void TestWipeUserConfig()
{
	using Vm = MigotoCommandVmUtils;
	const auto dir = std::filesystem::current_path() /
		(L"settings-wipe-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create isolated wipe fixture");
	const auto root = dir / L"main.ini";
	const auto first = dir / L"first.ini";
	const auto second = dir / L"second.ini";
	auto configure = [&](const char *owner, const char *include = "") {
		std::ofstream(root) << "[Include]\nuser_config=" << owner << "\n" << include <<
			"[Hunting]\nreload_config=no_modifiers F6\nwipe_user_config=ctrl alt no_shift F7\n"
			"[Constants]\nglobal persist $choice=3\nglobal locked $fixed=7\n";
	};
	auto press = [](std::initializer_list<int> keys) {
		DX12UiActions::PollKeyboard([&](int vk) { return std::find(keys.begin(), keys.end(), vk) != keys.end(); }, GetTickCount64());
		return DX12UiActions::Consume();
	};
	auto bytes = [](const std::filesystem::path &path) {
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	};
	auto wipe = [] { return DX12ModRuntimeReload(DX12ModLoadReason::WipeUserConfig); };
	configure("first.ini");
	std::ofstream(first) << "[Constants]\n$choice=12\n$inactive=-4\n$fixed=19\n";
	Require(DX12ModRuntimeLoad(root.c_str()), "load real saved settings and configurable keys");
	Vm::State vm;
	Require(vm.Evaluate(L"$choice") == 12, "load saved override before reset");
	press({});
	vm.Assign(L"$choice", L"15");
	Require(press({VK_F6}) && !Vm::PersistDirty() && bytes(first).find("$choice = 15") != std::string::npos,
		"ordinary configured reload saves dirty settings");
	press({});
	vm.Assign(L"$choice", L"18");
	const auto before = bytes(first);
	HANDLE lock = CreateFileW(first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	Require(lock != INVALID_HANDLE_VALUE, "deny deletion of actual settings file");
	const bool refused = !wipe();
	CloseHandle(lock);
	Require(refused && Vm::PersistDirty() && vm.Evaluate(L"$choice") == 18 && bytes(first) == before,
		"failed wipe retains dirty live values and original bytes");

	// Interleave an edit after reading the candidate. A fresh explicit
	// request can discard that edit, a stale publication cannot.
	auto snapshot = Vm::BeginSettingsReload(true);
	Bunny::MigotoIniLoadResult config;
	Require(Bunny::MigotoIniLoader::LoadWithIncludes(root.c_str(), &config, snapshot.wipePath), "stage defaults");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(config.document, &lists);
	vm.Assign(L"$choice", L"21");
	auto stale = Bunny::DX12ConstantsVmUtils::Initialize(lists, config.userConfigPath, nullptr, snapshot);
	Require(stale.status == Vm::SettingsPublishStatus::ChangedDuringLoad && bytes(first) == before &&
		vm.Evaluate(L"$choice") == 21, "concurrent edit cancels stale wipe");

	configure("first.ini", "include=missing.ini\n");
	Require(!wipe() && Vm::PersistDirty() && bytes(first) == before, "incomplete mod tree preserves settings");
	configure("first.ini", "include=first.ini\n");
	Require(!wipe() && bytes(first) == before, "file required by a regular include survives reset");
	configure("first.ini");
	Require(!press({VK_CONTROL, VK_MENU, VK_SHIFT, VK_F7}) && press({VK_CONTROL, VK_MENU, VK_F7}) &&
		!std::filesystem::exists(first) && !Vm::PersistDirty() && vm.Evaluate(L"$choice") == 3 &&
		vm.Evaluate(L"$fixed") == 7, "configured wipe skips the dirty save and restores declared defaults");
	Require(!press({VK_CONTROL, VK_MENU, VK_F7}), "held key across a real reload cannot wipe twice");
	press({});
	DX12ModSavePersistStateOnExit();
	Require(!std::filesystem::exists(first) && wipe(), "clean exit and reset of an absent file");
	vm.Assign(L"$choice", L"8");
	DX12ModSavePersistStateOnExit();
	Require(bytes(first).find("inactive") == std::string::npos, "discarded unknown settings stay discarded");

	std::ofstream(second) << "[Constants]\n$choice=41\n";
	const auto other = bytes(second);
	configure("second.ini");
	Require(wipe() && !std::filesystem::exists(first) && bytes(second) == other &&
		vm.Evaluate(L"$choice") == 41, "wipe deletes prior owner and loads the new owner's existing settings");
	configure("main.ini");
	Require(DX12ModRuntimeLoad(root.c_str()), "load config with an overlapping save path");
	const auto mainBytes = bytes(root);
	Require(!wipe() && bytes(root) == mainBytes, "wipe cannot delete root configuration");
	for (const auto &file : {second, root})
		std::filesystem::remove(file);
	std::filesystem::remove(dir);
	Vm::ClearGlobals();
}

void TestUnknownSettingsCleanup()
{
	const auto dir = std::filesystem::current_path() /
		(L"settings-cleanup-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create cleanup fixture");
	const auto root = dir / L"main.ini";
	const auto first = dir / L"first.ini";
	const auto second = dir / L"second.ini";
	auto configure = [&](const char *owner, int generation, const char *option = "", const char *include = "") {
		std::ofstream file(root);
		file << "[Include]\nuser_config=" << owner << "\n" << include << "[System]\n" << option <<
			"[Constants]\nglobal $generation=" << generation << "\n[PoolKeep]\n";
	};
	auto settings = [](const std::filesystem::path &path, const char *zero) {
		std::ofstream file(path);
		file << "[Constants]\n$missing=" << zero << "\n$old_nan=nan\n$poolabsent[3]=7\n$poolkeep[3]=4\n";
	};
	auto bytes = [](const std::filesystem::path &path) {
		std::ifstream file(path, std::ios::binary);
		Require(file.good(), "read cleanup fixture");
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	};
	auto notice = [](const wchar_t *expected) {
		wchar_t status[512]{};
		Require(DX12OverlayStatusState::GetTransientStatus(status, std::size(status), nullptr) &&
			wcsstr(status, expected), "reload overlay explains pending or completed cleanup");
	};

	configure("first.ini", 1);
	settings(first, "-0");
	const auto original = bytes(first);
	Require(DX12ModRuntimeLoad(root.c_str()) && DX12ModRuntimeReload(DX12ModLoadReason::Initial),
		"load startup config without arming cleanup");
	DX12Input::CompleteReload();
	notice(L"Kept 3 unknown");
	Require(bytes(first) == original, "first explicit reload preserves exact bytes");
	settings(first, "+0");
	Require(DX12ModRuntimeReload() && bytes(first).find("$missing=+0") != std::string::npos,
		"changing only the sign bit starts another grace period");
	notice(L"Another reload");
	const auto lastGood = bytes(first);
	configure("FIRST.ini", 2);
	HANDLE locked = CreateFileW(first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(locked != INVALID_HANDLE_VALUE, "deny replacement of cleanup target");
	const bool failed = !DX12ModRuntimeReload();
	CloseHandle(locked);
	MigotoCommandVmUtils::State vm;
	Require(failed && bytes(first) == lastGood && vm.Evaluate(L"$generation") == 1,
		"failed cleanup keeps previous file, confirmation and active generation");
	Require(DX12ModRuntimeReload() && vm.Evaluate(L"$generation") == 2, "retry confirmed cleanup");
	notice(L"Cleared 3 unknown");
	const auto cleaned = bytes(first);
	Require(cleaned.find("$missing") == cleaned.npos && cleaned.find("$old_nan") == cleaned.npos &&
		cleaned.find("$poolabsent") == cleaned.npos && cleaned.find("$poolkeep[3] = 4") != cleaned.npos,
		"bit-identical NaNs confirm, absent pool slots clear, declared pool slots survive");

	// An identical set in another owner needs its own confirmation.
	settings(first, "0");
	Require(DX12ModRuntimeReload(), "arm new set in first owner");
	settings(second, "0");
	const auto secondInitial = bytes(second);
	configure("second.ini", 3);
	Require(DX12ModRuntimeReload() && bytes(second) == secondInitial, "confirmation stays with its owner");
	configure("second.ini", 4, "", "include=missing.ini\n");
	Require(DX12ModRuntimeReload() && bytes(second) == secondInitial, "failed include cannot confirm deletion");
	configure("second.ini", 5);
	Require(DX12ModRuntimeReload() && bytes(second) == secondInitial, "first complete reload confirms again");
	// Re-enabling one declaration changes the unknown set before cleanup.
	std::ofstream(root, std::ios::app) << "[Constants]\nglobal persist $missing=9\n";
	Require(DX12ModRuntimeReload() && vm.Evaluate(L"$missing") == 0 && bytes(second) == secondInitial,
		"re-enabled mod claims its saved value before comparing snapshots");
	Require(DX12ModRuntimeReload() && bytes(second).find("$missing = 0") != std::string::npos &&
		bytes(second).find("$old_nan") == std::string::npos, "only the still-unknown values are removed");

	configure("second.ini", 6, "clear_unknown_settings = (3 - 3)\n");
	settings(second, "0");
	Require(DX12ModRuntimeReload() && DX12ModRuntimeReload() &&
		bytes(second).find("$old_nan") != std::string::npos, "false static expression preserves unknowns");
	notice(L"Kept 3 unknown");
	configure("second.ini", 7, "clear_unknown_settings = yes\n");
	Require(DX12ModRuntimeReload() && bytes(second).find("$old_nan") == std::string::npos,
		"enabling cleanup uses the last identical explicit snapshot");
	configure("second.ini", 8);
	std::ofstream(second) << "[Constants]\n$last=1\n";
	Require(DX12ModRuntimeReload() && DX12ModRuntimeReload() &&
		bytes(second).find("[Constants]") != std::string::npos && bytes(second).find("$last") == std::string::npos,
		"unknown-only save can commit an empty Constants section");
	// Deterministically interleave a normal autosave between a reload's file
	// read and publication. The dirty bit alone no longer records that edit.
	std::ofstream(root, std::ios::app) << "[Constants]\nglobal persist $active=1\n";
	Require(DX12ModRuntimeReload(), "load mutable variable for interleaved save");
	const auto revision = MigotoCommandVmUtils::BeginSettingsReload();
	Bunny::MigotoIniLoadResult config;
	Require(Bunny::MigotoIniLoader::LoadWithIncludes(root.c_str(), &config), "read candidate configuration");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(config.document, &lists);
	vm.Assign(L"$active", L"42");
	const auto dirtyRevision = MigotoCommandVmUtils::BeginSettingsReload();
	DX12ModSavePersistStateOnExit();
	const auto latest = bytes(second);
	const auto stale = Bunny::DX12ConstantsVmUtils::Initialize(lists, config.userConfigPath,
		&config.document, revision);
	Require(stale.status == MigotoCommandVmUtils::SettingsPublishStatus::ChangedDuringLoad &&
		vm.Evaluate(L"$active") == 42 && latest.find("$active = 42") != latest.npos &&
		bytes(second) == latest, "stale reload cannot replace either live state or an intervening save");
	const auto unsavedAtRead = Bunny::DX12ConstantsVmUtils::Initialize(lists, config.userConfigPath,
		&config.document, dirtyRevision);
	Require(unsavedAtRead.status == MigotoCommandVmUtils::SettingsPublishStatus::ChangedDuringLoad &&
		vm.Evaluate(L"$active") == 42, "a save during the read also invalidates a previously dirty snapshot");
	for (const auto &file : {root, first, second})
		std::filesystem::remove(file);
	std::filesystem::remove(dir);
	MigotoCommandVmUtils::ClearGlobals();
}

// A signalling NaN stored as a VM double raises MXCSR invalid when an actual
// lookup converts it to float. This observes the production read without hooks.
bool LogicalReadRaisesInvalid(MigotoCommandVmUtils::State &vm, const std::wstring &text, double expected)
{
	const auto saved = _mm_getcsr();
	_mm_setcsr((saved | _MM_MASK_INVALID) & ~_MM_EXCEPT_INVALID);
	const auto result = vm.Evaluate(text);
	const bool invalid = (_mm_getcsr() & _MM_EXCEPT_INVALID) != 0;
	_mm_setcsr(saved);
	Require(result == expected, "logical probe result");
	return invalid;
}

void TestLogicalExpressions()
{
	using Vm = MigotoCommandVmUtils;
	const auto dir = std::filesystem::current_path() /
		(L"logical-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create logical fixture directory");
	const auto root = dir / L"main.ini";
	auto load = [&](const char *rendering, const char *logging = "") {
		std::ofstream(root) << "[Rendering]\n" << rendering << "[Logging]\n" << logging
			<< "[Constants]\n" << migoto::tests::logicalPolicyDeclaration << "global $result=0\n$result=1 || random(1)\n";
		WritePrivateProfileStringW(nullptr, nullptr, nullptr, root.c_str());
		const auto before = GetExpressionRandomSampleCount();
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload) &&
			GetExpressionRandomSampleCount() == before + 1 && Vm::LookupVariable({}, L"$result") == 1,
			"install logical settings and replay eager random in Constants");
	};
	load(migoto::tests::logicalPolicyKey);
	Vm::State vm;
	vm.PushLocalScope();
	vm.localScopes.back()[L"$ini_expression_regression_value"] = std::bit_cast<double>(UINT64_C(0x7ff0000000000001));
	const std::wstring marker = L"$ini_expression_regression_value";
	Require(LogicalReadRaisesInvalid(vm, L"1 && " + marker, 1), "needed signalling variable is really read");
	for (const auto &test : migoto::tests::logicalCases)
		Require(LogicalReadRaisesInvalid(vm, migoto::tests::GuardedOperand(test), 0) == !test.safe,
			"DX12 skips exactly the eligible DX11 subtrees");
	migoto::tests::CheckLogicalRandomOrder([&](const std::wstring &text) { return vm.Evaluate(text); }, Require);
	const auto guard = L"1 || " + marker;
	Require(!LogicalReadRaisesInvalid(vm, guard, 1), "true OR skips scalar read");
	load(migoto::tests::logicalPolicyKey);
	Require(vm.Evaluate(L"$logical_policy_flag") == 0 && !LogicalReadRaisesInvalid(vm, guard, 1),
		"early Rendering settings cannot see the outgoing or newly declared variable");
	load("expression_short_circuit=random(1) < $logical_policy_flag\n");
	Require(!LogicalReadRaisesInvalid(vm, guard, 1), "unbound early setting defaults before sampling random");
	load("expression_short_circuit=1 < 2\n");
	Require(!LogicalReadRaisesInvalid(vm, L"0 && " + marker + L" && random(1)", 0),
		"eligible inner prefix still skips when outer operator must evaluate eagerly");
	Require(LogicalReadRaisesInvalid(vm, L"(random(1) >= 0) || " + marker, 1), "unsafe left side forbids skipping the right side");
	load("expression_short_circuit=0\n");
	Require(LogicalReadRaisesInvalid(vm, guard, 1), "per-feature opt-out uses the eager path");
	load("mod_runtime_optimizations=0\nexpression_short_circuit=true\n");
	Require(LogicalReadRaisesInvalid(vm, guard, 1), "global runtime opt-out uses the eager path");
	load("", "log_level=debug\n");
	Require(LogicalReadRaisesInvalid(vm, guard, 1), "debug logging forces eager evaluation");
	load("", "calls=1\ndebug=1\n");
	Require(LogicalReadRaisesInvalid(vm, guard, 1), "legacy debug logging forces eager evaluation");
	load("", "log_level=info\ncalls=1\ndebug=1\n");
	Require(!LogicalReadRaisesInvalid(vm, guard, 1), "explicit nondebug level restores the shortcut");
	const auto settings = dir / L"d3dx_user.ini";
	HANDLE locked = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(locked != INVALID_HANDLE_VALUE, "exclusively lock user settings for a failed expression reload");
	std::ofstream(root) << "[Rendering]\nexpression_short_circuit=0\n";
	const bool applied = DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload);
	CloseHandle(locked);
	std::filesystem::remove(settings);
	Require(!applied && !LogicalReadRaisesInvalid(vm, guard, 1), "failed reload keeps the old expression policy");
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload) &&
		LogicalReadRaisesInvalid(vm, guard, 1), "retry publishes the eager policy for the same cached expression");
	load("");
	Require(DX12FrameAnalysisState::Begin(), "begin real frame analysis for expression diagnostics");
	const bool analysisRead = LogicalReadRaisesInvalid(vm, guard, 1);
	DX12FrameAnalysisState::End();
	Require(analysisRead && !LogicalReadRaisesInvalid(vm, guard, 1), "capture start and end affect already cached expressions");
	Require(!Vm::ReadStaticIniExpression(L"1 || $undeclared"), "static settings validate and evaluate every variable");
	auto context = vm.Context();
	context.requireDeclaredVariables = true;
	Vm::ExpressionParser strict(guard, context);
	Require(strict.Parse() == 1 && strict.ok, "strict context evaluates a declared local");
	const std::wstring bad = L"1 || (random(1) && 2 +)";
	const auto samples = GetExpressionRandomSampleCount();
	Vm::ExpressionParser invalid(bad, vm.Context());
	Require(invalid.Parse() == 0 && !invalid.ok && GetExpressionRandomSampleCount() == samples,
		"invalid skipped syntax cannot consume a random sample");
	std::wstring changing = L"0 && " + marker;
	Require(!LogicalReadRaisesInvalid(vm, changing, 0), "first plan for mutable source");
	changing[0] = L'1';
	Require(LogicalReadRaisesInvalid(vm, changing, 1), "cache keys compare content after buffer reuse");
	std::wstring longChain = L"0";
	for (unsigned i = 0; i < 1300; ++i) longChain += L" && " + marker;
	Require(!LogicalReadRaisesInvalid(vm, longChain, 0), "long flat chain keeps bounded call depth and skips every right read");
	Require(!LogicalReadRaisesInvalid(vm, L"(0 && " + marker + L") + (1 || " + marker + L")", 1),
		"separate logical groups within arithmetic consume their exact operands");
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
}

void TestExpressionNumbers()
{
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Constants]\nglobal $mask = 0b101 + 0B11 * 2\n"
		L"global $power = 2**3**2\nglobal $neg = -2**2 + 4**-1\n"
		L"global $signed = 0 !== -0\nglobal $equal = $mask === 11\n"
		L"global $shift = 1 << 2\nglobal $flagged = ($mask ^ $shift) & 15\n"
		L"global $large = 16777216\nlocal $next = $large + 1\n"
		L"global $tiny = 0.00000025\nif $tiny\nx7 = 1 / $tiny\nendif\n"
		L"global $delta = -1\nif $large == $next\n$delta = $next - $large\nendif\n"),
		"parse numeric expressions");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Bunny::DX12ConstantsVmUtils::Initialize(lists);
	MigotoCommandVmUtils::State vm;
	Require(vm.Evaluate(L"$mask") == 11 && vm.Evaluate(L"$power") == 512 &&
		vm.Evaluate(L"$neg") == 4.25, "binary literals and DX11 exponent precedence");
	Require(vm.Evaluate(L"$signed") == 1 && vm.Evaluate(L"$equal") == 1 &&
		vm.Evaluate(L"1 === 1.000000001") == 1, "bit comparison uses float32");
	Require(vm.Evaluate(L"$flagged") == 15, "bitwise operators execute through Constants with variable operands");
	for (const auto &test : migoto::test::BitwiseCases) {
		const std::wstring expression(test.expression);
		MigotoCommandVmUtils::ExpressionParser parser(expression, vm.Context());
		const auto actual = parser.Parse();
		if (!parser.ok || actual != test.expected)
			std::fprintf(stderr, "DX12 bitwise %ls produced %.9g, expected %.9g\n", test.expression, actual, test.expected);
		Require(parser.ok && actual == test.expected, "bitwise precedence, chaining, rounding and boundaries");
	}
	for (const auto &test : migoto::test::ArithmeticCases) {
		const std::wstring expression(test.expression);
		MigotoCommandVmUtils::ExpressionParser parser(expression, vm.Context());
		const double actual = parser.Parse();
		if (!parser.ok || !test.Matches(actual))
			std::fprintf(stderr, "DX12 %ls produced %.17g, expected %.9g\n", test.expression, actual, test.expected);
		Require(parser.ok && test.Matches(actual), "float32 arithmetic, comparison and truth values");
	}
	Require(vm.Evaluate(L"$delta") == 0 && vm.Evaluate(L"x7") == 4000000,
		"Constants locals, conditions and shader constants use single-precision expressions");
	MigotoCommandVmUtils::SetGlobal(L"$external", 16777217.0);
	vm.Assign(L"$rounded", L"$external");
	Require(MigotoCommandVmUtils::LookupVariable({}, L"$rounded") == 16777216.0,
		"externally published operands round on entry to the expression");
	const std::wstring maximum = L"0b" + std::wstring(64, L'1');
	Require(vm.Evaluate(maximum) == static_cast<float>(UINT64_MAX), "maximum binary literal");
	for (const std::wstring &invalid : {maximum + L"1", std::wstring(L"0b"),
		std::wstring(L"0b102"), std::wstring(L"2**"), std::wstring(L"1 & & 2"),
		std::wstring(L"1 ||| 2"), std::wstring(L"1 <<"), std::wstring(L"~")}) {
		MigotoCommandVmUtils::ExpressionParser parser(invalid, vm.Context());
		Require(parser.Parse() == 0 && !parser.ok, "malformed expression rejected");
	}
	MigotoCommandVmUtils::ClearGlobals();
}

void Check(HRESULT hr)
{
	if (FAILED(hr)) {
		fprintf(stderr, "HRESULT 0x%08lx\n", hr);
		throw std::runtime_error("D3D12 call failed");
	}
}

void TestUnaryMath()
{
	MigotoCommandVmUtils::State vm;
	for (const auto &test : migoto::test::UnaryMathCases) {
		const double result = vm.Evaluate(test.expression);
		Require(std::isfinite(result) &&
			std::abs(result - test.expected) <= 1e-5 * (1 + std::abs(test.expected)),
			"shared DX11 unary math case");
	}
	Require(std::isnan(vm.Evaluate(L"sin(inf)")) && std::isnan(vm.Evaluate(L"sqrt(-1)")) &&
		std::isinf(vm.Evaluate(L"rsqrt(0)")), "IEEE math domain and pole results");
	vm.PushLocalScope();
	vm.DeclareLocal(L"local $input", L"-3");
	Require(vm.Evaluate(L"ABS($input) + SQRT(4)") == 5, "case insensitive names and VM variables");
	for (std::wstring invalid : {L"sqrt()", L"sin(1, 2)", L"unknown(2)", L"abs(-2"}) {
		MigotoCommandVmUtils::ExpressionParser parser(invalid, vm.Context());
		Require(parser.Parse() == 0 && !parser.ok, "invalid unary call rejected");
	}
}

void TestFrameTiming()
{
	MigotoCommandVmUtils::State vm;
	Require(vm.Evaluate(L"time + frame_time + frame_number + fps") == 0,
		"timing operands start at zero before the first Present");
	const uint64_t start = migoto::GetSystemTicks();
	DX12RuntimeState::BeginPresent(start);
	const double firstTime = vm.Evaluate(L"time");
	Require(firstTime >= 0 && vm.Evaluate(L"frame_time") == firstTime && vm.Evaluate(L"fps") == 0,
		"first Present measures time since launch and initializes the FPS counter");
	DX12RuntimeState::IncrementPresentCount(start);
	DX12RuntimeState::BeginPresent(start + 10000);
	Require(vm.Evaluate(L"frame_number") == 1 && vm.Evaluate(L"frame_time") == 0.01f &&
		std::abs(vm.Evaluate(L"fps") - 100) < 0.001, "second Present publishes delta in seconds and FPS");
	const std::wstring operand = L"frame_number";
	MigotoCommandVmUtils::ExpressionParser ongoing(operand, vm.Context());
	Require(ongoing.LookupIdentifier(L"FRAME_TIME") == 0.01f, "capture this expression's timing sample");
	DX12RuntimeState::IncrementPresentCount(start + 10000);
	Require(ongoing.Parse() == 1 && vm.Evaluate(operand) == 2, "a running expression sees one complete frame");
	DX12RuntimeState::BeginPresent(start + 30000);
	const double smoothedFps = vm.Evaluate(L"fps");
	Require(vm.Evaluate(L"frame_time") == 0.02f && std::abs(smoothedFps - 1 / 0.011) < 0.001 &&
		std::abs(vm.Evaluate(L"time") - firstTime - 0.03) < 1e-5, "elapsed time and DX11 EMA smoothing");
	DX12RuntimeState::BeginPresent(start + 30000);
	Require(vm.Evaluate(L"frame_time") == 0 && vm.Evaluate(L"fps") == smoothedFps, "duplicate tick preserves FPS");
	DX12RuntimeState::BeginPresent(start + 2030000);
	Require(vm.Evaluate(L"frame_time") == 2 && vm.Evaluate(L"fps") == smoothedFps, "long frame preserves FPS");

	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Constants]\nglobal $observed = frame_number + frame_time\n"), "parse frame operands");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Bunny::DX12ConstantsVmUtils::Initialize(lists);
	Require(vm.Evaluate(L"$observed") == 4 && vm.Evaluate(L"fps") == smoothedFps,
		"reloading Constants preserves the frame sample");
	DX12RuntimeState::BeginPresent(0);
	Require(vm.Evaluate(L"time + frame_time") == 0 && vm.Evaluate(L"fps") == smoothedFps,
		"backwards timestamp clamps time at zero");
	MigotoCommandVmUtils::ClearGlobals();
}

void TestRandomExpression()
{
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Constants]\nglobal $bound = 8\nglobal $sample = RaNdOm(-$bound)\n"
		L"[CommandListBadDraw]\ndrawinstanced = 3,random(2),0,1+\n"), "parse random configuration");
	const uint32_t before = GetExpressionRandomSampleCount();
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Require(lists.at(L"CommandListBadDraw").actions.empty() && GetExpressionRandomSampleCount() == before,
		"invalid draw arguments do not sample random during load or playback");
	Bunny::DX12ConstantsVmUtils::Initialize(lists);
	MigotoCommandVmUtils::State vm;
	const double sample = vm.Evaluate(L"$sample");
	Require(GetExpressionRandomSampleCount() == before + 1 && sample <= 0 && sample > -8,
		"Constants evaluates a variable random bound exactly once");
	Require(vm.Evaluate(L"random(0)") == 0 && std::isnan(vm.Evaluate(L"random(inf)")) &&
		std::isnan(vm.Evaluate(L"random(nan)")) && GetExpressionRandomSampleCount() == before + 1,
		"zero and nonfinite bounds do not consume samples");
	for (const std::wstring invalid : {L"random()", L"random(2,3)", L"random(2) +",
		L"random(2) + typo(3)", L"random(random(2)", L"random + 1"}) {
		MigotoCommandVmUtils::ExpressionParser parser(invalid, vm.Context());
		Require(parser.Parse() == 0 && !parser.ok, "malformed random expression rejected");
	}
	Require(GetExpressionRandomSampleCount() == before + 1, "invalid expressions leave the sample stream untouched");

	// Replay the next two native samples at the same generator inputs through the VM.
	const auto seedNext = []() {
		SeedExpressionRandom(73u - 0x9e3779b9u * GetExpressionRandomSampleCount(), 41);
	};
	seedNext();
	const float inner = RandomExpressionValue(8.0f);
	const float expected = RandomExpressionValue(inner);
	Require(inner > 0, "nested random fixture has a nonzero bound");
	seedNext();
	Require(vm.Evaluate(L"random(random($bound))") == expected && GetExpressionRandomSampleCount() == before + 5,
		"nested VM calls consume the shared samples in argument order");
	Require(vm.Evaluate(L"1 || random(1)") == 1 && GetExpressionRandomSampleCount() == before + 6,
		"observable random on the right of a logical operator runs once");
	const uint32_t frame = static_cast<uint32_t>(DX12RuntimeState::GetPresentCount()) + 1u;
	const uint64_t ticks = 0x12345678abcdef01ull;
	// Offset the reference frame by one call so the following sample has
	// the same hash input after the real frame boundary advances the seed.
	SeedExpressionRandom(frame + 0x9e3779b9u, static_cast<uint32_t>(ticks));
	const float nextFrame = RandomExpressionValue(16);
	Require(static_cast<uint32_t>(DX12RuntimeState::IncrementPresentCount(ticks)) == frame &&
		vm.Evaluate(L"random(16)") == nextFrame && GetExpressionRandomSampleCount() == before + 8,
		"frame and low clock bits seed the next sample without resetting the ordinal");
	MigotoCommandVmUtils::ClearGlobals();
}

ComPtr<ID3D12CommandQueue> Queue(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
{
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	ComPtr<ID3D12CommandQueue> queue;
	Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
	return queue;
}

void Drain(ID3D12Device *device, ID3D12CommandQueue *queue)
{
	ComPtr<ID3D12Fence> fence;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	Check(queue->Signal(fence.Get(), 1));
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	Require(event != nullptr, "completion event");
	Check(fence->SetEventOnCompletion(1, event));
	DWORD result = WaitForSingleObject(event, 5000);
	CloseHandle(event);
	Require(result == WAIT_OBJECT_0 && fence->GetCompletedValue() == 1, "GPU completion");
}

struct List
{
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> list;

	List(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
	{
		Check(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator)));
		Check(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
	}
	~List() { DX12ReadbackForgetRecording(list.Get()); }

	void Submit(ID3D12CommandQueue *queue)
	{
		ID3D12CommandList *batch[] = {list.Get()};
		DX12ReadbackExecute(queue, 1, batch, [](ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists) {
			q->ExecuteCommandLists(n, lists);
		});
	}
};

ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, D3D12_HEAP_TYPE type, UINT64 width = 256)
{
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = type;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = width;
	desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> resource;
	const auto state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ :
		D3D12_RESOURCE_STATE_COPY_DEST;
	Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
		IID_PPV_ARGS(&resource)));
	return resource;
}

void Write(ID3D12Resource *resource, UINT value)
{
	void *ptr = nullptr;
	D3D12_RANGE noRead = {};
	Check(resource->Map(0, &noRead, &ptr));
	memcpy(ptr, &value, sizeof(value));
	D3D12_RANGE written = {0, sizeof(value)};
	resource->Unmap(0, &written);
}

bool HasValue(ID3D12Resource *target, UINT value)
{
	const uint8_t *bytes = nullptr;
	size_t size = 0;
	return DX12TryGetBufferContent(target->GetGPUVirtualAddress(), sizeof(value), &bytes, &size) &&
		size >= sizeof(value) && !memcmp(bytes, &value, sizeof(value));
}

void TestReadback(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
{
	auto queue = Queue(device, type);
	auto upload = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	auto target = Buffer(device, D3D12_HEAP_TYPE_DEFAULT, sizeof(UINT));
	auto desc = target->GetDesc();
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	RecordResource(target.Get(), &desc, &heap, nullptr, D3D12_RESOURCE_STATE_COPY_DEST);

	List a(device, type), b(device, type);
	Write(upload.Get(), 17);
	a.list->CopyBufferRegion(target.Get(), 0, upload.Get(), 0, 4);
	DX12ReadbackRecordBuffer(a.list.Get(), target.Get(), 0, upload.Get(), 0, 4);
	Check(a.list->Close());
	Check(b.list->Close());
	b.Submit(queue.Get());
	Require(!DX12ReadbackAwaitingPublication(), "unrelated list consumed a capture");
	a.Submit(queue.Get());
	Drain(device, queue.Get());
	DX12ReadbackPublishReady();
	Require(HasValue(target.Get(), 17), "buffer snapshot value");

	Write(upload.Get(), 23);
	a.Submit(queue.Get());
	Require(!HasValue(target.Get(), 17), "old cached value visible during a queued write");
	Drain(device, queue.Get());
	Write(upload.Get(), 42);
	a.Submit(queue.Get());
	Drain(device, queue.Get());
	// Reset may occur before the CPU worker reads the old recording.
	DX12ReadbackForgetRecording(a.list.Get());
	Check(a.allocator->Reset());
	Check(a.list->Reset(a.allocator.Get(), nullptr));
	Write(upload.Get(), 99);
	a.list->CopyBufferRegion(target.Get(), 0, upload.Get(), 0, 4);
	DX12ReadbackRecordBuffer(a.list.Get(), target.Get(), 0, upload.Get(), 0, 4);
	Check(a.list->Close());
	DX12ReadbackPublishReady();
	Require(HasValue(target.Get(), 42), "repeat/reset aliased the previous readback");
	DX12ReadbackForgetRecording(a.list.Get());
	b.Submit(queue.Get());
	Require(!DX12ReadbackAwaitingPublication(), "discarded recording published");
	Drain(device, queue.Get());
	DX12InvalidateBufferContent(target.Get());
}

void TestQueueHandoff(ID3D12Device *device)
{
	auto copy = Queue(device, D3D12_COMMAND_LIST_TYPE_COPY);
	auto direct = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto first = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	auto next = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	auto target = Buffer(device, D3D12_HEAP_TYPE_DEFAULT, 4);
	auto desc = target->GetDesc();
	RecordResource(target.Get(), &desc, nullptr, nullptr, D3D12_RESOURCE_STATE_COPY_DEST);
	Write(first.Get(), 11);
	Write(next.Get(), 22);
	List a(device, D3D12_COMMAND_LIST_TYPE_COPY), b(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto record = [&](List &commands, ID3D12Resource *source) {
		commands.list->CopyBufferRegion(target.Get(), 0, source, 0, 4);
		DX12ReadbackRecordBuffer(commands.list.Get(), target.Get(), 0, source, 0, 4);
		Check(commands.list->Close());
	};
	record(b, next.Get());
	record(a, first.Get());
	ComPtr<ID3D12Fence> order, gate;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&order)));
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
	a.Submit(copy.Get());
	Check(copy->Signal(order.Get(), 1));
	Check(direct->Wait(order.Get(), 1));
	Check(direct->Wait(gate.Get(), 1));
	b.Submit(direct.Get());
	Drain(device, copy.Get());
	DX12ReadbackPublishReady();
	const bool hidden = !HasValue(target.Get(), 11);
	Check(gate->Signal(1));
	Drain(device, direct.Get());
	DX12ReadbackPublishReady();
	Require(hidden && HasValue(target.Get(), 22), "cross-queue overwrite exposed stale data");
	DX12InvalidateBufferContent(target.Get());
}

void TestTexture(ID3D12Device *device, bool partial, bool enhanced = false)
{
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	List producer(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto upload = Buffer(device, D3D12_HEAP_TYPE_UPLOAD, 1024);
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = 8;
	desc.Height = 2;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	ComPtr<ID3D12Resource> target;
	if (enhanced) {
		D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
		if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) ||
			!options.EnhancedBarriersSupported) {
			puts("Enhanced-barrier execution unavailable on this WARP adapter");
			return;
		}
		ComPtr<ID3D12Device10> device10;
		Check(device->QueryInterface(IID_PPV_ARGS(&device10)));
		D3D12_RESOURCE_DESC1 desc1 = {};
		desc1.Dimension = desc.Dimension;
		desc1.Width = desc.Width;
		desc1.Height = desc.Height;
		desc1.MipLevels = desc1.DepthOrArraySize = desc1.SampleDesc.Count = 1;
		desc1.Format = desc.Format;
		Check(device10->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc1,
			D3D12_BARRIER_LAYOUT_COPY_DEST, nullptr, nullptr, 0, nullptr, IID_PPV_ARGS(&target)));
	} else {
		Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&target)));
	}
	D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.pResource = target.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.pResource = upload.Get();
	device->GetCopyableFootprints(&desc, 0, 1, 0, &src.PlacedFootprint, nullptr, nullptr, nullptr);
	const UINT pitch = src.PlacedFootprint.Footprint.RowPitch;
	std::vector<BYTE> data(1024, 0xcc);
	for (UINT row = 0; row < desc.Height; ++row)
		for (UINT i = 0; i < desc.Width * 4; ++i)
			data[row * pitch + i] = static_cast<BYTE>(i + row);
	void *mapped = nullptr;
	Check(upload->Map(0, nullptr, &mapped));
	memcpy(mapped, data.data(), data.size());
	upload->Unmap(0, nullptr);
	producer.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	DX12ReadbackRecordTextureCopy(producer.list.Get(), target.Get(), 0, 0, 0, &src, nullptr);
	if (partial) {
		// Replace the second half of each row with the first half. Hashing the
		// partial source or including row padding gives a different result.
		const D3D12_BOX half = {0, 0, 0, 4, 2, 1};
		producer.list->CopyTextureRegion(&dst, 4, 0, 0, &src, &half);
		DX12ReadbackRecordTextureCopy(producer.list.Get(), target.Get(), 4, 0, 0, &src, &half);
		for (UINT row = 0; row < desc.Height; ++row)
			memcpy(data.data() + row * pitch + 16, data.data() + row * pitch, 16);
		if (enhanced) {
			ComPtr<ID3D12GraphicsCommandList7> list7;
			Check(producer.list.As(&list7));
			D3D12_TEXTURE_BARRIER barriers[2] = {};
			auto &first = barriers[0];
			first.pResource = target.Get();
			first.SyncBefore = first.SyncAfter = D3D12_BARRIER_SYNC_ALL;
			first.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
			first.AccessAfter = D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
			first.LayoutBefore = D3D12_BARRIER_LAYOUT_COPY_DEST;
			first.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			barriers[1] = first;
			std::swap(barriers[1].LayoutBefore, barriers[1].LayoutAfter);
			std::swap(barriers[1].AccessBefore, barriers[1].AccessAfter);
			D3D12_BARRIER_GROUP group = {};
			group.Type = D3D12_BARRIER_TYPE_TEXTURE;
			group.NumBarriers = 2;
			group.pTextureBarriers = barriers;
			DX12ReadbackForwardEnhancedBarriers(producer.list.Get(), list7.Get(), 1, &group);
		} else {
			D3D12_RESOURCE_BARRIER barriers[2] = {};
			for (auto &barrier : barriers) {
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = target.Get();
			}
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			DX12ReadbackForwardBarriers(producer.list.Get(), 2, barriers, nullptr);
		}
	}
	Check(producer.list->Close());
	uint32_t expected = 0;
	Require(DX12TextureHashUtils::Texture2DTopMipHashFromRows(desc, data.data(), pitch, 32, 2,
		&expected), "expected texture hash");
	uint32_t actual = 0;
	Require(!DX12TryGetTextureHash(target.Get(), &actual), "unsubmitted texture was published");
	DX12ResourceCollectQueue::Start();
	producer.Submit(queue.Get());
	Drain(device, queue.Get());
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < deadline &&
		(!DX12TryGetTextureHash(target.Get(), &actual) || expected != actual))
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	DX12ResourceCollectQueue::Stop();
	Require(actual == expected, "worker published incorrect texture hash");
	DX12InvalidateTextureHash(target.Get());
}

void TestRenderPass(ID3D12Device *device)
{
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapper = native.list;
	void *pointer = wrapper.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer),
		"render-pass wrapper");
	wrapper.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	ComPtr<ID3D12GraphicsCommandList4> pass;
	Check(wrapper.As(&pass));
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Width = desc.Height = 4;
	desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	ComPtr<ID3D12Resource> texture;
	Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));
	DX12MarkTextureHashWanted(texture.Get());
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	pass->BeginRenderPass(0, nullptr, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
	pass->ResourceBarrier(1, &barrier);
	pass->EndRenderPass();
	Check(pass->Close());
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	native.Submit(queue.Get());
	Drain(device, queue.Get());
	Require(!DX12ReadbackAwaitingPublication(), "injected a copy inside a render pass");
	DX12ReadbackForgetWantedTexture(texture.Get(), true);
}

void TestWrapperState(ID3D12Device *device)
{
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapped = native.list;
	void *pointer = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer),
		"command list wrapper");
	wrapped.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	auto *state = DX12CommandListRuntime::GetStatePtrFast(native.list.Get());
	Require(state != nullptr, "registered wrapper state");
	DX12CommandListRuntime::RememberPrimitiveTopology(state, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	wrapped->ClearState(nullptr);
	Require(state->ia.primitiveTopology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED, "stale IA after ClearState");
	gHasPresentRuntimeEffect = 1;
	const auto flags = DX12HotPathState::SourceState::Capture();
	gPresentCommandListExecutedPresent = DX12RuntimeState::GetPresentCount() - 1;
	const auto original = [](ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT) {};
	DX12WrapperRuntimeUtils::DrawInstanced(native.list.Get(), state, 3, 1, 0, 0,
		original, flags.BuildHookWorkFlags());
	Require(!DX12ModPresentReplacementPendingForCurrentPresent(), "Present VM missed the first draw");
	gHasPresentRuntimeEffect = 0;
	Check(wrapped->Close());
	Require(flags.NeedsRecordWork() && !flags.NeedsModDrawWork() &&
		(flags.BuildHookWorkFlags() & DX12HotPathState::FLAG_PRESENT_REPLACEMENT),
		"Present-only configuration skipped");
}

void TestUploadFailure(ID3D12Device *device)
{
	using Upload = DX12DdsTextureLoadUtils;
	auto texture = Buffer(device, D3D12_HEAP_TYPE_DEFAULT);
	auto staging = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	Upload::UploadChannelSlot slot;
	Require(Upload::FinishUploadLocked(slot, texture.Get(), staging.Get(), 1, S_OK),
		"successful upload publication");
	Require(!Upload::FinishUploadLocked(slot, texture.Get(), staging.Get(), 2, E_FAIL) &&
		Upload::s_uploadPendingFenceValue == 1 && slot.busyUntilFence == 2,
		"failed signal advertised or released its slot");
	texture.Reset();
	staging.Reset();
	Require(slot.unfencedTexture && slot.unfencedUpload &&
		slot.unfencedTexture->GetDesc().Width == 256, "unfenced operands released");
	slot.ReleaseUnfencedResources();
	Upload::s_uploadHasPending.store(false);
}

void TestBarrierGroups(ID3D12Device *device)
{
	auto raw = Buffer(device, D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> wrapped = raw;
	void *result = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12ResourceResult(__uuidof(ID3D12Resource), &result), "wrapped resource");
	wrapped.Attach(static_cast<ID3D12Resource*>(result));
	D3D12_BUFFER_BARRIER barriers[2] = {};
	barriers[0].pResource = barriers[1].pResource = raw.Get();
	D3D12_BARRIER_GROUP groups[2] = {};
	groups[0].Type = D3D12_BARRIER_TYPE_GLOBAL;
	D3D12_GLOBAL_BARRIER global = {};
	groups[0].NumBarriers = 1;
	groups[0].pGlobalBarriers = &global;
	groups[1].Type = D3D12_BARRIER_TYPE_BUFFER;
	groups[1].NumBarriers = 2;
	groups[1].pBufferBarriers = barriers;
	std::vector<D3D12_BARRIER_GROUP> storage;
	std::vector<std::vector<D3D12_TEXTURE_BARRIER>> textures;
	std::vector<std::vector<D3D12_BUFFER_BARRIER>> buffers;
	const D3D12_BARRIER_GROUP *effective = nullptr;
	DX12WrapperRuntimeUtils::UnwrapBarrierGroups(groups, 2, &storage, &textures, &buffers, &effective);
	Require(effective == groups && !storage.capacity() && !textures.capacity() && !buffers.capacity(),
		"allocated for native/global barriers");
	barriers[1].pResource = wrapped.Get();
	DX12WrapperRuntimeUtils::UnwrapBarrierGroups(groups, 2, &storage, &textures, &buffers, &effective);
	Require(effective != groups && effective[1].pBufferBarriers[1].pResource == raw.Get() &&
		effective[0].pGlobalBarriers == &global && barriers[1].pResource == wrapped.Get(),
		"mixed barrier unwrapping");
}

void TestControllerFrameDispatch(IDXGISwapChain1 *chain)
{
	using Source = RecordedControllers;
	using Vm = MigotoCommandVmUtils;
	const auto dir = std::filesystem::current_path() /
		(L"xinput-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create XInput fixture");
	const auto root = dir / L"main.ini";
	std::ofstream(root) << "[Constants]\nglobal $held=3\nglobal $count=0\n[Hunting]\ntoggle_hunting=XB2_START\n"
		"[KeyHold]\ntype=hold\nkey=XB2_A\n$held=7\n[KeyEvent]\nkey=XB2_B\nrun=CommandListCount\n"
		"[CommandListCount]\n$count=$count+1\n";
	Source::hardware = {};
	Source::reads = {};
	auto &port = Source::hardware.slots[1];
	port.connected = true;
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_START;
	Require(!DX12HuntIsEnabled() && DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr,
		DX12ModLoadReason::Initial, Source::Read), "load controller-only overrides with recorded hardware input");
	DX12HotPathState::Update();
	Require(DX12HotPathState::skipAll != 0 && Source::reads == std::array<unsigned,4>{0,1,0,0},
		"key-only config uses lightweight Present and probes only bound ports");
	Source::reads = {};
	const auto now = (GetTickCount64() / 4000 * 4 + 5) * 1000;
	auto sample = [&](ULONGLONG tick) {
		return DX12UiActions::PollKeyboard([](int) { return false; }, tick);
	};
	auto initial = sample(now);
	Require(!DX12UiActions::Consume() && !DX12HuntIsEnabled(), "installation cannot trigger a held controller chord");
	DX12ModProcessIdleFrameKeys(nullptr, initial);
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
	auto pressed = sample(now + 10);
	// Change the physical source between consumers; Mod sees the sample that
	// was read alongside the UI keys, even on a keyboard/controller-only mod.
	port.state.Gamepad.wButtons = 0;
	DX12ModProcessIdleFrameKeys(nullptr, pressed);
	Vm::State vm;
	Require(vm.Evaluate(L"$held") == 7 && vm.Evaluate(L"$count") == 1 &&
		Source::reads == std::array<unsigned,4>{0,2,0,0}, "one port read shared across UI and idle Mod actions");
	port.connected = false;
	DX12ModBeginFrame(nullptr, sample(now + 20));
	Require(vm.Evaluate(L"$held") == 3 && vm.Evaluate(L"$count") == 1, "lost controller releases hold on normal frame");
	port.connected = true;
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_START;
	sample(now + 4000);
	Require(DX12UiActions::Consume() && DX12HuntIsEnabled(), "controller reconnect toggles Hunting through UI dispatch");
	sample(now + 4010);
	Require(!DX12UiActions::Consume(), "held controller toggle fires once");
	port.state.Gamepad.wButtons = 0;
	sample(now + 4020);
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_START;
	sample(now + 4030);
	Require(DX12UiActions::Consume() && !DX12HuntIsEnabled(), "release and repress restores Hunting state");
	port.state.Gamepad.wButtons = 0;
	Check(chain->Present(0, 0));
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B;
	const auto before = Source::reads[1];
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 7 && vm.Evaluate(L"$count") == 2 && Source::reads[1] == before + 1,
		"wrapped WARP Present delivers controller-only hold and command list");
	port.connected = false;
	DXGI_PRESENT_PARAMETERS parameters{};
	Check(chain->Present1(0, DXGI_PRESENT_TEST, &parameters));
	Require(vm.Evaluate(L"$held") == 7 && Source::reads[1] == before + 1, "test Present1 does not consume input");
	Check(chain->Present1(0, 0, &parameters));
	Require(vm.Evaluate(L"$held") == 3 && Source::reads[1] == before + 2, "wrapped Present1 releases disconnected hold");
	std::ofstream(root) << "[Constants]\nglobal $held=5\n";
	Source::reads = {};
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read),
		"reload without controller bindings using the same reader");
	DX12ModProcessIdleFrameKeys(nullptr, sample(now + 8000));
	Require(Source::reads == std::array<unsigned,4>{} && vm.Evaluate(L"$held") == 5, "removed bindings stop polling");
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
	Vm::ClearGlobals();
}

void TestForegroundFrameInput(IDXGISwapChain1 *chain)
{
	using Window = migoto::tests::InputTestWindow;
	using Source = RecordedControllers;
	using Vm = MigotoCommandVmUtils;
	Window window;
	Require(window.Valid(), "create native hidden foreground window");
	const auto dir = std::filesystem::current_path() / (L"focus-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create foreground config directory");
	const auto root = dir / L"main.ini";
	const auto include = dir / L"settings.ini";
	Source::hardware = {};
	auto &port = Source::hardware.slots[1];
	port.connected = true;
	auto load = [&](const char *system, const char *included, bool expectWarning = false) {
		std::ofstream(include) << "[System]\n" << included;
		std::ofstream(root) << "[Include]\ninclude=settings.ini\n[System]\n" << system
			<< "[Constants]\nglobal $focus=2\nglobal $allow=1\n"
			"[Hunting]\nanalyse_frame=XB2_X\n[KeyFocus]\nkey=XB2_A\ntype=hold\n$focus=8\ntransition=1000000\n";
		bool warnings = false;
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, &warnings, nullptr, DX12ModLoadReason::UserReload, Source::Read),
			"publish foreground policy with bindings");
		Require(warnings == expectWarning, "diagnose invalid foreground check at load");
	};
	unsigned keysRead = 0;
	auto poll = [&](migoto::InputWindowSource source) {
		return DX12UiActions::PollKeyboard([&](int) { ++keysRead; return false; }, GetTickCount64(), source);
	};
	Vm::State vm;
	load("check_foreground_window=$allow\n", "", true);
	Require(poll(Window::Absent()).has_value(), "early foreground setting rejects a declared Mod variable");
	keysRead = 0;
	load("check_foreground_window=0\n", "check_foreground_window=(2 > 1)\nadditional_foreground_window=Bunny Input Audit\n");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X;
	Source::reads = {};
	auto blocked = poll(Window::Absent());
	DX12ModProcessIdleFrameKeys(nullptr, blocked);
	Require(!blocked && !keysRead && !Source::reads[1] && !DX12UiActions::PendingActions && gKeyFrame.pending.empty(),
		"blocked foreground skips keyboard, XInput, UI and idle Mod dispatch");
	auto accepted = poll(Window::Own());
	DX12ModProcessIdleFrameKeys(nullptr, accepted);
	Require(accepted && Source::reads[1] == 1 && keysRead && gKeyFrame.pending.at(L"$focus").target == 8 &&
		InterlockedExchange(&DX12UiActions::PendingActions, 0) == static_cast<LONG>(DX12UiActionBits::FrameAnalysis),
		"own process accepts both UI and Mod press on one sample");
	gKeyFrame.pending.at(L"$focus").activation_time = GetTickCount64() - 1000001;
	port.state.Gamepad.wButtons = 0;
	Require(window.Rename(L"bunny input audit"), "set mismatched title case");
	const auto before = keysRead;
	const auto settings = dir / L"d3dx_user.ini";
	HANDLE exclusive = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(exclusive != INVALID_HANDLE_VALUE, "hold user settings exclusively for a failed reload");
	const bool reloaded = DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read);
	CloseHandle(exclusive);
	std::filesystem::remove(settings);
	Require(!reloaded, "locked settings abort foreground policy reload");
	DX12ModBeginFrame(nullptr, poll(Window::Foreign()));
	Require(Source::reads[1] == 1 && keysRead == before && vm.Evaluate(L"$focus") == 8 &&
		gKeyFrame.saves.size() == 1 && gKeyFrame.pending.empty(),
		"normal background frame completes transition without manufacturing a release");
	Require(window.Rename(L"Bunny Input Audit"), "restore title case");
	DX12ModProcessIdleFrameKeys(nullptr, poll(Window::Foreign()));
	Require(Source::reads[1] == 2 && vm.Evaluate(L"$focus") == 2 && gKeyFrame.saves.empty(),
		"matching additional window resumes and observes physical release");
	load("check_foreground_window=true\n", "");
	Require(poll(Window::Foreign()).has_value(), "reloading without a title retains the configured additional window");
	const std::wstring longTitle(MAX_PATH, L'T');
	const std::string titleConfig = "check_foreground_window=1\nadditional_foreground_window=" + std::string(MAX_PATH, 'T') + "\n";
	load(titleConfig.c_str(), "", true);
	Require(window.Rename(longTitle.c_str()) && !poll(Window::Foreign()), "long title is not a prefix match");
	Require(window.Rename(longTitle.substr(1).c_str()) && poll(Window::Foreign()).has_value(),
		"configured title uses DX11's MAX_PATH minus one bound");
	load("check_foreground_window=invalid\n", "", true);
	const auto queries = Window::queries;
	Require(poll(Window::Absent()).has_value() && Window::queries == queries,
		"invalid foreground check defaults to off without querying Win32");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	Check(chain->Present(0, 0));
	Require(gKeyFrame.saves.size() == 1, "wrapped Present dispatches with foreground check disabled");
	load("check_foreground_window=true\nadditional_foreground_window=Bunny Unused Audit Window\n", "");
	DWORD foregroundOwner = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &foregroundOwner);
	Require(foregroundOwner != GetCurrentProcessId(), "hidden WARP test runs outside the foreground process");
	Source::reads = {};
	DXGI_PRESENT_PARAMETERS args{};
	Check(chain->Present1(0, 0, &args));
	Require(!Source::reads[1] && gKeyFrame.saves.empty(), "wrapped Present1 respects the native foreground filter");
	port.state.Gamepad.wButtons = 0;
	std::ofstream(root) << "[Input]\ninput=off\ninput_disable_mode=all\ntoggle_input=XB2_START\n"
		"[Constants]\nglobal $held=2\n[KeyHeld]\nkey=XB2_A\ntype=hold\n$held=8\n";
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	auto stale = DX12UiActions::PollKeyboard([](int) { return false; }, GetTickCount64(), Window::Own());
	Require(stale.has_value(), "capture a controller press under the old policy");
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read),
		"publish controller input toggle and hold");
	DX12ModProcessIdleFrameKeys(nullptr, stale);
	Require(vm.Evaluate(L"$held") == 2, "an old frame sample cannot activate bindings published by reload");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 8, "runtime enable survives config reload requesting initial off");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START;
	Check(chain->Present1(0, 0, &args));
	port.state.Gamepad.wButtons = 0;
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 8, "all mode defers hold release across wrapped presentations");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_START;
	Check(chain->Present1(0, DXGI_PRESENT_TEST, &args));
	Require(vm.Evaluate(L"$held") == 8, "test presentation does not consume the privileged toggle");
	Check(chain->Present1(0, 0, &args));
	Require(vm.Evaluate(L"$held") == 2, "controller toggle resumes delayed hold release on Present1");
	std::ofstream(root) << "[Constants]\n";
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr), "uninstall foreground controller reader");
	std::filesystem::remove(include);
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
}

void TestKeyCyclesOnPresent(IDXGISwapChain1 *chain)
{
	using Source = RecordedControllers;
	using Vm = MigotoCommandVmUtils;
	const auto dir = std::filesystem::current_path() /
		(L"cycle-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	Require(std::filesystem::create_directory(dir), "create cycle fixture");
	const auto root = dir / L"main.ini";
	Source::hardware = {};
	auto &port = Source::hardware.slots[1];
	port.connected = true;
	Vm::State vm;
	std::ofstream(root) << "[Constants]\nglobal $hits=0\nglobal $blocked=0\n"
		"[KeyLogical]\nkey=XB2_A\ncondition=1 || $hits > 0\nrun=CommandListLogical\n"
		"[CommandListLogical]\n$hits=$hits+1\n"
		"[KeyBlocked]\nkey=XB2_A\ncondition=0 && random(1)\n$blocked=1\n";
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::Initial, Source::Read),
		"load logical key conditions on the WARP swap chain");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	const auto before = GetExpressionRandomSampleCount();
	Check(chain->Present(0, DXGI_PRESENT_TEST));
	Require(vm.Evaluate(L"$hits") == 0 && GetExpressionRandomSampleCount() == before, "test Present skips key evaluation");
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$hits") == 1 && vm.Evaluate(L"$blocked") == 0 && GetExpressionRandomSampleCount() == before + 1,
		"Present runs the eligible key and consumes one sample in the false eager guard");
	port.state.Gamepad.wButtons = 0;
	std::ofstream(root) << "[Constants]\n" << migoto::tests::inputDelayIni;
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::Initial, Source::Read),
		"load key delays and production command lists");
	migoto::tests::CheckInputDelayFrames([&](std::uint64_t tick, std::string_view keys, bool gate) {
		Vm::SetGlobal(L"$gate", gate);
		Bunny::MigotoKeyBindingUtils::ProcessBindings(gKeyBindings, gKeyFrame, [&](int vk) {
			return vk >= 'A' && vk <= 'Z' && keys.find(static_cast<char>(vk)) != keys.npos;
		}, {}, [](const std::wstring &name, bool post) { DX12CommandListExecutor::RunKeyCommandPhase(name, post); },
			[tick] { return tick; });
	}, [&](int held, int presses, int releases, int step, const char *why) {
		Require(vm.Evaluate(L"$held") == held && vm.Evaluate(L"$presses") == presses &&
			vm.Evaluate(L"$releases") == releases && vm.Evaluate(L"$step") == step, why);
	});
	Require(vm.Evaluate(L"$invalid") == 1 && vm.Evaluate(L"$negative") == 0,
		"invalid delay falls back to zero and negative delay stays pending");

	std::ofstream(root) << "[Constants]\nglobal $held=2\n[KeyWait]\nkey=XB2_A\ntype=hold\n"
		"$held=7\ndelay=16\nrelease_delay=16\n";
	auto reloadDelay = [&] {
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read),
			"reload timed controller binding");
	};
	reloadDelay();
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 2, "first controller sample only arms the press timer");
	Sleep(32);
	reloadDelay();
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 2, "reload cancels pending press, even if old deadline passed");
	Sleep(32);
	DXGI_PRESENT_PARAMETERS delayArgs{};
	Check(chain->Present1(0, DXGI_PRESENT_TEST, &delayArgs));
	Require(vm.Evaluate(L"$held") == 2, "test Present1 cannot deliver a matured press");
	Check(chain->Present1(0, 0, &delayArgs));
	Require(vm.Evaluate(L"$held") == 7, "non-test Present1 delivers the matured controller press");
	port.connected = false;
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 7, "disconnect starts release delay");
	Sleep(32);
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$held") == 2, "idle Present delivers the delayed restoration");
	port.connected = true;
	port.state.Gamepad.wButtons = 0;
	std::ofstream(root) << "[Constants]\n" << migoto::tests::keyPhaseIni;
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::Initial, Source::Read),
		"load transition phase callbacks on the WARP swap chain");
	for (const auto &sample : migoto::tests::keyPhaseSamples) {
		port.state.Gamepad.wButtons = (sample.tap ? XINPUT_GAMEPAD_A : 0) |
			(sample.hold ? XINPUT_GAMEPAD_B : 0) | (sample.toggle ? XINPUT_GAMEPAD_X : 0);
		Check(chain->Present(0, 0));
		Require(vm.Evaluate(L"$v") == sample.value && vm.Evaluate(L"$presses") == sample.presses &&
			vm.Evaluate(L"$releases") == sample.releases && vm.Evaluate(L"$release_value") == sample.releaseValue,
			"WARP hold and toggle overlap, release runs only post before restoration");
		Require(vm.Evaluate(L"$pre") == 2 && vm.Evaluate(L"$post") == 8 &&
			vm.Evaluate(L"$saved") == 2 && vm.Evaluate(L"$trace") == 123412 && vm.Evaluate(L"$order") == 28,
			"WARP pre/post order, shared local, nested inherited and explicit phases");
	}
	port.state.Gamepad.wButtons = 0;
	std::ofstream(root) << "[Constants]\nglobal $v=2\n[KeyTimed]\nkey=XB2_A\ntype=hold\n"
		"$v=10\ntransition=1000000\nrelease_transition=1000000\nrelease_transition_type=cosine\n";
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read),
		"reload timed transition");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	Check(chain->Present(0, 0));
	Require(vm.Evaluate(L"$v") < 2.1 && gKeyFrame.saves.size() == 1, "timed hold starts on Present");
	const auto settings = dir / L"d3dx_user.ini";
	HANDLE exclusive = CreateFileW(settings.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	Require(exclusive != INVALID_HANDLE_VALUE, "create exclusively held user settings");
	const bool reloaded = DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read);
	CloseHandle(exclusive);
	std::filesystem::remove(settings);
	Require(!reloaded && !gKeyFrame.pending.empty() && !gKeyFrame.saves.empty(),
		"a settings read failure preserves the active transition generation");
	// Move only this fixture's start time. The actual Present1 clock,
	// interpolation, write and cleanup all run through the runtime.
	gKeyFrame.pending.at(L"$v").activation_time = GetTickCount64() - 250000;
	DXGI_PRESENT_PARAMETERS presentArgs{};
	Check(chain->Present1(0, DXGI_PRESENT_TEST, &presentArgs));
	Require(vm.Evaluate(L"$v") < 2.1, "test presentation does not publish a transition sample");
	Check(chain->Present1(0, 0, &presentArgs));
	Require(std::abs(vm.Evaluate(L"$v") - 4.0) < 0.02, "Present1 samples a running transition after failed reload");
	port.connected = false;
	Check(chain->Present(0, 0));
	Require(gKeyFrame.saves.empty() && gKeyFrame.pending.at(L"$v").target == 2, "disconnect schedules the release destination");
	gKeyFrame.pending.at(L"$v").activation_time = GetTickCount64() - 500000;
	Check(chain->Present(0, 0));
	Require(std::abs(vm.Evaluate(L"$v") - 3.0) < 0.03, "cosine release progresses across idle presentations");
	gKeyFrame.pending.at(L"$v").activation_time = GetTickCount64() - 1000001;
	Check(chain->Present(0, 0));
	Require(gKeyFrame.pending.empty() && vm.Evaluate(L"$v") == 2, "release finishes on a frame with no pressed keys");
	port.connected = true;
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr, DX12ModLoadReason::UserReload, Source::Read),
		"reprobe the connected controller");
	port.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
	Check(chain->Present(0, 0));
	Require(!gKeyFrame.pending.empty() && !gKeyFrame.saves.empty(), "transition and original value live before reload");
	for (const auto &test : migoto::tests::keyCycleCases) {
		port.state.Gamepad.wButtons = 0;
		std::ofstream(root) << "[Constants]\n" << test.Config();
		Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr,
			DX12ModLoadReason::UserReload, Source::Read), "reload cycle config");
		Require(vm.Evaluate(L"$phase") == 99 && vm.Evaluate(L"$score") == 0 && gKeyFrame.pending.empty() && gKeyFrame.saves.empty(),
			"reload publishes fresh cycle globals and discards outgoing transitions and owners");
		for (const auto &press : test.presses) {
			if (press.externalValue)
				Vm::SetGlobal(L"$phase", *press.externalValue);
			port.state.Gamepad.wButtons = press.back ? XINPUT_GAMEPAD_B : XINPUT_GAMEPAD_A;
			Check(chain->Present(0, 0));
			Require(vm.Evaluate(L"$phase") == press.value && vm.Evaluate(L"$score") == press.score, test.name);
			port.state.Gamepad.wButtons = 0;
			Check(chain->Present(0, 0));
		}
	}
	std::ofstream(root) << "[Constants]\n";
	Require(DX12ModRuntimeLoad(root.c_str(), nullptr, nullptr, nullptr), "uninstall test cycle reader and bindings");
	std::filesystem::remove(root);
	std::filesystem::remove(dir);
}

void TestSwapChain(IDXGIFactory4 *factory, ID3D12Device *device)
{
	auto first = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto second = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = desc.Height = 64;
	desc.BufferCount = 2;
	desc.SampleDesc.Count = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	ComPtr<IDXGIFactory4> wrappedFactory = factory;
	void *factoryResult = wrappedFactory.Detach();
	Require(DX12Wrapper::WrapDXGIFactoryResult(__uuidof(IDXGIFactory4), &factoryResult), "DXGI wrapper");
	wrappedFactory.Attach(static_cast<IDXGIFactory4*>(factoryResult));
	ComPtr<ID3D12CommandQueue> wrapped = first;
	void *queueResult = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12CommandQueue), &queueResult), "queue wrapper");
	wrapped.Attach(static_cast<ID3D12CommandQueue*>(queueResult));
	ComPtr<IDXGISwapChain1> chain;
	Check(wrappedFactory->CreateSwapChainForComposition(wrapped.Get(), &desc, nullptr, &chain));
	ComPtr<ID3D12CommandQueue> held;
	held.Attach(DX12SwapChainQueues::Acquire(chain.Get()));
	Require(held.Get() == first.Get(), "creation queue");
	const LONG frameBeforePresent = DX12RuntimeState::GetPresentCount();
	const auto samplesBeforePresent = GetExpressionRandomSampleCount();
	Check(chain->Present(0, 0));
	Require(DX12RuntimeState::GetPresentCount() == frameBeforePresent + 1 &&
		GetExpressionRandomSampleCount() == samplesBeforePresent, "wrapped WARP Present advances the frame without sampling");
	MigotoCommandVmUtils::State vm;
	Require(vm.Evaluate(L"frame_number") == frameBeforePresent + 1, "Present updates the INI frame number");
	const double presentTime = vm.Evaluate(L"time");
	DXGI_PRESENT_PARAMETERS parameters{};
	Check(chain->Present1(0, DXGI_PRESENT_TEST, &parameters));
	Require(vm.Evaluate(L"frame_number") == frameBeforePresent + 1 && vm.Evaluate(L"time") == presentTime,
		"test presentation leaves the frame snapshot untouched");
	Check(chain->Present1(0, 0, &parameters));
	Require(vm.Evaluate(L"frame_number") == frameBeforePresent + 2 && vm.Evaluate(L"time") >= presentTime,
		"wrapped Present1 publishes its clock and frame number");
	try {
		TestControllerFrameDispatch(chain.Get());
		TestKeyCyclesOnPresent(chain.Get());
		TestForegroundFrameInput(chain.Get());
	} catch (...) {
		Drain(device, first.Get()); // The debug layer also requires completion on a failed assertion.
		throw;
	}
	Drain(device, first.Get());
	ComPtr<IDXGISwapChain3> chain3;
	Check(chain.As(&chain3));
	UINT masks[] = {1, 1};
	IUnknown *queues[] = {second.Get(), second.Get()};
	Check(chain3->ResizeBuffers1(2, 64, 64, desc.Format, 0, masks, queues));
	ComPtr<ID3D12CommandQueue> current;
	current.Attach(DX12SwapChainQueues::Acquire(chain.Get()));
	Require(current.Get() == second.Get() && held.Get() == first.Get(), "back-buffer queue ownership");
}

void TestOverlay(ID3D12Device *device)
{
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto otherQueue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto *channel = Bunny::DX12PresentOverlayChannel::ForDevice(device);
	Require(channel != nullptr, "overlay channel");
	ComPtr<ID3D12Fence> gate;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
	Check(queue->Wait(gate.Get(), 1));
	bool filled = true;
	for (int i = 0; i < 2; ++i)
		filled = filled && channel->BeginRecord(true) && channel->Submit(queue.Get());
	const auto begin = std::chrono::steady_clock::now();
	const bool skipped = channel->BeginRecord(false) == nullptr;
	const bool grew = channel->BeginRecord(true) && channel->Submit(otherQueue.Get());
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	Check(gate->Signal(1));
	Drain(device, otherQueue.Get());
	Require(filled && skipped && grew && elapsed < std::chrono::milliseconds(500),
		"HUD skip and mod recording during GPU backlog");
	Bunny::DX12PresentOverlayChannel::ClearAll();
}

// Capture the native draw arguments at the hook's callback boundary.
std::vector<std::array<int64_t, 6>> recordedDraws;

DX12IaReplacementExecutorCallbacks RecordingDrawCallbacks()
{
	DX12IaReplacementExecutorCallbacks callbacks;
	callbacks.drawInstanced = [](ID3D12GraphicsCommandList*, UINT count, UINT instances, UINT start, UINT first) {
		recordedDraws.push_back({0, count, instances, start, 0, first});
	};
	callbacks.drawIndexedInstanced = [](ID3D12GraphicsCommandList*, UINT count, UINT instances,
		UINT start, INT base, UINT first) {
		recordedDraws.push_back({1, count, instances, start, base, first});
	};
	return callbacks;
}

void TestInstancedDraws(ID3D12Device *device)
{
	Bunny::IniDocument ini;
	Require(ini.Parse(
		L"[CommandListLiteral]\ndrawinstanced=9,2,3,4\ndrawindexedinstanced=8,5,2,-3,7\n"
		L"draw=4,1\ndrawindexedinstanced=0,0,0,-2147483648,4294967295\n"
		L"[CommandListDynamic]\nlocal $start = 2\n"
		L"drawindexedinstanced=6, $instances+1, $start, -$start, 11\n"
		L"drawinstanced=4,nan,0,0\ndrawinstanced=4,4294967296.0,0,0\n"
		L"drawinstanced=4,1+,0,0\n"
		L"[CommandListInvalid]\ndrawinstanced=2,1,0\ndrawindexedinstanced=1,2,3,4,5,6\n"
		L"drawinstanced=2,-1,0,0\ndrawindexedinstanced=1,1,0,2147483648,0\n"
		L"[CustomShaderCopies]\nvs=copy-vs.hlsl\nps=copy-ps.hlsl\ndrawinstanced=0,$instances,3,9\n"
		L"[CustomShaderIndexed]\nvs=copy-vs.hlsl\nps=copy-ps.hlsl\ndrawindexedinstanced=4,1,0,0,0\n"
		L"[CustomShaderDefault]\nvs=copy-vs.hlsl\nps=copy-ps.hlsl\n"),
		"instanced draw config");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	Require(lists.at(L"CommandListInvalid").actions.empty(), "reject incorrect arity and literal bounds");
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> plans;
	BuildDx12CompiledCommandListPlans(lists, &plans);
	Require(!plans.at(L"CommandListLiteral").unsupported && plans.at(L"CommandListDynamic").unsupported,
		"literal draw plans compile, expression draws require execution");

	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	const DX12IaHashState ia{};
	DX12ModIaReplacement replacement;
	const auto original = DX12IaDrawInvocation::ForDrawInstanced(0, 13, 0, 21);
	DX12CommandListExecutor executor(device, native.list.Get(), ia, original.Parameters(), &replacement);
	executor.SetExecutionMode(true, true);
	executor.ExecuteCompiledCommandListPlan(plans.at(L"CommandListLiteral"), 0, false);
	for (int instances : {2, 6}) {
		MigotoCommandVmUtils::SetGlobal(L"$instances", instances);
		executor.RunActions(lists.at(L"CommandListDynamic").actions, 0, false, nullptr);
	}
	Require(executor.observedDynamicCommandState, "expression draws bypass cross-draw caching");
	const auto callbacks = RecordingDrawCallbacks();
	recordedDraws.clear();
	const DX12ActiveIaState activeIa{};
	DX12IaReplacementDrawExecutor(native.list.Get(), callbacks).ExecuteReplacementDraws(replacement, activeIa, &original);
	const std::vector<std::array<int64_t, 6>> expected{
		{0,9,2,3,0,4}, {1,8,5,2,-3,7}, {0,4,13,1,0,21}, {1,0,0,0,INT32_MIN,UINT32_MAX},
		{1,6,3,2,-2,11}, {1,6,7,2,-2,11}};
	Require(recordedDraws == expected, "explicit instance parameters, dynamic values and inherited legacy draw");
	auto elements = std::make_shared<DX12CustomShaderElementMap>();
	Bunny::DX12CustomShaderElementParser::BuildRegistry(lists, elements.get());
	const auto previous = gCustomShaderElementSnapshot.exchange(elements);
	for (const auto *section : {L"CustomShaderCopies", L"CustomShaderIndexed", L"CustomShaderDefault"})
		executor.RunCommandList(lists.at(section), 0, false, nullptr);
	gCustomShaderElementSnapshot.store(previous);
	const auto &records = replacement.customElementRecords;
	Require(records.size() == 2, "standalone indexed draw without an index binding skips recording");
	Require(records[0].drawCount == 0 && records[0].drawStart == 3 &&
		records[0].drawInstanceCount == 6 && records[0].drawFirstInstance == 9,
		"standalone element preserves explicit zero count and evaluated instance range");
	Require(records[1].drawCount == 4 && records[1].drawInstanceCount == 1 &&
		records[1].drawFirstInstance == 0, "standalone element default quad");
	Check(native.list->Close());
	MigotoCommandVmUtils::ClearGlobals();
}

void TestDrawCallParameters(ID3D12Device *device)
{
	constexpr char shader[] = "float4 main(uint id:SV_VertexID):SV_Position{return float4(id,0,0,1);}";
	ComPtr<ID3DBlob> vs;
	Check(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0",
		0, 0, &vs, nullptr));
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	Bunny::DX12CustomShaderPipelineAssembler::FillGraphicsPipelineDesc(
		{}, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, nullptr, &desc);
	desc.pRootSignature = Bunny::DX12CustomShaderPipelineCache::EnsureRootSignature(device);
	desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
	desc.NumRenderTargets = 0;
	desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	ComPtr<ID3D12PipelineState> pipeline;
	Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)));
	DX12RecordGraphicsPipelineState(pipeline.Get(), &desc);
	DX12PsoShaderInfo shaderInfo{};
	Require(DX12GetPipelineStateShaderInfo(pipeline.Get(), &shaderInfo) && shaderInfo.hasVS,
		"registered vertex shader");
	const auto path = std::filesystem::current_path() /
		(L"draw-context-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
	auto load = [&](const char *links) {
		std::ofstream ini(path);
		ini << "[Constants]\nglobal $outside = draw_type + vertex_count + first_vertex\n"
			"[ShaderOverrideContext]\nhash=" << std::hex << shaderInfo.vs << '\n' << links
			<< "[CommandListMixed]\nchecktextureoverride=vb0\nrun=CommandListCapture\n"
			"[CommandListCapture]\n$visits = $visits + 1\n"
			"if instance_count > 0\n$nonempty = $nonempty + 1\nendif\n"
			"$kind = draw_type\n$vertices = vertex_count\n$indices = index_count\n"
			"$instances = instance_count\n$base = first_vertex\n$start = first_index\n$first = first_instance\n"
			"[CommandListAfter]\n$after = first_vertex\n$after_kind = draw_type\n";
		ini.close();
		const bool loaded = DX12ModRuntimeLoad(path.c_str());
		std::filesystem::remove(path);
		Require(loaded && DX12ModHasActiveShaderOverrides(), "loaded draw parameter shader override");
		DX12ModRefreshRuntimeStateAfterReload();
	};
	load("run=CommandListCapture\n");
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapped = native.list;
	void *pointer = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer),
		"draw context command list wrapper");
	wrapped.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	wrapped->SetPipelineState(pipeline.Get());
	auto *state = DX12CommandListRuntime::GetStatePtrFast(native.list.Get());
	Require(state != nullptr, "draw context command list state");
	const auto value = [](const wchar_t *name) { return MigotoCommandVmUtils::LookupVariable({}, name); };
	auto flags = DX12HotPathState::SourceState::Capture().BuildHookWorkFlags();
	auto callbacks = RecordingDrawCallbacks();
	recordedDraws.clear();
	DX12WrapperRuntimeUtils::DrawInstanced(native.list.Get(), state, 16777217, 0, 11, 2,
		callbacks.drawInstanced, flags);
	Require(value(L"$outside") == 0 && value(L"$visits") == 1 && value(L"$kind") == 3 &&
		value(L"$vertices") == 16777216 && value(L"$indices") == 0 && value(L"$instances") == 0 &&
		value(L"$base") == 11 && value(L"$start") == 0 && value(L"$first") == 2 &&
		value(L"$nonempty") == 0, "non-indexed zero-instance draw and float32 vertex count");
	for (int i = 0; i < 2; ++i)
		DX12WrapperRuntimeUtils::DrawIndexedInstanced(native.list.Get(), state, 0, 3, 4, -3, 7,
			callbacks.drawIndexedInstanced, flags);
	Require(value(L"$visits") == 3 && value(L"$nonempty") == 2 && value(L"$kind") == 4 &&
		value(L"$vertices") == 0 && value(L"$indices") == 0 && value(L"$instances") == 3 &&
		value(L"$base") == 4294967296.0 && value(L"$start") == 4 && value(L"$first") == 7 &&
		recordedDraws.size() == 3, "identical draws re-evaluate script and keep signed native base");
	Require(recordedDraws.back()[4] == -3 && value(L"draw_type") == 0 && value(L"first_vertex") == 0,
		"draw context lifetime and native base");
	load("run=CommandListMixed\npost=CommandListAfter\n");
	wrapped->SetPipelineState(pipeline.Get());
	flags = DX12HotPathState::SourceState::Capture().BuildHookWorkFlags();
	DX12WrapperRuntimeUtils::DrawIndexedInstanced(native.list.Get(), state, 6, 1, 2, -1, 3,
		callbacks.drawIndexedInstanced, flags);
	Require(value(L"$indices") == 6 && value(L"$after") == 4294967296.0 && value(L"$after_kind") == 4,
		"unbound optional target preserves pre actions and post uses the same indexed draw");
	load("post=CommandListAfter\n");
	wrapped->SetPipelineState(pipeline.Get());
	flags = DX12HotPathState::SourceState::Capture().BuildHookWorkFlags();
	DX12WrapperRuntimeUtils::DrawInstanced(native.list.Get(), state, 0, 0, 17, 0,
		callbacks.drawInstanced, flags);
	Require(value(L"$after") == 17 && value(L"$after_kind") == 3,
		"post-only shader override observes a zero-count draw");
	Check(wrapped->Close());
}

void TestPreSkinDispatchParameters(ID3D12Device *device)
{
	ComPtr<ID3D12Device> wrappedDevice = device;
	void *pointer = wrappedDevice.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12Device), &pointer), "compute device wrapper");
	wrappedDevice.Attach(static_cast<ID3D12Device*>(pointer));

	D3D12_DESCRIPTOR_RANGE ranges[] = {
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0},
		{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1}};
	D3D12_ROOT_PARAMETER table{};
	table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	table.DescriptorTable = {2, ranges};
	D3D12_ROOT_SIGNATURE_DESC rootDesc{1, &table};
	ComPtr<ID3DBlob> blob;
	Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr));
	ComPtr<ID3D12RootSignature> root;
	Check(wrappedDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
	constexpr char shader[] =
		"StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> dst : register(u0);"
		"[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { if (all(id == 0)) dst[0] = src[0]; }";
	ComPtr<ID3DBlob> cs;
	Check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs, nullptr));
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = root.Get();
	desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
	ComPtr<ID3D12PipelineState> pso;
	Check(wrappedDevice->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso)));
	DX12PsoShaderInfo shaderInfo{};
	Require(DX12GetPipelineStateShaderInfo(DX12WrapperRuntimeUtils::UnwrapPipelineState(pso.Get()), &shaderInfo)
		&& shaderInfo.hasCS, "registered compute pipeline");
	TestHuntingKeyDispatch(DX12WrapperRuntimeUtils::UnwrapPipelineState(pso.Get()), shaderInfo.cs);

	const auto stem = L"dispatch-context-" + std::to_wstring(GetCurrentProcessId());
	const auto iniPath = std::filesystem::current_path() / (stem + L".ini");
	const auto dataPath = std::filesystem::current_path() / (stem + L".buf");
	auto load = [&](uint32_t viewHash, bool supplyResource) {
		std::ofstream ini(iniPath);
		ini << "[Constants]\nglobal $after = -1\n[ShaderOverrideCompute]\nhash=" << std::hex << shaderInfo.cs
			<< "\nchecktextureoverride=cs-t0\n[TextureOverrideSkin]\nhash=01234567\ncs_hash="
			<< shaderInfo.cs << "\nmatch_cs_t0_hash=" << viewHash
			<< "\ncs-t0=ResourceInput\nhandling=skip\ndispatch=2,3,4\n"
			"$visits=$visits+1\n$kind=draw_type\n$x=thread_group_count_x\n"
			"local $y=thread_group_count_y\nif draw_type == 8\nx87=$y\nendif\n"
			"post $after=$y + thread_group_count_z + $boundary\n";
		if (supplyResource) {
			std::array<UINT, 64> contents{77};
			std::ofstream data(dataPath, std::ios::binary);
			data.write(reinterpret_cast<const char*>(contents.data()), sizeof(contents));
			ini << "[ResourceInput]\ntype=Buffer\nstride=4\nfilename=" << dataPath.filename().string() << '\n';
		}
		ini.close();
		const bool loaded = DX12ModRuntimeLoad(iniPath.c_str());
		std::filesystem::remove(iniPath);
		Require(loaded && DX12ModNeedsPreSkinningUavProbe(), "loaded compute texture override");
		DX12ModRefreshRuntimeStateAfterReload();
	};
	load(1, false);
	auto input = Buffer(wrappedDevice.Get(), D3D12_HEAP_TYPE_UPLOAD);
	Write(input.Get(), 11);
	auto readback = Buffer(device, D3D12_HEAP_TYPE_READBACK);
	D3D12_RESOURCE_DESC outputDesc = input->GetDesc();
	outputDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_HEAP_PROPERTIES properties{};
	properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	ComPtr<ID3D12Resource> output, initialOutput;
	Check(wrappedDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &outputDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&initialOutput)));
	Check(wrappedDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &outputDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)));
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 257,
		D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
	ComPtr<ID3D12DescriptorHeap> heap;
	Check(wrappedDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));
	const UINT stride = wrappedDevice->GetDescriptorHandleIncrementSize(heapDesc.Type);
	auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += 255 * SIZE_T(stride);
	auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += 255 * UINT64(stride);
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heapDesc.NumDescriptors = 3;
	ComPtr<ID3D12DescriptorHeap> staging;
	Check(wrappedDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&staging)));
	auto stagingSrv = staging->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE stagingUav{stagingSrv.ptr + 2 * stride};
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srv.Buffer.StructureByteStride = 4;
	srv.Buffer.NumElements = 64;
	wrappedDevice->CreateShaderResourceView(input.Get(), &srv, cpu);
	wrappedDevice->CreateShaderResourceView(input.Get(), &srv, stagingSrv);
	D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
	uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uav.Buffer.StructureByteStride = 4;
	uav.Buffer.NumElements = 64;
	D3D12_CPU_DESCRIPTOR_HANDLE liveUav{cpu.ptr + stride}, stagingOutput{stagingSrv.ptr + stride};
	wrappedDevice->CreateUnorderedAccessView(initialOutput.Get(), nullptr, &uav, stagingOutput);
	wrappedDevice->CreateUnorderedAccessView(nullptr, nullptr, &uav, stagingUav);
	D3D12_CPU_DESCRIPTOR_HANDLE sources[]{stagingSrv, stagingUav};
	UINT count = 2;
	wrappedDevice->CopyDescriptors(1, &cpu, &count, 2, sources, nullptr, heapDesc.Type);
	wrappedDevice->CopyDescriptorsSimple(1, stagingUav, stagingOutput, heapDesc.Type);
	wrappedDevice->CopyDescriptorsSimple(1, liveUav, stagingUav, heapDesc.Type);
	// An interop producer can write the CPU slot through a native device. The
	// repeated copy must observe its actual contents even with unchanged logical metadata.
	device->CreateUnorderedAccessView(DX12WrapperRuntimeUtils::UnwrapResource(output.Get()), nullptr, &uav, stagingUav);
	wrappedDevice->CopyDescriptorsSimple(1, liveUav, stagingUav, heapDesc.Type);
	wrappedDevice->CreateShaderResourceView(nullptr, &srv, cpu);
	wrappedDevice->CopyDescriptorsSimple(1, cpu, stagingSrv, heapDesc.Type);
	// Reusing the CPU sources must not change either live table descriptor.
	wrappedDevice->CreateShaderResourceView(nullptr, &srv, stagingSrv);
	wrappedDevice->CreateUnorderedAccessView(nullptr, nullptr, &uav, stagingUav);

	List recording(wrappedDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto *native = static_cast<ID3D12GraphicsCommandList*>(DX12WrapperRuntimeUtils::UnwrapCommandList(recording.list.Get()));
	auto bind = [&] {
		recording.list->SetPipelineState(pso.Get());
		recording.list->SetComputeRootSignature(root.Get());
		ID3D12DescriptorHeap *heaps[]{heap.Get()};
		recording.list->SetDescriptorHeaps(1, heaps);
		recording.list->SetComputeRootDescriptorTable(0, gpu);
	};
	bind();
	std::vector<DX12CurrentComputeUavBinding> uavs, srvs, cbvs;
	std::vector<DX12CurrentRootConstants> constants;
	Require(DX12BindingTracker::GetCurrentComputeAll(native, &uavs, &srvs, &cbvs, &constants)
		&& uavs.size() == 1 && srvs.size() == 1, "collect real compute views");
	const uint32_t viewHash = HashComputeBufferBinding(srvs.front());
	Require(viewHash != 0 && viewHash != 1, "resolved live SRV hash");
	const auto value = [](const wchar_t *name) { return MigotoCommandVmUtils::LookupVariable({}, name); };
	const auto dispatch = [](ID3D12GraphicsCommandList *list, UINT x, UINT y, UINT z) {
		recordedDraws.push_back({x, y, z, static_cast<int64_t>(MigotoCommandVmUtils::LookupVariable({}, L"$after"))});
		MigotoCommandVmUtils::SetGlobal(L"$boundary", 10);
		list->Dispatch(x, y, z);
	};
	auto captureOutput = [&](UINT offset) {
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition = {DX12WrapperRuntimeUtils::UnwrapResource(output.Get()), 0,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
		native->ResourceBarrier(1, &barrier);
		native->CopyBufferRegion(readback.Get(), offset, barrier.Transition.pResource, 0, sizeof(UINT));
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		native->ResourceBarrier(1, &barrier);
	};
	recordedDraws.clear();
	DX12WrapperRuntimeUtils::Dispatch(native, 1, 1, 1, dispatch);
	Require(value(L"$visits") == 0 && value(L"$after") == -1, "SRV mismatch bypasses the script");
	captureOutput(0);
	load(viewHash, false);
	bind();
	for (UINT y : {3u, 3u, 7u}) {
		MigotoCommandVmUtils::SetGlobal(L"$boundary", 0);
		const auto previous = value(L"$after");
		DX12WrapperRuntimeUtils::Dispatch(native, 0, y, 5, dispatch);
		Require(value(L"$kind") == 8 && value(L"$x") == 0 && value(L"x87") == y &&
			value(L"$after") == y + 15 && recordedDraws.back()[3] == previous,
			"pre and post resolve original groups across the native call on failed replacement");
	}
	Require(value(L"$visits") == 3 && recordedDraws.size() == 4 && recordedDraws.back()[1] == 7,
		"failed patch retries the matched script on identical bindings and arguments");
	load(viewHash, true);
	bind();
	DX12WrapperRuntimeUtils::Dispatch(native, 6, 8, 9, dispatch);
	Require(recordedDraws.back() == std::array<int64_t, 6>{2, 3, 4, -1, 0, 0} &&
		value(L"$x") == 6 && value(L"x87") == 8 && value(L"$after") == 27,
		"successful patch forwards replacement groups and exposes original groups in post");
	captureOutput(sizeof(UINT));
	DX12WrapperRuntimeUtils::Dispatch(native, 2, 5, 1, dispatch);
	Require(value(L"$visits") == 2 && value(L"$x") == 2 && value(L"x87") == 5 && value(L"$after") == 16,
		"cached replacement executes commands with this dispatch's parameters");
	captureOutput(2 * sizeof(UINT));
	Check(recording.list->Close());
	auto queue = Queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ID3D12CommandList *batch[]{native};
	queue->ExecuteCommandLists(1, batch);
	Drain(device, queue.Get());
	void *mapped = nullptr;
	D3D12_RANGE reads{0, 3 * sizeof(UINT)};
	Check(readback->Map(0, &reads, &mapped));
	const auto *values = static_cast<const UINT*>(mapped);
	const bool correct = values[0] == 11 && values[1] == 77 && values[2] == 77;
	D3D12_RANGE noWrites{};
	readback->Unmap(0, &noWrites);
	std::filesystem::remove(dataPath);
	Require(correct, "native CS reads original then replacement SRVs and preserves the live UAV across staging reuse");
}

void TestAutomaticDraws(ID3D12Device *device)
{
	Bunny::IniDocument ini;
	Require(ini.Parse(L"[Present]\nrun=CommandListAuto\n[CommandListAuto]\n"
		L"draw=auto\ndrawindexed=auto\ndrawindexedinstanced=auto\n"), "automatic draw config");
	Bunny::CommandListMap lists;
	Bunny::MigotoCommandListParser::ParseSections(ini, &lists);
	const bool needsIa = AutomaticDrawsNeedIaState(lists, {}, {}, {});
	Require(needsIa, "Present-only mod needs resident IA through a nested list");
	lists.erase(L"Present");
	Require(!AutomaticDrawsNeedIaState(lists, {}, {}, {}), "unused automatic draws leave IA tracking idle");
	std::unordered_map<std::wstring, DX12CompiledCommandListPlan> plans;
	BuildDx12CompiledCommandListPlans(lists, &plans);
	const auto &plan = plans.at(L"CommandListAuto");
	Require(!plan.unsupported, "automatic draws compile without freezing their counts");

	const LONG previousIa = InterlockedExchange(&gHasIaResidentStateRequirements, needsIa);
	const LONG previousPresent = InterlockedExchange(&gHasPresentRuntimeEffect, 1);
	DX12HotPathState::Update();
	List native(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	ComPtr<ID3D12GraphicsCommandList> wrapped = native.list;
	void *pointer = wrapped.Detach();
	Require(DX12Wrapper::WrapD3D12DeviceResult(__uuidof(ID3D12GraphicsCommandList), &pointer), "auto draw wrapper");
	wrapped.Attach(static_cast<ID3D12GraphicsCommandList*>(pointer));
	auto *state = DX12CommandListRuntime::GetStatePtrFast(native.list.Get());
	Require(state != nullptr, "auto draw wrapper state");
	auto buffer = Buffer(device, D3D12_HEAP_TYPE_UPLOAD);
	const auto address = buffer->GetGPUVirtualAddress();
	D3D12_VERTEX_BUFFER_VIEW vb{address + 16, 35, 8};
	D3D12_INDEX_BUFFER_VIEW ib{address + 32, 18, DXGI_FORMAT_R16_UINT};
	wrapped->IASetVertexBuffers(0, 1, &vb);
	wrapped->IASetIndexBuffer(&ib);

	const DX12IaHashState hashes{};
	DX12ModIaReplacement replacement;
	DX12CommandListExecutor vm(device, native.list.Get(), hashes, {}, &replacement);
	vm.SetExecutionMode(true, true);
	vm.ExecuteCompiledCommandListPlan(plan, 0, false);
	auto callbacks = RecordingDrawCallbacks();
	callbacks.setIndexBuffer = [](ID3D12GraphicsCommandList *list, const D3D12_INDEX_BUFFER_VIEW *view) {
		list->IASetIndexBuffer(view);
	};
	callbacks.setVertexBuffers = [](ID3D12GraphicsCommandList *list, UINT start, UINT count, const D3D12_VERTEX_BUFFER_VIEW *views) {
		list->IASetVertexBuffers(start, count, views);
	};
	const DX12IaReplacementDrawExecutor draws(native.list.Get(), callbacks);
	const auto caller = DX12IaDrawInvocation::ForDrawInstanced(7, 3, 0, 5);
	const auto execute = [&](const DX12IaDrawInvocation *original) {
		recordedDraws.clear();
		draws.ExecuteReplacementDraws(replacement, state->ia, original);
	};
	using Draws = std::vector<std::array<int64_t, 6>>;
	execute(&caller);
	Require(recordedDraws == Draws{{0,4,1,0,0,0}, {1,9,1,0,0,0}, {1,9,3,0,0,5}},
		"effective subviews supply truncated counts, only instanced auto inherits the caller");
	execute(nullptr);
	Require(recordedDraws == Draws{{0,4,1,0,0,0}, {1,9,1,0,0,0}}, "no calling draw for instanced auto");

	replacement.vertexBuffers = {{}, {address, 40, 4}};
	replacement.hasIndexBuffer = true;
	replacement.indexBuffer = {address, 20, DXGI_FORMAT_R32_UINT};
	DX12IaReplacementBindingExecutor binding(native.list.Get(), callbacks);
	const DX12ActiveIaState originalIa = state->ia;
	binding.Apply(replacement);
	execute(&caller);
	Require(recordedDraws == Draws{{0,4,1,0,0,0}, {1,5,1,0,0,0}, {1,5,3,0,0,5}},
		"sparse VB replacement preserves slot zero and IB replacement changes count");
	binding.Restore(replacement, originalIa);
	replacement.vertexBuffers.clear();
	replacement.hasIndexBuffer = false;
	vb.StrideInBytes = 0;
	wrapped->IASetVertexBuffers(0, 1, &vb);
	wrapped->IASetIndexBuffer(nullptr);
	execute(&caller);
	Require(recordedDraws.empty(), "zero stride and unbound index buffer skip automatic draws");
	replacement.hasIndexBuffer = true;
	replacement.indexBuffer = {address, 20, DXGI_FORMAT_UNKNOWN};
	replacement.vertexBuffers = {{address, 3, 4}};
	execute(&caller);
	Require(recordedDraws.empty(), "invalid index format and incomplete vertex skip automatic draws");
	replacement.indexBuffer = {};
	replacement.vertexBuffers = {{address, 24, 4}};
	execute(&caller);
	Require(recordedDraws == Draws{{0,6,1,0,0,0}}, "new slot-zero replacement restores automatic vertex count");

	Check(wrapped->Close());
	InterlockedExchange(&gHasPresentRuntimeEffect, previousPresent);
	InterlockedExchange(&gHasIaResidentStateRequirements, previousIa);
	DX12HotPathState::Update();
}

void TestDescriptors(ID3D12Device *device, bool benchmark)
{
	constexpr auto type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 256;
	desc.Type = type;
	ComPtr<ID3D12DescriptorHeap> source, dest;
	Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&source)));
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&dest)));
	for (auto *heap : {source.Get(), dest.Get()}) {
		void *ptr = heap;
		const auto heapDesc = heap->GetDesc();
		DX12ResourceTrackerHookRuntime::RecordCreatedDescriptorHeap(S_OK, device, &heapDesc, &ptr);
	}
	const UINT stride = device->GetDescriptorHandleIncrementSize(type);
	D3D12_CPU_DESCRIPTOR_HANDLE src[8], dst[8];
	for (UINT i = 0; i < 8; ++i) {
		src[i].ptr = source->GetCPUDescriptorHandleForHeapStart().ptr + i * 13 * stride;
		dst[i].ptr = dest->GetCPUDescriptorHandleForHeapStart().ptr + (7 - i) * stride;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = DXGI_FORMAT_R32_UINT;
		srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Buffer.NumElements = i + 1;
		device->CreateShaderResourceView(nullptr, &srv, src[i]);
		DX12ResourceTrackerHookRuntime::RecordCreatedShaderResourceView(device, nullptr, &srv, src[i]);
	}
	D3D12_CPU_DESCRIPTOR_HANDLE reversed[8];
	std::reverse_copy(src, src + 8, reversed);
	for (bool changing : {false, true}) {
		const int calls = benchmark ? 50000 : 2;
		for (int pass = 0; pass < (benchmark ? 5 : 1); ++pass) {
			const auto begin = std::chrono::steady_clock::now();
			for (int i = 0; i < calls; ++i) {
				const auto *sources = changing && !(i % 2) ? reversed : src;
				DX12ResourceTrackerHookRuntime::RecordCopiedDescriptors(
					device, 8, dst, nullptr, 8, sources, nullptr, type);
			}
			if (benchmark) {
				auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin);
				printf("descriptor %s ns/call %.1f\n", changing ? "changed" : "unchanged", ns.count() / calls);
			}
		}
	}
	for (UINT i = 0; i < 8; ++i) {
		DescriptorRecord record;
		Require(DX12DescriptorHeapShadowUtils::FindLatestDescriptor(dst[i].ptr, &record) &&
			record.srv.Buffer.NumElements == i + 1, "scattered descriptor value");
	}
}
}

int main(int argc, char **argv)
{
	try {
		if (argc == 2 && !strcmp(argv[1], "--early-config")) {
			TestEarlyConfig();
			return 0;
		}
		const bool benchmark = argc == 2 && !strcmp(argv[1], "--descriptors");
		ComPtr<ID3D12Debug> debug;
		if (!benchmark && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
			debug->EnableDebugLayer();
		ComPtr<IDXGIFactory4> factory;
		Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
		ComPtr<IDXGIAdapter> warp;
		Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
		ComPtr<ID3D12Device> device;
		Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
		TestDescriptors(device.Get(), benchmark);
		if (benchmark)
			return 0;
		TestConfiguredInputScopes();
		TestFileConditions();
		TestShaderRegexExclusions();
		TestLockedVariables();
		TestInactiveUserSettings();
		TestUnknownSettingsCleanup();
		TestSettingsHotkeys();
		TestHuntingKeyBindings();
		TestControllerInputs();
		TestSharedModTriggers();
		TestKeyTransitionClock();
		TestWipeUserConfig();
		TestLogicalExpressions();
		TestExpressionNumbers();
		TestUnaryMath();
		TestFrameTiming();
		TestRandomExpression();
		TestInstancedDraws(device.Get());
		TestAutomaticDraws(device.Get());
		TestReadback(device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
		TestReadback(device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
		TestQueueHandoff(device.Get());
		TestTexture(device.Get(), false);
		TestTexture(device.Get(), true);
		TestTexture(device.Get(), true, true);
		TestUploadFailure(device.Get());
		TestBarrierGroups(device.Get());
		TestWrapperState(device.Get());
		TestRenderPass(device.Get());
		TestSwapChain(factory.Get(), device.Get());
		TestOverlay(device.Get());
		TestDrawCallParameters(device.Get());
		TestPreSkinDispatchParameters(device.Get());
		if (debug) {
			ComPtr<ID3D12InfoQueue> info;
			Check(device.As(&info));
			for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
				SIZE_T size = 0;
				Check(info->GetMessage(i, nullptr, &size));
				std::vector<BYTE> storage(size);
				auto *msg = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
				Check(info->GetMessage(i, msg, &size));
				Require(msg->Severity > D3D12_MESSAGE_SEVERITY_ERROR, msg->pDescription);
			}
			puts("D3D12 debug layer checked");
		}
		Check(device->GetDeviceRemovedReason());
		puts("PASS DX12 WARP runtime audit");
		return 0;
	} catch (const std::exception &error) {
		fprintf(stderr, "FAIL %s\n", error.what());
		return 1;
	}
}
