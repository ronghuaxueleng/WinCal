#pragma once

#include <array>
#include <chrono>
#include "../third_party/lunar/almanac_data.h"
#include <string>
#include <vector>

namespace wincal
{
inline constexpr std::array<const wchar_t*, 24> SolarTermNames{
    L"小寒", L"大寒", L"立春", L"雨水", L"惊蛰", L"春分", L"清明", L"谷雨",
    L"立夏", L"小满", L"芒种", L"夏至", L"小暑", L"大暑", L"立秋", L"处暑",
    L"白露", L"秋分", L"寒露", L"霜降", L"立冬", L"小雪", L"大雪", L"冬至"};

// China civil dates from the embedded lunar_python 1.4.8 solar-term table.
inline std::array<int, 24> SolarTermDates(int year)
{
    if (year < 1900 || year > 2100) return {};
    return almanac_data::SolarTerms[static_cast<size_t>(year - 1900)];
}

inline std::vector<std::wstring> GregorianObservances(int year, int month, int day)
{
    std::vector<std::wstring> names;
    switch (month * 100 + day)
    {
    case 101: names.emplace_back(L"元旦"); break;
    case 214: names.emplace_back(L"情人节"); break;
    case 308: names.emplace_back(L"妇女节"); break;
    case 312: names.emplace_back(L"植树节"); break;
    case 401: names.emplace_back(L"愚人节"); break;
    case 501: names.emplace_back(L"劳动节"); break;
    case 504: names.emplace_back(L"青年节"); break;
    case 601: names.emplace_back(L"儿童节"); break;
    case 701: names.emplace_back(L"建党节"); break;
    case 801: names.emplace_back(L"建军节"); break;
    case 910: names.emplace_back(L"教师节"); break;
    case 1001: names.emplace_back(L"国庆节"); break;
    case 1031: names.emplace_back(L"万圣夜"); break;
    case 1101: names.emplace_back(L"万圣节"); break;
    case 1111: names.emplace_back(L"购物节"); break;
    case 1224: names.emplace_back(L"平安夜"); break;
    case 1225: names.emplace_back(L"圣诞节"); break;
    default: break;
    }
    const auto date = std::chrono::year{year}/static_cast<unsigned>(month)/static_cast<unsigned>(day);
    if (!date.ok()) return {};
    const unsigned weekday = std::chrono::weekday{std::chrono::sys_days{date}}.c_encoding();
    if (month == 5 && day >= 8 && day <= 14 && weekday == 0) names.emplace_back(L"母亲节");
    if (month == 6 && day >= 15 && day <= 21 && weekday == 0) names.emplace_back(L"父亲节");
    if (month == 11 && day >= 22 && day <= 28 && weekday == 4) names.emplace_back(L"感恩节");
    return names;
}
}
