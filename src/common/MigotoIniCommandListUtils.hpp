#pragma once

#include <stdint.h>
#include <cwctype>
#include <string>

class MigotoIniCommandListUtils
{
public:
	static std::wstring Trim(const std::wstring &value)
	{
		const wchar_t *spaces = L" \t\r\n";
		size_t first = value.find_first_not_of(spaces);
		if (first == std::wstring::npos)
			return L"";
		size_t last = value.find_last_not_of(spaces);
		return value.substr(first, last - first + 1);
	}

	static std::wstring ToLower(std::wstring value)
	{
		for (wchar_t &ch : value)
			ch = static_cast<wchar_t>(std::towlower(ch));
		return value;
	}

	static bool StartsWithI(const std::wstring &value, const wchar_t *prefix)
	{
		std::wstring lower = ToLower(value);
		std::wstring lowerPrefix = ToLower(prefix ? std::wstring(prefix) : std::wstring());
		return lower.rfind(lowerPrefix, 0) == 0;
	}

	static bool StartsWithTokenI(const std::wstring &value, const wchar_t *prefix)
	{
		std::wstring lower = ToLower(Trim(value));
		std::wstring lowerPrefix = ToLower(prefix ? std::wstring(prefix) : std::wstring());
		return lower.rfind(lowerPrefix, 0) == 0;
	}

	static bool EqualsI(const std::wstring &value, const wchar_t *text)
	{
		return ToLower(value) == ToLower(text ? std::wstring(text) : std::wstring());
	}

	static bool IsExplicitCommandListSection(const std::wstring &section)
	{
		return StartsWithI(section, L"CommandList") ||
			StartsWithI(section, L"BuiltInCommandList") ||
			StartsWithI(section, L"CustomShader") ||
			StartsWithI(section, L"BuiltInCustomShader") ||
			EqualsI(section, L"Present") ||
			EqualsI(section, L"Constants");
	}

	static bool ParseUnsignedSuffix(const std::wstring &text, size_t start, uint32_t *value)
	{
		if (!value || start >= text.size())
			return false;

		uint32_t parsed = 0;
		for (size_t i = start; i < text.size(); ++i) {
			if (text[i] < L'0' || text[i] > L'9')
				return false;
			parsed = parsed * 10 + static_cast<uint32_t>(text[i] - L'0');
		}

		*value = parsed;
		return true;
	}

	static bool ParseVertexBufferTarget(const std::wstring &text, uint32_t *slot)
	{
		std::wstring lower = ToLower(Trim(text));
		if (lower.size() < 3 || lower[0] != L'v' || lower[1] != L'b')
			return false;
		return ParseUnsignedSuffix(lower, 2, slot);
	}

	static bool ParseShaderStagePrefix(const std::wstring &text, size_t *prefixLength, wchar_t *stagePrefix)
	{
		if (!prefixLength || !stagePrefix)
			return false;

		std::wstring lower = ToLower(Trim(text));
		if (lower.size() < 3 || lower[2] != L'-')
			return false;

		if (lower[0] == L'v' && lower[1] == L's') {
			*prefixLength = 3;
			*stagePrefix = L'v';
			return true;
		}
		if (lower[0] == L'p' && lower[1] == L's') {
			*prefixLength = 3;
			*stagePrefix = L'p';
			return true;
		}
		if (lower[0] == L'c' && lower[1] == L's') {
			*prefixLength = 3;
			*stagePrefix = L'c';
			return true;
		}
		return false;
	}

	static bool IsControlFlowRawLine(const std::wstring &rawLine)
	{
		std::wstring lower = ToLower(Trim(rawLine));
		return lower == L"else" || lower == L"endif" ||
			lower.rfind(L"if ", 0) == 0 ||
			lower.rfind(L"elif ", 0) == 0 ||
			lower.rfind(L"else if ", 0) == 0;
	}

	static bool IsVariableAssignmentKey(const std::wstring &value)
	{
		std::wstring lower = ToLower(Trim(value));
		return !lower.empty() && lower[0] == L'$';
	}

	static bool IsVariableDeclarationKey(const std::wstring &value)
	{
		return StartsWithTokenI(value, L"global $") ||
			StartsWithTokenI(value, L"global persist $") ||
			StartsWithTokenI(value, L"persist $");
	}

	// A deferred "post" assignment defers any supported assignment target to
	// the post stage of the enclosing command-list invocation (DX11 routes
	// every "post <line>" into the section's post command list). The DX12 VM
	// supports two deferred targets: $variables and the custom shader
	// constants (x87/y87/...) written through AssignSymbol-style keys, e.g.
	// "post x87 = $bak_x87".
	static bool IsPostVariableAssignmentKey(const std::wstring &value)
	{
		std::wstring lower = ToLower(Trim(value));
		if (lower.rfind(L"post ", 0) != 0)
			return false;
		std::wstring target = Trim(lower.substr(5));
		if (target.empty())
			return false;
		return target[0] == L'$' || IsShaderConstantKey(target);
	}

	static bool IsLocalVariableAssignmentKey(const std::wstring &value)
	{
		return StartsWithTokenI(value, L"local $");
	}

	static bool IsResourceAssignmentKey(const std::wstring &value)
	{
		return StartsWithI(Trim(value), L"Resource");
	}

	static bool IsShaderConstantKey(const std::wstring &value)
	{
		std::wstring lower = ToLower(Trim(value));
		if (lower.size() < 2)
			return false;
		if (lower[0] != L'x' && lower[0] != L'y' && lower[0] != L'z' && lower[0] != L'w')
			return false;
		for (size_t i = 1; i < lower.size(); ++i) {
			if (lower[i] < L'0' || lower[i] > L'9')
				return false;
		}
		return true;
	}

	static bool IsRenderStateKey(const std::wstring &value)
	{
		std::wstring lower = ToLower(Trim(value));
		return lower == L"blend" || lower == L"cull" || lower == L"topology" ||
			lower == L"depth" || lower == L"stencil" ||
			(lower.size() >= 2 && lower[0] == L'o' && lower[1] >= L'0' && lower[1] <= L'9');
	}
};
