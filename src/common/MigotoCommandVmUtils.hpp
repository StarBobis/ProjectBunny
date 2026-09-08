#pragma once

#include <math.h>

#include <atomic>
#include <chrono>
#include <cwchar>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "MigotoIniCommandListUtils.hpp"

class MigotoCommandVmUtils
{
public:
	class VariableStore
	{
	public:
		inline static std::mutex globalsMutex;
		inline static std::unordered_map<std::wstring, double> globals;
		// Values of variables declared with the "persist" prefix, captured
		// just before a reload wipes globals and replayed after the
		// [Constants] section re-runs. Mirrors the DX11 reload semantics
		// (full d3dx_user.ini persistence is DX11-only); guarded by
		// globalsMutex.
		inline static std::unordered_map<std::wstring, double> persistSnapshot;
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
			while (Match(L"||"))
				lhs = Truthy(lhs) || Truthy(ParseAnd()) ? 1.0 : 0.0;
			return lhs;
		}

		double ParseAnd()
		{
			double lhs = ParseEquality();
			while (Match(L"&&"))
				lhs = Truthy(lhs) && Truthy(ParseEquality()) ? 1.0 : 0.0;
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
			return IsIdentifierStart(ch) || (ch >= L'0' && ch <= L'9');
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

	class State
	{
	public:
		std::vector<std::unordered_map<std::wstring, double>> localScopes;
		std::vector<BranchFrame> branches;
		std::vector<std::pair<std::wstring, std::wstring>> postAssignments;

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
			if (!name.empty())
				postAssignments.push_back({ name, value });
		}

		void FlushPostAssignments()
		{
			std::vector<std::pair<std::wstring, std::wstring>> pending;
			pending.swap(postAssignments);
			for (const auto &assignment : pending)
				SetGlobal(assignment.first, Evaluate(assignment.second));
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
		return MigotoIniCommandListUtils::ToLower(MigotoIniCommandListUtils::Trim(value));
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
		VariableStore::globals[NormalizeVariableName(name)] = value;
	}

	// Captures the current values of the given persist-declared variables
	// (normalized names, e.g. "$top") so they survive a reload. Names that
	// do not currently exist in the global table are dropped, which keeps
	// first-load defaults from the [Constants] replay intact.
	static void SavePersistSnapshot(const std::vector<std::wstring> &names)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		VariableStore::persistSnapshot.clear();
		for (const std::wstring &name : names) {
			const std::wstring normalized = NormalizeVariableName(name);
			auto found = VariableStore::globals.find(normalized);
			if (found != VariableStore::globals.end())
				VariableStore::persistSnapshot[normalized] = found->second;
		}
	}

	// Writes the captured persist values back into the global table. Must be
	// called after the [Constants] defaults have been replayed so that
	// runtime-modified persist values win over the ini defaults.
	static void RestorePersistSnapshot()
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		for (const auto &entry : VariableStore::persistSnapshot)
			VariableStore::globals[entry.first] = entry.second;
	}

	static void SetGlobals(std::initializer_list<std::pair<const wchar_t *, double>> values)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		for (const auto &value : values)
			VariableStore::globals[NormalizeVariableName(value.first)] = value.second;
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
		VariableStore::globals[normalizedName] = value;
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
};
