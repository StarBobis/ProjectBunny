#pragma once

#include <math.h>
#include <share.h>
#include <stdio.h>

#include <atomic>
#include <cerrno>
#include <optional>
#include <cwchar>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "MigotoIniCommandListUtils.hpp"
#include "IniExpressionNumbers.h"
#include "IniSavedSettings.h"
#include "IniBitwise.h"
#include "IniUnaryMath.h"
#include "ExpressionFunctions.h"
#include "DrawCallParameters.h"

class MigotoCommandVmUtils
{
public:
	class GlobalVariables
	{
	public:
		std::unordered_map<std::wstring, double> values;
		std::unordered_set<std::wstring> locked;
		std::vector<std::wstring> persistentNames;
		std::unordered_set<std::wstring> persistentKeys;
		std::unordered_map<std::wstring, float> savedUnknowns;
		migoto::UnknownSettingsSnapshot confirmedUnknowns;
		std::wstring savePath;
		std::wstring saveOwner;
		bool referencedFrameVariable = false;
	};

	class VariableStore
	{
	public:
		inline static std::mutex globalsMutex;
		inline static GlobalVariables current;
		inline static std::atomic<int> persistDirty = 0;
		inline static uint64_t settingsRevision = 0; // guarded by globalsMutex
		// Set once a command-list expression references any per-frame variable
		// (window size, cursor, DPI, hunting/analysis flags). DX11 evaluates
		// these lazily at match time; DX12 pre-collects them at Present, so
		// without this gate a game with command lists that never reference
		// frame variables still pays swap-chain desc queries, cursor polls and
		// ~20 global-map writes on every frame. Cleared on reload; re-armed by
		// the next expression evaluation.
		inline static std::atomic<int> frameVariableReferenced = 0;
	};

	class FrameTiming
	{
	public:
		float time = 0;
		float frameTime = 0;
		float fps = 0;
		float frameNumber = 0;

		using Field = float FrameTiming::*;
		static Field Find(const std::wstring &name)
		{
			if (name == L"time") return &FrameTiming::time;
			if (name == L"frame_time") return &FrameTiming::frameTime;
			if (name == L"fps") return &FrameTiming::fps;
			if (name == L"frame_number") return &FrameTiming::frameNumber;
			return nullptr;
		}
	};

	inline static std::atomic<FrameTiming> frameTiming{FrameTiming{0, 0, 0, 0}};

	class ExpressionContext
	{
	public:
		std::vector<std::unordered_map<std::wstring, double>> *localScopes = nullptr;
		const DrawCallParameters *call = nullptr;
		std::wstring_view iniNamespace;
		GlobalVariables *initializing = nullptr;
		bool requireDeclaredVariables = false;
		bool staticIniSetting = false;
	};

	class ExpressionParser
	{
	public:
		enum class Mode { Evaluate, Validate, Validated };
		const std::wstring *text = nullptr;
		size_t pos = 0;
		ExpressionContext context;
		bool ok = true;
		Mode mode;
		std::optional<FrameTiming> timingSample;

		ExpressionParser(const std::wstring &value, const ExpressionContext &evalContext, Mode mode = Mode::Evaluate)
			: text(&value), context(evalContext), mode(mode)
		{
		}

		static bool ValidSyntax(const std::wstring &value)
		{
			ExpressionParser syntax(value, {}, Mode::Validate);
			syntax.Parse();
			return syntax.ok;
		}

		float Parse()
		{
			float value = ParseOr();
			SkipSpaces();
			if (pos < text->size())
				ok = false;
			return ok ? value : 0.0f;
		}

		float ParseOr()
		{
			float lhs = ParseAnd();
			while (Match(L"||")) {
				// Parse the right operand unconditionally: a C++ short-circuit
				// ("Truthy(lhs) || Truthy(ParseAnd())") would leave the tokens
				// unconsumed, and Parse()'s end-of-text check would then fail
				// the whole expression to 0 - turning "1 || 0" into false.
				const float rhs = ParseAnd();
				lhs = Truthy(lhs) || Truthy(rhs);
			}
			return lhs;
		}

		float ParseAnd()
		{
			float lhs = ParseBitwiseOr();
			while (Match(L"&&")) {
				const float rhs = ParseBitwiseOr();
				lhs = Truthy(lhs) && Truthy(rhs);
			}
			return lhs;
		}

		float ParseBitwiseOr()
		{
			float lhs = ParseBitwiseXor();
			while (MatchSingleBitOperator(L'|'))
				lhs = migoto::bitwise::Or(lhs, ParseBitwiseXor());
			return lhs;
		}

		float ParseBitwiseXor()
		{
			float lhs = ParseBitwiseAnd();
			while (Match(L"^"))
				lhs = migoto::bitwise::Xor(lhs, ParseBitwiseAnd());
			return lhs;
		}

		float ParseBitwiseAnd()
		{
			float lhs = ParseEquality();
			while (MatchSingleBitOperator(L'&'))
				lhs = migoto::bitwise::And(lhs, ParseEquality());
			return lhs;
		}

		float ParseEquality()
		{
			float lhs = ParseRelational();
			for (;;) {
				if (Match(L"===")) {
					lhs = migoto::EqualFloatBits(lhs, ParseRelational());
				} else if (Match(L"!==")) {
					lhs = !migoto::EqualFloatBits(lhs, ParseRelational());
				} else if (Match(L"==")) {
					lhs = lhs == ParseRelational();
				} else if (Match(L"!=")) {
					lhs = lhs != ParseRelational();
				} else {
					return lhs;
				}
			}
		}

		float ParseRelational()
		{
			float lhs = ParseShift();
			for (;;) {
				if (Match(L">=")) {
					lhs = lhs >= ParseShift();
				} else if (Match(L"<=")) {
					lhs = lhs <= ParseShift();
				} else if (Match(L">")) {
					lhs = lhs > ParseShift();
				} else if (Match(L"<")) {
					lhs = lhs < ParseShift();
				} else {
					return lhs;
				}
			}
		}

		float ParseShift()
		{
			float lhs = ParseAdditive();
			for (;;) {
				if (Match(L"<<"))
					lhs = migoto::bitwise::ShiftLeft(lhs, ParseAdditive());
				else if (Match(L">>"))
					lhs = migoto::bitwise::ShiftRight(lhs, ParseAdditive());
				else
					return lhs;
			}
		}

		float ParseAdditive()
		{
			float lhs = ParseMultiplicative();
			for (;;) {
				if (Match(L"+")) {
					lhs = migoto::arithmetic::Add(lhs, ParseMultiplicative());
				} else if (Match(L"-")) {
					lhs = migoto::arithmetic::Subtract(lhs, ParseMultiplicative());
				} else {
					return lhs;
				}
			}
		}

		float ParseMultiplicative()
		{
			float lhs = ParsePower();
			for (;;) {
				if (Match(L"//")) {
					const float rhs = ParsePower();
					if (mode != Mode::Validate)
						lhs = migoto::arithmetic::FloorDivide(lhs, rhs);
				} else if (Match(L"*")) {
					lhs = migoto::arithmetic::Multiply(lhs, ParsePower());
				} else if (Match(L"/")) {
					const float rhs = ParsePower();
					if (mode != Mode::Validate)
						lhs = migoto::arithmetic::Divide(lhs, rhs);
				} else if (Match(L"%")) {
					const float rhs = ParsePower();
					if (mode != Mode::Validate)
						lhs = migoto::arithmetic::Remainder(lhs, rhs);
				} else {
					return lhs;
				}
			}
		}

		// Unary signs bind more tightly than ** in the DX11 grammar.
		// Fold from the right without recursing once per exponent.
		float ParsePower()
		{
			float value = ParseUnary();
			if (!Match(L"**"))
				return value;
			std::vector<float> bases;
			do {
				bases.push_back(value);
				value = ParseUnary();
			} while (Match(L"**"));
			if (mode == Mode::Validate)
				return 0.0f;
			for (auto it = bases.rbegin(); it != bases.rend(); ++it)
				value = migoto::arithmetic::Power(*it, value);
			return value;
		}

		float ParseUnary()
		{
			if (Match(L"~"))
				return migoto::bitwise::Not(ParseUnary());
			if (Match(L"!"))
				return !Truthy(ParseUnary());
			if (Match(L"-"))
				return -ParseUnary();
			if (Match(L"+"))
				return ParseUnary();
			return ParsePrimary();
		}

		float ParsePrimary()
		{
			SkipSpaces();
			if (Match(L"(")) {
				float value = ParseOr();
				if (!Match(L")"))
					ok = false;
				return value;
			}
			if (pos < text->size() && IsNumberStart((*text)[pos]))
				return ParseNumber();
			if (pos < text->size() && IsIdentifierStart((*text)[pos])) {
				const std::wstring name = ParseIdentifier();
				if (!Match(L"(")) {
					if (name.size() == 6 && _wcsicmp(name.c_str(), L"random") == 0) {
						ok = false;
						return 0.0f;
					}
					return mode == Mode::Validate ? 0.0f : LookupIdentifier(name);
				}
				const auto normalized = MigotoIniCommandListUtils::ToLower(name);
				const bool random = normalized == L"random";
				const auto *function = random ? nullptr : migoto::unary::Find(normalized);
				if (!random && !function) {
					ok = false;
					return 0.0f;
				}
				const float argument = ParseOr();
				if (!Match(L")"))
					ok = false;
				if (!ok || mode == Mode::Validate)
					return 0.0f;
				if (!random)
					return function->evaluate(argument);

				// A malformed expression must not advance the shared random stream.
				// Validate the entire expression once, before its first sample.
				if (mode == Mode::Evaluate) {
					if (!ValidSyntax(*text)) {
						ok = false;
						return 0.0f;
					}
					mode = Mode::Validated;
				}
				return RandomExpressionValue(argument);
			}
			ok = false;
			return 0.0f;
		}

		bool MatchSingleBitOperator(wchar_t op)
		{
			SkipSpaces();
			if (pos >= text->size() || (*text)[pos] != op ||
				(pos + 1 < text->size() && (*text)[pos + 1] == op))
				return false;
			++pos;
			return true;
		}

		template <size_t N>
		bool Match(const wchar_t (&op)[N])
		{
			SkipSpaces();
			// All grammar operators are literals. Compare them directly instead
			// of constructing a temporary string at every precedence level.
			constexpr size_t length = N - 1;
			if (text->compare(pos, length, op, length) != 0)
				return false;
			pos += length;
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

		float ParseNumber()
		{
			float value;
			size_t length;
			if (!migoto::ReadExpressionNumber(std::wstring_view(*text).substr(pos), value, length, true)) {
				ok = false;
				return 0.0f;
			}
			pos += length;
			return mode == Mode::Validate ? 0.0f : value;
		}

		std::wstring ParseIdentifier()
		{
			size_t start = pos;
			if ((*text)[pos] == L'$') {
				pos = migoto::VariableTokenEnd(*text, pos + 1, true);
				return text->substr(start, pos - start);
			}
			while (pos < text->size() && IsIdentifierChar((*text)[pos]))
				++pos;
			return text->substr(start, pos - start);
		}

		float LookupIdentifier(const std::wstring &name)
		{
			if (context.staticIniSetting && name.front() != L'$') {
				if (name == L"sli" || name == L"stereo_active" || name == L"stereo_available")
					return 0;
				if ((name == L"hunting" || name == L"frame_analysis") && !LookupVariable(context, L"hunting"))
					return 0;
				if (name != L"time" && name != L"frame_time" && name != L"frame_number" &&
					name != L"inf" && name != L"infinity" && name != L"nan") {
					ok = false;
					return 0;
				}
			}
			bool missing = false;
			const auto value = LookupVariable(context, name, &timingSample,
				context.requireDeclaredVariables && name.front() == L'$' ? &missing : nullptr);
			if (missing)
				ok = false;
			return static_cast<float>(value);
		}
	};

	class BranchFrame
	{
	public:
		bool parentActive = true;
		bool active = true;
		bool branchTaken = false;
	};

	// Defer expression evaluation in the originating INI namespace.
	class PostAssignment
	{
	public:
		std::wstring name;
		std::wstring expression;
		std::wstring iniNamespace;
	};

	class State
	{
	public:
		std::vector<std::unordered_map<std::wstring, double>> localScopes;
		DrawCallParameters call;
		GlobalVariables *initializing = nullptr;
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

		ExpressionContext Context(std::wstring_view iniNamespace = {})
		{
			ExpressionContext context;
			context.localScopes = &localScopes;
			context.call = &call;
			context.iniNamespace = iniNamespace;
			context.initializing = initializing;
			return context;
		}

		double Evaluate(const std::wstring &expression, std::wstring_view iniNamespace = {})
		{
			ExpressionParser parser(expression, Context(iniNamespace));
			return parser.Parse();
		}

		bool EvaluateBool(const std::wstring &expression, std::wstring_view iniNamespace = {})
		{
			return Truthy(Evaluate(expression, iniNamespace));
		}

		void DeclareLocal(const std::wstring &key, const std::wstring &value, std::wstring_view iniNamespace = {})
		{
			if (localScopes.empty())
				PushLocalScope();
			const auto name = ExtractVariableName(key);
			if (migoto::ValidVariableName(name)) {
				const auto key = iniNamespace.empty() ? name : migoto::NamespacedVariable(name, iniNamespace);
				localScopes.back()[key] = value.empty() ? 0.0 : Evaluate(value, iniNamespace);
			}
		}

		void Assign(const std::wstring &key, const std::wstring &value, std::wstring_view iniNamespace = {})
		{
			const auto name = ExtractVariableName(key);
			if (!name.empty())
				AssignExpression(name, value, iniNamespace);
		}

		void AssignSymbol(const std::wstring &key, const std::wstring &value, std::wstring_view iniNamespace = {})
		{
			AssignExpression(NormalizeVariableName(key), value, iniNamespace);
		}

		void AssignExpression(const std::wstring &name, const std::wstring &expression, std::wstring_view iniNamespace)
		{
			if (name.empty())
				return;
			const auto context = Context(iniNamespace);
			if (auto *local = FindLocal(context, name)) {
				*local = expression.empty() ? 0.0 : Evaluate(expression, iniNamespace);
				return;
			}
			if (IsLockedGlobal(context, name))
				return;
			const double value = expression.empty() ? 0.0 : Evaluate(expression, iniNamespace);
			SetScopedGlobal(context, name, value);
		}

		void QueuePostAssign(const std::wstring &key, const std::wstring &value, std::wstring_view iniNamespace = {})
		{
			const auto text = MigotoIniCommandListUtils::ToLower(MigotoIniCommandListUtils::Trim(key));
			const auto name = text.rfind(L"post ", 0) == 0 ?
				MigotoIniCommandListUtils::Trim(text.substr(5)) : text;
			if ((!name.empty() && name.front() == L'$') || MigotoIniCommandListUtils::IsShaderConstantKey(name))
				postAssignments.push_back({name, value, std::wstring(iniNamespace)});
		}

		void FlushPostAssignments(size_t first = 0)
		{
			for (size_t i = first; i < postAssignments.size(); ++i) {
				const auto &assignment = postAssignments[i];
				AssignExpression(assignment.name, assignment.expression, assignment.iniNamespace);
			}
			postAssignments.resize(first);
		}

		void ApplyControlFlow(const std::wstring &rawLine, std::wstring_view iniNamespace = {})
		{
			std::wstring line = MigotoIniCommandListUtils::ToLower(MigotoIniCommandListUtils::Trim(rawLine));
			if (line.rfind(L"if ", 0) == 0) {
				bool parent = Active();
				bool condition = parent && EvaluateBool(line.substr(3), iniNamespace);
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
				bool condition = frame.parentActive && !frame.branchTaken && EvaluateBool(line.substr(prefix), iniNamespace);
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
		return value != 0.0;
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
	static void MarkFrameVariableReferenced(const std::wstring &normalized, GlobalVariables *initializing)
	{
		if (initializing ? initializing->referencedFrameVariable : VariableStore::frameVariableReferenced.load(std::memory_order_relaxed))
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
				if (initializing)
					initializing->referencedFrameVariable = true;
				else
					VariableStore::frameVariableReferenced.store(1, std::memory_order_release);
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

	static double *FindLocal(ExpressionContext context, const std::wstring &name)
	{
		if (!context.localScopes || name.size() < 2 || name[0] != L'$' || name[1] == L'\\')
			return nullptr;
		std::wstring qualified;
		for (auto scope = context.localScopes->rbegin(); scope != context.localScopes->rend(); ++scope) {
			if (scope->empty())
				continue;
			if (!context.iniNamespace.empty() && qualified.empty())
				qualified = migoto::NamespacedVariable(name, context.iniNamespace);
			auto found = scope->find(qualified.empty() ? name : qualified);
			if (found != scope->end())
				return &found->second;
		}
		return nullptr;
	}

	static double LookupVariable(ExpressionContext context, const std::wstring &name,
		std::optional<FrameTiming> *timingSample = nullptr, bool *missing = nullptr)
	{
		std::wstring normalized = NormalizeVariableName(name);
		if (auto *local = FindLocal(context, normalized))
			return *local;

		if (!normalized.empty() && (normalized.front() == L't' || normalized.front() == L'f')) {
			if (const auto field = FrameTiming::Find(normalized)) {
				if (!timingSample)
					return frameTiming.load(std::memory_order_acquire).*field;
				if (!*timingSample)
					timingSample->emplace(frameTiming.load(std::memory_order_acquire));
				return (**timingSample).*field;
			}
		}
		if (!normalized.empty() && normalized.front() != L'$') {
			static constexpr DrawCallParameters noCall{};
			const auto &call = context.call ? *context.call : noCall;
			if (normalized == L"draw_type") return static_cast<float>(call.type);
			if (normalized == L"vertex_count") return static_cast<float>(call.VertexCount);
			if (normalized == L"index_count") return static_cast<float>(call.IndexCount);
			if (normalized == L"instance_count") return static_cast<float>(call.InstanceCount);
			if (normalized == L"first_vertex") return static_cast<float>(call.FirstVertex);
			if (normalized == L"first_index") return static_cast<float>(call.FirstIndex);
			if (normalized == L"first_instance") return static_cast<float>(call.FirstInstance);
			if (normalized == L"thread_group_count_x") return static_cast<float>(call.ThreadGroupCountX);
			if (normalized == L"thread_group_count_y") return static_cast<float>(call.ThreadGroupCountY);
			if (normalized == L"thread_group_count_z") return static_cast<float>(call.ThreadGroupCountZ);
		}
		if (normalized == L"inf" || normalized == L"infinity")
			return std::numeric_limits<double>::infinity();
		if (normalized == L"nan")
			return std::numeric_limits<double>::quiet_NaN();

		if (!normalized.empty() && normalized.front() != L'$')
			MarkFrameVariableReferenced(normalized, context.initializing);

		auto read = [&](const GlobalVariables &globals) {
			const auto found = FindGlobal(globals, normalized, context.iniNamespace);
			if (found != globals.values.end())
				return found->second;
			if (missing)
				*missing = true;
			return 0.0;
		};
		if (context.initializing)
			return read(*context.initializing);
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		return read(VariableStore::current);
	}

	static auto FindGlobal(const GlobalVariables &globals, const std::wstring &name, std::wstring_view iniNamespace)
		-> std::unordered_map<std::wstring, double>::const_iterator
	{
		if (!iniNamespace.empty() && name.size() > 1 && name[0] == L'$' && name[1] != L'\\') {
			const auto found = globals.values.find(migoto::NamespacedVariable(name, iniNamespace));
			if (found != globals.values.end())
				return found;
		}
		return globals.values.find(name);
	}

	static bool IsLockedGlobal(ExpressionContext context, const std::wstring &name)
	{
		auto test = [&](const GlobalVariables &globals) {
			if (globals.locked.empty())
				return false;
			const auto found = FindGlobal(globals, name, context.iniNamespace);
			return found != globals.values.end() && globals.locked.count(found->first) != 0;
		};
		if (context.initializing)
			return test(*context.initializing);
		if (!hasLockedGlobals.load(std::memory_order_acquire))
			return false;
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		return test(VariableStore::current);
	}

	static void SetScopedGlobal(ExpressionContext context, const std::wstring &name, double value)
	{
		auto write = [&](GlobalVariables &globals) {
			const auto found = FindGlobal(globals, name, context.iniNamespace);
			const auto &key = found == globals.values.end() ? name : found->first;
			if (!globals.locked.empty() && globals.locked.count(key))
				return;
			if (!context.initializing && (found == globals.values.end() || found->second != value) &&
				globals.persistentKeys.count(key))
				MarkPersistChangedLocked();
			globals.values[key] = value;
		};
		if (context.initializing) {
			write(*context.initializing);
			return;
		}
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		write(VariableStore::current);
	}

	static void SetGlobal(const std::wstring &name, double value)
	{
		SetScopedGlobal({}, NormalizeVariableName(name), value);
	}

	static void CopyGlobals(std::unordered_map<std::wstring, double> *out)
	{
		if (out) {
			std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
			*out = VariableStore::current.values;
		}
	}

	enum class UnknownSettingsPolicy { Observe, Preserve, ClearUnchanged };

	enum class SettingsPublishStatus { Applied, SaveFailed, ChangedDuringLoad };

	static uint64_t SettingsRevision()
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		return VariableStore::settingsRevision;
	}

	static void MarkPersistChangedLocked()
	{
		++VariableStore::settingsRevision;
		VariableStore::persistDirty.store(1, std::memory_order_release);
	}

	class SettingsLoadResult
	{
	public:
		SettingsPublishStatus status = SettingsPublishStatus::Applied;
		bool cleanupArmed = false;
		size_t unknown = 0;
		size_t removed = 0;
	};

	static SettingsLoadResult PublishGlobals(GlobalVariables globals,
		UnknownSettingsPolicy policy = UnknownSettingsPolicy::Observe,
		std::optional<uint64_t> expectedRevision = {})
	{
		SettingsLoadResult result;
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		auto &live = VariableStore::current;
		if (expectedRevision && (*expectedRevision != VariableStore::settingsRevision ||
			(!live.savePath.empty() && PersistDirty()))) {
			result.status = SettingsPublishStatus::ChangedDuringLoad;
			return result;
		}
		if (policy != UnknownSettingsPolicy::Observe) {
			auto &snapshot = globals.confirmedUnknowns;
			result.unknown = snapshot.size();
			const auto action = migoto::CompareUnknownSettings(policy == UnknownSettingsPolicy::ClearUnchanged,
				snapshot, globals.saveOwner == live.saveOwner ? &live.confirmedUnknowns : nullptr);
			if (action == migoto::UnknownSettingsAction::Remove) {
				for (const auto &[name, bits] : snapshot)
					globals.savedUnknowns.erase(name);
				result.removed = snapshot.size();
				snapshot.clear();
			}
			result.cleanupArmed = policy == UnknownSettingsPolicy::ClearUnchanged && !snapshot.empty();
			if (action != migoto::UnknownSettingsAction::Keep &&
				!WriteSettingsFile(globals, globals.savePath, true)) {
				result.status = SettingsPublishStatus::SaveFailed;
				result.removed = 0;
				return result;
			}
		}
		std::swap(live, globals);
		++VariableStore::settingsRevision;
		VariableStore::frameVariableReferenced.store(live.referencedFrameVariable, std::memory_order_release);
		hasLockedGlobals.store(!live.locked.empty(), std::memory_order_release);
		ClearPersistDirty();
		return result;
	}

	inline static std::atomic<bool> hasLockedGlobals{false};

	// True once a config has registered at least one persist declaration; the
	// offline persistence test uses it to prove the registry was rebuilt.
	static bool HasPersistedVariables()
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		return !VariableStore::current.persistentNames.empty();
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

	// Optional sink for persist-save failure diagnostics. This header is shared
	// and has no logger of its own; the DX12 runtime installs a sink that writes
	// to d3d12_log.txt. Without one (tests, early startup) the failure still
	// goes to stderr instead of being silent.
	using PersistFailureSink = void (*)(const char *stage, const wchar_t *path, int error);
	inline static std::atomic<PersistFailureSink> persistFailureSink{nullptr};

	static void SetPersistFailureSink(PersistFailureSink sink)
	{
		persistFailureSink = sink;
	}

	static void ReportPersistFailure(const char *stage, const std::wstring &path, int error)
	{
		if (const auto sink = persistFailureSink.load(std::memory_order_acquire)) {
			sink(stage, path.c_str(), error);
			return;
		}
		fprintf(stderr, "persist save failed at %s (error %d): %ls\n",
			stage ? stage : "", error, path.c_str());
	}

	static bool SaveCurrentIfDirty(std::wstring &savedPath)
	{
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		const auto &path = VariableStore::current.savePath;
		if (!PersistDirty() || path.empty())
			return true;
		if (!WriteSettingsLocked(path))
			return false;
		savedPath = path;
		return true;
	}

	static bool SavePersistedVariables(const PersistTarget &target)
	{
		if (target.directory.empty() || target.fileName.empty())
			return false;
		std::wstring path = target.directory;
		if (path.back() != L'\\' && path.back() != L'/')
			path += L'\\';
		path += target.fileName;
		std::lock_guard<std::mutex> lock(VariableStore::globalsMutex);
		return WriteSettingsLocked(path);
	}

	// Write live mutable declarations in their registration order. A failed
	// write, close or replacement leaves the old file and dirty flag intact.
	static bool WriteSettingsLocked(const std::wstring &path)
	{
		const auto &globals = VariableStore::current;
		const bool hasUnknowns = !globals.savedUnknowns.empty() &&
			globals.saveOwner == migoto::NormalizePersistentConfigPath(path);
		if (globals.persistentNames.empty() && !hasUnknowns) {
			ClearPersistDirty();
			return false;
		}
		if (!WriteSettingsFile(globals, path, hasUnknowns))
			return false;
		++VariableStore::settingsRevision;
		ClearPersistDirty();
		return true;
	}

	static bool WriteSettingsFile(const GlobalVariables &globals, const std::wstring &path, bool includeUnknowns)
	{
		migoto::StagedSettingsFile staged;
		FILE *file = nullptr;
		const int opened = staged.Open(&file, path, [](FILE **out, const wchar_t *candidate, const wchar_t *) {
			*out = _wfsopen(candidate, L"wx", _SH_DENYRW);
			return *out ? 0 : errno;
		});
		if (opened) {
			ReportPersistFailure("fopen", path, opened);
			return false;
		}

		// This file is the only copy of the user's persisted settings, so every
		// stdio step is checked. A failed write/flush/close keeps the dirty flag
		// armed and reports one line to the platform log; the next autosave or
		// reload then retries the complete file. The success-path bytes are the
		// same DX11-compatible format as before this check.
		bool ok = true;
		const char *failedStage = "fputs";
		if (fputs("; AUTOMATICALLY GENERATED FILE - DO NOT EDIT\n"
		      ";\n"
		      "; 3DMigoto will overwrite this file whenever any persistent settings are\n"
		      "; altered by hot key or command list. Tag global variables with the \"persist\"\n"
		      "; keyword to save them in this file. Use the post keyword in the [Constants]\n"
		      "; command list if you need to do any intialisation after this file is loaded.\n"
		      ";\n"
		      "[Constants]\n", file) < 0) {
			ok = false;
		}

		auto writeValue = [&](const std::wstring &name, double value) {
			const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.c_str(), -1,
				nullptr, 0, nullptr, nullptr);
			std::string utf8(size, '\0');
			if (!size || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.c_str(), -1,
				utf8.data(), size, nullptr, nullptr)) {
				failedStage = "UTF-8";
				return false;
			}
			failedStage = "fprintf_s";
			return fprintf_s(file, "%s = %.9g\n", utf8.c_str(), value) >= 0;
		};
		for (const auto &name : globals.persistentNames) {
			if (!ok)
				break;
			ok = writeValue(name, globals.values.at(name));
		}
		if (includeUnknowns) {
			for (const auto &[name, value] : globals.savedUnknowns) {
				if (!ok)
					break;
				ok = writeValue(name, value);
			}
		}

		if (ferror(file)) {
			if (ok)
				failedStage = "ferror";
			ok = false;
		}
		if (fflush(file) != 0) {
			if (ok)
				failedStage = "fflush";
			ok = false;
		}
		if (fclose(file) != 0) {
			if (ok)
				failedStage = "fclose";
			ok = false;
		}
		if (!ok) {
			ReportPersistFailure(failedStage, path, errno);
			return false;
		}

		if (const auto error = staged.Replace(path)) {
			ReportPersistFailure("replace", path, static_cast<int>(error));
			return false;
		}
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
		auto found = VariableStore::current.values.find(normalizedName);
		if (found != VariableStore::current.values.end() && found->second == value)
			return;
		WriteGlobalLocked(normalizedName, value);
	}

	static void ClearGlobals()
	{
		PublishGlobals({});
	}

	// The frame publisher supplies built-in names. These writes never target
	// user declarations, which start with '$'.
	static void WriteGlobalLocked(const std::wstring &name, double value)
	{
		auto &globals = VariableStore::current;
		if (globals.locked.count(name))
			return;
		auto [it, inserted] = globals.values.try_emplace(name, value);
		const bool changed = inserted || it->second != value;
		it->second = value;
		if (changed && globals.persistentKeys.count(name))
			MarkPersistChangedLocked();
	}
};
