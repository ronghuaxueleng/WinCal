#include "../src/app_settings.cpp"
#include "../src/lunar_calendar.h"
#include "../src/taskbar_clock.h"
#include "../src/startup_setting.h"
#include "../src/shutdown_schedule.h"
#include <winrt/base.h>
#include <iostream>

int main()
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    int failures{};
    const auto check = [&](bool condition, const char* name)
    {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    wincal::LunarCalendar lunar;
    const auto hasObservance = [&](int year, int month, int day, const wchar_t* name)
    {
        const auto names = lunar.ObservancesForDate(year, month, day);
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    check(hasObservance(2026, 11, 7, L"立冬"), "2026 beginning of winter");
    check(hasObservance(2026, 11, 11, L"购物节"), "shopping festival");
    check(hasObservance(2026, 11, 22, L"小雪"), "2026 minor snow");
    check(hasObservance(2026, 11, 23, L"下元节"), "lower yuan lunar festival");
    check(hasObservance(2026, 11, 26, L"感恩节"), "Thanksgiving is fourth Thursday");
    check(!hasObservance(2026, 11, 19, L"感恩节"), "third Thursday is not Thanksgiving");
    check(hasObservance(2026, 2, 4, L"立春"), "2026 beginning of spring");
    check(hasObservance(2026, 4, 5, L"清明"), "2026 Qingming");
    check(hasObservance(2026, 12, 22, L"冬至"), "2026 winter solstice");
    check(hasObservance(2025, 12, 21, L"冬至"), "solar terms vary by year");
    check(hasObservance(2026, 5, 10, L"母亲节"), "second Sunday in May");
    check(hasObservance(2026, 6, 21, L"父亲节"), "third Sunday in June");
    check(hasObservance(2020, 10, 1, L"国庆节") && hasObservance(2020, 10, 1, L"中秋节"),
          "overlapping Gregorian and lunar observances retained");
    check(lunar.ObservancesForDate(2026, 11, 3).empty(), "ordinary dates retain lunar labels");
    check(lunar.ObservancesForDate(2026, 2, 30).empty(), "invalid dates have no observances");
    check(!hasObservance(2023, 3, 7, L"感恩节"), "unrelated dates have no Thanksgiving");
    wincal::AppSettings syncSettings;
    syncSettings.themeMode = L"Dark";
    syncSettings.fontSizeOffset = 4;
    syncSettings.icsUrls = {L"https://example.com/日历.ics?a=1&b=2"};
    syncSettings.icsAliases = {L"工作\"日历"};
    syncSettings.taskbarFormat = L"yyyy-mm-dd\\nMD '测试'";
    syncSettings.monthPaging = false;
    syncSettings.weatherUseWttr = true;
    syncSettings.fortuneBirthday = L"1990-04-24";
    syncSettings.fortuneBirthTime = L"05:30";
    syncSettings.fortuneGender = L"Female";
    const auto synced = wincal::SettingsStore::Deserialize(wincal::SettingsStore::Serialize(syncSettings));
    check(synced.themeMode == syncSettings.themeMode && synced.fontSizeOffset == 4 &&
          synced.icsUrls == syncSettings.icsUrls && synced.icsAliases == syncSettings.icsAliases &&
          synced.taskbarFormat == syncSettings.taskbarFormat && !synced.monthPaging && synced.weatherUseWttr &&
          synced.fortuneBirthday == syncSettings.fortuneBirthday && synced.fortuneBirthTime == syncSettings.fortuneBirthTime &&
          synced.fortuneGender == syncSettings.fortuneGender,
          "cloud settings round trip preserves Unicode, escaping, and options");
    std::tm shutdownNow{};
    shutdownNow.tm_year = 126;
    shutdownNow.tm_mon = 10;
    shutdownNow.tm_mday = 3;
    shutdownNow.tm_hour = 23;
    shutdownNow.tm_min = 45;
    shutdownNow.tm_isdst = -1;
    const auto scheduleNow = std::mktime(&shutdownNow);
    check(wincal::ShutdownDeadline(L" 30 ", false, scheduleNow) == scheduleNow + 1800,
          "shutdown countdown accepts custom minutes across midnight");
    check(wincal::ShutdownDeadline(L"10080", false, scheduleNow) == scheduleNow + 604800,
          "shutdown countdown supports seven days");
    for (const auto input : {L"", L"0", L"-1", L"1.5", L"30junk", L"10081", L"999999999"})
        check(!wincal::ShutdownDeadline(input, false, scheduleNow), "invalid shutdown minutes rejected");
    for (const auto input : {L"24:00", L"23:60", L"8:30", L"12:30junk", L"-1:00"})
        check(!wincal::ShutdownDeadline(input, true, scheduleNow), "invalid shutdown time rejected");
    check(wincal::ShutdownDeadline(L"23:50", true, scheduleNow) == scheduleNow + 300,
          "future shutdown time stays today");
    check(wincal::ShutdownDeadline(L"00:15", true, scheduleNow) == scheduleNow + 1800,
          "past shutdown time rolls over to tomorrow");
    check(wincal::ShutdownDeadline(L"23:45", true, scheduleNow) == scheduleNow + 86400,
          "equal shutdown time schedules tomorrow instead of immediately");
    SYSTEMTIME sample{2026, 11, 2, 3, 9, 5, 7, 0};
    wincal::TaskbarFields fields;
    fields[2] = L"九月廿五"; fields[4] = L"8℃"; fields[5] = L"湿度28%";
    fields[6] = L"西北风3级"; fields[8] = L"多云";
    check(wincal::FormatTaskbar(L"yyyy-mm-dd HH:ff:ss rhu\\nMD ddd rtm we2 rwd", sample,
          [&](size_t i) { return fields[i]; }) == L"2026-11-03 09:05:07 湿度28%\n九月廿五 周二 8℃ 多云 西北风3级",
          "SoftMedia sample distinguishes months, minutes, lunar date and weather fields");
    check(wincal::FormatTaskbar(L"'mystery' 'HH' '' H:f:s", sample, [&](size_t i) { return fields[i]; }) ==
          L"mystery HH ' 9:5:7", "quoted literals are not parsed as tokens");
    check(wincal::NormalizeTaskbarFormat(L"first\r\nsecond") == L"first\\nsecond", "edit newlines normalize for persistence");
    check(lunar.TextForDate(2025, 1, 28) == L"除夕", "29-day final lunar month has New Year's Eve");
    check(lunar.TextForDate(2024, 2, 9) == L"除夕", "30-day final lunar month has New Year's Eve");
    check(lunar.TextForDate(2025, 1, 29) == L"春节", "lunar New Year");
    check(lunar.TextForDate(2025, 10, 6) == L"中秋节", "festival after leap month");
    check(lunar.TextForDate(2025, 7, 25) == L"闰六月", "leap lunar month name");
    check(lunar.FullTextForDate(2025, 7, 25) == L"闰六月初一", "full lunar date retains leap month");
    check(lunar.FullTextForDate(2025, 10, 6) == L"八月十五", "full lunar date remains a date on festivals");
    check(lunar.FullTextForDate(2026, 11, 2) == L"九月廿四", "taskbar date includes lunar month and day");
    const auto aliases = StringArray(R"({"IcsAliases":["\u4f11\ud83c\udf89","Chinese 中文"]})", "IcsAliases");
    check(aliases.size() == 2 && aliases[0] == L"休🎉" && aliases[1] == L"Chinese 中文",
          "settings aliases accept escaped and literal Unicode");
    check(EscapeJson(L"line\b\f\n\r\t") == R"(line\b\f\n\r\t)", "JSON writer escapes control characters");

    wchar_t temporary[MAX_PATH]{}, unique[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(temporary), temporary);
    GetTempFileNameW(temporary, L"WCT", 0, unique);
    const std::filesystem::path path{unique};
    check(wincal::WriteFileAtomically(path, "old settings"), "initial atomic save");
    HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(held != INVALID_HANDLE_VALUE, "lock old settings against replacement");
    check(!wincal::WriteFileAtomically(path, "new settings"), "failed replacement reports failure");
    check(ReadFile(path) == "old settings", "failed save retains old settings");
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    check(wincal::WriteFileAtomically(path, "new settings") && ReadFile(path) == "new settings",
          "successful save replaces settings");
    auto leftover = path;
    leftover += L".tmp";
    check(!std::filesystem::exists(leftover), "temporary save file is cleaned up");
    std::filesystem::remove(path);
    check(!wincal::IsClockAutomationIdentity(L"SystemTray.NormalSystemTrayElement", L"SystemTrayIcon"),
          "ordinary tray icons are not clocks");
    check(wincal::IsClockAutomationIdentity(L"SystemTray.DateTimeIcon", L""), "Windows 11 clock identity");
    check(wincal::IsClockAutomationIdentity(L"TrayClockWClass", L""), "legacy clock identity");
    check(wincal::IsClockAutomationElement(L"SystemTray.OmniButtonLeft", L"SystemTrayIcon", L"时钟 09:05:07 湿度28%"),
          "clock remains clickable when custom format omits date");
    check(wincal::IsClockAutomationIdentity(L"360TrayClockWClass", L""), "installed 360 clock identity");
    SYSTEMTIME today{};
    GetLocalTime(&today);
    wchar_t currentDate[256]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &today, nullptr,
                    currentDate, ARRAYSIZE(currentDate), nullptr);
    check(wincal::IsClockAutomationElement(L"SystemTray.OmniButtonRight", L"SystemTrayIcon",
          L"Clock 12:00\n\u200e" + std::wstring{currentDate}), "Windows 11 clock with directional date marks");
    wchar_t isoDate[32]{};
    swprintf_s(isoDate, L"%04u-%02u-%02u", today.wYear, today.wMonth, today.wDay);
    check(wincal::IsClockAutomationElement(L"SystemTray.OmniButton", L"SystemTrayIcon",
          L"Clock 10:39:02 " + std::wstring{isoDate} + L" Humidity 48%"),
          "Windows 11 clock on builds using the shared OmniButton class");
    check(!wincal::IsClockAutomationElement(L"SystemTray.OmniButton", L"SystemTrayIcon", L"Notifications"),
          "shared OmniButton without a date is not treated as the clock");
    check(!wincal::IsClockAutomationElement(L"SystemTray.OmniButtonRight", L"SystemTrayIcon", L"Notifications") &&
          !wincal::IsClockAutomationElement(L"SystemTray.OmniButtonLeft", L"SystemTrayIcon", L"Volume"),
          "notification and audio buttons are not mistaken for the clock");
    wchar_t extensionDate[64]{};
    GetDateFormatEx(L"zh-CN", 0, &today, L"yyyy'年'M'月'd'日'",
                    extensionDate, ARRAYSIZE(extensionDate), nullptr);
    check(wincal::IsClockAutomationElement(L"SystemTray.OmniButtonLeft", L"SystemTrayIcon",
          L"时钟 晴 8°C 星期一 23:53:07（子时） 北京 " + std::wstring{extensionDate} + L" 九月廿四"),
          "custom taskbar clock date works independently of system date formats");

    // Isolated key; never touch the user's real Run key.
    const std::wstring testKey = L"SOFTWARE\\WinCal-Test-" + std::to_wstring(GetCurrentProcessId()) +
                                 L"-" + std::to_wstring(GetTickCount64());
    HKEY registry{};
    check(RegCreateKeyExW(HKEY_CURRENT_USER, testKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                         nullptr, &registry, nullptr) == ERROR_SUCCESS, "create isolated startup test key");
    if (registry)
    {
        wincal::StartupSettingSnapshot absent{testKey.c_str()};
        check(absent.Capture(), "snapshot disabled startup");
        constexpr wchar_t legacy[] = L"\"%LOCALAPPDATA%\\legacy\\WinCal.exe\" --legacy";
        check(RegSetValueExW(registry, L"miniCal", 0, REG_EXPAND_SZ,
                            reinterpret_cast<const BYTE*>(legacy), sizeof(legacy)) == ERROR_SUCCESS,
              "set isolated legacy startup command");
        wincal::StartupSettingSnapshot enabled{testKey.c_str()};
        check(enabled.Capture(), "snapshot exact legacy startup command");
        check(RegDeleteValueW(registry, L"miniCal") == ERROR_SUCCESS && enabled.Restore(),
              "rollback restores removed startup command");
        wchar_t restored[256]{};
        DWORD type{}, bytes = sizeof(restored);
        check(RegGetValueW(registry, nullptr, L"miniCal", RRF_RT_ANY | RRF_NOEXPAND,
                           &type, restored, &bytes) == ERROR_SUCCESS &&
              type == REG_EXPAND_SZ && std::wstring_view{restored} == legacy,
              "rollback preserves the exact startup path, arguments and registry type");
        check(absent.Restore(), "rollback restores disabled startup");
        check(RegGetValueW(registry, nullptr, L"miniCal", RRF_RT_ANY, nullptr, nullptr, &bytes)
              == ERROR_FILE_NOT_FOUND, "disabled startup has no value after rollback");
        RegCloseKey(registry);
        check(RegDeleteKeyW(HKEY_CURRENT_USER, testKey.c_str()) == ERROR_SUCCESS,
              "remove isolated startup test key");
    }
    std::cout << "Core regression failures: " << failures << '\n';
    return failures ? 1 : 0;
}
