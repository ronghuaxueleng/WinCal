#include "../src/fortune.h"
#include <iostream>
#include "fortune_fixtures.h"
#include "fortune_luck_fixtures.h"

int main()
{
    int failures{};
    const auto birth = wincal::ParseFortuneBirthday(L"1990-04-24");
    const auto day = wincal::ParseFortuneBirthday(L"2026-11-04");
    if (!birth || !day) return 1;
    for (const auto bad : {L"2025-02-29", L"2000-13-01", L"1990-4-24", L"1990-04-24junk", L"1899-01-01", L""})
        if (wincal::ParseFortuneBirthday(bad)) ++failures;
    if (!wincal::ParseFortuneBirthday(L"2000-02-29")) ++failures;
    for (const auto& expected : PillarFixtures)
    {
        const auto date = std::chrono::sys_days{std::chrono::year{expected.year}/
            static_cast<unsigned>(expected.month)/static_cast<unsigned>(expected.day)};
        const auto chart = wincal::BuildFortuneChart(date, expected.hour * 60 + expected.minute, expected.second);
        if (!chart || chart->Text() != expected.pillars)
        {
            std::cerr << "Pillar mismatch: " << expected.year << '-' << expected.month << '-' << expected.day <<
                ' ' << expected.hour << ':' << expected.minute << ':' << expected.second << '\n';
            if (chart)
            {
                for (const auto& pillar : chart->pillars) std::cerr << pillar.stem << ',' << pillar.branch << ' ';
                std::cerr << "expected ";
                const std::wstring reference = expected.pillars;
                for (int p = 0; p < 4; ++p)
                    std::cerr << std::wstring_view{L"甲乙丙丁戊己庚辛壬癸"}.find(reference[p*3]) << ',' <<
                        std::wstring_view{L"子丑寅卯辰巳午未申酉戌亥"}.find(reference[p*3+1]) << ' ';
                std::cerr << '\n';
            }
            ++failures;
        }
    }
    for (const auto bad : {L"24:00", L"12:60", L"5:30", L"05:30junk", L"aa:bb", L""})
        if (wincal::ParseFortuneTime(bad)) ++failures;
    if (wincal::ParseFortuneTime(L"05:30") != 330 || wincal::ParseFortuneTime(L"23:59") != 1439) ++failures;
    const auto chart = wincal::BuildFortuneChart(*birth, 330);
    const auto otherTime = wincal::BuildFortuneChart(*birth, 990);
    const auto unknownTime = wincal::BuildFortuneChart(*birth);
    if (!chart || !otherTime || !unknownTime) return 1;
    if (!chart->hasTime || unknownTime->hasTime || chart->pillars[3].Text() == otherTime->pillars[3].Text()) ++failures;
    int fullTotal{}, unknownTotal{};
    for (const int value : chart->elements) fullTotal += value;
    for (const int value : unknownTime->elements) unknownTotal += value;
    if (fullTotal <= 0 || unknownTotal <= 0) ++failures;
    for (const auto& expected : LuckFixtures)
    {
        const auto birthDay = std::chrono::sys_days{std::chrono::year{expected.year}/
            static_cast<unsigned>(expected.month)/static_cast<unsigned>(expected.day)};
        const auto natal = wincal::BuildFortuneChart(birthDay, expected.hour * 60 + expected.minute, expected.second,
            expected.gender == 1 ? wincal::FortuneGender::Male : wincal::FortuneGender::Female);
        if (!natal || natal->luckStart.time_since_epoch().count() != expected.start || natal->luckForward != expected.forward)
        {
            std::cerr << "Luck start mismatch: " << expected.year << '-' << expected.month << '-' << expected.day <<
                ' ' << expected.hour << ':' << expected.minute << ':' << expected.second << " gender=" << expected.gender << '\n';
            ++failures;
            continue;
        }
        const auto first = wincal::FortuneLuckAt(*natal, natal->luckStart);
        const auto lastOfFirst = wincal::FortuneLuckAt(*natal, wincal::FortuneAddYears(natal->luckStart,10) - std::chrono::seconds{1});
        const auto second = wincal::FortuneLuckAt(*natal, wincal::FortuneAddYears(natal->luckStart,10));
        if (wincal::FortuneLuckAt(*natal, natal->luckStart - std::chrono::seconds{1}) || !first || !second ||
            !lastOfFirst || first->Text() != expected.first || lastOfFirst->Text() != expected.first ||
            second->Text() != expected.secondPillar) ++failures;
    }
    if (wincal::FortuneLuckAt(*unknownTime, *day) || wincal::FortuneLuckAt(*chart, *day)) ++failures;
    if (wincal::FortuneSeasonFactor(2,0) != 150 || wincal::FortuneSeasonFactor(2,1) != 120 ||
        wincal::FortuneSeasonFactor(2,2) != 50 || wincal::FortuneSeasonFactor(2,3) != 70 ||
        wincal::FortuneSeasonFactor(2,4) != 90) ++failures;
    auto formationChart = *chart;
    formationChart.hasTime = false;
    formationChart.pillars[0].branch = 8; formationChart.pillars[1].branch = 0; formationChart.pillars[2].branch = 2;
    formationChart.affinity[4] = 8;
    if (wincal::FortuneEvaluateLayer(formationChart, {4,4}).formation != 4 ||
        wincal::FortuneEvaluateLayer(formationChart, {4,4}, {{{4,4},{},{}}}, 1).formation != 0) ++failures;
    const auto priorToLiChun = wincal::FortuneForDate(*chart, *wincal::ParseFortuneBirthday(L"2026-02-03"));
    const auto afterLiChun = wincal::FortuneForDate(*chart, *wincal::ParseFortuneBirthday(L"2026-02-05"));
    if (priorToLiChun.yearPillar.Text() == afterLiChun.yearPillar.Text() ||
        priorToLiChun.monthPillar.Text() == afterLiChun.monthPillar.Text()) ++failures;
    if (wincal::BuildFortuneChart(*birth, -1) || wincal::BuildFortuneChart(*birth, 1440) ||
        wincal::BuildFortuneChart(*birth, 0, 60) ||
        wincal::BuildFortuneChart(std::chrono::sys_days{std::chrono::year{1899}/12/31})) ++failures;
    if (wincal::FortuneBranchBonus(0,1) != 6 || wincal::FortuneBranchBonus(0,6) != -8 ||
        wincal::FortuneBranchBonus(0,3) != -5 || wincal::FortuneBranchBonus(0,7) != -4 ||
        wincal::FortuneBranchBonus(0,9) != -3 || wincal::FortuneBranchBonus(0,4) != 3 ||
        wincal::FortuneBranchBonus(4,4) != -3) ++failures;
    for (int a = 0; a < 12; ++a)
        for (int b = 0; b < 12; ++b)
            if (wincal::FortuneBranchBonus(a,b) != wincal::FortuneBranchBonus(b,a)) ++failures;
    if (wincal::FortuneTenGod(0, 0) != L"比肩" || wincal::FortuneTenGod(0, 1) != L"劫财" ||
        wincal::FortuneTenGod(0, 6) != L"七杀" || wincal::FortuneTenGod(0, 7) != L"正官") ++failures;
    bool changed{}, timeChangesScores{};
    for (int i = 0; i < 730; ++i)
    {
        const auto current = *day + std::chrono::days{i};
        const auto a = wincal::FortuneForDate(*chart, current), b = wincal::FortuneForDate(*chart, current);
        if (a.overall != b.overall || a.overall != (a.social + a.work + a.vitality + 1) / 3 ||
            a.social < 5 || a.social > 95 || a.work < 5 || a.work > 95 || a.vitality < 5 || a.vitality > 95 ||
            a.contextBonus < -20 || a.contextBonus > 20 || a.luckPillar ||
            wincal::FortuneAdvice(a).empty()) ++failures;
        if (a.overall != wincal::FortuneForDate(*chart, *day).overall) changed = true;
        if (a.overall != wincal::FortuneForDate(*otherTime, current).overall) timeChangesScores = true;
    }
    if (!changed || !timeChangesScores) ++failures;
    std::cout << "Fortune checks: " << std::size(PillarFixtures) << " charts; " << std::size(LuckFixtures) <<
        " luck references; failures=" << failures << '\n';
    return failures ? 1 : 0;
}
