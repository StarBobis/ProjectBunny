#pragma once

#include <stddef.h>
#include <wchar.h>

void DX12JsonEscapeString(char *dst, size_t dstSize, const char *src);
void DX12JsonEscapeWString(char *dst, size_t dstSize, const wchar_t *src);
void DX12JsonAppendLogFieldsFromText(char *dst, size_t dstSize, const char *text);
void DX12JsonAppendRawField(char *dst, size_t dstSize, const char *name, const char *value);
void DX12JsonAppendStringField(char *dst, size_t dstSize, const char *name, const char *value);
void DX12JsonAppendWStringField(char *dst, size_t dstSize, const char *name, const wchar_t *value);
