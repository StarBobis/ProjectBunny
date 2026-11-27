#pragma once

#include <cwchar>
#include "util_min.h"

template <class T1, class T2>
static T2 lookup_enum_val(struct EnumName_t<T1, T2> *enum_names, T1 name, size_t len, T2 defaultValue, bool *found=NULL)
{
	for (; enum_names->name; enum_names++) {
		if (!_wcsnicmp(name, enum_names->name, len)) {
			if (found)
				*found = true;
			return enum_names->val;
		}
	}

	if (found)
		*found = false;

	return defaultValue;
}

// This is similar to the above, but stops parsing when it hits an unrecognised
// keyword and returns the position without throwing any errors. It also
// doesn't modify the option_string, allowing it to be used with C++ strings.
template <class T1, class T2>
static T2 parse_enum_option_string_prefix(struct EnumName_t<T1, T2> *enum_names, T1 option_string, T1 *unrecognised)
{
	T1 ptr = option_string, cur;
	T2 ret = (T2)0;
	T2 tmp = T2::INVALID;
	size_t len;

	if (unrecognised)
		*unrecognised = NULL;

	while (*ptr) {
		// Skip over whitespace:
		for (; *ptr == L' '; ptr++) {}

		// Mark start of current entry:
		cur = ptr;

		// Scan until the next whitespace or end of string:
		for (; *ptr && *ptr != L' '; ptr++) {}

		// Note word length:
		len = ptr - cur;

		// Advance pointer if not at end of string:
		if (*ptr)
			ptr++;

		// Lookup the value of the current entry:
		tmp = lookup_enum_val<T1, T2> (enum_names, cur, len, T2::INVALID);
		if (tmp != T2::INVALID) {
			ret |= tmp;
		} else {
			if (unrecognised)
				*unrecognised = cur;
			return ret;
		}
	}
	return ret;
}
