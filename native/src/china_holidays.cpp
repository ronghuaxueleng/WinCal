#include "china_holidays.h"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <chrono>

namespace wincal
{
std::optional<ChinaHolidayYear> ParseChinaHolidayYear(std::string_view json, int year)
{
    try
    {
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        if (!JsonObject::TryParse(winrt::to_hstring(json), root) ||
            root.GetNamedNumber(L"code", -1) != 0) return std::nullopt;
        const auto days = root.GetNamedObject(L"holiday");
        ChinaHolidayYear result;
        for (const auto& item : days)
        {
            const auto day = item.Value().GetObject();
            const std::wstring date{day.GetNamedString(L"date")};
            const bool offDay = day.GetNamedBoolean(L"holiday");
            // Work-day names contain suffixes such as "国庆节后补班"; show the
            // associated festival while the badge already conveys "班".
            const std::wstring name{offDay ? day.GetNamedString(L"name") :
                day.GetNamedString(L"target", day.GetNamedString(L"name"))};
            if (date.size() != 10 || date[4] != L'-' || date[7] != L'-' || name.empty())
                return std::nullopt;
            int number{};
            for (size_t i = 0; i < date.size(); ++i)
            {
                if (i == 4 || i == 7) continue;
                if (date[i] < L'0' || date[i] > L'9') return std::nullopt;
                number = number * 10 + date[i] - L'0';
            }
            const std::chrono::year_month_day civil{
                std::chrono::year{number / 10000},
                std::chrono::month{static_cast<unsigned>(number / 100 % 100)},
                std::chrono::day{static_cast<unsigned>(number % 100)}};
            if (number / 10000 != year || !civil.ok() ||
                std::wstring{item.Key()} != date.substr(5)) return std::nullopt;
            if (!result.emplace(number, ChinaHoliday{name, offDay}).second)
                return std::nullopt;
        }
        // Empty/unpublished years must not replace a previously valid cache.
        if (result.empty()) return std::nullopt;
        return result;
    }
    catch (const winrt::hresult_error&)
    {
        return std::nullopt;
    }
}
}
