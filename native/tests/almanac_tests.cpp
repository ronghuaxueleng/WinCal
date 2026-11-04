#include "../src/almanac.h"
#include "almanac_fixtures.h"
#include <iostream>

int main()
{
    int failures{};
    for (const auto& expected : AlmanacFixtures)
    {
        const auto actual = wincal::AlmanacForDate(expected.year, expected.month, expected.day);
        if (!actual || actual->monthPillar != expected.monthPillar || actual->dayPillar != expected.dayPillar ||
            actual->suitable != expected.yi || actual->avoid != expected.ji)
        {
            std::cerr << "Almanac mismatch: " << expected.year << '-' << expected.month << '-' << expected.day << '\n';
            ++failures;
        }
    }
    if (wincal::AlmanacForDate(2026, 2, 30) || wincal::AlmanacForDate(1899, 12, 31) ||
        wincal::AlmanacForDate(2101, 1, 1)) ++failures;
    std::cout << "Almanac reference checks: " << std::size(AlmanacFixtures) << "; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
