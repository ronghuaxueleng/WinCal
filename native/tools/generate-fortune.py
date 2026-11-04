"""Generate exact Jie boundaries and independent four-pillar fixtures.

Development only: pip install lunar_python==1.4.8. Runtime is native/offline.
Times follow lunar-python's China civil time convention, not UTC instants.
"""
from datetime import datetime, timedelta
from pathlib import Path
from importlib.metadata import version
from lunar_python import Solar

assert version("lunar_python") == "1.4.8"
root = Path(__file__).resolve().parents[1]
epoch = datetime(1970, 1, 1)
lines = ["// Generated from lunar_python 1.4.8 (MIT); China civil seconds since 1970-01-01.",
         "#pragma once", "#include <array>", "#include <cstdint>",
         "namespace wincal::fortune_data {",
         "inline constexpr std::array<std::array<std::int64_t, 12>, 203> Jie{{"]
boundaries = []
for year in range(1899, 2102):
    table = Solar.fromYmd(year, 6, 1).getLunar().getJieQiTable()
    terms = sorted(datetime(v.getYear(), v.getMonth(), v.getDay(), v.getHour(), v.getMinute(), v.getSecond())
                   for k, v in table.items() if v.getYear() == year and
                   k in ("小寒", "立春", "惊蛰", "清明", "立夏", "芒种", "小暑", "立秋", "白露", "寒露", "立冬", "大雪"))
    assert len(terms) == 12
    lines.append("    {{" + ",".join(str(int((t-epoch).total_seconds())) + "LL" for t in terms) + "}}, // " + str(year))
    if year in (1900, 2000, 2026, 2100):
        boundaries.extend(t + timedelta(seconds=d) for t in terms for d in (-1, 0, 1))
lines += ["}};", "}"]
(root / "third_party/lunar/fortune_terms.h").write_text("\n".join(lines)+"\n", encoding="utf-8")
fixtures = ["// Generated independently by lunar_python 1.4.8, sect 2 (midnight day boundary).",
            "struct PillarFixture { int year, month, day, hour, minute, second; const wchar_t* pillars; };",
            "constexpr PillarFixture PillarFixtures[]{"]
samples = set(boundaries)
for year in (1900, 1950, 2000, 2026, 2100):
    for hour, minute in ((0, 0), (0, 59), (1, 0), (5, 30), (22, 59), (23, 0), (23, 59)):
        samples.add(datetime(year, 1, 1, hour, minute))
for t in sorted(samples):
    e = Solar.fromYmdHms(t.year, t.month, t.day, t.hour, t.minute, t.second).getLunar().getEightChar()
    e.setSect(2)
    fixtures.append(f'    {{{t.year},{t.month},{t.day},{t.hour},{t.minute},{t.second},L"{e.getYear()} {e.getMonth()} {e.getDay()} {e.getTime()}"}},')
fixtures += ["};"]
(root / "tests/fortune_fixtures.h").write_text("\n".join(fixtures)+"\n", encoding="utf-8")
print(f"Generated 203 Jie years and {len(samples)} four-pillar reference cases.")

# Independent reference for sect-2 start date and forward/reverse luck pillars.
luck = ["// Generated from lunar_python 1.4.8, Yun sect 2.",
        "struct LuckFixture { int year, month, day, hour, minute, second, gender; long long start; bool forward; const wchar_t *first, *secondPillar; };",
        "constexpr LuckFixture LuckFixtures[]{"]
samples.add(datetime(2000, 2, 29, 5, 30))
for t in sorted(samples):
    eight = Solar.fromYmdHms(t.year, t.month, t.day, t.hour, t.minute, t.second).getLunar().getEightChar()
    for gender in (0, 1):
        yun = eight.getYun(gender, 2)
        start = yun.getStartSolar()
        start_time = datetime(start.getYear(), start.getMonth(), start.getDay(), start.getHour(), start.getMinute(), start.getSecond())
        cycles = yun.getDaYun(3)
        luck.append(f'    {{{t.year},{t.month},{t.day},{t.hour},{t.minute},{t.second},{gender},'
                    f'{int((start_time-epoch).total_seconds())}LL,{str(yun.isForward()).lower()},'
                    f'L"{cycles[1].getGanZhi()}",L"{cycles[2].getGanZhi()}"}},')
luck += ["};"]
(root / "tests/fortune_luck_fixtures.h").write_text("\n".join(luck)+"\n", encoding="utf-8")
print(f"Generated {len(samples)*2} luck reference cases.")
