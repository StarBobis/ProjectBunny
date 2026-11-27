#pragma once

#include <cstddef>
#include <string_view>

namespace migoto {

template<typename Visitor>
bool VisitDrawArguments(std::wstring_view text, std::size_t count, Visitor visit)
{
    for (std::size_t index = 0; index < count; ++index) {
        const auto comma = text.find(L',');
        const bool last = comma == std::wstring_view::npos;
        if (last != (index + 1 == count) || !visit(index, text.substr(0, comma)))
            return false;
        if (last)
            return true;
        text.remove_prefix(comma + 1);
    }
    return false;
}

} // namespace migoto
