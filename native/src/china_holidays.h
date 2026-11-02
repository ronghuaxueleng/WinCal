#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace wincal
{
struct ChinaHoliday
{
    std::wstring name;
    bool offDay{};
};

using ChinaHolidayYear = std::map<int, ChinaHoliday>; // YYYYMMDD, civil dates in China.
// Call on a COM-initialized UI or worker thread.
[[nodiscard]] std::optional<ChinaHolidayYear> ParseChinaHolidayYear(std::string_view json, int year);
}
