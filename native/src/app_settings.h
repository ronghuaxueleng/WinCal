#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "taskbar_format.h"

namespace wincal
{
struct AppSettings
{
    std::wstring themeMode{L"FollowSystem"};
    int fontSizeOffset{};
    bool autoStartup{};
    std::wstring dataSource{L"SystemCalendar"};
    std::vector<std::wstring> icsUrls;
    std::vector<std::wstring> icsAliases;
    int icsRefreshMinutes{30};
    int upcomingDays{3};
    std::wstring weekStartDay{L"Monday"};
    bool monthPaging{true};
    bool weatherAutoLocation{true};
    bool weatherUseWttr{};
    std::wstring fortuneBirthday;
    std::wstring fortuneBirthTime;
    std::wstring fortuneGender{L"Unknown"};
    bool taskbarStatus{true};
    std::wstring taskbarFormat{DefaultTaskbarFormat};
};

class SettingsStore
{
public:
    [[nodiscard]] static AppSettings Load();
    [[nodiscard]] static bool Save(const AppSettings& settings);
    [[nodiscard]] static std::string Serialize(const AppSettings& settings);
    [[nodiscard]] static AppSettings Deserialize(std::string_view content);
    [[nodiscard]] static std::filesystem::path SettingsPath();
    [[nodiscard]] static std::filesystem::path CacheDirectory();
};
}
