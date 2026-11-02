#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <string_view>
#include <functional>

namespace wincal
{
// SoftMedia-compatible tokens. Fields are supplied by WinCal; time is always
// formatted in Explorer so seconds keep ticking between weather updates.
inline constexpr std::array<std::wstring_view, 29> TaskbarFieldNames{
    L"weather", L"pos", L"MD", L"shichen", L"rtm", L"rhu", L"rwd",
    L"we1", L"we2", L"we3", L"we4", L"we5", L"we6", L"we7",
    L"tl1", L"tl2", L"tl3", L"tl4", L"tl5", L"tl6", L"tl7",
    L"th1", L"th2", L"th3", L"th4", L"th5", L"th6", L"th7", L"icn"};
using TaskbarFields = std::array<std::wstring, TaskbarFieldNames.size()>;
inline constexpr wchar_t DefaultTaskbarFormat[] = L"yyyy年m月d日 MD\\ndddd HH:ff:ss（shichen）pos weather";

inline std::wstring NormalizeTaskbarFormat(std::wstring_view text)
{
    std::wstring result;
    for (wchar_t c : text)
    {
        if (c == L'\r') continue;
        if (c == L'\n') result += L"\\n";
        else result += c;
    }
    return result;
}

inline std::wstring FormatTaskbar(std::wstring_view text, const SYSTEMTIME& time,
    const std::function<std::wstring(size_t)>& field)
{
    const auto number = [](int n, bool pad) { return (pad && n < 10 ? L"0" : L"") + std::to_wstring(n); };
    constexpr std::array<std::wstring_view, 20> tokens{L"yyyy", L"dddd", L"ddd", L"yy", L"mm", L"dd", L"HH", L"hh",
        L"ff", L"ss", L"tt", L"TT", L"m", L"d", L"H", L"h", L"f", L"s", L"M", L"D"};
    std::wstring output;
    while (!text.empty())
    {
        if (text.starts_with(L"\\n")) { output += L'\n'; text.remove_prefix(2); continue; }
        if (text.front() == L'\'')
        {
            text.remove_prefix(1);
            if (text.starts_with(L"'")) { output += L'\''; text.remove_prefix(1); continue; }
            const auto end = text.find(L'\'');
            output += text.substr(0, end);
            text = end == text.npos ? std::wstring_view{} : text.substr(end + 1);
            continue;
        }
        size_t matched = 0, index = 0;
        for (size_t i = 0; i < TaskbarFieldNames.size(); ++i)
            if (text.starts_with(TaskbarFieldNames[i]) && TaskbarFieldNames[i].size() > matched)
                { matched = TaskbarFieldNames[i].size(); index = i; }
        if (matched) { output += field(index); text.remove_prefix(matched); continue; }
        for (const auto token : tokens)
        {
            if (!text.starts_with(token)) continue;
            if (token == L"yyyy") output += number(time.wYear, false);
            else if (token == L"yy") output += number(time.wYear % 100, true);
            else if (token == L"m" || token == L"mm") output += number(time.wMonth, token.size() == 2);
            else if (token == L"d" || token == L"dd") output += number(time.wDay, token.size() == 2);
            else if (token == L"H" || token == L"HH") output += number(time.wHour, token.size() == 2);
            else if (token == L"h" || token == L"hh") output += number((time.wHour + 11) % 12 + 1, token.size() == 2);
            else if (token == L"f" || token == L"ff") output += number(time.wMinute, token.size() == 2);
            else if (token == L"s" || token == L"ss") output += number(time.wSecond, token.size() == 2);
            else if (token == L"tt") output += time.wHour < 12 ? L"AM" : L"PM";
            else if (token == L"TT") output += time.wHour < 12 ? L"上午" : L"下午";
            else if (token == L"ddd" || token == L"dddd")
            {
                output += token == L"ddd" ? L"周" : L"星期";
                output += L"日一二三四五六"[time.wDayOfWeek % 7];
            }
            else if (token == L"M" || token == L"D")
            {
                const auto lunar = field(2); const auto end = lunar.find(L'月');
                output += end == lunar.npos ? lunar : token == L"M" ? lunar.substr(0, end + 1) : lunar.substr(end + 1);
            }
            matched = token.size(); break;
        }
        if (matched) text.remove_prefix(matched);
        else { output += text.front(); text.remove_prefix(1); }
    }
    return output;
}
}
