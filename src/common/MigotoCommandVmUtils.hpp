#pragma once

#include <math.h>
#include <share.h>
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <cwchar>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "MigotoIniCommandListUtils.hpp"

class MigotoCommandVmUtils
{
public:
	// A variable declared with the "persist" prefix in [Constants]. DX11 holds
	// the same data in CommandListVariable::name plus VariableFlags::PERSIST;
	// `name` here is already collapsed onto the flat global the DX12 VM uses
	// ("$swapkey0", not "$\LiuYin\swapkey0"), because that is the name that has
	// to be written to d3dx_user.ini for the file to resolve back to the same
	// slot on the next load. `key` is the lookup form used to match writes.
	class PersistedVariable
	{
	public:
		std::wstring name;
		std::wstring key;
	};

	class VariableStore
	{
	public:
		inline static std::mutex globalsMutex;
		inline static std::unordered_map<std::wstring, double> globals;
		// Persist registry (DX11 parity for d3dx_user.ini):
		//   - `persistedVariables` holds every variable declared with the
		//     "persist" prefix, in declaration order, exactly as DX11's
		//     persistent_variables vector does;
		//   - `persistedKeys` indexes them by a namespace-collapsed key so a
		//     write through the DX12 VM's flattened name ("$swapkey0") matches
		//     an entry declared as "$\LiuYin\swapkey0";
		//   - `persistDirty` is the equivalent of DX11's user_config_dirty: it
		//     is set only when a persist variable's value actually changes, so
		//     an idle session never rewrites the file.
		// All three are guarded by globalsMutex; persistDirty is duplicated in
		// an atomic so the per-write check is a single relaxed load.
		inline static std::vector<PersistedVariable> persistedVariables;
		inline static std::unordered_set<std::wstring> persistedKeys;
		inline static std::atomic<int> persistDirty = 0;
		// Set once a command-list expression references any per-frame variable
		// (window size, cursor, DPI, hunting/analysis flags). DX11 evaluates
		// these lazily at match time; DX12 pre-collects them at Present, so
		// without this gate a game with command lists that never reference
		// frame variables still pays swap-chain desc queries, cursor polls and
		// ~20 global-map writes on every frame. Cleared on reload; re-armed by
		// the next expression evaluation.
		inline static std::atomic<int> frameVariableReferenced = 0;
	};

	class ExpressionContext
	{
	public:
		std::vector<std::unordered_map<std::wstring, double>> *localScopes = nullptr;
	};

	class ExpressionParser
	{
	public:
		const std::wstring *text = nullptr;
		size_t pos = 0;
		ExpressionContext context;
		bool ok = true;

		ExpressionParser(const std::wstring &value, const ExpressionContext &evalContext)
			: text(&value), context(evalContext)
		{
		}

		double Parse()
		{
			double value = ParseOr();
			SkipSpaces();
			if (pos < text->size())
				ok = false;
			return ok ? value : 0.0;
		}

		double ParseOr()
		{
			double lhs = ParseAnd();
			while (Match(L"||")) {
				// Parse the right operand unconditionally: a C++ short-circuit
				// ("Truthy(lhs) || Truthy(ParseAnd())") would leave the tokens
				// unconsumed, and Parse()'s end-of-text check would then fail
				// the whole expression to 0 - turning "1 || 0" into false.
				const double rhs = ParseAnd();
				lhs = (Truthy(lhs) || Truthy(rhs)) ? 1.0 : 0.0;
			}
			return lhs;
		}

		double ParseAnd()
		{
			double lhs = ParseEquality();
			while (Match(L"&&")) {
				const double rhs = ParseEquality();
				lhs = (Truthy(lhs) && Truthy(rhs)) ? 1.0 : 0.0;
			}
			return lhs;
		}

		double ParseEquality()
		{
			double lhs = ParseRelational();
			for (;;) {
				if (Match(L"==")) {
					lhs = fabs(lhs - ParseRelational()) < 0.000001 ? 1.0 : 0.0;
				} else if (Match(L"!=")) {
					lhs = fabs(lhs - ParseRelational()) >= 0.000001 ? 1.0 : 0.0;
				} else {
					return lhs;
				}
			}
		}

		double ParseRelational()
		{
			double lhs = ParseAdditive();
			for (;;) {
				if (Match(L">=")) {
					lhs = lhs >= ParseAdditive() ? 1.0 : 0.0;
				} else if (Match(L"<=")) {
					lhs = lhs <= ParseAdditive() ? 1.0 : 0.0;
				} else if (Match(L">")) {
					lhs = lhs > ParseAdditive() ? 1.0 : 0.0;
				} else if (Match(L"<")) {
					lhs = lhs < ParseAdditive() ? 1.0 : 0.0;
				} else {
					return lhs;
				}
			}
		}

		double ParseAdditive()
		{
			double lhs = ParseMultiplicative();
			for (;;) {
				if (Match(L"+")) {
					lhs += ParseMultiplicative();
				} else if (Match(L"-")) {
					lhs -= ParseMultiplicative();
				} else {
					return lhs;
				}
			}
		}

		double ParseMultiplicative()
		{
			double lhs = ParseUnary();
			for (;;) {
				if (Match(L"//")) {
					double rhs = ParseUnary();
					lhs = fabs(rhs) < 0.000001 ? 0.0 : floor(lhs / rhs);
				} else if (Match(L"*")) {
					lhs *= ParseUnary();
				} else if (Match(L"/")) {
					double rhs = ParseUnary();
					lhs = fabs(rhs) < 0.000001 ? 0.0 : lhs / rhs;
				} else if (Match(L"%")) {
					double rhs = ParseUnary();
					lhs = fabs(rhs) < 0.000001 ? 0.0 : fmod(lhs, rhs);
				} else {
					return lhs;
				}
			}
		}

		double ParseUnary()
		{
			if (Match(L"!"))
				return Truthy(ParseUnary()) ? 0.0 : 1.0;
			if (Match(L"-"))
				return -ParseUnary();
			if (Match(L"+"))
				return ParseUnary();
			return ParsePrimary();
		}

		double ParsePrimary()
		{
			SkipSpaces();
			if (Match(L"(")) {
				double value = ParseOr();
				if (!Match(L")"))
					ok = false;
				return value;
			}
			if (pos < text->size() && IsNumberStart((*text)[pos]))
				return ParseNumber();
			if (pos < text->size() && IsIdentifierStart((*text)[pos]))
				return LookupIdentifier(ParseIdentifier());
			ok = false;
			return 0.0;
		}

		bool Match(const wchar_t *op)
		{
			SkipSpaces();
			std::wstring token(op);
			if (text->compare(pos, token.size(), token) != 0)
				return false;
			pos += token.size();
			return true;
		}

		void SkipSpaces()
		{
			while (pos < text->size() && ((*text)[pos] == L' ' || (*text)[pos] == L'\t'))
				++pos;
		}

		static bool IsNumberStart(wchar_t ch)
		{
			return (ch >= L'0' && ch <= L'9') || ch == L'.';
		}

		static bool IsIdentifierStart(wchar_t ch)
		{
			return ch == L'$' || ch == L'_' ||
				(ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
		}

		static bool IsIdentifierChar(wchar_t ch)
		{
			// '\\' is legal inside namespaced variable references of the form
			// $\namespace\var (DX11 CommandList.cpp tokenizes them the same
			// way); no expression operator uses it.
			return IsIdentifierStart(ch) || ch == L'\\' ||
				(ch >= L'0' && ch <= L'9');
		}

		double ParseNumber()
		{
			const wchar_t *start = text->c_str() + pos;
			wchar_t *end = nullptr;
			double value = wcstod(start, &end);
			if (!end || end == start) {
				ok = false;
				return 0.0;
			}
			pos += static_cast<size_t>(end - start);
			return value;
		}

		std::wstring ParseIdentifier()
		{
			size_t start = pos;
			while (pos < text->size() && IsIdentifierChar((*text)[pos]))
				++pos;
			return text->substr(start, pos - start);
		}

		double LookupIdentifier(const std::wstring &name)
		{
			return MigotoCommandVmUtils::LookupVariable(context, name);
		}
	};

	class BranchFrame
	{
	public:
		bool parentActive = true;
		bool active = true;
		bool branchTaken = false;
	};

	// A single deferred "post" assignment queued while a command list runs
	// and evaluated when its scope flushes. `constantTarget` marks writes to
	// the custom shader constants (x87/...): those are stored in the same
	// global table as $variables, but the target name has no '$' prefix, so
	// the flush path must not apply ExtractVariableName-style filtering.
	class PostAssignment
	{
	public:
		std::wstring name;
		std::wstring expression;
		bool constantTarget = false;
	};

	class State
	{
	public:
		std::vector<std::unordered_map<std::wstring, double>> localScopes;
		std::vector<BranchFrame> branches;
		std::vector<PostAssignment> postAssignments;

		void PushLocalScope()
		{
			localScopes.emplace_back();
		}

		void PopLocalScope()
		{
			if (!localScopes.empty())
				localScopes.pop_back();
		}

		bool Active() const
		{
			return branches.empty() ? true : branches.back().active;
		}

		ExpressionContext Context()
		{
			ExpressionContext context;
			context.localScopes = &localScopes;
			return context;
		}

		double Evaluate(const std::wstring &expression)
		{
			ExpressionParser parser(expression, Context());
			return parser.Parse();
		}

		bool EvaluateBool(const std::wstring &expression)
		{
			return Truthy(Evaluate(expression));
		}

		void DeclareLocal(const std::wstring &key, const std::wstring &value)
		{
			if (localScopes.empty())
				PushLocalScope();
			std::wstring name = ExtractVariableName(key);
			if (name.empty())
				return;
			localScopes.back()[name] = value.empty() ? 0.0 : Evaluate(value);
		}

		void Assign(const std::wstring &key, const std::wstring &value)
		{
			std::wstring name = ExtractVariableName(key);
			if (name.empty())
				return;
			AssignResolvedName(name, value.empty() ? 0.0 : Evaluate(value));
		}

		void AssignSymbol(const std::wstring &key, const std::wstring &value)
		{
			std::wstring name = NormalizeVariableName(key);
			if (!name.empty())
				AssignResolvedName(name, value.empty() ? 0.0 : Evaluate(value));
		}

		void AssignResolvedName(const std::wstring &name, double evaluated)
		{
			for (auto scope = localScopes.rbegin(); scope != localScopes.rend(); ++scope) {
				auto found = scope->find(name);
				if (found != scope->end()) {
					found->second = evaluated;
					return;
				}
			}
			SetGlobal(name, evaluated);
		}

		void QueuePostAssign(const std::wstring &key, const std::wstring &value)
		{
			std::wstring name = ExtractVariableName(key);
			if (!name.empty()) {
				postAssignments.push_back(PostAssignment{ name, value, false });
				return;
			}

			// Shader-constant targets arrive as "post x87" - a prefix the
			// '$'-only ExtractVariableName rejects. Strip the prefix and keep
			// the assignment only when the remainder is a supported constant
			// name, so unrecognised deferred targets stay visible as silent
			// drops instead of corrupting the global table.
			std::wstring text = MigotoIniCommandListUtils::ToLower(
				MigotoIniCommandListUtils::Trim(key));
			if (text.rfind(L"post ", 0) != 0)
				return;
			text = MigotoIniCommandListUtils::Trim(text.substr(5));
			if (text.empty() || !MigotoIniCommandListUtils::IsShaderConstantKey(text))
				return;
			postAssignments.push_back(PostAssignment{ text, value, true });
		}

		void FlushPostAssignments()
		{
			std::vector<PostAssignment> pending;
			pending.swap(postAssignments);
			for (const PostAssignment &assignment : pending) {
				const double evaluated = Evaluate(assignment.expression);
				if (assignment.constantTarget) {
					// Mirror a live "x87 = expr" write (AssignSymbol ->
					// AssignResolvedName) so deferred and immediate constant
					// assignments share one write path. Constant names never
					// collide with '$' locals, so this resolves to the same
					// global slot SetGlobal would use.
					AssignResolvedName(assignment.name, evaluated);
				} else {
					SetGlobal(assignment.name, evaluated);
				}
			}
		}

		void ApplyControlFlow(const std::wstring &rawLine)
		{
			std::wstring line = MigotoIniCommandListUtils::ToLower(MigotoIniCommandListUtils::Trim(rawLine));
			if (line.rfind(L"if ", 0) == 0) {
				bool parent = Active();
				bool condition = parent && EvaluateBool(line.substr(3));
				BranchFrame frame;
				frame.parentActive = parent;
				frame.active = condition;
				frame.branchTaken = condition;
				branches.push_back(frame);
				return;
			}
			if (line.rfind(L"elif ", 0) == 0 || line.rfind(L"else if ", 0) == 0) {
				if (branches.empty())
					return;
				BranchFrame &frame = branches.back();
				size_t prefix = line.rfind(L"elif ", 0) == 0 ? 5 : 8;
				bool condition = frame.parentActive && !frame.branchTaken && EvaluateBool(line.substr(prefix));
				frame.active = condition;
				frame.branchTaken = frame.branchTaken || condition;
				return;
			}
			if (line == L"else") {
				if (branches.empty())
					return;
				BranchFrame &frame = branches.back();
				frame.active = frame.parentActive && !frame.branchTaken;
				frame.branchTaken = true;
				return;
			}
			if (line == L"endif") {
				if (!branches.empty())
					branches.pop_back();
			}
		}
	};

	static bool Truthy(double value)
	{
		return fabs(value) > 0.000001;
	}

	static double EvaluateExpression(ExpressionContext context, const std::wstring &expression)
	{
		ExpressionParser parser(expression, context);
		return parser.Parse();
	}

	static std::wstring NormalizeVariableName(const std::wstring &value)
	{
		std::wstring normalized = MigotoIniCommandListUtils::ToLower(MigotoIniCommandListUtils::Trim(value));
		// Collapse DX11-style namespaced global references ("$\namespace\var")
		// onto the flat global name ("$var"). The DX12 VM stores every global
		// without namespacing, so the namespace qualifier names the same
		// variable; without this, reads of "$\Default\swapkey0" would miss the
		// "$swapkey0" written by the Default namespace's [Constants] and keys.
		if (normalized.size() > 2 && normalized[0] == L'$' && normalized[1] == L'\\') {
			const size_t lastSeparator = normalized.rfind(L'\\');
			if (lastSeparator > 1 && lastSeparator + 1 < normalized.size())
				normalized = L"$" + normalized.substr(lastSeparator + 1);
		}
		return normalized;
	}

	// Cold path: runs once per referenced variable per command-list execution,
	// so a linear scan over the ~22 per-frame variable names is fine. The flag
	// gate first makes the common case (already referenced) a single relaxed
	// load.
	static void MarkFrameVariableReferenced(const std::wstring &normalized)
	{
		if (VariableStore::frameVariableReferenced.load(std::memory_order_relaxed))
			return;
		static const wchar_t *const kFrameVariables[] = {
			L"window_width", L"window_height",
			L"res_width", L"res_height",
			L"rt_width", L"rt_height",
			L"cursor_screen_x", L"cursor_screen_y",
			L"cursor_window_x", L"cursor_window_y",
			L"cursor_x", L"cursor_y",
			L"cursor_showing", L"cursor_visible",
			L"cursor_hotspot_x", L"cursor_hotspot_y",
			L"effective_dpi", L"hunting", L"frame_analysis", L"sli",
			L"stereo_active", L"stereo_available",
		};
		for (const wchar_t *candidate : kFrameVariables) {
			if (normalized == candidate) {
				VariableStore::frameVariableReferenced.store(
					1, std::memory_order_release);
				return;
			}
		}
	}

	static std::wstring ExtractVariableName(const std::wstring &key)
	{
		std::wstring text = MigotoIniCommandListUtils::ToLower(MigotoIniCommandListUtils::Trim(key));
		const wchar_t *prefixes[] = { L"post ", L"local ", L"global persist ", L"global ", L"persist " };
		for (const wchar_t *prefix : prefixes) {
			std::wstring p(prefix);
			if (text.rfind(p, 0) == 0) {
				text = MigotoIniCommandListUtils::Trim(text.substr(p.size()));
				break;
			}
		}
		return text.empty() || text[0] != L'$' ? L"" : text;
	}

	static double LookupVariable(ExpressionContext context, const std::wstring &name)
	{
		std::wstring normalized = NormalizeVariableName(name);
		if (context.localScopes) {
			for (auto scope = context.localScopes->rbegin(); scope != context.localScopes->rend(); ++scope) {
				auto found = scope->find(normalized);
				if (found != scope->end())
					return found->second;
			}
		}

		if (normalized == L"time") {
			using clock = std::chrono::steady_clock;
			static const auto start = clock::now();
			return std::chrono::duration<double>(clock::now() - start).count();
		}
		if (normalized == L"draw_type")
			return 1.0;

		MarkFrameVariableReferenced(normalized);

		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		auto found = VariableStore::globals.find(normalized);
		return found != VariableStore::globals.end() ? found->second : 0.0;
	}

	static void SetGlobal(const std::wstring &name, double value)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		WriteGlobalLocked(NormalizeVariableName(name), value);
	}

	// Copies the whole global table under a single lock acquisition. Bulk
	// readers (the custom element ini-params pack consults up to 2048
	// register names per element) must not pay one lock round-trip per name.
	static void CopyGlobals(std::unordered_map<std::wstring, double> *out)
	{
		if (!out)
			return;
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		*out = VariableStore::globals;
	}

	// DX11 parity for the persist prefix: records which variables a "persist"
	// declaration covers. Called once per config load, after every ini file
	// (d3dx_user.ini included) has been parsed and the [Constants] section has
	// been replayed, so the registry always describes the generation that is
	// live now - exactly like ParseConstantsSection() rebuilding
	// persistent_variables. `names` are declaration-order names as written in
	// the ini ("$\Mods\LiuYin.ini\swapkey0", "$swapkey0", ...); duplicates that
	// collapse onto the same variable keep their first declaration, matching
	// DX11's "redeclaration keeps the original flags" behaviour.
	static void RegisterPersistedVariables(const std::vector<std::wstring> &names)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		VariableStore::persistedVariables.clear();
		VariableStore::persistedKeys.clear();
		for (const std::wstring &name : names) {
			const std::wstring key = PersistedVariableKey(name);
			if (key.empty())
				continue;
			if (!VariableStore::persistedKeys.insert(key).second)
				continue;
			PersistedVariable variable;
			variable.name = name;
			variable.key = key;
			VariableStore::persistedVariables.push_back(std::move(variable));
		}
	}

	// True once a config has registered at least one persist declaration; the
	// offline persistence test uses it to prove the registry was rebuilt.
	static bool HasPersistedVariables()
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		return !VariableStore::persistedVariables.empty();
	}

	// Fast, lock-free "is there anything to write" probe for the per-frame
	// autosave check.
	static bool PersistDirty()
	{
		return VariableStore::persistDirty.load(std::memory_order_acquire) != 0;
	}

	static void ClearPersistDirty()
	{
		VariableStore::persistDirty.store(0, std::memory_order_release);
	}

	// Split form of the resolved persist path, for the writer.
	class PersistTarget
	{
	public:
		std::wstring directory;
		std::wstring fileName;
	};

	// Full path of the persist file the last config load resolved
	// ([Include] user_config, else d3dx_user.ini next to the root ini). It
	// lives here rather than in the loader because the runtime's save path must
	// not depend on the loader header being the first file to include it, and
	// because the VM utils are the one header every mod-runtime unit already
	// shares.
	inline static std::wstring userConfigPath;

	// Splits a resolved user-config path into the directory and file name the
	// save side needs.
	static PersistTarget MakePersistTarget(const std::wstring &path)
	{
		PersistTarget target;
		const size_t separator = path.find_last_of(L"\\/");
		if (separator == std::wstring::npos) {
			target.fileName = path;
			return target;
		}
		target.directory = path.substr(0, separator);
		target.fileName = path.substr(separator + 1);
		return target;
	}

	// Writes the live values of every persist variable to `target` in the
	// exact DX11 SavePersistentSettings() format, so a d3dx_user.ini produced
	// here is byte-compatible with the DX11 runtime and vice versa:
	//
	//   ; AUTOMATICALLY GENERATED FILE - DO NOT EDIT
	//   ...
	//   [Constants]
	//   $some_persistent_variable = 1
	//
	// What goes in the file is the registry built by
	// RegisterPersistedVariables, and the values come from the live global
	// table, so the file always reflects the newest hotkey state. Returns false
	// without touching the file when nothing is declared as persistent or the
	// file cannot be opened.
	static bool SavePersistedVariables(const PersistTarget &target)
	{
		if (target.directory.empty() || target.fileName.empty())
			return false;

		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		if (VariableStore::persistedVariables.empty()) {
			// Nothing is declared as persist any more (for instance the mod was
			// removed): drop the pending flag instead of retrying every frame.
			ClearPersistDirty();
			return false;
		}

		std::wstring path = target.directory;
		if (path.back() != L'\\' && path.back() != L'/')
			path += L'\\';
		path += target.fileName;

		FILE *file = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
		if (!file)
			return false;

		fputs("; AUTOMATICALLY GENERATED FILE - DO NOT EDIT\n"
		      ";\n"
		      "; 3DMigoto will overwrite this file whenever any persistent settings are\n"
		      "; altered by hot key or command list. Tag global variables with the \"persist\"\n"
		      "; keyword to save them in this file. Use the post keyword in the [Constants]\n"
		      "; command list if you need to do any intialisation after this file is loaded.\n"
		      ";\n"
		      "[Constants]\n", file);

		for (const PersistedVariable &declaration : VariableStore::persistedVariables) {
			auto found = VariableStore::globals.find(
				NormalizeVariableName(declaration.name));
			if (found == VariableStore::globals.end())
				continue;
			fwprintf_s(file, L"%ls = %.9g\n",
				declaration.name.c_str(), found->second);
		}

		fclose(file);
		ClearPersistDirty();
		return true;
	}

	static void SetGlobals(std::initializer_list<std::pair<const wchar_t *, double>> values)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		for (const auto &value : values)
			WriteGlobalLocked(NormalizeVariableName(value.first), value.second);
	}

	// Writes a normalized variable name only when its value changed. The
	// per-frame frame-variable update used to insert all ~20 keys into the
	// map on every Present (wstring hash + node alloc each), even though only
	// the cursor coordinates move between frames.
	static void SetGlobalIfChanged(const std::wstring &normalizedName, double value)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		auto found = VariableStore::globals.find(normalizedName);
		if (found != VariableStore::globals.end() && found->second == value)
			return;
		WriteGlobalLocked(normalizedName, value);
	}

	// DX12 flattens namespaced globals ("$\LiuYin\swapkey0" and "$swapkey0"
	// name the same slot, see NormalizeVariableName), so the persist registry
	// has to answer "does this flattened name belong to a persist variable"
	// without knowing which namespace it came from. Every caller normalizes
	// first, so this only rejects names the VM itself would not collapse onto a
	// flat global ("$\foo", with no second separator, keeps its qualifier).
	static std::wstring PersistedVariableKey(const std::wstring &name)
	{
		const std::wstring normalized = NormalizeVariableName(name);
		if (normalized.size() < 2 || normalized[0] != L'$' ||
		    normalized[1] == L'\\')
			return L"";
		return normalized;
	}

	static void ClearGlobals()
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		VariableStore::globals.clear();
		// A reload may have dropped the command lists that referenced frame
		// variables; re-arm the lazy-collection gate. The next expression
		// evaluation re-registers if the new config still uses them.
		VariableStore::frameVariableReferenced.store(0, std::memory_order_release);
	}

	static void WriteGlobalLocked(const std::wstring &normalizedName, double value)
	{
		auto found = VariableStore::globals.find(normalizedName);
		const bool changed = found == VariableStore::globals.end() ||
			found->second != value;
		VariableStore::globals[normalizedName] = value;
		if (changed)
			MarkPersistedWriteLocked(normalizedName);
	}

	// The DX11 counterpart is "G->user_config_dirty |= (var->fval != orig)"
	// inside VariableAssignment::run(). The relaxed load in front keeps the
	// common case - a write while the file is already pending - down to a
	// single atomic read, and the set lookup only runs on the first write
	// after each save.
	static void MarkPersistedWriteLocked(const std::wstring &normalizedName)
	{
		if (VariableStore::persistDirty.load(std::memory_order_relaxed))
			return;
		if (!VariableStore::persistedKeys.count(normalizedName))
			return;
		VariableStore::persistDirty.store(1, std::memory_order_release);
	}
};
