#pragma once

#include <map>
#include <string>
#include <tuple>
#include "calendar_observances.h"

namespace wincal
{
class LunarCalendar
{
public:
    [[nodiscard]] std::wstring TextForDate(int year, int month, int day);
    [[nodiscard]] std::wstring FullTextForDate(int year, int month, int day);
    [[nodiscard]] std::vector<std::wstring> ObservancesForDate(int year, int month, int day);

private:
    std::map<std::tuple<int, int, int>, std::wstring> cache_;
    std::map<std::tuple<int, int, int>, std::wstring> fullCache_;
    std::map<int, std::array<int, 24>> solarTerms_;
};
}
