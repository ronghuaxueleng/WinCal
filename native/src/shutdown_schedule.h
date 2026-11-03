#pragma once

#include <ctime>
#include <optional>
#include <string_view>

namespace wincal
{
// Resolve user input without performing any system action.
inline std::optional<std::time_t> ShutdownDeadline(std::wstring_view text, bool atTime, std::time_t now)
{
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos) return std::nullopt;
    text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
    const auto number = [](std::wstring_view value) -> std::optional<int>
    {
        if (value.empty() || value.size() > 5) return std::nullopt;
        int result{};
        for (const auto ch : value)
        {
            if (ch < L'0' || ch > L'9') return std::nullopt;
            result = result * 10 + ch - L'0';
        }
        return result;
    };
    if (!atTime)
    {
        const auto minutes = number(text);
        if (!minutes || *minutes < 1 || *minutes > 10080) return std::nullopt;
        return now + static_cast<std::time_t>(*minutes) * 60;
    }
    if (text.size() != 5 || text[2] != L':') return std::nullopt;
    const auto hours = number(text.substr(0, 2)), minutes = number(text.substr(3, 2));
    if (!hours || !minutes || *hours > 23 || *minutes > 59) return std::nullopt;
    std::tm local{};
    localtime_s(&local, &now);
    local.tm_hour = *hours;
    local.tm_min = *minutes;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    auto due = std::mktime(&local);
    if (due <= now)
    {
        ++local.tm_mday;
        local.tm_isdst = -1;
        due = std::mktime(&local);
    }
    if (due == static_cast<std::time_t>(-1)) return std::nullopt;
    return due;
}
}
