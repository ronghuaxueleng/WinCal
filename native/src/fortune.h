#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include "../third_party/lunar/fortune_terms.h"

namespace wincal
{
inline std::optional<std::chrono::sys_days> ParseFortuneBirthday(std::wstring_view value)
{
    if (value.size() != 10 || value[4] != L'-' || value[7] != L'-') return {};
    int parts[3]{};
    int part{};
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (i == 4 || i == 7) { ++part; continue; }
        if (value[i] < L'0' || value[i] > L'9') return {};
        parts[part] = parts[part] * 10 + value[i] - L'0';
    }
    const auto date = std::chrono::year{parts[0]}/static_cast<unsigned>(parts[1])/static_cast<unsigned>(parts[2]);
    if (!date.ok() || parts[0] < 1900 || parts[0] > 2100) return {};
    return std::chrono::sys_days{date};
}

inline int FortuneMod(long long value, int base)
{
    return static_cast<int>((value % base + base) % base);
}

inline std::optional<int> ParseFortuneTime(std::wstring_view value)
{
    if (value.size() != 5 || value[2] != L':') return {};
    for (const int i : {0, 1, 3, 4}) if (value[i] < L'0' || value[i] > L'9') return {};
    const int hour = (value[0] - L'0') * 10 + value[1] - L'0';
    const int minute = (value[3] - L'0') * 10 + value[4] - L'0';
    if (hour > 23 || minute > 59) return {};
    return hour * 60 + minute;
}

struct FortunePillar
{
    int stem{}, branch{};
    std::wstring Text() const
    {
        return {L"甲乙丙丁戊己庚辛壬癸"[stem], L"子丑寅卯辰巳午未申酉戌亥"[branch]};
    }
};

// Hidden stems: principal / middle / residual Qi. Order and identities match
// lunar-python's ZHI_HIDE_GAN; weights below are WinCal's explicit scoring policy.
inline constexpr int FortuneHiddenStems[12][3]{
    {9,-1,-1}, {5,9,7}, {0,2,4}, {1,-1,-1}, {4,1,9}, {2,6,4},
    {3,5,-1}, {5,3,1}, {6,8,4}, {7,-1,-1}, {4,7,3}, {8,0,-1}};
inline constexpr int FortuneHiddenWeights[3][3]{{10,0,0}, {7,3,0}, {6,3,1}};

enum class FortuneGender { Unknown, Male, Female };

inline int FortuneSeasonElement(int branch)
{
    constexpr int seasons[]{4,2,0,0,2,1,1,2,3,3,2,4};
    return seasons[branch];
}

// Wang / Xiang / Si / Qiu / Xiu. Numeric multipliers are WinCal policy.
inline int FortuneSeasonFactor(int monthBranch, int element)
{
    constexpr int factors[]{150,120,50,70,90};
    return factors[FortuneMod(element - FortuneSeasonElement(monthBranch), 5)];
}

inline std::chrono::sys_seconds FortuneAddYears(std::chrono::sys_seconds instant, int years)
{
    const auto day = std::chrono::floor<std::chrono::days>(instant);
    const std::chrono::year_month_day old{day};
    auto date = old + std::chrono::years{years};
    if (!date.ok()) date = std::chrono::year_month_day{date.year()/date.month()/std::chrono::last};
    return std::chrono::sys_days{date} + (instant - day);
}

struct FortuneChart
{
    std::array<FortunePillar, 4> pillars{};
    bool hasTime{};
    std::array<int, 5> elements{}; // Wood, fire, earth, metal, water.
    int supportPercent{};
    int strength{}; // -1 weak, 0 balanced, +1 strong: simplified support ratio.
    std::array<int, 5> affinity{};
    std::array<int, 5> climateAffinity{};
    std::array<int, 4> rootPower{};
    std::chrono::sys_seconds birthInstant{}, luckStart{};
    FortuneGender gender{FortuneGender::Unknown};
    bool luckForward{};
    std::wstring pattern;

    std::wstring Text() const
    {
        return pillars[0].Text() + L" " + pillars[1].Text() + L" " + pillars[2].Text() +
            (hasTime ? L" " + pillars[3].Text() : L"（时辰未填）");
    }
};

// Sect 2: year at exact Li Chun, month at exact Jie, day at midnight;
// the 23:00 Zi hour uses the following day's stem, as lunar-python does.
// Inputs use Beijing civil time (UTC+8), without true-solar-time correction.
// No time supplied: explicitly use a three-pillar chart at civil noon.
inline std::optional<std::array<FortunePillar,4>> FortunePillarsAt(std::chrono::sys_days birth, int minutes, int second = 0)
{
    const std::chrono::year_month_day date{birth};
    const int year = static_cast<int>(date.year());
    if (year < 1899 || year > 2101 || minutes < 0 || minutes >= 1440 || second < 0 || second > 59) return {};
    const auto& terms = fortune_data::Jie[year - 1899];
    const auto instant = std::chrono::duration_cast<std::chrono::seconds>(birth.time_since_epoch()).count() +
        minutes * 60 + second;
    const int yearCycle = FortuneMod(year - 4 - (instant < terms[1] ? 1 : 0), 60);
    int monthBranch = 0; // Before this year's Xiao Han: preceding Zi month.
    for (int i = 0; i < 12; ++i) if (instant >= terms[i]) monthBranch = (i + 1) % 12;
    const int monthStem = (yearCycle % 10 % 5 * 2 + 2 + FortuneMod(monthBranch - 2, 12)) % 10;
    const int dayCycle = FortuneMod(birth.time_since_epoch().count() + 2440577, 60);
    const int hourBranch = ((minutes / 60 + 1) / 2) % 12;
    const int hourDayStem = (dayCycle % 10 + (minutes >= 1380 ? 1 : 0)) % 10;
    return std::array<FortunePillar,4>{{{yearCycle % 10, yearCycle % 12}, {monthStem, monthBranch},
                     {dayCycle % 10, dayCycle % 12}, {(hourDayStem % 5 * 2 + hourBranch) % 10, hourBranch}}};
}

inline std::wstring_view FortuneTenGod(int dayStem, int otherStem);

inline std::optional<FortuneChart> BuildFortuneChart(std::chrono::sys_days birth,
    std::optional<int> minutes = {}, int second = 0, FortuneGender gender = FortuneGender::Unknown)
{
    const int year = static_cast<int>(std::chrono::year_month_day{birth}.year());
    if (year < 1900 || year > 2100) return {};
    const auto pillars = FortunePillarsAt(birth, minutes.value_or(720), second);
    if (!pillars) return {};
    FortuneChart chart;
    chart.hasTime = minutes.has_value();
    chart.pillars = *pillars;
    chart.gender = gender;
    chart.birthInstant = birth + std::chrono::minutes{minutes.value_or(720)} + std::chrono::seconds{second};
    const int countPillars = chart.hasTime ? 4 : 3;
    const int monthBranch = chart.pillars[1].branch;
    // Root at the same pillar 100%, adjacent 90%, farther 80%.
    for (int i = 0; i < countPillars; ++i)
        for (int k = 0; k < countPillars; ++k)
        {
            const auto& hidden = FortuneHiddenStems[chart.pillars[k].branch];
            const int count = hidden[1] < 0 ? 1 : hidden[2] < 0 ? 2 : 3;
            for (int j = 0; j < count; ++j)
                if (hidden[j] / 2 == chart.pillars[i].stem / 2)
                    chart.rootPower[i] += FortuneHiddenWeights[count - 1][j] * (i == k ? 10 : std::abs(i-k) == 1 ? 9 : 8);
        }
    for (int i = 0; i < countPillars; ++i)
    {
        const auto& pillar = chart.pillars[i];
        const int distance = i == 2 ? 100 : i == 0 ? 80 : 90;
        const int element = pillar.stem / 2;
        chart.elements[element] += 40 * distance * FortuneSeasonFactor(monthBranch, element) *
            (chart.rootPower[i] > 0 ? 100 : 70) / 1000000;
        const auto& hidden = FortuneHiddenStems[pillar.branch];
        const int count = hidden[1] < 0 ? 1 : hidden[2] < 0 ? 2 : 3;
        for (int j = 0; j < count; ++j)
            chart.elements[hidden[j] / 2] += FortuneHiddenWeights[count - 1][j] * 10 * distance *
                FortuneSeasonFactor(monthBranch, hidden[j] / 2) / 10000;
    }
    const int dayElement = chart.pillars[2].stem / 2;
    const int resource = (dayElement + 4) % 5;
    int total{};
    for (const auto value : chart.elements) total += value;
    chart.supportPercent = (chart.elements[dayElement] + chart.elements[resource]) * 100 / total;
    chart.strength = chart.supportPercent > 55 ? 1 : chart.supportPercent < 45 ? -1 : 0;
    for (int element = 0; element < 5; ++element)
    {
        const bool supports = element == dayElement || element == resource;
        chart.affinity[element] = chart.strength == 0 ?
            (chart.elements[element] * 5 < total ? 6 : chart.elements[element] * 5 > total ? -6 : 0) :
            ((supports ? -1 : 1) * chart.strength * 8);
    }
    // Limited seasonal climate adjustment, kept separate from Fuyi affinity.
    if (monthBranch == 11 || monthBranch == 0 || monthBranch == 1)
    { chart.climateAffinity[1] = 4; chart.climateAffinity[4] = -3; }
    else if (monthBranch == 5 || monthBranch == 6 || monthBranch == 7)
    { chart.climateAffinity[4] = 4; chart.climateAffinity[1] = -3; }
    if (monthBranch == 10) chart.climateAffinity[4] = 2;
    if (monthBranch == 4) chart.climateAffinity[1] = 2;
    const int monthGod = FortuneHiddenStems[monthBranch][0];
    chart.pattern = std::wstring{FortuneTenGod(chart.pillars[2].stem, monthGod)} + L"当令";
    if (chart.supportPercent >= 85 || (chart.supportPercent <= 15 && chart.rootPower[2] == 0))
        chart.pattern += L"（极端命局，普通扶抑仅作参考）";
    if (gender != FortuneGender::Unknown && chart.hasTime)
    {
        chart.luckForward = (chart.pillars[0].stem % 2 == 0) == (gender == FortuneGender::Male);
        std::int64_t previous{}, next{};
        const auto instant = chart.birthInstant.time_since_epoch().count();
        for (int y = year - 1; y <= year + 1; ++y)
            for (const auto term : fortune_data::Jie[y - 1899])
            {
                if (term <= instant && (!previous || term > previous)) previous = term;
                if (term > instant && (!next || term < next)) next = term;
            }
        // Sect 2: three days -> one year; minute-level interval, ignoring seconds.
        const auto civilMinute = [](std::int64_t seconds) {
            return std::chrono::floor<std::chrono::minutes>(std::chrono::sys_seconds{std::chrono::seconds{seconds}}).time_since_epoch().count();
        };
        auto interval = chart.luckForward ? civilMinute(next) - civilMinute(instant) : civilMinute(instant) - civilMinute(previous);
        const int years = static_cast<int>(interval / 4320); interval %= 4320;
        const int months = static_cast<int>(interval / 360); interval %= 360;
        const int days = static_cast<int>(interval / 12), hours = static_cast<int>(interval % 12) * 2;
        auto start = FortuneAddYears(chart.birthInstant, years);
        const auto startDay = std::chrono::floor<std::chrono::days>(start);
        auto date = std::chrono::year_month_day{startDay} + std::chrono::months{months};
        if (!date.ok()) date = std::chrono::year_month_day{date.year()/date.month()/std::chrono::last};
        chart.luckStart = std::chrono::sys_days{date} + (start - startDay) + std::chrono::days{days} + std::chrono::hours{hours};
    }
    return chart;
}

inline std::wstring_view FortuneTenGod(int dayStem, int otherStem)
{
    const int relation = FortuneMod(otherStem / 2 - dayStem / 2, 5);
    const bool samePolarity = dayStem % 2 == otherStem % 2;
    constexpr std::wstring_view names[5][2]{{L"劫财",L"比肩"}, {L"伤官",L"食神"},
        {L"正财",L"偏财"}, {L"正官",L"七杀"}, {L"正印",L"偏印"}};
    return names[relation][samePolarity ? 1 : 0];
}

inline int FortuneBranchBonus(int a, int b)
{
    const auto pair = [&](int x, int y) { return (a == x && b == y) || (a == y && b == x); };
    if (a == b) return (a == 4 || a == 6 || a == 9 || a == 11) ? -3 : 0; // Self punishment.
    if (pair(0,1) || pair(2,11) || pair(3,10) || pair(4,9) || pair(5,8) || pair(6,7)) return 6;
    if (FortuneMod(a - b, 12) == 6) return -8;
    if (pair(0,3) || pair(2,5) || pair(5,8) || pair(2,8) || pair(1,10) || pair(10,7) || pair(1,7)) return -5;
    if (pair(0,7) || pair(1,6) || pair(2,5) || pair(3,4) || pair(8,11) || pair(9,10)) return -4;
    if (pair(0,9) || pair(1,4) || pair(2,11) || pair(3,6) || pair(5,8) || pair(7,10)) return -3;
    if (a % 4 == b % 4) return 3; // Partial San He only; never claim complete transformation.
    return 0;
}

struct FortuneDay
{
    int overall{}, social{}, work{}, vitality{};
    int elementBonus{}, branchBonus{}, tenGodBonus{};
    int dayStem{}, dayBranch{};
    FortunePillar yearPillar{}, monthPillar{};
    std::optional<FortunePillar> luckPillar;
    int luckBonus{}, yearBonus{}, monthBonus{}, climateBonus{}, formationBonus{}, contextBonus{};
};

inline std::optional<FortunePillar> FortuneLuckAt(const FortuneChart& chart, std::chrono::sys_seconds instant)
{
    if (chart.gender == FortuneGender::Unknown || !chart.hasTime || instant < chart.luckStart) return {};
    const int year = static_cast<int>(std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(instant)}.year());
    const int startYear = static_cast<int>(std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(chart.luckStart)}.year());
    int period = (year - startYear) / 10;
    if (instant < FortuneAddYears(chart.luckStart, period * 10)) --period;
    const int offset = (period + 1) * (chart.luckForward ? 1 : -1);
    return FortunePillar{FortuneMod(chart.pillars[1].stem + offset, 10), FortuneMod(chart.pillars[1].branch + offset, 12)};
}

struct FortuneLayer
{
    int element{}, branch{}, tenGod{}, climate{}, formation{};
    int Total() const { return std::clamp(element + branch / 2 + tenGod / 2 + climate + formation, -30, 30); }
};

inline FortuneLayer FortuneEvaluateLayer(const FortuneChart& chart, FortunePillar pillar,
    const std::array<FortunePillar,3>& upper = {}, int upperCount = 0)
{
    FortuneLayer result;
    const auto& hidden = FortuneHiddenStems[pillar.branch];
    const int count = hidden[1] < 0 ? 1 : hidden[2] < 0 ? 2 : 3;
    int affinity{}, climate{};
    for (int j = 0; j < count; ++j)
    {
        const int weight = FortuneHiddenWeights[count - 1][j] * 10;
        affinity += chart.affinity[hidden[j] / 2] * weight;
        climate += chart.climateAffinity[hidden[j] / 2] * weight;
    }
    result.element = (chart.affinity[pillar.stem / 2] * 40 + affinity) / 140;
    result.climate = (chart.climateAffinity[pillar.stem / 2] * 40 + climate) / 140;
    result.tenGod = chart.affinity[pillar.stem / 2] / 2;
    std::array<bool,12> present{};
    const auto add = [&](FortunePillar other) {
        present[other.branch] = true;
        result.branch += FortuneBranchBonus(pillar.branch, other.branch);
    };
    for (int i = 0; i < (chart.hasTime ? 4 : 3); ++i) add(chart.pillars[i]);
    for (int i = 0; i < upperCount; ++i) add(upper[i]);
    result.branch = std::clamp(result.branch, -20, 20);
    // Newly completed San He / San Hui. No automatic element transformation.
    constexpr int groups[8][4]{{8,0,4,4},{11,3,7,0},{2,6,10,1},{5,9,1,3},
        {2,3,4,0},{5,6,7,1},{8,9,10,3},{11,0,1,4}};
    for (const auto& group : groups)
    {
        bool before = true, after = true;
        for (int i = 0; i < 3; ++i)
        {
            before &= present[group[i]];
            after &= present[group[i]] || pillar.branch == group[i];
        }
        if (after && !before) result.formation += chart.affinity[group[3]] / 2;
    }
    result.formation = std::clamp(result.formation, -8, 8);
    return result;
}

// Rule v3: seasonal natal strength + ordered luck/year/month/day overlays.
// All numeric scoring weights are explicit WinCal policy, not a standard.
inline FortuneDay FortuneForDate(const FortuneChart& chart, std::chrono::sys_days day)
{
    const int cycle = FortuneMod(day.time_since_epoch().count() + 2440577, 60);
    const int stem = cycle % 10, branch = cycle % 12;
    FortuneDay result;
    result.dayStem = stem; result.dayBranch = branch;
    std::array<FortunePillar,3> upper{};
    int upperCount{};
    const auto instant = day + std::chrono::hours{12};
    result.luckPillar = FortuneLuckAt(chart, instant);
    if (result.luckPillar)
    {
        result.luckBonus = FortuneEvaluateLayer(chart, *result.luckPillar).Total() / 2;
        upper[upperCount++] = *result.luckPillar;
    }
    if (const auto calendar = FortunePillarsAt(day, 720))
    {
        result.yearPillar = (*calendar)[0]; result.monthPillar = (*calendar)[1];
        result.yearBonus = FortuneEvaluateLayer(chart, result.yearPillar, upper, upperCount).Total() / 3;
        upper[upperCount++] = result.yearPillar;
        result.monthBonus = FortuneEvaluateLayer(chart, result.monthPillar, upper, upperCount).Total() / 2;
        upper[upperCount++] = result.monthPillar;
    }
    const auto daily = FortuneEvaluateLayer(chart, {stem, branch}, upper, upperCount);
    result.elementBonus = daily.element; result.branchBonus = daily.branch;
    result.tenGodBonus = daily.tenGod; result.climateBonus = daily.climate;
    result.formationBonus = daily.formation;
    result.contextBonus = std::clamp(result.luckBonus + result.yearBonus + result.monthBonus, -20, 20);
    const auto clamp = [](int score) { return std::clamp(score, 5, 95); };
    result.social = clamp(60 + result.contextBonus + daily.element + daily.branch + daily.formation);
    result.work = clamp(60 + result.contextBonus + daily.element * 2 + daily.tenGod + daily.branch / 2 + daily.climate + daily.formation);
    result.vitality = clamp(60 + result.contextBonus + daily.element + daily.branch / 2 + daily.climate);
    result.overall = (result.social + result.work + result.vitality + 1) / 3;
    return result;
}

inline std::wstring FortuneRuleDetails(const FortuneChart& chart, const FortuneDay& day)
{
    const auto sign = [](int value) { return (value >= 0 ? L"+" : L"") + std::to_wstring(value); };
    std::wstring detail = L"命盘：" + chart.Text() + L"\n月令：" + chart.pillars[1].Text().substr(1) +
        L"；月令本气：" + chart.pattern + L"\n旺衰：" +
        (chart.strength > 0 ? L"偏强" : chart.strength < 0 ? L"偏弱" : L"中和") +
        L"；季节加权后生扶占比 " + std::to_wstring(chart.supportPercent) + L"%。\n日主通根力量：" +
        std::to_wstring(chart.rootPower[2]) + L"；透干及通根力量（年/月/日/时）：";
    for (int i = 0; i < (chart.hasTime ? 4 : 3); ++i)
        detail += chart.pillars[i].Text().substr(0,1) + L" " + std::to_wstring(chart.rootPower[i]) + L"  ";
    detail += L"\n五行季节加权力量（木火土金水）：";
    for (const int value : chart.elements) detail += std::to_wstring(value) + L" ";
    if (day.luckPillar)
    {
        const std::chrono::year_month_day start{std::chrono::floor<std::chrono::days>(chart.luckStart)};
        detail += L"\n大运：" + day.luckPillar->Text() + (chart.luckForward ? L"（顺排）" : L"（逆排）") +
            L"；起运日期 " + std::to_wstring(static_cast<int>(start.year())) + L"-" +
            std::to_wstring(static_cast<unsigned>(start.month())) + L"-" + std::to_wstring(static_cast<unsigned>(start.day())) +
            L"；贡献 " + sign(day.luckBonus);
    }
    else detail += chart.gender == FortuneGender::Unknown ? L"\n大运未计入：请选择性别并点击更新。" :
        !chart.hasTime ? L"\n大运未计入：请填写出生时间并点击更新。" : L"\n尚未起运，不计大运项。";
    detail += L"\n流年：" + day.yearPillar.Text() + L" " + sign(day.yearBonus) +
        L"；流月：" + day.monthPillar.Text() + L" " + sign(day.monthBonus) + L"；背景合计 " + sign(day.contextBonus) +
        L"\n流日：" + FortunePillar{day.dayStem,day.dayBranch}.Text() + L"；十神：" +
        std::wstring{FortuneTenGod(chart.pillars[2].stem, day.dayStem)} + L"。\n五行 " + sign(day.elementBonus) +
        L"；地支 " + sign(day.branchBonus) + L"；十神 " + sign(day.tenGodBonus) +
        L"；调候 " + sign(day.climateBonus) + L"；三合三会引动 " + sign(day.formationBonus) + L"。\n\n"
        L"人际 = 60 + 背景 + 五行 + 地支 + 引动\n行动 = 60 + 背景 + 五行×2 + 十神 + 地支÷2 + 调候 + 引动\n节奏 = 60 + 背景 + 五行 + 地支÷2 + 调候\n三项分别限于 5～95，综合取平均四舍五入。\n\n"
        L"北京时间排盘，立春换年、交节换月、零点换日；流年流月按所选日期中午确定。\n"
        L"季节采用旺相休囚死，结合通根、透干与远近折减判断扶抑。调候仅作寒暖燥湿辅助；“当令十神”不等于完整格局判定，不自动判从格或合化。\n"
        L"大运采用阳男阴女顺排、阴男阳女逆排，按节气间隔起运（分钟法），此后每满十年换运。\n\n"
        L"规则 v3 的数值权重由 WinCal 定义，不是其他应用的评分公式。完整公式和来源见项目 docs/fortune-rules.md。仅供民俗娱乐参考；生辰和性别随配置同步。";
    return detail;
}

inline std::wstring_view FortuneLabel(int score)
{
    return score >= 75 ? L"积极把握" : score >= 55 ? L"平稳推进" : L"放缓节奏";
}

inline std::wstring FortuneAdvice(const FortuneDay& score)
{
    return score.overall >= 75 ? L"适合整理目标、主动沟通。把精力放在一件重要的小事上，稳步推进。" :
        score.overall >= 55 ? L"按计划推进，留出休息与沟通的时间。遇到分歧先确认事实，再作决定。" :
        L"适当放慢节奏，先完成熟悉的事情。重要安排结合实际信息判断，不必被分数左右。";
}
}
