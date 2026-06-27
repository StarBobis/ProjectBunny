#include "DX12Json.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void AppendText(char *dst, size_t dstSize, const char *text)
{
	if (!dst || dstSize == 0 || !text)
		return;
	const size_t len = strlen(dst);
	if (len >= dstSize - 1)
		return;
	sprintf_s(dst + len, dstSize - len, "%s", text);
}

void DX12JsonAppendStringField(char *dst, size_t dstSize, const char *name, const char *value)
{
	char escaped[2048];
	DX12JsonEscapeString(escaped, sizeof(escaped), value ? value : "");
	AppendText(dst, dstSize, ",\"");
	AppendText(dst, dstSize, name);
	AppendText(dst, dstSize, "\":");
	AppendText(dst, dstSize, escaped);
}

void DX12JsonAppendWStringField(char *dst, size_t dstSize, const char *name, const wchar_t *value)
{
	char escaped[2048];
	DX12JsonEscapeWString(escaped, sizeof(escaped), value ? value : L"");
	AppendText(dst, dstSize, ",\"");
	AppendText(dst, dstSize, name);
	AppendText(dst, dstSize, "\":");
	AppendText(dst, dstSize, escaped);
}

void DX12JsonAppendRawField(char *dst, size_t dstSize, const char *name, const char *value)
{
	AppendText(dst, dstSize, ",\"");
	AppendText(dst, dstSize, name);
	AppendText(dst, dstSize, "\":");
	AppendText(dst, dstSize, value ? value : "0");
}

static bool IsIntegerText(const char *value)
{
	if (!value || !*value)
		return false;
	if (*value == '-')
		value++;
	if (!*value)
		return false;
	for (; *value; ++value) {
		if (!isdigit(static_cast<unsigned char>(*value)))
			return false;
	}
	return true;
}

static bool IsHexText(const char *value)
{
	if (!value || value[0] != '0' || (value[1] != 'x' && value[1] != 'X'))
		return false;
	value += 2;
	if (!*value)
		return false;
	for (; *value; ++value) {
		if (!isxdigit(static_cast<unsigned char>(*value)))
			return false;
	}
	return true;
}

static bool IsIdentifierKeyChar(char c)
{
	return isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool IsSkippablePunctuation(char c)
{
	return c == ':' || c == ',' || c == ';';
}

void DX12JsonEscapeString(char *dst, size_t dstSize, const char *src)
{
	if (!dst || dstSize == 0)
		return;

	size_t pos = 0;
	if (pos + 1 < dstSize)
		dst[pos++] = '"';
	if (src) {
		for (; *src && pos + 6 < dstSize; ++src) {
			switch (*src) {
			case '"': dst[pos++] = '\\'; dst[pos++] = '"'; break;
			case '\\': dst[pos++] = '\\'; dst[pos++] = '\\'; break;
			case '\b': dst[pos++] = '\\'; dst[pos++] = 'b'; break;
			case '\f': dst[pos++] = '\\'; dst[pos++] = 'f'; break;
			case '\n': dst[pos++] = '\\'; dst[pos++] = 'n'; break;
			case '\r': dst[pos++] = '\\'; dst[pos++] = 'r'; break;
			case '\t': dst[pos++] = '\\'; dst[pos++] = 't'; break;
			default:
				if (static_cast<unsigned char>(*src) < 0x20) {
					sprintf_s(dst + pos, dstSize - pos, "\\u%04x",
						static_cast<unsigned char>(*src));
					pos += 6;
				} else {
					dst[pos++] = *src;
				}
			}
		}
	}
	if (pos + 1 < dstSize)
		dst[pos++] = '"';
	if (pos < dstSize)
		dst[pos] = '\0';
}

void DX12JsonEscapeWString(char *dst, size_t dstSize, const wchar_t *src)
{
	if (!dst || dstSize == 0)
		return;

	size_t pos = 0;
	if (pos + 1 < dstSize)
		dst[pos++] = '"';
	if (src) {
		for (; *src && pos + 6 < dstSize; ++src) {
			wchar_t c = *src;
			if (c < 0x20) {
				sprintf_s(dst + pos, dstSize - pos, "\\u%04x",
					static_cast<unsigned int>(c));
				pos += 6;
			} else if (c == L'"') {
				dst[pos++] = '\\';
				dst[pos++] = '"';
			} else if (c == L'\\') {
				dst[pos++] = '\\';
				dst[pos++] = '\\';
			} else if (c < 0x80) {
				dst[pos++] = static_cast<char>(c);
			} else if (c < 0x800 && pos + 2 < dstSize) {
				dst[pos++] = static_cast<char>(0xC0 | (c >> 6));
				dst[pos++] = static_cast<char>(0x80 | (c & 0x3F));
			} else if (pos + 3 < dstSize) {
				dst[pos++] = static_cast<char>(0xE0 | (c >> 12));
				dst[pos++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				dst[pos++] = static_cast<char>(0x80 | (c & 0x3F));
			}
		}
	}
	if (pos + 1 < dstSize)
		dst[pos++] = '"';
	if (pos < dstSize)
		dst[pos] = '\0';
}

void DX12JsonAppendLogFieldsFromText(char *dst, size_t dstSize, const char *text)
{
	if (!dst || dstSize == 0)
		return;

	if (!text)
		text = "";

	char clean[2048];
	strncpy_s(clean, text, _TRUNCATE);
	size_t cleanLen = strlen(clean);
	while (cleanLen > 0 && (clean[cleanLen - 1] == '\n' || clean[cleanLen - 1] == '\r')) {
		clean[--cleanLen] = '\0';
	}

	const char *cursor = clean;
	const char *firstKey = nullptr;
	for (const char *scan = clean; *scan; ++scan) {
		if ((scan == clean || !IsIdentifierKeyChar(*(scan - 1))) && IsIdentifierKeyChar(*scan)) {
			const char *keyEnd = scan;
			while (IsIdentifierKeyChar(*keyEnd))
				keyEnd++;
			if (*keyEnd == '=') {
				firstKey = scan;
				break;
			}
			scan = keyEnd;
			if (!*scan)
				break;
		}
	}
	if (firstKey)
		cursor = firstKey;
	else
		cursor += strlen(cursor);

	char prefix[512] = "";
	if (cursor > clean) {
		size_t prefixLen = static_cast<size_t>(cursor - clean);
		if (prefixLen >= sizeof(prefix))
			prefixLen = sizeof(prefix) - 1;
		memcpy(prefix, clean, prefixLen);
		prefix[prefixLen] = '\0';
		while (prefixLen > 0 && isspace(static_cast<unsigned char>(prefix[prefixLen - 1])))
			prefix[--prefixLen] = '\0';
	}

	bool wroteKeyValue = false;
	bool wroteText = false;
	if (prefix[0]) {
		DX12JsonAppendStringField(dst, dstSize, "text", prefix);
		wroteText = true;
	}

	while (*cursor) {
		while (*cursor && (isspace(static_cast<unsigned char>(*cursor)) || IsSkippablePunctuation(*cursor)))
			cursor++;
		if (!*cursor)
			break;

		const char *keyStart = cursor;
		while (IsIdentifierKeyChar(*cursor))
			cursor++;
		if (cursor == keyStart || *cursor != '=')
			break;

		char key[64];
		size_t keyLen = static_cast<size_t>(cursor - keyStart);
		if (keyLen >= sizeof(key))
			keyLen = sizeof(key) - 1;
		strncpy_s(key, keyLen + 1, keyStart, keyLen);
		cursor++;

		const char *valueStart = cursor;
		while (*cursor && !isspace(static_cast<unsigned char>(*cursor)))
			cursor++;

		char value[512];
		size_t valueLen = static_cast<size_t>(cursor - valueStart);
		if (valueLen >= sizeof(value))
			valueLen = sizeof(value) - 1;
		strncpy_s(value, valueLen + 1, valueStart, valueLen);

		while (valueLen > 0 && (value[valueLen - 1] == ',' || value[valueLen - 1] == ';')) {
			value[--valueLen] = '\0';
		}

		if (!strcmp(value, "true") || !strcmp(value, "false") || IsIntegerText(value))
			DX12JsonAppendRawField(dst, dstSize, key, value);
		else
			DX12JsonAppendStringField(dst, dstSize, key, value);
		wroteKeyValue = true;
	}

	if (!wroteKeyValue && !wroteText)
		DX12JsonAppendStringField(dst, dstSize, "text", clean);
}
