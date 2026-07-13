#pragma once

#include <Windows.h>
#include <Shlwapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "util_min.h"

enum D3D12ProcessGateRole
{
	D3D12ProcessGateRoleReject = 0,
	D3D12ProcessGateRoleBootstrapOnly = 1,
	D3D12ProcessGateRoleTargetRuntime = 2
};

class D3D12ProcessGateDecision
{
public:
	bool allowed = false;
	D3D12ProcessGateRole role = D3D12ProcessGateRoleReject;
	wchar_t reason[256] = {};
	wchar_t modulePath[MAX_PATH] = {};
	wchar_t moduleDirectory[MAX_PATH] = {};
	wchar_t processPath[MAX_PATH] = {};
	wchar_t processDirectory[MAX_PATH] = {};
	wchar_t configPath[MAX_PATH] = {};
	wchar_t target[MAX_PATH] = {};
	wchar_t loader[MAX_PATH] = {};
};

class D3D12ProcessGateUtils
{
public:
	static bool VerifyIntendedTarget(HINSTANCE module)
	{
		D3D12ProcessGateDecision decision = Evaluate(module);
		DebugDecision(decision);
		return decision.role != D3D12ProcessGateRoleReject;
	}

	static D3D12ProcessGateDecision Evaluate(HINSTANCE module)
	{
		D3D12ProcessGateDecision decision;
		if (!ReadPaths(module, &decision)) {
			CopyReason(&decision, L"path discovery failed");
			return decision;
		}

		if (ReadLoaderConfig(&decision)) {
			if (MatchesConfiguredTarget(decision.processPath, decision.target)) {
				decision.allowed = true;
				decision.role = D3D12ProcessGateRoleTargetRuntime;
				CopyReason(&decision, L"matched configured loader target");
			} else if (MatchesConfiguredLoader(decision.processPath, decision.loader) ||
					(IsSameDirectory(decision.moduleDirectory, decision.processDirectory) &&
						IsBootstrapProcess(decision.processPath))) {
				decision.allowed = true;
				decision.role = D3D12ProcessGateRoleBootstrapOnly;
				CopyReason(&decision, L"matched configured loader bootstrap");
			} else {
				CopyReason(&decision, L"rejected by configured loader target");
			}
			return decision;
		}

		if (IsSameDirectory(decision.moduleDirectory, decision.processDirectory)) {
			decision.allowed = true;
			decision.role = D3D12ProcessGateRoleTargetRuntime;
			CopyReason(&decision, L"same directory proxy fallback");
		} else {
			CopyReason(&decision, L"no configured target and not same directory");
		}
		return decision;
	}

	static bool ReadPaths(HINSTANCE module, D3D12ProcessGateDecision *decision)
	{
		wchar_t *moduleBase = nullptr;
		wchar_t *processBase = nullptr;

		if (!decision)
			return false;
		if (!GetModuleFileNameW(module, decision->modulePath, MAX_PATH))
			return false;
		if (!GetModuleFileNameW(nullptr, decision->processPath, MAX_PATH))
			return false;

		wcsncpy_s(decision->moduleDirectory, MAX_PATH, decision->modulePath, _TRUNCATE);
		wcsncpy_s(decision->processDirectory, MAX_PATH, decision->processPath, _TRUNCATE);

		moduleBase = wcsrchr(decision->moduleDirectory, L'\\');
		processBase = wcsrchr(decision->processDirectory, L'\\');
		if (!moduleBase || !processBase)
			return false;

		*moduleBase = L'\0';
		*processBase = L'\0';

		wcsncpy_s(decision->configPath, MAX_PATH, decision->moduleDirectory, _TRUNCATE);
		PathAppendW(decision->configPath, L"d3dx.ini");
		return true;
	}

	static bool ReadLoaderConfig(D3D12ProcessGateDecision *decision)
	{
		HANDLE file = INVALID_HANDLE_VALUE;
		DWORD fileSize = 0;
		DWORD readSize = 0;
		char *buffer = nullptr;
		const char *section = nullptr;
		char targetUtf8[MAX_PATH] = {};
		char loaderUtf8[MAX_PATH] = {};
		bool foundTarget = false;
		bool foundLoader = false;

		if (!decision)
			return false;

		file = CreateFileW(decision->configPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;

		fileSize = GetFileSize(file, nullptr);
		if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
			goto out_close;

		buffer = new char[fileSize + 1];
		if (!buffer)
			goto out_close;

		if (!ReadFile(file, buffer, fileSize, &readSize, nullptr) || fileSize != readSize)
			goto out_free;
		buffer[fileSize] = '\0';

		section = find_ini_section_lite(buffer, "loader");
		if (!section)
			goto out_free;
		if (find_ini_setting_lite(section, "target", targetUtf8, MAX_PATH) &&
				MultiByteToWideChar(CP_UTF8, 0, targetUtf8, -1, decision->target, MAX_PATH)) {
			TrimQuotes(decision->target);
			foundTarget = decision->target[0] != L'\0';
		}

		if (find_ini_setting_lite(section, "loader", loaderUtf8, MAX_PATH) &&
				MultiByteToWideChar(CP_UTF8, 0, loaderUtf8, -1, decision->loader, MAX_PATH)) {
			TrimQuotes(decision->loader);
			foundLoader = decision->loader[0] != L'\0';
		}

	out_free:
		delete [] buffer;
	out_close:
		CloseHandle(file);
		return foundTarget || foundLoader;
	}

	static bool MatchesConfiguredTarget(const wchar_t *processPath, const wchar_t *target)
	{
		size_t processLength = 0;
		size_t targetLength = 0;

		if (!processPath || !target || target[0] == L'\0')
			return false;

		processLength = wcslen(processPath);
		targetLength = wcslen(target);
		if (processLength < targetLength)
			return false;

		if (target[0] != L'\\' && target[1] != L':' && processLength > targetLength) {
			if (processPath[processLength - targetLength - 1] != L'\\')
				return false;
		}

		return _wcsicmp(processPath + processLength - targetLength, target) == 0;
	}

	static bool IsSameDirectory(const wchar_t *left, const wchar_t *right)
	{
		if (!left || !right)
			return false;
		return _wcsicmp(left, right) == 0;
	}

	static bool MatchesConfiguredLoader(const wchar_t *processPath, const wchar_t *loader)
	{
		const wchar_t *baseName = nullptr;

		if (!processPath || !loader || loader[0] == L'\0')
			return false;

		baseName = wcsrchr(processPath, L'\\');
		if (baseName)
			baseName++;
		else
			baseName = processPath;

		return _wcsicmp(baseName, loader) == 0;
	}

	static bool IsBootstrapProcess(const wchar_t *processPath)
	{
		const wchar_t *baseName = nullptr;

		if (!processPath)
			return false;

		baseName = wcsrchr(processPath, L'\\');
		if (baseName)
			baseName++;
		else
			baseName = processPath;

		return _wcsicmp(baseName, L"Run.exe") == 0;
	}

	static const wchar_t *RoleName(D3D12ProcessGateRole role)
	{
		switch (role) {
		case D3D12ProcessGateRoleBootstrapOnly:
			return L"bootstrap";
		case D3D12ProcessGateRoleTargetRuntime:
			return L"target";
		default:
			return L"reject";
		}
	}

	static void TrimQuotes(wchar_t *text)
	{
		size_t length = 0;

		if (!text)
			return;

		length = wcslen(text);
		if (length >= 2 && text[0] == L'"' && text[length - 1] == L'"') {
			memmove(text, text + 1, (length - 2) * sizeof(wchar_t));
			text[length - 2] = L'\0';
		}
	}

	static void CopyReason(D3D12ProcessGateDecision *decision, const wchar_t *reason)
	{
		if (!decision || !reason)
			return;
		wcsncpy_s(decision->reason, 256, reason, _TRUNCATE);
	}

	static void DebugDecision(const D3D12ProcessGateDecision &decision)
	{
		wchar_t line[1024] = {};
		swprintf_s(line, 1024,
			L"[Bunny-D3D12Gate] %s role=%s: pid=%lu process=%s module=%s target=%s loader=%s config=%s reason=%s\n",
			decision.allowed ? L"allow" : L"deny",
			RoleName(decision.role),
			GetCurrentProcessId(),
			decision.processPath,
			decision.modulePath,
			decision.target,
			decision.loader,
			decision.configPath,
			decision.reason);
		OutputDebugStringW(line);
	}
};
