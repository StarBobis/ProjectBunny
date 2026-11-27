#pragma once

#include <cstddef>
#include <string_view>

namespace migoto {

// Leave the last field in place after the list is exhausted. DX11 reuses
// it when another column (e.g. condition or run) has more cycle entries.
template <class Char>
bool NextKeyCycleField(std::basic_string_view<Char> list, std::size_t &offset, std::basic_string_view<Char> &field)
{
	if (offset >= list.size())
		return false;
	const auto comma = list.find(Char(','), offset);
	field = list.substr(offset, comma == list.npos ? list.size() - offset : comma - offset);
	offset = comma == list.npos ? list.size() : comma + 1;
	while (!field.empty() && field.front() == Char(' '))
		field.remove_prefix(1);
	while (!field.empty() && field.back() == Char(' '))
		field.remove_suffix(1);
	return true;
}

struct KeyCycleCursor
{
	static constexpr std::size_t unset = static_cast<std::size_t>(-1);
	std::size_t index = unset;
	bool smart = true;
	bool wrap = true;

	template <class Matches>
	bool Resync(std::size_t count, Matches matches)
	{
		if (index < count && matches(index))
			return false;
		for (std::size_t i = 0; i < count; ++i) {
			if (i != index && matches(i)) {
				index = i;
				return true;
			}
		}
		return false;
	}

	bool Advance(std::size_t count, bool back)
	{
		if (!count)
			return false;
		if (index == unset) {
			index = back ? count - 1 : 0;
		} else if (back && index > 0) {
			--index;
		} else if (!back && index < count - 1) {
			++index;
		} else if (wrap) {
			index = back ? count - 1 : 0;
		} else {
			return false;
		}
		return true;
	}
};

} // namespace migoto
