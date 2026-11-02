#pragma once

#include <string>
#include <string_view>

namespace wincal
{
inline void AppendJsonCodepoint(std::string& output, unsigned value)
{
    if (value <= 0x7f) output += static_cast<char>(value);
    else if (value <= 0x7ff)
    {
        output += static_cast<char>(0xc0 | (value >> 6));
        output += static_cast<char>(0x80 | (value & 0x3f));
    }
    else if (value <= 0xffff)
    {
        output += static_cast<char>(0xe0 | (value >> 12));
        output += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (value & 0x3f));
    }
    else
    {
        output += static_cast<char>(0xf0 | (value >> 18));
        output += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
        output += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (value & 0x3f));
    }
}

inline std::string DecodeJsonText(std::string_view value)
{
    const auto hex = [&](size_t start, unsigned& result)
    {
        if (start + 4 > value.size()) return false;
        result = 0;
        for (size_t i = start; i < start + 4; ++i)
        {
            const char c = value[i];
            const int digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return false;
            result = result * 16 + static_cast<unsigned>(digit);
        }
        return true;
    };
    std::string result;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] != '\\' || i + 1 == value.size()) { result += value[i]; continue; }
        switch (value[++i])
        {
        case 'b': result += '\b'; break;
        case 'f': result += '\f'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'u':
        {
            unsigned point{};
            if (!hex(i + 1, point)) return {};
            i += 4;
            if (point >= 0xd800 && point <= 0xdbff)
            {
                unsigned low{};
                if (i + 2 >= value.size() || value[i + 1] != '\\' || value[i + 2] != 'u' ||
                    !hex(i + 3, low) || low < 0xdc00 || low > 0xdfff) return {};
                point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                i += 6;
            }
            else if (point >= 0xdc00 && point <= 0xdfff) return {};
            AppendJsonCodepoint(result, point);
            break;
        }
        default: result += value[i]; break;
        }
    }
    return result;
}
}
