#pragma once

#include <Windows.h>
#include <climits>
#include <cstring>
#include <string>
#include "log.h"

inline std::wstring ReadShaderHeaderLine(const char* source, size_t size)
{
    if (!source || !size)
        return {};
    const auto* newline = static_cast<const char*>(std::memchr(source, '\n', size));
    const size_t length = newline ? static_cast<size_t>(newline - source) : size;
    if (!length)
        return {};
    if (length > INT_MAX) {
        LogInfo("WARNING: Shader display header exceeds the UTF-8 decoder length limit\n");
        return {};
    }
    const int count = static_cast<int>(length);
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, count, nullptr, 0);
    if (!required) {
        LogInfo("WARNING: Shader display header is not valid UTF-8 (error %lu)\n", GetLastError());
        return {};
    }
    std::wstring header(required, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, count, &header[0], required) != required) {
        LogInfo("WARNING: Shader display header conversion failed (error %lu)\n", GetLastError());
        return {};
    }
    return header;
}
