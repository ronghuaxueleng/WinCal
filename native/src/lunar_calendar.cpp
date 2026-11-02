#include "lunar_calendar.h"

#include <windows.h>

#include <icu.h>
#include <chrono>
#include <memory>

#include <array>
#include <string_view>

namespace
{
std::wstring_view LunarMonthName(int month)
{
    static constexpr std::array<std::wstring_view, 13> names{
        L"", L"正月", L"二月", L"三月", L"四月", L"五月", L"六月",
        L"七月", L"八月", L"九月", L"十月", L"冬月", L"腊月"};
    return month >= 1 && month <= 12 ? names[static_cast<size_t>(month)] : L"";
}

std::wstring_view LunarDayName(int day)
{
    static constexpr std::array<std::wstring_view, 31> names{
        L"", L"初一", L"初二", L"初三", L"初四", L"初五", L"初六", L"初七", L"初八", L"初九", L"初十",
        L"十一", L"十二", L"十三", L"十四", L"十五", L"十六", L"十七", L"十八", L"十九", L"二十",
        L"廿一", L"廿二", L"廿三", L"廿四", L"廿五", L"廿六", L"廿七", L"廿八", L"廿九", L"三十"};
    return day >= 1 && day <= 30 ? names[static_cast<size_t>(day)] : L"";
}

std::wstring FestivalName(int month, int day, bool leapMonth)
{
    if (leapMonth) return {};
    const int key = month * 100 + day;
    switch (key)
    {
    case 101: return L"春节";
    case 115: return L"元宵节";
    case 505: return L"端午节";
    case 707: return L"七夕";
    case 815: return L"中秋节";
    case 909: return L"重阳节";
    default: return {};
    }
}
}

namespace wincal
{
std::wstring LunarCalendar::TextForDate(int year, int month, int day)
{
    const auto key = std::tuple{year, month, day};
    if (const auto found = cache_.find(key); found != cache_.end())
        return found->second;

    std::wstring text;
    try
    {
        // Windows ICU exposes the actual lunar month and leap flag. WinRT's
        // Chinese calendar only exposes an ordinal month (1..13), even in names.
        using CalendarPointer = std::unique_ptr<UCalendar, decltype(&ucal_close)>;
        static CalendarPointer calendar{[]
        {
            UErrorCode status = U_ZERO_ERROR;
            return ucal_open(u"Asia/Shanghai", -1, "zh_CN@calendar=chinese", UCAL_DEFAULT, &status);
        }(), &ucal_close};
        if (!calendar) return {};
        const std::chrono::year_month_day date{std::chrono::year{year},
            std::chrono::month{static_cast<unsigned>(month)}, std::chrono::day{static_cast<unsigned>(day)}};
        if (!date.ok()) return {};
        const auto noon = std::chrono::sys_days{date} + std::chrono::hours(4); // Noon in China.
        UErrorCode status = U_ZERO_ERROR;
        ucal_setMillis(calendar.get(), static_cast<UDate>(
            std::chrono::duration_cast<std::chrono::milliseconds>(noon.time_since_epoch()).count()), &status);
        const int lunarDay = ucal_get(calendar.get(), UCAL_DATE, &status);
        const int lunarMonth = ucal_get(calendar.get(), UCAL_MONTH, &status) + 1;
        const bool isLeapMonth = ucal_get(calendar.get(), UCAL_IS_LEAP_MONTH, &status) != 0;
        ucal_add(calendar.get(), UCAL_DATE, 1, &status);
        const bool newYearsEve = ucal_get(calendar.get(), UCAL_MONTH, &status) == 0 &&
            ucal_get(calendar.get(), UCAL_DATE, &status) == 1 &&
            ucal_get(calendar.get(), UCAL_IS_LEAP_MONTH, &status) == 0;
        if (U_FAILURE(status)) return {};
        if (fullCache_.size() >= 512) fullCache_.clear();
        fullCache_[key] = (isLeapMonth ? L"闰" : L"") + std::wstring(LunarMonthName(lunarMonth)) +
            std::wstring(LunarDayName(lunarDay));

        if (newYearsEve)
            text = L"除夕";
        else if (const auto festival = FestivalName(lunarMonth, lunarDay, isLeapMonth); !festival.empty())
            text = festival;
        else if (lunarDay == 1 && lunarMonth > 0)
            text = isLeapMonth ? L"闰" + std::wstring(LunarMonthName(lunarMonth))
                               : std::wstring(LunarMonthName(lunarMonth));
        else
            text = LunarDayName(lunarDay);
    }
    catch (...)
    {
        // 不支持的日期范围或系统农历不可用时，保留日期主信息。
    }

    // 日历切换时只需保留有限的最近日期；避免长期浏览年份导致缓存无边界增长。
    if (cache_.size() >= 512)
        cache_.clear();
    cache_.emplace(key, text);
    return text;
}

std::wstring LunarCalendar::FullTextForDate(int year, int month, int day)
{
    const auto key = std::tuple{year, month, day};
    if (const auto found = fullCache_.find(key); found != fullCache_.end()) return found->second;
    cache_.erase(key);
    static_cast<void>(TextForDate(year, month, day));
    if (const auto found = fullCache_.find(key); found != fullCache_.end()) return found->second;
    return {};
}
}
