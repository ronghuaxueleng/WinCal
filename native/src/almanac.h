#pragma once

#include "calendar_observances.h"
#include <optional>

namespace wincal
{
struct Almanac
{
    std::wstring monthPillar;
    std::wstring dayPillar;
    std::wstring suitable;
    std::wstring avoid;
};

// Daily almanac uses civil-day boundaries and the solar-term month (sect 1).
// Activity tables are derived from lunar_python 1.4.8, Copyright (c) 2020 6tail.
inline std::optional<Almanac> AlmanacForDate(int year, int month, int day)
{
    const auto date = std::chrono::year{year}/static_cast<unsigned>(month)/static_cast<unsigned>(day);
    if (!date.ok() || year < 1900 || year > 2100) return std::nullopt;
    const auto terms = SolarTermDates(year);
    const auto mod = [](long long n, int divisor) { return static_cast<int>((n % divisor + divisor) % divisor); };
    int index = -2;
    for (int i = 0; i < 24; i += 2)
        if (month * 100 + day >= terms[i]) index = i / 2 - 1;
    const int yearGan = mod(year - 4 - (month * 100 + day < terms[2] ? 1 : 0), 10);
    const int ganOffset = ((yearGan + (index < 0 ? 1 : 0)) % 5 + 1) * 2;
    const int monthGan = mod(index + ganOffset, 10), monthZhi = mod(index + 2, 12);
    int monthCycle{};
    for (; monthCycle < 60; ++monthCycle)
        if (monthCycle % 10 == monthGan && monthCycle % 12 == monthZhi) break;
    const int dayCycle = mod(std::chrono::sys_days{date}.time_since_epoch().count() + 2440577, 60);
    constexpr std::wstring_view stems = L"甲乙丙丁戊己庚辛壬癸", branches = L"子丑寅卯辰巳午未申酉戌亥";
    Almanac result;
    result.monthPillar = std::wstring{stems[monthGan], branches[monthZhi]};
    result.dayPillar = std::wstring{stems[dayCycle % 10], branches[dayCycle % 12]};
    constexpr std::string_view hex = "0123456789ABCDEF";
    const auto encode = [&](int value) { return std::string{hex[value / 16], hex[value % 16]}; };
    const auto decode = [&](std::string_view values)
    {
        std::wstring text;
        for (size_t i = 0; i + 1 < values.size(); i += 2)
        {
            const auto activity = hex.find(values[i]) * 16 + hex.find(values[i + 1]);
            if (!text.empty()) text += L" · ";
            text += almanac_data::Activities[activity];
        }
        return text.empty() ? std::wstring{L"无"} : text;
    };
    const auto key = encode(dayCycle) + "=";
    auto remaining = almanac_data::DayRules;
    while (true)
    {
        const auto found = remaining.find(key);
        if (found == std::string_view::npos) break;
        remaining.remove_prefix(found + key.size());
        const auto next = remaining.find('=');
        const auto record = remaining.substr(0, next == std::string_view::npos ? next : next - 2);
        const auto colon = record.find(':');
        bool matches{};
        for (size_t i = 0; i < colon; i += 2)
            if (record.substr(i, 2) == encode(monthCycle)) { matches = true; break; }
        if (!matches) continue;
        const auto comma = record.find(',', colon);
        result.suitable = decode(record.substr(colon + 1, comma - colon - 1));
        result.avoid = decode(record.substr(comma + 1));
        return result;
    }
    return std::nullopt;
}
}
