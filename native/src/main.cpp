#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <d2d1.h>
#ifdef WINCAL_SETTINGS_PREVIEW
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#endif
#include <dwrite.h>
#include <imm.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <compare>
#include <condition_variable>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "calendar_data.h"
#include "refresh_policy.h"
#include "app_settings.h"
#include "lunar_calendar.h"
#include "almanac.h"
#include "solar_term_readings.h"
#include "fortune.h"
#include "startup_setting.h"
#include "taskbar_clock.h"
#include "clock_extension.h"
#include "weather.h"
#include "shutdown_schedule.h"
#include "config_sync.h"
#include "../resources/resource.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kWindowClass[] = L"WinCalNativePopup";
constexpr wchar_t kSettingsWindowClass[] = L"WinCalNativeSettings";
constexpr wchar_t kWeatherWindowClass[] = L"WinCalNativeWeather";
constexpr wchar_t kDetailWindowClass[] = L"WinCalNativeEventDetails";
constexpr wchar_t kContextMenuWindowClass[] = L"WinCalNativeContextMenu";
constexpr wchar_t kWindowTitle[] = L"WinCal";

constexpr UINT kShowPopupMessage = WM_APP + 2;
constexpr UINT kScheduleOutsideHideMessage = WM_APP + 3;
constexpr UINT kCalendarDataUpdatedMessage = WM_APP + 4;
constexpr UINT kSettingsChangedMessage = WM_APP + 5;
constexpr UINT kShowSettingsMessage = WM_APP + 6;
constexpr UINT kTaskbarClockClickMessage = WM_APP + 7;
constexpr UINT kWeatherUpdatedMessage = WM_APP + 8;
constexpr UINT kTaskbarClockContextMenuMessage = WM_APP + 9;
constexpr UINT kDismissContextMenuMessage = WM_APP + 10;
constexpr UINT kClockBoundsUpdatedMessage = WM_APP + 11;
constexpr UINT kSyncCompletedMessage = WM_APP + 12;
constexpr UINT kGlobalMouseMessage = WM_APP + 13;
constexpr UINT kConfirmedClockClickMessage = WM_APP + 14;
constexpr int kSettingPageSync = 2050;
constexpr int kSyncAccount = 2051, kSyncPassword = 2052, kSyncTest = 2053, kSyncUpload = 2054, kSyncDownload = 2055;
constexpr UINT kAnimationTimer = 1;
constexpr UINT kOutsideClickTimer = 2;
constexpr UINT kCalendarRefreshTimer = 3;
constexpr UINT kClockTimer = 4;
constexpr UINT kShutdownTimer = 6;
constexpr int kShutdownInput = 2100;
constexpr int kFortuneInput = 2101;
constexpr int kShutdownTimeInput = 2102;
constexpr int kFortuneTimeInput = 2103;
constexpr int kFortuneGenderInput = 2104;
constexpr DWORD kEventObjectUncloak = 0x8018;
constexpr UINT kMenuSettings = 1001;
constexpr UINT kMenuExit = 1002;
constexpr wchar_t kCalendarFontFamily[] = L"Microsoft YaHei UI";
constexpr float kCalendarTitleFontSize = 20.0f;
constexpr float kCalendarBodyFontSize = 16.0f;
constexpr float kCalendarCaptionFontSize = 12.0f;

float CalendarFontScale(int offset)
{
    return 1.0f + static_cast<float>(std::clamp(offset, -2, 10)) * 0.06f;
}

constexpr int kLogicalWidth = 430;
constexpr int kClockHeight = 86;
constexpr int kLogicalHeight = 526 + kClockHeight;
constexpr int kSettingsLogicalWidth = 680;
constexpr int kSettingsLogicalHeight = 592;
constexpr int kDetailLogicalWidth = 360;
constexpr int kDetailLogicalHeight = 286;
constexpr int kHeaderTop = 22;
constexpr int kWeekTop = 96;
constexpr int kGridTop = 124;
constexpr int kCellWidth = 58;
constexpr int kCellHeight = 59;
constexpr int kGridLeft = 12;
constexpr wchar_t kAgendaWindowClass[] = L"WinCalNativeAgenda";
constexpr int kAgendaWidth = 360;
constexpr int kSettingTheme = 2001;
constexpr int kSettingFontOffset = 2002;
constexpr int kSettingFontMinus = 2013;
constexpr int kSettingFontPlus = 2014;
constexpr int kSettingThemeLight = 2015;
constexpr int kSettingThemeDark = 2016;
constexpr int kSettingPageAppearance = 2020;
constexpr int kSettingPageSource = 2021;
constexpr int kSettingPageGeneral = 2022;
constexpr int kSettingPageWeather = 2023;
constexpr int kSettingWeatherLocation = 2024;
constexpr int kSettingWeatherProvider = 2028;
constexpr int kSettingTaskbarStatus = 2025;
constexpr int kSettingTaskbarFormatPage = 2026;
constexpr int kSettingTaskbarFormat = 2027;
constexpr int kSettingAutoStartup = 2003;
constexpr int kSettingDataSource = 2004;
constexpr int kSettingIcsUrls = 2005;
constexpr int kSettingRefresh = 2006;
constexpr int kSettingWeekStart = 2007;
constexpr int kSettingMonthPaging = 2008;
constexpr int kSettingSave = 2010;
constexpr int kSettingDefaults = 2011;
constexpr int kSettingCancel = 2012;

struct Date
{
    int year{};
    int month{};
    int day{};

    auto operator<=>(const Date&) const = default;
};

enum class HideReason : ULONG_PTR
{
    None,
    Tray,
    OutsideClick,
    ContextMenu,
    EscapeKey,
};

enum class TaskbarEdge
{
    Unknown,
    Left,
    Top,
    Right,
    Bottom,
};

enum class DatePickerMode { Closed, Months, Years };

struct Theme
{
    D2D1_COLOR_F background;
    D2D1_COLOR_F card;
    D2D1_COLOR_F primary;
    D2D1_COLOR_F secondary;
    D2D1_COLOR_F muted;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F hover;
    D2D1_COLOR_F separator;
    D2D1_COLOR_F eventDot;
    D2D1_COLOR_F restBadgeBackground;
    D2D1_COLOR_F restBadgeForeground;
    D2D1_COLOR_F workBadgeBackground;
    D2D1_COLOR_F workBadgeForeground;
    D2D1_COLOR_F weekend;
};

struct SettingsPalette
{
    COLORREF background;
    COLORREF card;
    COLORREF input;
    COLORREF primary;
    COLORREF secondary;
    COLORREF muted;
    COLORREF accent;
    COLORREF border;
};

long long DaysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468;
}

Date CivilFromDays(long long days)
{
    days += 719468;
    const long long era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
    return {year, static_cast<int>(month), static_cast<int>(day)};
}

float EaseOutCubic(float t)
{
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

Date Today()
{
    SYSTEMTIME value{};
    GetLocalTime(&value);
    return {value.wYear, value.wMonth, value.wDay};
}

std::wstring EventTimeText(const wincal::CalendarEvent& event, const Date& date)
{
    if (event.allDay)
        return L"全天";
    if (event.startYear != date.year || event.startMonth != date.month || event.startDay != date.day)
    {
        return std::to_wstring(event.startMonth) + L"/" + std::to_wstring(event.startDay) + L" " +
               std::to_wstring(event.startHour) + L":" +
               (event.startMinute < 10 ? L"0" : L"") + std::to_wstring(event.startMinute);
    }
    wchar_t time[8]{};
    swprintf_s(time, L"%02d:%02d", event.startHour, event.startMinute);
    return time;
}

std::wstring ReadControlText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0)
        GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

std::vector<std::wstring> LinesFromText(std::wstring_view value)
{
    std::vector<std::wstring> lines;
    size_t start{};
    while (start < value.size())
    {
        const size_t end = value.find_first_of(L"\r\n", start);
        std::wstring line(value.substr(start, end == std::wstring_view::npos ? end : end - start));
        const auto first = line.find_first_not_of(L" \t");
        if (first != std::wstring::npos)
        {
            line.erase(0, first);
            const auto last = line.find_last_not_of(L" \t");
            line.erase(last + 1);
            if (line.rfind(L"http://", 0) == 0 || line.rfind(L"https://", 0) == 0)
                lines.push_back(std::move(line));
        }
        if (end == std::wstring_view::npos) break;
        start = end + 1;
        if (start < value.size() && value[end] == L'\r' && value[start] == L'\n') ++start;
    }
    return lines;
}

bool IsDarkMode()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    const auto status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return status == ERROR_SUCCESS && value == 0;
}

Theme GetTheme(bool dark)
{
    if (dark)
    {
        return {
            D2D1::ColorF(0x17191D), D2D1::ColorF(0x202329), D2D1::ColorF(0xF4F6F8),
            D2D1::ColorF(0xABB2BD), D2D1::ColorF(0x6F7782), D2D1::ColorF(0x65A5FF),
            D2D1::ColorF(0x2A3039), D2D1::ColorF(0x31363E), D2D1::ColorF(0x4CC2FF),
            D2D1::ColorF(0x294735), D2D1::ColorF(0xA9D6B3),
            D2D1::ColorF(0x55491F), D2D1::ColorF(0xF2D46B), D2D1::ColorF(0xFF787D)};
    }

    return {
        D2D1::ColorF(0xF8F9FB), D2D1::ColorF(0xFFFFFF), D2D1::ColorF(0x20242A),
        D2D1::ColorF(0x6C737F), D2D1::ColorF(0xA1A7B0), D2D1::ColorF(0x3478F6),
        D2D1::ColorF(0xEDF3FF), D2D1::ColorF(0xE7E9ED), D2D1::ColorF(0x3478F6),
        D2D1::ColorF(0xDDEBE0), D2D1::ColorF(0x2F6840),
        D2D1::ColorF(0xFFF0B8), D2D1::ColorF(0x806000), D2D1::ColorF(0xE5484D)};
}

class App
{
public:
#ifdef WINCAL_SETTINGS_PREVIEW
    bool RenderDetailPreview(HINSTANCE instance, int dpi, bool dark, int offset, const wchar_t* path)
    {
        instance_ = instance;
        dark_ = dark;
        settings_.fontSizeOffset = offset;
        if (!RegisterDetailWindowClass()) return false;
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kDetailWindowClass, L"Detail preview",
            WS_POPUP, -30000, -30000, 360, 286, nullptr, nullptr, instance_, this);
        if (!window) return false;
        // Render at the requested DPI without moving a real window between monitors.
        DeleteObject(detailTitleFont_); DeleteObject(detailBodyFont_); DeleteObject(detailCaptionFont_);
        detailDpi_ = dpi;
        const auto font = [&](float size, int weight)
        {
            return CreateFontW(-static_cast<int>(std::lround(size * CalendarFontScale(offset) * dpi / 96.0f)),
                0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, kCalendarFontFamily);
        };
        detailTitleFont_ = font(kCalendarTitleFontSize, FW_SEMIBOLD);
        detailBodyFont_ = font(kCalendarBodyFontSize, FW_NORMAL);
        detailCaptionFont_ = font(kCalendarCaptionFontSize, FW_NORMAL);
        detailEvent_.title = L"项目评审：下一阶段的设计方案与交付计划";
        detailEvent_.startYear = detailEvent_.endYear = 2026;
        detailEvent_.startMonth = detailEvent_.endMonth = 9;
        detailEvent_.startDay = detailEvent_.endDay = 22;
        detailEvent_.startHour = 14; detailEvent_.startMinute = 30;
        detailEvent_.endHour = 16;
        bool ok = DetailWhen().second == L"14:30 – 16:00";
        detailEvent_.allDay = true;
        detailEvent_.endDay = 24;
        ok &= DetailWhen().first == L"2026年9月22日 – 2026年9月23日";
        detailEvent_.allDay = false;
        detailEvent_.endDay = 23;
        ok &= DetailWhen().second == L"2026年9月23日  16:00 结束";
        detailEvent_.endDay = 22;
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = DetailPx(kDetailLogicalWidth);
        info.bmiHeader.biHeight = DetailContentHeight(dc);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); DestroyWindow(window); return false; }
        const auto previous = SelectObject(dc, bitmap);
        DrawDetailCard(dc, {0, 0, info.bmiHeader.biWidth, info.bmiHeader.biHeight});
        GdiFlush();
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        const DWORD bytes = info.bmiHeader.biWidth * info.bmiHeader.biHeight * 4;
        header.bfSize = header.bfOffBits + bytes;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written{};
        ok &= file != INVALID_HANDLE_VALUE &&
            WriteFile(file, &header, sizeof(header), &written, nullptr) &&
            WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr) &&
            WriteFile(file, pixels, bytes, &written, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DestroyWindow(window);
        fwprintf_s(stderr, L"detail preview: checks=%s\n", ok ? L"passed" : L"failed");
        return ok;
    }

    bool RenderAgendaPreview(HINSTANCE instance, int dpi, bool dark, int offset, const wchar_t* path, bool emptyPreview = false)
    {
        instance_ = instance;
        agendaPreview_ = true;
        agendaDpi_ = dpi;
        dark_ = dark;
        settings_.fontSizeOffset = offset;
        selected_ = {2026, 9, 22};
        displayYear_ = 2026;
        displayMonth_ = 9;
        if (!RegisterWindowClass() || !RegisterAgendaWindowClass()) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Agenda test owner",
            WS_POPUP | WS_VISIBLE, -30000, -30000, kLogicalWidth, kLogicalHeight, nullptr, nullptr, instance_, this);
        if (!window_) return false;
        PresentAgenda();
        bool ok = !IsWindowVisible(agendaWindow_);
        if (emptyPreview)
        {
            selected_ = {2026, 11, 7};
            displayMonth_ = 11;
        }
        dateDetailsOpen_ = true;
        PresentAgenda();
        ok &= IsWindowVisible(agendaWindow_) != FALSE;
        ok &= AgendaHeight() == kLogicalHeight;
        UpdateWindow(agendaWindow_);
        const int month = displayMonth_;
        SendMessageW(agendaWindow_, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        ok &= agendaScroll_ == std::min(48, agendaScrollMax_) && displayMonth_ == month;
        agendaScroll_ = 0;
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = AgendaPx(kAgendaWidth);
        info.bmiHeader.biHeight = AgendaPx(AgendaHeight());
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); DestroyWindow(window_); return false; }
        const auto previous = SelectObject(dc, bitmap);
        DrawAgenda(dc, {0, 0, info.bmiHeader.biWidth, info.bmiHeader.biHeight});
        GdiFlush();
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        const DWORD bytes = info.bmiHeader.biWidth * info.bmiHeader.biHeight * 4;
        header.bfSize = header.bfOffBits + bytes;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written{};
        ok &= file != INVALID_HANDLE_VALUE &&
            WriteFile(file, &header, sizeof(header), &written, nullptr) &&
            WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr) &&
            WriteFile(file, pixels, bytes, &written, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        SendMessageW(agendaWindow_, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA * 100)), 0);
        ok &= agendaScroll_ == agendaScrollMax_;
        PresentAgenda();
        ok &= IsWindowVisible(agendaWindow_) != FALSE;
        ok &= AgendaHeight() == kLogicalHeight;
        ok &= RegisterDetailWindowClass();
        for (int target = 0; target < 3; ++target)
        {
            // Keep the test windows offscreen while exercising all focus routes.
            CreateWindowExW(WS_EX_TOOLWINDOW, kDetailWindowClass, L"Details test",
                WS_POPUP | WS_VISIBLE, -30000, -30000, 360, 286,
                window_, nullptr, instance_, this);
            ok &= IsWindowVisible(detailWindow_) != FALSE;
            const HWND recipient = target == 0 ? window_ : target == 1 ? agendaWindow_ : detailWindow_;
            SendMessageW(recipient, WM_KEYDOWN, VK_ESCAPE, 1);
            ok &= !IsWindow(detailWindow_) && IsWindowVisible(window_) && IsWindowVisible(agendaWindow_);
            SendMessageW(window_, WM_KEYDOWN, VK_ESCAPE, (static_cast<LPARAM>(1) << 30) | 1);
            ok &= IsWindowVisible(window_) && IsWindowVisible(agendaWindow_);
        }
        SendMessageW(window_, WM_KEYDOWN, VK_ESCAPE, 1);
        ok &= !IsWindowVisible(agendaWindow_) && !IsWindowVisible(window_);
        DestroyWindow(window_);
        window_ = nullptr;
        fwprintf_s(stderr, L"agenda preview: checks=%s\n", ok ? L"passed" : L"failed");
        return ok;
    }

    // Verify selection controls only; never start a shutdown timer.
    bool CheckShutdownTools(HINSTANCE instance)
    {
        instance_ = instance;
        if (!RegisterWindowClass() || !CreateMainWindow()) return false;
        ToggleTools();
        OpenShutdownTool();
        bool ok = toolsOpen_ && IsWindow(toolInput_) && IsWindow(toolTimeInput_);
        ok &= SendMessageW(toolInput_, CB_GETCOUNT, 0, 0) == 15;
        SendMessageW(toolInput_, CB_SETCURSEL, 4, 0);
        ok &= ShutdownInputValue() == L"30";
        shutdownAtTime_ = true;
        ShowShutdownInputs();
        SYSTEMTIME time{};
        GetLocalTime(&time);
        time.wHour = 8; time.wMinute = 45;
        DateTime_SetSystemtime(toolTimeInput_, GDT_VALID, &time);
        ok &= ShutdownInputValue() == L"08:45";
        shutdownAtTime_ = false;
        ShowShutdownInputs();
        ok &= ShutdownInputValue() == L"30";
        OpenFortuneTool();
        SYSTEMTIME birth{};
        birth.wYear = 1990; birth.wMonth = 4; birth.wDay = 24;
        DateTime_SetSystemtime(fortuneInput_, GDT_VALID, &birth);
        SYSTEMTIME birthTime{};
        GetLocalTime(&birthTime);
        birthTime.wHour = 5; birthTime.wMinute = 30;
        DateTime_SetSystemtime(fortuneTimeInput_, GDT_VALID, &birthTime);
        SYSTEMTIME selectedTime{};
        ok &= DateTime_GetSystemtime(fortuneTimeInput_, &selectedTime) == GDT_VALID;
        ok &= selectedTime.wHour == 5 && selectedTime.wMinute == 30;
        SendMessageW(fortuneGenderInput_, CB_SETCURSEL, 2, 0);
        GenerateFortune();
        ok &= fortuneChart_ && fortuneChart_->gender == wincal::FortuneGender::Female;
        OpenShutdownTool();
        ok &= !IsWindowVisible(fortuneTimeInput_);
        ok &= !IsWindowVisible(fortuneGenderInput_);
        OpenFortuneTool();
        ok &= DateTime_GetSystemtime(fortuneTimeInput_, &selectedTime) == GDT_VALID;
        ok &= selectedTime.wHour == 5 && selectedTime.wMinute == 30;
        ok &= SendMessageW(fortuneGenderInput_, CB_GETCURSEL, 0, 0) == 2;
        ok &= fortuneBirth_ == wincal::ParseFortuneBirthday(L"1990-04-24");
        SYSTEMTIME range[2]{};
        ok &= DateTime_GetRange(fortuneInput_, range) == (GDTR_MIN | GDTR_MAX);
        ok &= range[0].wYear == 1900;
        ok &= !shutdownDue_ && !shutdownProcess_;
        DestroyWindow(window_);
        window_ = nullptr;
        fwprintf_s(stderr, L"tool selection controls: %s (no shutdown timer started)\n", ok ? L"passed" : L"failed");
        return ok;
    }

    // Test-only: real controls, no visible window, user settings or network.
    bool RenderSettingsPreview(HINSTANCE instance, int dpi, int page, bool dark, int offset, const wchar_t* path)
    {
        instance_ = instance;
        settingsPreviewDpi_ = dpi;
        settingsDraft_.themeMode = dark ? L"Dark" : L"Light";
        settingsDraft_.fontSizeOffset = offset;
        settingsDraft_.dataSource = L"IcsUrl";
        settingsDraft_.icsUrls = {L"https://example.com/calendar.ics"};
        if (!RegisterSettingsWindowClass()) return false;
        HWND window = CreateWindowExW(0, kSettingsWindowClass, L"WinCal Settings Preview",
            WS_POPUP, 0, 0, MulDiv(kSettingsLogicalWidth, dpi, 96), MulDiv(kSettingsLogicalHeight, dpi, 96), nullptr, nullptr, instance, this);
        if (!window) return false;
        const auto original = settingsDraft_;
        const auto originalRuntime = settings_;
        settingsBeforeEdit_ = settings_;
        settingsLivePreview_ = true;
        const auto click = [&](int id) { HandleSettingsMessage(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); };
        bool controlsOk = true;
        SetDlgItemInt(window, kSettingFontOffset, static_cast<UINT>(-2), TRUE);
        click(kSettingFontMinus);
        controlsOk &= static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, nullptr, TRUE)) == -2;
        click(kSettingFontPlus);
        controlsOk &= static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, nullptr, TRUE)) == -1;
        SetDlgItemInt(window, kSettingFontOffset, 10, TRUE);
        click(kSettingFontPlus);
        controlsOk &= GetDlgItemInt(window, kSettingFontOffset, nullptr, TRUE) == 10;
        controlsOk &= settings_.fontSizeOffset == 10;
        click(kSettingThemeDark);
        controlsOk &= settingsWindowDark_;
        controlsOk &= settings_.themeMode == L"Dark";
        click(kSettingThemeLight);
        controlsOk &= !settingsWindowDark_;
        click(kSettingPageSource);
        controlsOk &= settingsPage_ == 1;
        SetSettingSelection(window, kSettingDataSource, 0);
        UpdateSettingsSourceControls(window);
        controlsOk &= !IsWindowEnabled(GetDlgItem(window, kSettingIcsUrls));
        SetSettingSelection(window, kSettingDataSource, 2);
        UpdateSettingsSourceControls(window);
        controlsOk &= IsWindowEnabled(GetDlgItem(window, kSettingIcsUrls)) != 0;
        click(kSettingPageGeneral);
        POINT navigationPoint{SettingsPx(80), SettingsPx(160)};
        const LPARAM navigationPosition = MAKELPARAM(navigationPoint.x, navigationPoint.y);
        HandleSettingsMessage(window, WM_MOUSEWHEEL,
            MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), navigationPosition);
        controlsOk &= settingsPage_ == 3;
        HandleSettingsMessage(window, WM_MOUSEWHEEL,
            MAKEWPARAM(0, static_cast<WORD>(WHEEL_DELTA)), navigationPosition);
        controlsOk &= settingsPage_ == 2;
        POINT contentPoint{SettingsPx(300), SettingsPx(240)};
        const LPARAM contentPosition = MAKELPARAM(contentPoint.x, contentPoint.y);
        HandleSettingsMessage(window, WM_MOUSEWHEEL,
            MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), contentPosition);
        controlsOk &= settingsPage_ == 3;
        HandleSettingsMessage(window, WM_MOUSEWHEEL,
            MAKEWPARAM(0, static_cast<WORD>(WHEEL_DELTA)), contentPosition);
        controlsOk &= settingsPage_ == 2;
        const int before = SettingSelection(window, kSettingAutoStartup);
        click(kSettingAutoStartup);
        controlsOk &= SettingSelection(window, kSettingAutoStartup) != before;
        click(kSettingDefaults);
        controlsOk &= GetDlgItemInt(window, kSettingFontOffset, nullptr, TRUE) == 0;
        SetSettingSelection(window, kSettingWeekStart, 1);
        ApplyLiveSettingsPreview(window);
        controlsOk &= settings_.weekStartDay == L"Monday";
        FinishLiveSettingsPreview();
        controlsOk &= settings_.themeMode == originalRuntime.themeMode &&
                      settings_.fontSizeOffset == originalRuntime.fontSizeOffset &&
                      settings_.weekStartDay == originalRuntime.weekStartDay;
        settingsBeforeEdit_ = settings_;
        settingsLivePreview_ = true;
        settingsSaveAccepted_ = true;
        settingsDraft_ = original;
        FinishLiveSettingsPreview();
        controlsOk &= settings_.themeMode == original.themeMode &&
                      settings_.fontSizeOffset == original.fontSizeOffset &&
                      settings_.weekStartDay == original.weekStartDay;
        settingsSaveAccepted_ = false;
        settingsDraft_ = original;
        PopulateSettingsControls(window);
        UpdateSettingsFontControls(window);
        ApplySettingsWindowAppearance(window);
        SelectSettingsPage(window, page);
        for (const auto& control : settingsControls_)
            controlsOk &= control.x >= 0 && control.y >= 0 &&
                          control.x + control.width <= kSettingsLogicalWidth && control.y + control.height <= kSettingsLogicalHeight;
        // Even at a small viewport, the bottom-right save action must be reachable.
        SetWindowPos(window, nullptr, 0, 0, SettingsPx(360), SettingsPx(400), SWP_NOZORDER | SWP_NOMOVE);
        settingsScrollX_ = SettingsPx(kSettingsLogicalWidth);
        settingsScrollY_ = SettingsPx(kSettingsLogicalHeight);
        LayoutSettingsViewport(window);
        RECT smallClient{};
        GetClientRect(window, &smallClient);
        controlsOk &= settingsScrollX_ > 0 && settingsScrollY_ > 0;
        controlsOk &= SettingsPx(kSettingsLogicalWidth - 28) - settingsScrollX_ <= smallClient.right;
        controlsOk &= SettingsPx(568) - settingsScrollY_ <= smallClient.bottom;
        SetWindowPos(window, nullptr, 0, 0, SettingsPx(kSettingsLogicalWidth), SettingsPx(kSettingsLogicalHeight), SWP_NOZORDER | SWP_NOMOVE);
        settingsScrollX_ = settingsScrollY_ = 0;
        LayoutSettingsViewport(window);
        RECT restoredClient{};
        GetClientRect(window, &restoredClient);
        controlsOk &= restoredClient.right == SettingsPx(kSettingsLogicalWidth) && restoredClient.bottom == SettingsPx(kSettingsLogicalHeight);
        if (!controlsOk) { DestroyWindow(window); return false; }
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = SettingsPx(kSettingsLogicalWidth);
        info.bmiHeader.biHeight = SettingsPx(kSettingsLogicalHeight);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); DestroyWindow(window); return false; }
        const auto previous = SelectObject(dc, bitmap);
        RenderSettingsWindow(window, dc);
        for (const auto& control : settingsControls_)
        {
            if (control.page >= 0 && control.page != page) continue;
            const int saved = SaveDC(dc);
            SetViewportOrgEx(dc, SettingsPx(control.x), SettingsPx(control.y), nullptr);
            IntersectClipRect(dc, 0, 0, SettingsPx(control.width), SettingsPx(control.height));
            SendMessageW(control.window, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                         PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
            RestoreDC(dc, saved);
        }
        GdiFlush();
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        const DWORD bytes = info.bmiHeader.biWidth * info.bmiHeader.biHeight * 4;
        header.bfSize = header.bfOffBits + bytes;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written{};
        const bool ok = file != INVALID_HANDLE_VALUE &&
            WriteFile(file, &header, sizeof(header), &written, nullptr) &&
            WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &written, nullptr) &&
            WriteFile(file, pixels, bytes, &written, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DestroyWindow(window);
        return ok;
    }

    bool CheckContinuousScrolling(HINSTANCE instance)
    {
        instance_ = instance;
        if (!RegisterWindowClass()) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Scroll test",
            WS_POPUP, -30000, -30000, 430, 466, nullptr, nullptr, instance_, this);
        if (!window_) return false;
        settings_.monthPaging = false;
        displayYear_ = 2026;
        displayMonth_ = 12;
        dpi_ = 96;
        bool ok = true;
        const auto settle = [&]
        {
            for (int i = 0; i < 100 && continuousAnimating_; ++i) AdvanceContinuousScroll();
            return !continuousAnimating_;
        };
        for (bool monday : {false, true})
        {
            weekStartsMonday_ = monday;
            displayYear_ = 2026;
            displayMonth_ = 12;
            ResetContinuousScroll();
            const auto initialAnchor = continuousAnchor_;
            ScrollContinuously(-60);
            ok &= settle() && std::abs(continuousPosition_ - 0.375) < 0.0001;
            const int x = kGridLeft + kCellWidth / 2;
            const int y = kClockHeight + kGridTop + kCellHeight - 1;
            const int hit = HitTestCell(x, y);
            ok &= hit == 7 && ContinuousDateForCell(hit) == CivilFromDays(initialAnchor + 7);
            dpi_ = 144;
            ok &= HitTestCell(MulDiv(x, 144, 96), MulDiv(y, 144, 96)) == 7;
            dpi_ = 96;
            ok &= HitTestCell(x, kClockHeight + kGridTop - 1) == -1;
            ok &= HitTestCell(x, kClockHeight + kGridTop + 6 * kCellHeight) == -1;
            ScrollContinuously(-60);
            ScrollContinuously(-120);
            ok &= settle() && continuousAnchor_ == initialAnchor + 7 &&
                std::abs(continuousPosition_ - 0.5) < 0.0001;
            ScrollContinuously(240);
            ok &= settle() && continuousAnchor_ == initialAnchor && continuousPosition_ == 0.0;
            ScrollContinuously(60);
            ok &= settle() && continuousAnchor_ == initialAnchor - 7 &&
                std::abs(continuousPosition_ - 0.625) < 0.0001;
            ScrollContinuously(-60);
            ok &= settle() && continuousAnchor_ == initialAnchor && continuousPosition_ == 0.0;
            ScrollContinuously(-960);
            ok &= settle() && displayYear_ == 2027 && displayMonth_ == 1;
            const auto position = continuousPosition_;
            const auto anchor = continuousAnchor_;
            HandleMessage(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            ok &= continuousPosition_ == position && continuousAnchor_ == anchor;
            ResetContinuousScroll();
            ok &= continuousPosition_ == 0.0 && continuousTarget_ == 0.0;
        }
        DestroyWindow(window_);
        window_ = nullptr;
        fwprintf_s(stderr, L"continuous scroll: checks=%s\n", ok ? L"passed" : L"failed");
        return ok;
    }

    bool CheckDatePicker(HINSTANCE instance)
    {
        instance_ = instance;
        if (!RegisterWindowClass()) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Picker test",
            WS_POPUP, -30000, -30000, 430, 466, nullptr, nullptr, instance_, this);
        if (!window_) return false;
        bool ok = true;
        for (UINT dpi : {96U, 144U, 192U})
        {
            dpi_ = dpi;
            ok &= HitTestNavigation(MulDiv(310, dpi, 96), MulDiv(30, dpi, 96)) == -6;
            ok &= HitTestCell(MulDiv(50, dpi, 96), MulDiv(35, dpi, 96)) == -1;
            weatherPanelOpen_ = true;
            ok &= HitTestWeather(MulDiv(398, dpi, 96), MulDiv(30, dpi, 96)) == 1;
            ok &= HitTestWeather(MulDiv(380, dpi, 96), MulDiv(74, dpi, 96)) == 2;
            ok &= HitTestCell(MulDiv(50, dpi, 96), MulDiv(300, dpi, 96)) >= 0;
            HandleMessage(WM_KEYDOWN, VK_ESCAPE, 1);
            ok &= !weatherPanelOpen_;
            for (bool paging : {true, false})
            {
                settings_.monthPaging = paging;
                displayYear_ = 2026;
                displayMonth_ = 11;
                pickerMode_ = DatePickerMode::Closed;
                const auto click = [&](int x, int y)
                {
                    HandleMessage(WM_LBUTTONUP, 0, MAKELPARAM(MulDiv(x, dpi, 96), MulDiv(y + kClockHeight, dpi, 96)));
                };
                click(100, 40);
                ok &= pickerMode_ == DatePickerMode::Months && pickerYear_ == 2026;
                click(349, 40);
                ok &= pickerYear_ == 2027 && displayYear_ == 2026;
                click(100, 40);
                ok &= pickerMode_ == DatePickerMode::Years;
                const int firstYear = pickerFirstYear_;
                click(349, 40);
                ok &= pickerFirstYear_ == firstYear + 12;
                click(342, 238); // Row two, column four: index 7.
                const int chosenYear = firstYear + 12 + 7;
                ok &= pickerMode_ == DatePickerMode::Months && pickerYear_ == chosenYear;
                click(160, 146); // February.
                ok &= pickerMode_ == DatePickerMode::Closed && displayYear_ == chosenYear && displayMonth_ == 2;
                ok &= selected_ == Date{chosenYear, 2, 1};
                if (!paging) ok &= continuousPosition_ == 0.0 && continuousTarget_ == 0.0;
                click(100, 40);
                HandleMessage(WM_KEYDOWN, VK_ESCAPE, 1);
                ok &= pickerMode_ == DatePickerMode::Closed && displayYear_ == chosenYear;
                click(100, 40);
                click(393, 42);
                const auto today = Today();
                ok &= pickerMode_ == DatePickerMode::Closed && displayYear_ == today.year &&
                      displayMonth_ == today.month && selected_ == today;
                click(100, 40);
                HandleMessage(WM_KEYDOWN, VK_RIGHT, 0);
                const int focusedMonth = pickerFocus_ + 1;
                HandleMessage(WM_KEYDOWN, VK_RETURN, 0);
                ok &= pickerMode_ == DatePickerMode::Closed && displayMonth_ == focusedMonth;
                click(100, 40);
                pickerYear_ = 1601;
                ChangePickerPage(-1);
                ok &= pickerYear_ == 1601;
                pickerYear_ = 9999;
                ChangePickerPage(1);
                ok &= pickerYear_ == 9999;
                HandleMessage(WM_KEYDOWN, VK_ESCAPE, 1);
            }
        }
        DestroyWindow(window_);
        window_ = nullptr;
        fwprintf_s(stderr, L"date picker: checks=%s\n", ok ? L"passed" : L"failed");
        return ok;
    }

    // Test-only: render calendar frames with a WARP software device context into a
    // CPU-readable bitmap, so verification never depends on window composition
    // (works while the screen is locked or the display is asleep).
    bool EnsurePreviewRenderTarget(int width, int height)
    {
        if (!factory_ && FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.ReleaseAndGetAddressOf())))
            return false;
        ComPtr<ID3D11Device> d3d;
        // BGRA_SUPPORT 是 D2D 与 D3D 互操作（CreateDevice）的硬性要求。
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION,
            d3d.ReleaseAndGetAddressOf(), nullptr, nullptr);
        if (FAILED(result))
        {
            fwprintf_s(stderr, L"preview: D3D11CreateDevice failed hr=0x%08X\n", static_cast<unsigned>(result));
            return false;
        }
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(result = d3d.As(&dxgi)))
        {
            fwprintf_s(stderr, L"preview: IDXGIDevice failed hr=0x%08X\n", static_cast<unsigned>(result));
            return false;
        }
        ComPtr<ID2D1Factory1> factory1;
        if (FAILED(result = factory_.As(&factory1)))
        {
            fwprintf_s(stderr, L"preview: ID2D1Factory1 failed hr=0x%08X\n", static_cast<unsigned>(result));
            return false;
        }
        ComPtr<ID2D1Device> device;
        if (FAILED(result = factory1->CreateDevice(dxgi.Get(), device.ReleaseAndGetAddressOf())))
        {
            fwprintf_s(stderr, L"preview: CreateDevice failed hr=0x%08X\n", static_cast<unsigned>(result));
            return false;
        }
        ComPtr<ID2D1DeviceContext> context;
        if (FAILED(result = device->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.ReleaseAndGetAddressOf())))
        {
            fwprintf_s(stderr, L"preview: CreateDeviceContext failed hr=0x%08X\n", static_cast<unsigned>(result));
            return false;
        }
        // Drawing coordinates already include Scale(); match the HWND target.
        constexpr float dpi = 96.0f;
        const auto pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        const auto size = D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height));

        // 绘制目标（TARGET）与 CPU 回读位图（CANNOT_DRAW|CPU_READ）必须分开创建：
        // TARGET|CPU_READ 组合在本机 D2D 上会被 E_INVALIDARG 拒绝。
        const auto targetProps = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, pixelFormat, dpi, dpi);
        ComPtr<ID2D1Bitmap1> targetBitmap;
        if (FAILED(result = context->CreateBitmap(
                size, nullptr, 0, targetProps, targetBitmap.ReleaseAndGetAddressOf())))
        {
            fwprintf_s(stderr, L"preview: target CreateBitmap failed hr=0x%08X\n",
                       static_cast<unsigned>(result));
            return false;
        }
        const auto stagingProps = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ,
            pixelFormat, dpi, dpi);
        ComPtr<ID2D1Bitmap1> stagingBitmap;
        if (FAILED(result = context->CreateBitmap(
                size, nullptr, 0, stagingProps, stagingBitmap.ReleaseAndGetAddressOf())))
        {
            fwprintf_s(stderr, L"preview: staging CreateBitmap failed hr=0x%08X\n",
                       static_cast<unsigned>(result));
            return false;
        }
        context->SetDpi(dpi, dpi);
        context->SetTarget(targetBitmap.Get());
        previewContext_ = context;
        previewBitmap_ = stagingBitmap;
        previewTargetBitmap_ = targetBitmap;
        renderTarget_ = context;
        return true;
    }

    // Test-only: render calendar frames and verify the month-switch animation
    // settles exactly on the static grid.
    bool RenderFortunePreview(HINSTANCE instance, int dpi, bool dark, int offset, int selection, const wchar_t* path)
    {
        instance_ = instance;
        dpi_ = toolDpi_ = dpi;
        dark_ = dark;
        settings_.fontSizeOffset = offset;
        fontScale_ = CalendarFontScale(offset);
        fortuneToolOpen_ = true;
        fortuneBirth_ = wincal::ParseFortuneBirthday(L"1990-04-24");
        fortuneChart_ = wincal::BuildFortuneChart(*fortuneBirth_, 330, 0, wincal::FortuneGender::Female);
        settings_.fortuneBirthTime = L"05:30";
        settings_.fortuneGender = L"Female";
        const auto today = Today();
        fortuneStart_ = std::chrono::sys_days{std::chrono::year{today.year}/static_cast<unsigned>(today.month)/static_cast<unsigned>(today.day)};
        fortuneSelected_ = std::clamp(selection, 0, 14);
        const int width = ToolPx(560), height = ToolPx(kLogicalHeight);
        if (!EnsurePreviewRenderTarget(width, height) || !EnsureDeviceResources()) return false;
        renderTarget_->BeginDraw();
        renderTarget_->Clear(GetTheme(dark_).background);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Translation(-Scale(20), -Scale(250)));
        DrawToolPanel(GetTheme(dark_));
        if (FAILED(renderTarget_->EndDraw()) || FAILED(previewBitmap_->CopyFromRenderTarget(nullptr, previewContext_.Get(), nullptr))) return false;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(previewBitmap_->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + width * height * 4;
        const BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), width, -height, 1, 32};
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written{};
        bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, &header, sizeof(header), &written, nullptr) &&
            WriteFile(file, &info, sizeof(info), &written, nullptr);
        for (int row = 0; row < height && ok; ++row)
            ok = WriteFile(file, mapped.bits + row * mapped.pitch, width * 4, &written, nullptr) != FALSE;
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        previewBitmap_->Unmap();
        fwprintf_s(stderr, L"fortune preview: selection=%d exported=%d\n", fortuneSelected_, ok ? 1 : 0);
        return ok;
    }

    bool RenderCalendarPreview(HINSTANCE instance, int dpi, bool dark, int dir,
                               int fromYear, int fromMonth, float progress, const wchar_t* path,
                               bool slide = false, bool continuous = false, std::string_view holidayJson = {},
                               DatePickerMode pickerPreview = DatePickerMode::Closed, bool forecast = false, bool wttrPreview = false)
    {
        weatherForecastPreview_ = forecast;
        weatherWttrPreview_ = wttrPreview;
        if (!holidayJson.empty() && !calendarData_.LoadHolidayYear(fromYear, holidayJson)) return false;
        instance_ = instance;
        dpi_ = dpi;
        fontScale_ = 1.0f;
        dark_ = dark;
        weekStartsMonday_ = false;
        const int value = fromYear * 12 + fromMonth - 1 + (dir > 0 ? 1 : -1);
        displayYear_ = value / 12;
        displayMonth_ = value % 12 + 1;
        selected_ = Today();
        scrollDir_ = dir > 0 ? 1 : -1;
        scrollFromYear_ = fromYear;
        scrollFromMonth_ = fromMonth;
        const long long delta =
            WeekRowStartDays(displayYear_, displayMonth_) - WeekRowStartDays(fromYear, fromMonth);
        const long long rows = (delta < 0 ? -delta : delta) / 7;
        scrollRows_ = static_cast<int>(std::clamp(rows, 1LL, 6LL));

        if (!EnsurePreviewRenderTarget(static_cast<int>(Scale(kLogicalWidth)), static_cast<int>(Scale(kLogicalHeight))) ||
            !EnsureDeviceResources())
            return false;

        const int width = static_cast<int>(Scale(kLogicalWidth));
        const int height = static_cast<int>(Scale(kLogicalHeight));
        std::vector<BYTE> staticPixels, settledPixels, midPixels;
        const auto capture = [&](std::vector<BYTE>& pixels) {
            previewContext_->SetTarget(previewTargetBitmap_.Get());
            Paint();
            const HRESULT copyResult = previewBitmap_->CopyFromRenderTarget(
                nullptr, previewContext_.Get(), nullptr);
            if (FAILED(copyResult))
            {
                fwprintf_s(stderr, L"calendar preview: CopyFromRenderTarget failed hr=0x%08X\n",
                           static_cast<unsigned>(copyResult));
                return false;
            }
            D2D1_MAPPED_RECT mapped{};
            const HRESULT mapResult = previewBitmap_->Map(D2D1_MAP_OPTIONS_READ, &mapped);
            if (FAILED(mapResult))
            {
                fwprintf_s(stderr, L"calendar preview: Map failed hr=0x%08X\n", static_cast<unsigned>(mapResult));
                return false;
            }
            const size_t stride = static_cast<size_t>(width) * 4;
            pixels.resize(stride * height);
            for (int y = 0; y < height; ++y)
                memcpy(pixels.data() + y * stride, mapped.bits + y * mapped.pitch, stride);
            previewBitmap_->Unmap();
            return true;
        };
        const auto differingPixels = [](const std::vector<BYTE>& a, const std::vector<BYTE>& b) {
            size_t count{};
            for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
            {
                const int r = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
                const int g = a[i + 1] > b[i + 1] ? a[i + 1] - b[i + 1] : b[i + 1] - a[i + 1];
                const int bl = a[i + 2] > b[i + 2] ? a[i + 2] - b[i + 2] : b[i + 2] - a[i + 2];
                if (r > 2 || g > 2 || bl > 2) ++count;
            }
            return count;
        };

        scrollAnimating_ = false;
        slideAnimating_ = false;
        slidePos_ = 0.0f;
        slideTarget_ = 0.0f;
        transition_ = 1.0f;
        bool ok = capture(staticPixels);
        const int trackDir = dir > 0 ? 1 : -1;
        if (slide)
            slideAnimating_ = true;
        else
            scrollAnimating_ = true;
        scrollProgress_ = 1.0f;
        slidePos_ = slideTarget_ = static_cast<float>(trackDir);
        if (continuous)
        {
            settings_.monthPaging = false;
            ResetContinuousScroll();
            scrollAnimating_ = false;
        }
        ok = ok && capture(settledPixels);
        slideTarget_ = static_cast<float>(trackDir);
        slidePos_ = trackDir * std::clamp(progress, 0.0f, 0.99f);
        scrollProgress_ = std::clamp(progress, 0.0f, 0.99f);
        if (continuous)
        {
            scrollAnimating_ = false;
            continuousPosition_ = continuousTarget_ = 0.375;
        }
        ok = ok && capture(midPixels);
        {
            const size_t total = staticPixels.size() / 4;
            const size_t settledDiff = differingPixels(staticPixels, settledPixels);
            const size_t midDiff = differingPixels(staticPixels, midPixels);
            const bool framesOk = total > 0 && settledDiff * 1000 <= total &&
                (forecast ? midDiff == 0 : midDiff * 100 >= total);
            fwprintf_s(stderr, L"calendar preview: total=%zu settledDiff=%zu midDiff=%zu framesOk=%d\n",
                       total, settledDiff, midDiff, framesOk ? 1 : 0);
            ok = ok && framesOk;

            // Holiday previews export the settled month after checking the animation.
            if (!holidayJson.empty()) midPixels = staticPixels;
            if (pickerPreview != DatePickerMode::Closed)
            {
                scrollAnimating_ = slideAnimating_ = false;
                OpenDatePicker();
                if (pickerPreview == DatePickerMode::Years) OpenDatePicker();
                ok = capture(midPixels) && ok;
            }
            BITMAPFILEHEADER header{};
            header.bfType = 0x4d42;
            header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + static_cast<DWORD>(midPixels.size());
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD written{};
            // Map 读回的是 top-down 行序，负 biHeight 才能按原方向写出。
            BITMAPINFOHEADER bmi{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, 0, 0, 0, 0, 0};
            const bool wrote = file != INVALID_HANDLE_VALUE &&
                WriteFile(file, &header, sizeof(header), &written, nullptr) &&
                WriteFile(file, &bmi, sizeof(bmi), &written, nullptr) &&
                WriteFile(file, midPixels.data(), static_cast<DWORD>(midPixels.size()), &written, nullptr);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            ok = ok && wrote;
        }
        renderTarget_ = nullptr;
        previewBitmap_ = nullptr;
        previewTargetBitmap_ = nullptr;
        previewContext_ = nullptr;
        DiscardDeviceResources();
        return ok;
    }
#endif
    int Run(HINSTANCE instance, int, bool openSettings)
    {
        instance_ = instance;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (!RegisterWindowClass() || !RegisterSettingsWindowClass() ||
            !RegisterContextMenuWindowClass() ||
            !RegisterDetailWindowClass() || !RegisterAgendaWindowClass() || !CreateMainWindow())
            return 1;

        calendarData_.Load();
        weatherAutoLocation_ = settings_.weatherAutoLocation;
        weatherUseWttr_ = settings_.weatherUseWttr;
        StartWeatherRefresh();
        UpdateCalendarDiagnostics();
        UpdateTaskbarStatus();
        StartSystemCalendarInterceptor();
        RefreshTaskbarClockBounds();
        StartGlobalMouseMonitor();
        StartCalendarRefresh();
        SetTimer(window_, kCalendarRefreshTimer, 60000, nullptr);
        RequestSystemCalendarMonth(displayYear_, displayMonth_);
        if (openSettings)
            PostMessageW(window_, kShowSettingsMessage, 0, 0);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (IsWindow(settingsWindow_) && IsDialogMessageW(settingsWindow_, &message))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (calendarRefreshThread_.joinable())
        {
            calendarRefreshThread_.request_stop();
            calendarRefreshThread_.join();
        }
        if (systemCalendarThread_.joinable())
        {
            systemCalendarThread_.request_stop();
            systemCalendarThread_.join();
        }
        if (weatherThread_.joinable())
        {
            weatherThread_.request_stop();
            weatherThread_.join();
        }
        StopGlobalMouseMonitor();
        StopSystemCalendarInterceptor();
        DiscardDeviceResources();
        CoUninitialize();
        return static_cast<int>(message.wParam);
    }

private:
    void StartWeatherRefreshIfStale()
    {
        const auto weather = weatherData_.Snapshot();
        if (!weather.available || std::chrono::system_clock::now() - weather.updatedAt > std::chrono::minutes(15))
            StartWeatherRefresh();
    }

    void UpdateTaskbarStatus()
    {
        if (!settings_.taskbarStatus) { taskbarStatus_.Stop(); return; }
        if (taskbarStatus_.Running()) return;
        taskbarStatus_.Start(instance_, [this] { return CurrentTaskbarFields(); }, settings_.taskbarFormat);
    }

    wincal::TaskbarFields CurrentTaskbarFields()
    {
            SYSTEMTIME time{};
            GetLocalTime(&time);
            constexpr std::array<const wchar_t*, 12> hours{L"子", L"丑", L"寅", L"卯", L"辰", L"巳", L"午", L"未", L"申", L"酉", L"戌", L"亥"};
            const auto weather = weatherData_.Snapshot();
            wchar_t temperature[24]{};
            std::wstring forecast = weather.failed ? L"天气暂不可用" : L"天气加载中";
            if (weather.available)
            {
                swprintf_s(temperature, L" %.0f°C", weather.temperature);
                const bool cached = weather.failed || std::chrono::system_clock::now() - weather.updatedAt > std::chrono::hours(1);
                forecast = (weather.condition.empty() ? wincal::WeatherDescription(weather.code) : weather.condition) + temperature + (cached ? L"（缓存）" : L"");
            }
            const auto lunar = lunarCalendar_.FullTextForDate(time.wYear, time.wMonth, time.wDay);
            wincal::TaskbarFields fields;
            fields.fill(L"—");
            fields[0] = forecast; fields[1] = weather.location.city; fields[2] = lunar;
            fields[3] = std::wstring(hours[(time.wHour + 1) / 2 % 12]) + L"时";
            const auto degrees = [](double value) { return std::to_wstring(static_cast<int>(std::round(value))) + L"℃"; };
            if (weather.available) fields[4] = degrees(weather.temperature);
            if (weather.humidity) fields[5] = L"湿度" + std::to_wstring(static_cast<int>(std::round(*weather.humidity))) + L"%";
            if (weather.windSpeed && weather.windDirection)
            {
                constexpr std::array<const wchar_t*, 8> directions{L"北风", L"东北风", L"东风", L"东南风", L"南风", L"西南风", L"西风", L"西北风"};
                constexpr std::array<double, 12> limits{1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
                const auto level = std::count_if(limits.begin(), limits.end(), [&](double n) { return *weather.windSpeed >= n; });
                fields[6] = std::wstring(directions[static_cast<size_t>((*weather.windDirection + 22.5) / 45) % 8]) + std::to_wstring(level) + L"级";
            }
            // Forecast indices refer to local calendar dates, even while using a cache from yesterday.
            for (int i = 0; i < 7; ++i)
                for (const auto& day : weather.days)
                {
                    int year{}, month{}, date{};
                    if (swscanf_s(day.date.c_str(), L"%d-%d-%d", &year, &month, &date) == 3 &&
                        DaysFromCivil(year, month, date) == DaysFromCivil(time.wYear, time.wMonth, time.wDay) + i)
                    {
                        fields[7 + i] = wincal::WeatherDescription(day.code);
                        fields[14 + i] = degrees(day.low); fields[21 + i] = degrees(day.high);
                    }
                }
            if (weather.available) fields[28] = weather.code == 0 ? L"☀" : weather.code <= 3 ? L"☁" :
                (weather.code >= 71 && weather.code <= 86) ? L"❄" : L"☂";
            return fields;
    }

    void StartWeatherRefresh()
    {
#ifndef WINCAL_SETTINGS_PREVIEW
        {
            const std::lock_guard lock(weatherRefreshMutex_);
            weatherRefreshRequested_ = true;
        }
        if (weatherThread_.joinable()) { weatherWake_.notify_all(); return; }
        weatherThread_ = std::jthread([this](std::stop_token stop)
        {
            if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
            weatherData_.LoadCache(wincal::SettingsStore::CacheDirectory());
            if (!stop.stop_requested()) PostMessageW(window_, kWeatherUpdatedMessage, 0, 0);
            std::unique_lock lock(weatherRefreshMutex_);
            while (!stop.stop_requested())
            {
                weatherRefreshRequested_ = false;
                lock.unlock();
                try
                {
                    if (weatherData_.Refresh(stop, weatherAutoLocation_.load(), weatherUseWttr_.load()) && !stop.stop_requested())
                        PostMessageW(window_, kWeatherUpdatedMessage, 0, 0);
                }
                catch (...)
                {
                    // Keep clock and calendar usable when weather is unavailable.
                }
                lock.lock();
                const auto delay = weatherData_.Snapshot().failed ? std::chrono::minutes(5) : std::chrono::minutes(15);
                weatherWake_.wait_for(lock, stop, delay, [this] { return weatherRefreshRequested_; });
            }
            CoUninitialize();
        });
#endif
    }

    void ToggleWeatherForecast()
    {
        if (weatherPanelOpen_) { CloseWeatherForecast(); return; }
        if (!IsWindow(weatherWindow_))
        {
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = WeatherWindowProc;
            wc.hInstance = instance_;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kWeatherWindowClass;
            if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
            weatherWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWeatherWindowClass, L"WinCal 天气",
                WS_POPUP, 0, 0, static_cast<int>(Scale(kLogicalWidth)), static_cast<int>(Scale(kLogicalHeight)),
                window_, nullptr, instance_, this);
            if (!weatherWindow_) return;
            const DWORD corner = 2;
            DwmSetWindowAttribute(weatherWindow_, 33, &corner, sizeof(corner));
        }
        weatherPanelOpen_ = true;
        PositionWeatherForecast();
        if (IsWindow(agendaWindow_)) ShowWindow(agendaWindow_, SW_HIDE);
        ShowWindow(weatherWindow_, SW_SHOWNOACTIVATE);
        if (weatherData_.Snapshot().days.empty()) StartWeatherRefresh();
        InvalidateRect(weatherWindow_, nullptr, FALSE);
    }

    void CloseWeatherForecast()
    {
        weatherPanelOpen_ = false;
        if (IsWindow(weatherWindow_)) ShowWindow(weatherWindow_, SW_HIDE);
        UpdateAgenda();
        if (IsWindowVisible(window_)) SetFocus(window_);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void PositionWeatherForecast()
    {
        if (!IsWindow(weatherWindow_)) return;
        RECT anchor{};
        GetWindowRect(window_, &anchor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = static_cast<int>(Scale(kLogicalWidth));
        const int height = anchor.bottom - anchor.top;
        const int gap = static_cast<int>(Scale(8));
        LONG x = anchor.left - width - gap;
        if (x < monitor.rcWork.left && anchor.right + gap + width <= monitor.rcWork.right)
            x = anchor.right + gap;
        x = std::max(monitor.rcWork.left, x);
        SetWindowPos(weatherWindow_, HWND_TOPMOST, x, anchor.top, width, height, SWP_NOACTIVATE);
    }

    int HitTestWeather(int x, int y) const
    {
        const float px = x / Scale(1), py = y / Scale(1);
        if (px >= 378 && px < 418 && py >= 12 && py < 52) return 1;
        if (px >= 344 && px < 418 && py >= 60 && py < 90) return 2;
        return 0;
    }

    static LRESULT CALLBACK WeatherWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->weatherWindow_ = window;
        }
        if (!app) return DefWindowProcW(window, message, wParam, lParam);
        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            app->PaintWeatherForecast();
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (app->weatherRenderTarget_)
                app->weatherRenderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            return 0;
        case WM_LBUTTONUP:
            if (app->HitTestWeather(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) == 1) app->CloseWeatherForecast();
            else if (app->HitTestWeather(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) == 2) app->StartWeatherRefresh();
            return 0;
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            SetCursor(LoadCursorW(nullptr, app->HitTestWeather(point.x, point.y) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) app->CloseWeatherForecast();
            else if (wParam == VK_F5) app->StartWeatherRefresh();
            return 0;
        case WM_CLOSE: app->CloseWeatherForecast(); return 0;
        case WM_DESTROY:
            app->weatherWindow_ = nullptr;
            app->weatherRenderTarget_.Reset();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void PaintWeatherForecast()
    {
        if (!EnsureDeviceResources()) return;
        if (!weatherRenderTarget_)
        {
            RECT client{};
            GetClientRect(weatherWindow_, &client);
            if (FAILED(factory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), 96, 96),
                D2D1::HwndRenderTargetProperties(weatherWindow_, D2D1::SizeU(client.right, client.bottom)),
                &weatherRenderTarget_))) return;
        }
        const auto calendarTarget = renderTarget_;
        renderTarget_ = weatherRenderTarget_;
        renderTarget_->BeginDraw();
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
        renderTarget_->Clear(GetTheme(dark_).background);
        DrawWeatherForecast(GetTheme(dark_));
        const auto result = renderTarget_->EndDraw();
        renderTarget_ = calendarTarget;
        if (result == D2DERR_RECREATE_TARGET) weatherRenderTarget_.Reset();
    }

    void UpdateCalendarDiagnostics() const
    {
        const Date today = Today();
        const Date tomorrow = CivilFromDays(DaysFromCivil(today.year, today.month, today.day) + 1);
        SetPropW(
            window_, L"WinCal.EventCount",
            reinterpret_cast<HANDLE>(calendarData_.EventCount() + 1));
        SetPropW(
            window_, L"WinCal.IcsEventCount",
            reinterpret_cast<HANDLE>(calendarData_.IcsEventCount() + 1));
        SetPropW(
            window_, L"WinCal.SystemEventCount",
            reinterpret_cast<HANDLE>(calendarData_.SystemEventCount() + 1));
        SetPropW(
            window_, L"WinCal.SystemLoadedMonthCount",
            reinterpret_cast<HANDLE>(calendarData_.SystemLoadedMonthCount() + 1));
        SetPropW(
            window_, L"WinCal.SystemCalendarState",
            reinterpret_cast<HANDLE>(
                static_cast<ULONG_PTR>(calendarData_.SystemState()) + 1));
        SetPropW(
            window_, L"WinCal.TodayEventCount",
            reinterpret_cast<HANDLE>(
                calendarData_.EventCountForDate(today.year, today.month, today.day) + 1));
        SetPropW(
            window_, L"WinCal.TomorrowEventCount",
            reinterpret_cast<HANDLE>(
                calendarData_.EventCountForDate(tomorrow.year, tomorrow.month, tomorrow.day) + 1));
    }

    void StartCalendarRefresh()
    {
        {
            const std::lock_guard lock(calendarRefreshMutex_);
            calendarRefreshRequested_ = true;
        }
        if (calendarRefreshThread_.joinable())
        {
            calendarRefreshWake_.notify_all();
            return;
        }
        calendarRefreshThread_ = std::jthread([this](std::stop_token stopToken)
        {
            if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
            std::unique_lock lock(calendarRefreshMutex_);
            while (!stopToken.stop_requested())
            {
                calendarRefreshRequested_ = false;
                lock.unlock();
                try
                {
                    if (calendarData_.RefreshFromNetwork(stopToken) &&
                        !stopToken.stop_requested() && IsWindow(window_))
                        PostMessageW(window_, kCalendarDataUpdatedMessage, 0, 0);
                }
                catch (...)
                {
                    // A failed refresh keeps the last successful local data.
                }
                lock.lock();
                calendarRefreshWake_.wait_for(lock, stopToken, std::chrono::minutes(1),
                    [this] { return calendarRefreshRequested_; });
            }
            CoUninitialize();
        });
    }

    void RequestSystemCalendarMonth(int year, int month)
    {
#ifdef WINCAL_SETTINGS_PREVIEW
        // Offscreen checks use supplied fixtures and must not load user caches
        // or start background services while exercising month navigation.
        static_cast<void>(year);
        static_cast<void>(month);
#else
        bool holidaysRequested = calendarData_.RequestIcsYear(year);
        for (int requestedYear : {year - 1, year, year + 1})
            holidaysRequested = calendarData_.RequestHolidayYear(requestedYear) || holidaysRequested;
        if (holidaysRequested) StartCalendarRefresh();
        if (!calendarData_.UsesSystemCalendar())
            return;

        {
            const std::lock_guard lock(systemCalendarRequestMutex_);
            pendingSystemCalendarMonth_ = std::pair{year, month};
            if (systemCalendarWorkerRunning_)
                return;
            systemCalendarWorkerRunning_ = true;
        }

        if (systemCalendarThread_.joinable())
            systemCalendarThread_.join();
        systemCalendarThread_ = std::jthread([this](std::stop_token stopToken)
        {
            while (!stopToken.stop_requested())
            {
                std::optional<std::pair<int, int>> request;
                {
                    const std::lock_guard lock(systemCalendarRequestMutex_);
                    if (!pendingSystemCalendarMonth_)
                    {
                        systemCalendarWorkerRunning_ = false;
                        return;
                    }
                    request = pendingSystemCalendarMonth_;
                    pendingSystemCalendarMonth_.reset();
                }

                if (calendarData_.RefreshSystemCalendar(
                        request->first, request->second, stopToken) &&
                    !stopToken.stop_requested() && IsWindow(window_))
                {
                    PostMessageW(window_, kCalendarDataUpdatedMessage, 0, 0);
                }
            }

            const std::lock_guard lock(systemCalendarRequestMutex_);
            systemCalendarWorkerRunning_ = false;
        });
#endif
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        App* app = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->window_ = window;
        }
        else
        {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        return app ? app->HandleMessage(message, wParam, lParam)
                   : DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK SettingsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        App* app = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->settingsWindow_ = window;
        }
        else
        {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return app ? app->HandleSettingsMessage(window, message, wParam, lParam)
                   : DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK DetailWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        App* app = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->detailWindow_ = window;
        }
        else
        {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return app ? app->HandleDetailMessage(window, message, wParam, lParam)
                   : DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK ContextMenuWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        App* app = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->contextMenuWindow_ = window;
        }
        else
        {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return app ? app->HandleContextMenuMessage(window, message, wParam, lParam)
                   : DefWindowProcW(window, message, wParam, lParam);
    }

    bool RegisterWindowClass() const
    {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClass;
        windowClass.hIconSm = windowClass.hIcon;
        return RegisterClassExW(&windowClass) != 0;
    }

    bool RegisterSettingsWindowClass() const
    {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = SettingsWindowProc;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kSettingsWindowClass;
        windowClass.hIconSm = windowClass.hIcon;
        return RegisterClassExW(&windowClass) != 0;
    }

    bool RegisterDetailWindowClass() const
    {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = DetailWindowProc;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kDetailWindowClass;
        windowClass.hIconSm = windowClass.hIcon;
        return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool RegisterContextMenuWindowClass() const
    {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = ContextMenuWindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kContextMenuWindowClass;
        return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool CreateMainWindow()
    {
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kWindowClass,
            kWindowTitle,
            WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            kLogicalWidth,
            kLogicalHeight,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!window_)
            return false;

        // 工具面板只输入数字和时刻，保持禁用输入法以避免不必要的插件注入。
        ImmAssociateContextEx(window_, nullptr, IACE_IGNORENOCONTEXT);
        dpi_ = GetDpiForWindow(window_);
        UpdateWindowSize();
        settings_ = wincal::SettingsStore::Load();
        if (settings_.autoStartup)
            ApplyAutoStartup(true);
        const auto today = Today();
        displayYear_ = today.year;
        displayMonth_ = today.month;
        selected_ = today;
        ApplyDwmAppearance();
        return true;
    }

    void ApplyDwmAppearance()
    {
        dark_ = settings_.themeMode == L"Dark" ||
                (settings_.themeMode != L"Light" && IsDarkMode());
        fontScale_ = CalendarFontScale(settings_.fontSizeOffset);
        const bool previousWeekStart = weekStartsMonday_;
        weekStartsMonday_ = settings_.weekStartDay != L"Sunday";
        if (settings_.monthPaging)
        {
            continuousInitialized_ = false;
            continuousAnimating_ = false;
        }
        else if (!continuousInitialized_ || previousWeekStart != weekStartsMonday_)
            ResetContinuousScroll();
        const BOOL darkValue = dark_ ? TRUE : FALSE;
        DwmSetWindowAttribute(window_, 20, &darkValue, sizeof(darkValue));
        const DWORD roundPreference = 2;
        DwmSetWindowAttribute(window_, 33, &roundPreference, sizeof(roundPreference));
        if (IsWindow(agendaWindow_))
        {
            DwmSetWindowAttribute(agendaWindow_, 20, &darkValue, sizeof(darkValue));
            InvalidateRect(agendaWindow_, nullptr, FALSE);
        }
    }

    float TextScale(float value) const
    {
        return Scale(value * fontScale_);
    }

    // All settings geometry uses DIPs. Fonts and controls are scaled together.
    int SettingsPx(int value) const { return MulDiv(value, settingsDpi_, 96); }

    RECT SettingsRect(int x, int y, int width, int height) const
    {
        return {SettingsPx(x), SettingsPx(y), SettingsPx(x + width), SettingsPx(y + height)};
    }

    static int SettingSelection(HWND window, int id)
    {
        return static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(GetDlgItem(window, id), L"Selection")));
    }

    static std::vector<const wchar_t*> SettingOptions(int id)
    {
        switch (id)
        {
        case kSettingTheme: return {L"跟随系统", L"浅色", L"深色"};
        case kSettingDataSource: return {L"系统日历", L"ICS 订阅", L"系统日历 + ICS"};
        case kSettingRefresh: return {L"10 分钟", L"30 分钟", L"60 分钟", L"120 分钟", L"1 天"};
        case kSettingWeekStart: return {L"周日", L"周一"};
        case kSettingWeatherLocation: return {L"自动定位", L"固定北京"};
        case kSettingWeatherProvider: return {L"Open-Meteo（默认）", L"wttr.in"};
        default: return {};
        }
    }

    void SetSettingSelection(HWND window, int id, int index)
    {
        HWND control = GetDlgItem(window, id);
        SetPropW(control, L"Selection", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(index)));
        const auto options = SettingOptions(id);
        if (id == kSettingTheme)
            SetWindowTextW(control, L"跟随系统");
        else if (!options.empty())
            SetWindowTextW(control, options[std::clamp(index, 0, static_cast<int>(options.size()) - 1)]);
        InvalidateRect(control, nullptr, FALSE);
    }

    static LRESULT CALLBACK SettingsControlProc(HWND window, UINT message, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto* app = reinterpret_cast<App*>(data);
        if (message == WM_MOUSEWHEEL)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT controlRect{};
            GetWindowRect(window, &controlRect);
            wchar_t className[16]{};
            GetClassNameW(window, className, ARRAYSIZE(className));
            if (wcscmp(className, L"EDIT") != 0 || !PtInRect(&controlRect, point))
                return app->HandleSettingsMessage(GetParent(window), message, wParam, lParam);
        }
        if (message == WM_MOUSEMOVE)
        {
            if (app->settingsHotControl_ != window)
            {
                if (app->settingsHotControl_) InvalidateRect(app->settingsHotControl_, nullptr, FALSE);
                app->settingsHotControl_ = window;
                InvalidateRect(window, nullptr, FALSE);
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
            }
        }
        else if (message == WM_MOUSELEAVE)
        {
            if (app->settingsHotControl_ == window) app->settingsHotControl_ = nullptr;
            InvalidateRect(window, nullptr, FALSE);
        }
        else if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
            InvalidateRect(GetParent(window), nullptr, FALSE);
        else if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, SettingsControlProc, 1);
        return DefSubclassProc(window, message, wParam, lParam);
    }

    HWND AddSettingControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
                           int id, int x, int y, int width, int height, int page = -1)
    {
        RECT rect = SettingsRect(x, y, width, height);
        HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(settingsBodyFont_), FALSE);
        SetWindowSubclass(control, SettingsControlProc, 1, reinterpret_cast<DWORD_PTR>(this));
        ImmAssociateContextEx(control, nullptr, IACE_IGNORENOCONTEXT);
        settingsControls_.push_back({control, x, y, width, height, page});
        return control;
    }

    void PopulateSettingsControls(HWND window)
    {
        SetSettingSelection(window, kSettingTheme, settingsDraft_.themeMode == L"Light" ? 1 : settingsDraft_.themeMode == L"Dark" ? 2 : 0);
        SetDlgItemInt(window, kSettingFontOffset, static_cast<UINT>(settingsDraft_.fontSizeOffset), TRUE);
        SetSettingSelection(window, kSettingAutoStartup, settingsDraft_.autoStartup ? 1 : 0);
        SetSettingSelection(window, kSettingDataSource, settingsDraft_.dataSource == L"IcsUrl" ? 1 : settingsDraft_.dataSource == L"Both" ? 2 : 0);
        std::wstring urls;
        for (const auto& url : settingsDraft_.icsUrls)
        {
            if (!urls.empty()) urls += L"\r\n";
            urls += url;
        }
        SetDlgItemTextW(window, kSettingIcsUrls, urls.c_str());
        SetSettingSelection(window, kSettingRefresh,
            settingsDraft_.icsRefreshMinutes == 10 ? 0 : settingsDraft_.icsRefreshMinutes == 60 ? 2 :
            settingsDraft_.icsRefreshMinutes == 120 ? 3 : settingsDraft_.icsRefreshMinutes == 1440 ? 4 : 1);
        SetSettingSelection(window, kSettingWeekStart, settingsDraft_.weekStartDay == L"Monday" ? 1 : 0);
        SetSettingSelection(window, kSettingMonthPaging, settingsDraft_.monthPaging ? 1 : 0);
        SetSettingSelection(window, kSettingWeatherLocation, settingsDraft_.weatherAutoLocation ? 0 : 1);
        SetSettingSelection(window, kSettingWeatherProvider, settingsDraft_.weatherUseWttr ? 1 : 0);
        SetSettingSelection(window, kSettingTaskbarStatus, settingsDraft_.taskbarStatus ? 1 : 0);
        SetDlgItemTextW(window, kSettingTaskbarFormat, settingsDraft_.taskbarFormat.c_str());
    }

    void SelectSettingsPage(HWND window, int page)
    {
        settingsPage_ = page;
        for (const auto& control : settingsControls_)
            ShowWindow(control.window, control.page < 0 || control.page == page ? SW_SHOW : SW_HIDE);
        UpdateSettingsSourceControls(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    void LayoutSettingsViewport(HWND window)
    {
        if (settingsLayoutActive_) return;
        settingsLayoutActive_ = true;
        RECT client{};
        GetClientRect(window, &client);
        const auto style = GetWindowLongPtrW(window, GWL_STYLE);
        const UINT nonClientDpi = GetDpiForWindow(window);
        const int barWidth = GetSystemMetricsForDpi(SM_CXVSCROLL, nonClientDpi);
        const int barHeight = GetSystemMetricsForDpi(SM_CYHSCROLL, nonClientDpi);
        const int availableWidth = client.right + ((style & WS_VSCROLL) ? barWidth : 0);
        const int availableHeight = client.bottom + ((style & WS_HSCROLL) ? barHeight : 0);
        bool needHorizontal = availableWidth < SettingsPx(kSettingsLogicalWidth);
        bool needVertical = availableHeight < SettingsPx(kSettingsLogicalHeight);
        for (int pass = 0; pass < 2; ++pass)
        {
            needHorizontal = availableWidth - (needVertical ? barWidth : 0) < SettingsPx(kSettingsLogicalWidth);
            needVertical = availableHeight - (needHorizontal ? barHeight : 0) < SettingsPx(kSettingsLogicalHeight);
        }
        ShowScrollBar(window, SB_HORZ, needHorizontal);
        ShowScrollBar(window, SB_VERT, needVertical);
        GetClientRect(window, &client);
        SCROLLINFO horizontal{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS,
            0, SettingsPx(kSettingsLogicalWidth) - 1, static_cast<UINT>(client.right), settingsScrollX_};
        SetScrollInfo(window, SB_HORZ, &horizontal, TRUE);
        // Adding one scrollbar reduces the space available to the other.
        GetClientRect(window, &client);
        SCROLLINFO vertical{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS,
            0, SettingsPx(kSettingsLogicalHeight) - 1, static_cast<UINT>(client.bottom), settingsScrollY_};
        SetScrollInfo(window, SB_VERT, &vertical, TRUE);
        GetClientRect(window, &client);
        horizontal.nPage = client.right;
        SetScrollInfo(window, SB_HORZ, &horizontal, TRUE);
        settingsScrollX_ = GetScrollPos(window, SB_HORZ);
        settingsScrollY_ = GetScrollPos(window, SB_VERT);
        for (const auto& control : settingsControls_)
        {
            RECT rect = SettingsRect(control.x, control.y, control.width, control.height);
            SetWindowPos(control.window, nullptr, rect.left - settingsScrollX_, rect.top - settingsScrollY_,
                rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        settingsLayoutActive_ = false;
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    void UpdateSettingsSourceControls(HWND window)
    {
        const bool useIcs = SettingSelection(window, kSettingDataSource) != 0;
        EnableWindow(GetDlgItem(window, kSettingIcsUrls), useIcs);
        EnableWindow(GetDlgItem(window, kSettingRefresh), useIcs);
    }

    void CreateSettingsControls(HWND window)
    {
        settingsDpi_ = GetDpiForWindow(window);
#ifdef WINCAL_SETTINGS_PREVIEW
        settingsDpi_ = settingsPreviewDpi_;
#endif
        EnsureSettingsFonts(window);
        const auto button = [&](const wchar_t* text, int id, int x, int y, int w, int h, int page = -1)
        {
            return AddSettingControl(window, L"BUTTON", text, BS_OWNERDRAW, id, x, y, w, h, page);
        };
        button(L"外观设置", kSettingPageAppearance, 12, 82, 132, 42);
        button(L"日历来源", kSettingPageSource, 12, 132, 132, 42);
        button(L"常规设置", kSettingPageGeneral, 12, 182, 132, 42);
        button(L"天气设置", kSettingPageWeather, 12, 232, 132, 42);
        button(L"配置同步", kSettingPageSync, 12, 282, 132, 42);
        const auto credentials =
#ifdef WINCAL_SETTINGS_PREVIEW
            wincal::SyncCredentials{};
#else
            wincal::LoadSyncCredentials();
#endif
        AddSettingControl(window, L"EDIT", credentials.account.c_str(), ES_AUTOHSCROLL,
            kSyncAccount, 202, 184, 390, 26, 5);
        AddSettingControl(window, L"EDIT", credentials.password.c_str(), ES_AUTOHSCROLL | ES_PASSWORD,
            kSyncPassword, 202, 256, 390, 26, 5);
        button(L"测试连接", kSyncTest, 190, 324, 126, 36, 5);
        button(L"上传配置", kSyncUpload, 328, 324, 126, 36, 5);
        button(L"下载配置", kSyncDownload, 466, 324, 142, 36, 5);
        for (int id : {kSyncTest, kSyncUpload, kSyncDownload}) EnableWindow(GetDlgItem(window, id), !syncBusy_);

        button(L"跟随系统", kSettingTheme, 190, 178, 134, 42, 0);
        button(L"浅色", kSettingThemeLight, 332, 178, 134, 42, 0);
        button(L"深色", kSettingThemeDark, 474, 178, 134, 42, 0);
        button(L"−", kSettingFontMinus, 468, 276, 36, 36, 0);
        AddSettingControl(window, L"EDIT", L"0", ES_CENTER | ES_AUTOHSCROLL,
                          kSettingFontOffset, 512, 284, 52, 22, 0);
        button(L"+", kSettingFontPlus, 572, 276, 36, 36, 0);

        button(L"", kSettingDataSource, 190, 168, 254, 38, 1);
        button(L"", kSettingRefresh, 460, 168, 148, 38, 1);
        AddSettingControl(window, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                          kSettingIcsUrls, 202, 278, 394, 98, 1);

        button(L"开机启动", kSettingAutoStartup, 552, 150, 56, 30, 2);
        button(L"", kSettingWeekStart, 408, 230, 200, 38, 2);
        button(L"整月翻滚", kSettingMonthPaging, 552, 312, 56, 30, 2);
        button(L"任务栏时钟", kSettingTaskbarStatus, 552, 390, 56, 30, 2);
        button(L"显示格式…", kSettingTaskbarFormatPage, 482, 431, 126, 24, 2);
        AddSettingControl(window, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                          kSettingTaskbarFormat, 190, 166, 418, 62, 4);
        SendDlgItemMessageW(window, kSettingTaskbarFormat, EM_SETLIMITTEXT, 512, 0);
        button(L"", kSettingWeatherLocation, 190, 178, 418, 38, 3);
        button(L"", kSettingWeatherProvider, 190, 258, 418, 38, 3);

        button(L"恢复默认", kSettingDefaults, 28, 532, 104, 36);
        button(L"取消", kSettingCancel, 438, 532, 88, 36);
        button(L"保存设置", kSettingSave, 538, 532, 114, 36);
        PopulateSettingsControls(window);
        UpdateSettingsFontControls(window);
        SelectSettingsPage(window, 0);
    }

    void UpdateSettingsFontControls(HWND window)
    {
        BOOL valid{};
        const int offset = std::clamp(static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, &valid, TRUE)), -2, 10);
        EnableWindow(GetDlgItem(window, kSettingFontMinus), offset > -2);
        EnableWindow(GetDlgItem(window, kSettingFontPlus), offset < 10);
        ApplyLiveSettingsPreview(window);
        InvalidateRect(window, nullptr, FALSE);
    }

    void RefreshCalendarAppearance()
    {
        if (!IsWindow(window_)) return;
        ApplyDwmAppearance();
        DiscardDeviceResources();
        InvalidateRect(window_, nullptr, FALSE);
        if (IsWindow(detailWindow_))
        {
            BOOL darkValue = dark_ ? TRUE : FALSE;
            DwmSetWindowAttribute(detailWindow_, 20, &darkValue, sizeof(darkValue));
            EnsureDetailFonts(detailWindow_);
            PositionDetailWindow();
            InvalidateRect(detailWindow_, nullptr, FALSE);
        }
    }

    void ApplyLiveSettingsPreview(HWND window)
    {
        if (!settingsLivePreview_) return;
        const int theme = SettingSelection(window, kSettingTheme);
        settings_.themeMode = theme == 1 ? L"Light" : theme == 2 ? L"Dark" : L"FollowSystem";
        BOOL valid{};
        const int offset = static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, &valid, TRUE));
        settings_.fontSizeOffset = std::clamp(valid ? offset : 0, -2, 10);
        settings_.weekStartDay = SettingSelection(window, kSettingWeekStart) == 1 ? L"Monday" : L"Sunday";
        RefreshCalendarAppearance();
        if (IsWindow(window_))
        {
            KillTimer(window_, kOutsideClickTimer);
            ShowWindow(window_, SW_SHOWNOACTIVATE);
        }
    }

    void FinishLiveSettingsPreview()
    {
        if (!settingsLivePreview_) return;
        settingsLivePreview_ = false;
        settings_ = settingsSaveAccepted_ ? settingsDraft_ : settingsBeforeEdit_;
        settingsBeforeEdit_ = {};
        RefreshCalendarAppearance();
        if (IsWindowVisible(window_)) PositionNearTray(false);
    }

    void PositionLivePreview()
    {
        if (!settingsLivePreview_ || !IsWindow(settingsWindow_) || !IsWindow(window_)) return;
        RECT settingsRect{};
        GetWindowRect(settingsWindow_, &settingsRect);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(settingsWindow_, MONITOR_DEFAULTTONEAREST), &monitor);
        const UINT dpi = GetDpiForWindow(settingsWindow_);
        const int width = MulDiv(kLogicalWidth, dpi, 96);
        const int height = MulDiv(kLogicalHeight, dpi, 96);
        const int gap = MulDiv(16, dpi, 96);
        LONG x = settingsRect.right + gap;
        if (x + width > monitor.rcWork.right)
            x = settingsRect.left - width - gap;
        x = std::clamp(x, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
        const LONG y = std::clamp(settingsRect.top, monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
        SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    void ApplySettingsWindowAppearance(HWND window)
    {
        const int theme = SettingSelection(window, kSettingTheme);
        settingsWindowDark_ = theme == 2 || (theme != 1 && IsDarkMode());
        const BOOL darkValue = settingsWindowDark_;
        DwmSetWindowAttribute(window, 20, &darkValue, sizeof(darkValue));
        if (settingsInputBrush_) DeleteObject(settingsInputBrush_);
        if (settingsCardBrush_) DeleteObject(settingsCardBrush_);
        const auto palette = GetSettingsPalette();
        settingsInputBrush_ = CreateSolidBrush(palette.input);
        settingsCardBrush_ = CreateSolidBrush(palette.card);
        ApplyLiveSettingsPreview(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    SettingsPalette GetSettingsPalette() const
    {
        if (settingsWindowDark_)
            return {RGB(24,25,31), RGB(33,35,43), RGB(40,42,52), RGB(237,238,245),
                    RGB(164,169,186), RGB(114,121,142), RGB(64,151,255), RGB(57,60,75)};
        return {RGB(240,242,245), RGB(255,255,255), RGB(248,249,251), RGB(30,38,50),
                RGB(133,140,151), RGB(202,206,212), RGB(40,132,255), RGB(231,234,238)};
    }

    void EnsureSettingsFonts(HWND)
    {
        if (settingsBodyFont_) return;
        const auto makeFont = [&](int size, int weight)
        {
            return CreateFontW(-SettingsPx(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH, L"Microsoft YaHei UI");
        };
        settingsTitleFont_ = makeFont(18, FW_NORMAL);
        settingsBodyFont_ = makeFont(13, FW_NORMAL);
        settingsLabelFont_ = makeFont(14, FW_NORMAL);
        settingsCaptionFont_ = makeFont(11, FW_NORMAL);
    }

    void SettingsBox(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius = 10) const
    {
        const auto brush = CreateSolidBrush(fill);
        const auto pen = CreatePen(PS_SOLID, std::max(1, SettingsPx(1)), border);
        const auto oldBrush = SelectObject(dc, brush);
        const auto oldPen = SelectObject(dc, pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, SettingsPx(radius), SettingsPx(radius));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }

    void SettingsText(HDC dc, const wchar_t* text, int x, int y, int width, int height,
                      HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) const
    {
        RECT rect = SettingsRect(x, y, width, height);
        const auto oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        ::DrawTextW(dc, text, -1, &rect, flags | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }

    void PaintSettingsWindow(HWND window)
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RenderSettingsWindow(window, dc);
        EndPaint(window, &paint);
    }

    void RenderSettingsWindow(HWND window, HDC dc)
    {
        const auto p = GetSettingsPalette();
        RECT client{};
        GetClientRect(window, &client);
        const auto bg = CreateSolidBrush(p.background);
        FillRect(dc, &client, bg);
        DeleteObject(bg);
        const int savedDc = SaveDC(dc);
        SetViewportOrgEx(dc, -settingsScrollX_, -settingsScrollY_, nullptr);

        DrawIconEx(dc, SettingsPx(24), SettingsPx(22), LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON)),
                   SettingsPx(28), SettingsPx(28), 0, nullptr, DI_NORMAL);
        SettingsText(dc, L"WinCal · 设置", 64, 20, 280, 32, settingsTitleFont_, p.primary);
        SettingsBox(dc, SettingsRect(156, 76, 508, 436), p.card, p.card, 16);
        const auto group = [&](int y, int height)
        {
            SettingsBox(dc, SettingsRect(174, y, 472, height), p.input, p.border, 8);
        };
        if (settingsPage_ == 0) { group(140, 104); group(252, 106); group(366, 106); }
        else if (settingsPage_ == 1) { group(140, 74); group(218, 248); }
        else if (settingsPage_ == 2) { group(140, 70); group(218, 156); group(382, 90); }
        else if (settingsPage_ == 3) { group(140, 90); group(238, 206); }
        else if (settingsPage_ == 5) { group(140, 172); group(316, 188); }
        else { group(140, 96); group(240, 100); group(344, 132); }
        if (settingsPage_ == 0)
        {
            SettingsText(dc, L"外观与字号", 190, 114, 280, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"主题模式", 190, 150, 220, 20, settingsBodyFont_, p.secondary);
            SettingsText(dc, L"文字大小", 190, 266, 220, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"仅调整日历中的文字", 190, 294, 230, 20, settingsCaptionFont_, p.secondary);
            SettingsBox(dc, SettingsRect(509, 276, 58, 36), p.input,
                        GetFocus() == GetDlgItem(window, kSettingFontOffset) ? p.accent : p.border, 8);
            SettingsText(dc, L"可调范围 −2 至 +10", 446, 322, 162, 20, settingsCaptionFont_, p.secondary, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SettingsText(dc, L"在日历窗口中实时预览", 206, 376, 360, 22, settingsBodyFont_, p.primary);
            SettingsText(dc, L"保存以保留修改；取消会恢复原来的效果。", 206, 400, 376, 20, settingsCaptionFont_, p.secondary);
        }
        else if (settingsPage_ == 1)
        {
            const bool useIcs = SettingSelection(window, kSettingDataSource) != 0;
            SettingsText(dc, L"日历与订阅", 190, 114, 280, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"日历来源", 190, 142, 240, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"自动刷新", 460, 142, 148, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"ICS 订阅链接", 190, 224, 300, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"每行一条公开的 .ics 链接", 190, 248, 400, 20, settingsCaptionFont_, p.secondary);
            SettingsBox(dc, SettingsRect(190, 270, 418, 118), p.input,
                        GetFocus() == GetDlgItem(window, kSettingIcsUrls) ? p.accent : p.border, 10);
            SettingsText(dc, useIcs ? L"保存后后台刷新，已有缓存会立即用于展示。" : L"当前使用系统日历，切换来源后可编辑订阅。",
                         190, 404, 418, 26, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"中国节假日与调休：提莫接口，每日自动更新。",
                         190, 436, 418, 22, settingsCaptionFont_, p.secondary);
        }
        else if (settingsPage_ == 2)
        {
            SettingsText(dc, L"启动与日历布局", 190, 114, 320, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"开机启动", 190, 146, 320, 24, settingsBodyFont_, p.primary);
            SettingsText(dc, L"登录 Windows 后自动运行 WinCal", 190, 172, 338, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"每周开始", 190, 228, 200, 24, settingsBodyFont_, p.primary);
            SettingsText(dc, L"选择日历每一行的第一天", 190, 254, 210, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"整月翻滚", 190, 312, 320, 24, settingsBodyFont_, p.primary);
            SettingsText(dc, L"关闭后翻月时逐周平滑滚动", 190, 338, 338, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"任务栏双行时钟", 190, 390, 320, 24, settingsBodyFont_, p.primary);
            SettingsText(dc, L"保存后生效，可自定义显示格式。", 190, 420, 280, 22, settingsCaptionFont_, p.secondary);
        }
        else if (settingsPage_ == 3)
        {
            SettingsText(dc, L"天气与位置", 190, 114, 320, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"定位方式", 190, 150, 320, 20, settingsBodyFont_, p.secondary);
            SettingsText(dc, L"天气来源", 190, 230, 418, 22, settingsBodyFont_, p.secondary);
            SettingsText(dc, L"定位优先使用系统位置，其次网络定位，默认北京。", 190, 310, 418, 22, settingsCaptionFont_, p.secondary);
            const auto weather = weatherData_.Snapshot();
            const std::wstring position = weather.available ? L"当前：" + weather.location.city + L" · " + weather.location.source : L"当前：正在获取位置与天气";
            SettingsText(dc, position.c_str(), 190, 338, 418, 22, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"Open-Meteo 提供 7 天预报，wttr.in 通常提供 3 天。", 190, 370, 418, 22, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"点击顶部天气可查看体感、湿度、风速及紫外线。", 190, 402, 418, 22, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"每 15 分钟更新；穿衣建议根据天气生成。", 190, 434, 418, 22, settingsCaptionFont_, p.secondary);
        }
        else if (settingsPage_ == 5)
        {
            SettingsText(dc, L"坚果云 · 配置同步", 190, 114, 400, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"坚果云账号", 190, 152, 400, 22, settingsBodyFont_, p.secondary);
            SettingsBox(dc, SettingsRect(190, 178, 418, 38), p.card, p.border, 8);
            SettingsText(dc, L"第三方应用密码", 190, 224, 400, 22, settingsBodyFont_, p.secondary);
            SettingsBox(dc, SettingsRect(190, 250, 418, 38), p.card, p.border, 8);
            SettingsText(dc, L"坚果云网页 → 账户信息 → 安全选项 → 第三方应用管理", 190, 292, 440, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"云端位置：WinCal/settings.json", 190, 380, 440, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"上传已保存配置；下载后需点击保存设置。", 190, 406, 440, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"凭据仅存于此电脑的 Windows 凭据管理器。", 190, 432, 440, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, syncStatus_.c_str(), 190, 462, 440, 38, settingsCaptionFont_, p.secondary, DT_WORDBREAK);
        }
        else
        {
            SettingsText(dc, L"任务栏显示格式", 190, 114, 320, 24, settingsLabelFont_, p.primary);
            SettingsText(dc, L"使用 \\n 分隔上下两行，或直接按回车换行。", 190, 143, 418, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"预览", 190, 245, 418, 22, settingsBodyFont_, p.secondary);
            SYSTEMTIME now{}; GetLocalTime(&now);
            const auto fields = CurrentTaskbarFields();
            const auto preview = wincal::FormatTaskbar(ReadControlText(GetDlgItem(window, kSettingTaskbarFormat)), now,
                [&](size_t i) { return fields[i]; });
            SettingsText(dc, preview.c_str(), 190, 276, 418, 60, settingsBodyFont_, p.primary, DT_WORDBREAK);
            SettingsText(dc, L"yyyy 年 / mm 月 / dd 日 / HH:ff:ss 时分秒", 190, 350, 418, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"MD 农历 / ddd 周几 / shichen 时辰 / pos 城市", 190, 372, 418, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"rtm 实时温度 / rhu 湿度 / rwd 风力风向", 190, 394, 418, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"we1～we7 天气 / tl1～tl7 最低温 / th1～th7 最高温", 190, 416, 418, 20, settingsCaptionFont_, p.secondary);
            SettingsText(dc, L"序号 1 表示今天，2 表示明天；缺少数据时显示 —。", 190, 438, 418, 18, settingsCaptionFont_, p.secondary);
        }

        RECT line = SettingsRect(28, 516, 624, 1);
        const auto lineBrush = CreateSolidBrush(p.border);
        FillRect(dc, &line, lineBrush);
        DeleteObject(lineBrush);
        RestoreDC(dc, savedDc);
    }

    LRESULT DrawSettingsButton(const DRAWITEMSTRUCT& item) const
    {
        const auto p = GetSettingsPalette();
        const int id = item.CtlID;
        const bool enabled = IsWindowEnabled(item.hwndItem);
        const bool hot = settingsHotControl_ == item.hwndItem;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool tab = (id >= kSettingPageAppearance && id <= kSettingPageWeather) || id == kSettingPageSync;
        const bool theme = id == kSettingTheme || id == kSettingThemeLight || id == kSettingThemeDark;
        const bool toggle = id == kSettingAutoStartup || id == kSettingMonthPaging || id == kSettingTaskbarStatus;
        const bool dropdown = !theme && !SettingOptions(id).empty();
        const bool active = (tab && (settingsPage_ == id - kSettingPageAppearance || (id == kSettingPageSync && settingsPage_ == 5) ||
            (settingsPage_ == 4 && id == kSettingPageGeneral))) ||
            (theme && SettingSelection(settingsWindow_, kSettingTheme) == (id == kSettingTheme ? 0 : id == kSettingThemeLight ? 1 : 2));
        const bool primary = id == kSettingSave;
        const auto tint = tab ? (settingsWindowDark_ ? RGB(54,56,62) : RGB(226,228,231)) :
            (settingsWindowDark_ ? RGB(40,62,86) : RGB(235,244,255));
        const auto fill = primary ? p.accent : active || hot || pressed ? tint : tab ? p.background : p.card;
        const auto background = CreateSolidBrush(tab || id == kSettingDefaults || id == kSettingCancel || primary ? p.background : p.input);
        FillRect(item.hDC, &item.rcItem, background);
        DeleteObject(background);
        if (toggle)
        {
            const bool on = SettingSelection(settingsWindow_, id) != 0;
            RECT track{SettingsPx(8),SettingsPx(4),SettingsPx(52),SettingsPx(26)};
            SettingsBox(item.hDC, track, on ? p.accent : p.muted, on ? p.accent : p.muted, 22);
            const int knob = on ? 33 : 11;
            SettingsBox(item.hDC, {SettingsPx(knob),SettingsPx(7),SettingsPx(knob+16),SettingsPx(23)},
                        RGB(255,255,255), RGB(255,255,255), 16);
        }
        else
        {
            SettingsBox(item.hDC, item.rcItem, fill, primary ? p.accent : tab ? fill : active ? p.accent : p.border, 10);
            wchar_t text[96]{};
            // The first segment keeps its label even when a different theme is selected.
            if (id == kSettingTheme) wcscpy_s(text, L"跟随系统");
            else GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
            RECT rect = item.rcItem;
            if (dropdown) { rect.left += SettingsPx(12); rect.right -= SettingsPx(30); }
            const auto oldFont = SelectObject(item.hDC, settingsBodyFont_);
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, !enabled ? p.muted : primary ?
                         (settingsWindowDark_ ? RGB(24,25,31) : RGB(255,255,255)) : active && !tab ? p.accent : p.primary);
            ::DrawTextW(item.hDC, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (dropdown ? DT_LEFT : DT_CENTER));
            SelectObject(item.hDC, oldFont);
            if (dropdown)
            {
                const int x = item.rcItem.right - SettingsPx(18);
                const int y = (item.rcItem.top + item.rcItem.bottom) / 2;
                const auto pen = CreatePen(PS_SOLID, std::max(1, SettingsPx(1)), p.secondary);
                const auto oldPen = SelectObject(item.hDC, pen);
                MoveToEx(item.hDC, x - SettingsPx(4), y - SettingsPx(2), nullptr);
                LineTo(item.hDC, x, y + SettingsPx(2));
                LineTo(item.hDC, x + SettingsPx(4), y - SettingsPx(2));
                SelectObject(item.hDC, oldPen);
                DeleteObject(pen);
            }
        }
        if (item.itemState & ODS_FOCUS)
        {
            RECT focus = item.rcItem;
            InflateRect(&focus, -SettingsPx(3), -SettingsPx(3));
            DrawFocusRect(item.hDC, &focus);
        }
        return TRUE;
    }

    void TogglePopup()
    {
        if (IsWindowVisible(window_))
        {
            HidePopup(HideReason::Tray);
            return;
        }

        ShowPopup(false);
    }

    void ShowPopup(bool preferCursorMonitor)
    {
        KillTimer(window_, kOutsideClickTimer);
        if (IsWindowVisible(window_))
        {
            PositionNearTray(preferCursorMonitor);
            SetForegroundWindow(window_);
            SetFocus(window_);
            return;
        }

        pickerMode_ = DatePickerMode::Closed;
        const auto today = Today();
        displayYear_ = today.year;
        displayMonth_ = today.month;
        selected_ = today;
        ResetContinuousScroll();
        agendaScroll_ = 0;
        hoveredCell_ = -1;
        RequestSystemCalendarMonth(displayYear_, displayMonth_);
        scrollAnimating_ = false;
        slideAnimating_ = false;
        slidePos_ = 0.0f;
        slideTarget_ = 0.0f;
        transition_ = 0.0f;
        SetTimer(window_, kAnimationTimer, 16, nullptr);
        PositionNearTray(preferCursorMonitor);
        ShowWindow(window_, SW_SHOW);
        SetTimer(window_, kClockTimer, 50, nullptr);
        StartWeatherRefreshIfStale();
        UpdateAgenda();
        SetForegroundWindow(window_);
        SetFocus(window_);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void HidePopup(HideReason reason)
    {
        dateDetailsOpen_ = false;
        toolsOpen_ = false;
        if (IsWindow(toolsWindow_)) ShowWindow(toolsWindow_, SW_HIDE);
        if (IsWindow(toolInput_)) ShowWindow(toolInput_, SW_HIDE);
        if (IsWindow(toolTimeInput_)) ShowWindow(toolTimeInput_, SW_HIDE);
        KillTimer(window_, kClockTimer);
        weatherPanelOpen_ = false;
        if (IsWindow(weatherWindow_)) ShowWindow(weatherWindow_, SW_HIDE);
        pickerMode_ = DatePickerMode::Closed;
        SetPropW(window_, L"WinCal.LastHideReason", reinterpret_cast<HANDLE>(reason));
        KillTimer(window_, kAnimationTimer);
        continuousAnimating_ = false;
        scrollAnimating_ = false;
        slideAnimating_ = false;
        slidePos_ = 0.0f;
        slideTarget_ = 0.0f;
        KillTimer(window_, kOutsideClickTimer);
        if (IsWindow(agendaWindow_)) ShowWindow(agendaWindow_, SW_HIDE);
        ShowWindow(window_, SW_HIDE);
        CloseDetailWindow();
        hoveredCell_ = -1;
        DiscardDeviceResources();
    }

    void PositionNearTray(bool preferCursorMonitor)
    {
        HMONITOR monitor{};
        POINT cursor{};
        if (preferCursorMonitor && GetCursorPos(&cursor))
        {
            monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        }
        else
        {
            if (GetCursorPos(&cursor))
                monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
            else
                monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        }

        MONITORINFO info{sizeof(info)};
        if (!monitor || !GetMonitorInfoW(monitor, &info))
            return;

        const auto taskbarForMonitor = [monitor]() -> HWND
        {
            const HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
            if (primary && MonitorFromWindow(primary, MONITOR_DEFAULTTONULL) == monitor)
                return primary;

            HWND secondary{};
            while ((secondary = FindWindowExW(
                        nullptr, secondary, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr)
            {
                if (MonitorFromWindow(secondary, MONITOR_DEFAULTTONULL) == monitor)
                    return secondary;
            }
            return nullptr;
        }();

        RECT clockRect{};
        bool hasClockRect{};
        for (const auto& entry : taskbarClockBounds_)
        {
            const auto& bounds = entry.second;
            if (MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST) == monitor)
            {
                clockRect = bounds;
                hasClockRect = true;
                break;
            }
        }

        TaskbarEdge edge = TaskbarEdge::Unknown;
        RECT taskbarRect{};
        if (taskbarForMonitor && GetWindowRect(taskbarForMonitor, &taskbarRect))
        {
            const int taskbarWidth = taskbarRect.right - taskbarRect.left;
            const int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
            if (taskbarWidth >= taskbarHeight)
            {
                const int topDistance = std::abs(taskbarRect.top - info.rcMonitor.top);
                const int bottomDistance = std::abs(info.rcMonitor.bottom - taskbarRect.bottom);
                edge = topDistance <= bottomDistance ? TaskbarEdge::Top : TaskbarEdge::Bottom;
            }
            else
            {
                const int leftDistance = std::abs(taskbarRect.left - info.rcMonitor.left);
                const int rightDistance = std::abs(info.rcMonitor.right - taskbarRect.right);
                edge = leftDistance <= rightDistance ? TaskbarEdge::Left : TaskbarEdge::Right;
            }
        }
        else
        {
            const std::array insets{
                std::pair{TaskbarEdge::Left, info.rcWork.left - info.rcMonitor.left},
                std::pair{TaskbarEdge::Top, info.rcWork.top - info.rcMonitor.top},
                std::pair{TaskbarEdge::Right, info.rcMonitor.right - info.rcWork.right},
                std::pair{TaskbarEdge::Bottom, info.rcMonitor.bottom - info.rcWork.bottom},
            };
            const auto largest = std::max_element(
                insets.begin(), insets.end(),
                [](const auto& left, const auto& right) { return left.second < right.second; });
            if (largest != insets.end() && largest->second > 0)
                edge = largest->first;
        }

        const UINT taskbarDpi = taskbarForMonitor ? GetDpiForWindow(taskbarForMonitor) : 0;
        const UINT targetDpi = taskbarDpi ? taskbarDpi : (dpi_ ? dpi_ : 96);
        const int width = MulDiv(kLogicalWidth, targetDpi, 96);
        const int height = MulDiv(kLogicalHeight, targetDpi, 96);
        const int endMargin = MulDiv(12, targetDpi, 96);

        int x{};
        int y{};
        switch (edge)
        {
        case TaskbarEdge::Top:
            x = hasClockRect ? clockRect.right - width : info.rcWork.right - width - endMargin;
            y = taskbarForMonitor ? taskbarRect.bottom : info.rcWork.top;
            break;
        case TaskbarEdge::Left:
            x = taskbarForMonitor ? taskbarRect.right : info.rcWork.left;
            y = hasClockRect ? clockRect.bottom - height : info.rcWork.bottom - height - endMargin;
            break;
        case TaskbarEdge::Right:
            x = taskbarForMonitor ? taskbarRect.left - width : info.rcWork.right - width;
            y = hasClockRect ? clockRect.bottom - height : info.rcWork.bottom - height - endMargin;
            break;
        case TaskbarEdge::Bottom:
        case TaskbarEdge::Unknown:
            x = hasClockRect ? clockRect.right - width : info.rcWork.right - width - endMargin;
            y = taskbarForMonitor ? taskbarRect.top - height : info.rcWork.bottom - height;
            break;
        }

        const int maximumX = std::max(info.rcWork.left, info.rcWork.right - width);
        const int maximumY = std::max(info.rcWork.top, info.rcWork.bottom - height);
        x = std::clamp(x, static_cast<int>(info.rcWork.left), maximumX);
        y = std::clamp(y, static_cast<int>(info.rcWork.top), maximumY);

        SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    static LRESULT CALLBACK MouseHookProc(int code, WPARAM message, LPARAM data)
    {
        if (code >= HC_ACTION && activeInstance_)
        {
            auto& app = *activeInstance_;
            const auto& mouse = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            if (message == WM_LBUTTONUP && app.taskbarClockLeftButtonPending_)
            {
                app.taskbarClockLeftButtonPending_ = false;
                return 1;
            }
            if (message == WM_RBUTTONUP && app.taskbarClockRightButtonPending_)
            {
                app.taskbarClockRightButtonPending_ = false;
                return 1;
            }
            if (!app.settingsLivePreview_ && (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN))
            {
                if (app.IsCachedTaskbarClockPoint(mouse.pt))
                {
                    if (message == WM_LBUTTONDOWN)
                    {
                        app.taskbarClockLeftButtonPending_ = true;
                        PostMessageW(app.window_, kConfirmedClockClickMessage,
                            static_cast<WPARAM>(mouse.pt.x), static_cast<LPARAM>(mouse.pt.y));
                    }
                    else
                    {
                        app.taskbarClockRightButtonPending_ = true;
                        PostMessageW(app.window_, kTaskbarClockContextMenuMessage, 0, 0);
                    }
                    return 1;
                }
                PostMessageW(app.window_, kGlobalMouseMessage, message, MAKELPARAM(mouse.pt.x, mouse.pt.y));
            }
        }
        return CallNextHookEx(nullptr, code, message, data);
    }

    void StartGlobalMouseMonitor()
    {
        mouseHookThread_ = std::jthread([this](std::stop_token stop)
        {
            MSG message{};
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            mouseHookThreadId_ = GetCurrentThreadId();
            mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, instance_, 0);
            while (!stop.stop_requested() && GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
            mouseHook_ = nullptr;
        });
    }

    void RefreshTaskbarClockBounds()
    {
        {
            const std::lock_guard lock(clockBoundsMutex_);
            clockBoundsRequested_ = true;
        }
        if (clockBoundsThread_.joinable())
        {
            clockBoundsWake_.notify_all();
            return;
        }
        clockBoundsThread_ = std::jthread([this](std::stop_token stop)
        {
            if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
            std::unique_lock lock(clockBoundsMutex_);
            while (!stop.stop_requested())
            {
                clockBoundsRequested_ = false;
                lock.unlock();
                std::vector<std::pair<HWND, RECT>> discovered;
                const auto append = [&](HWND taskbar)
                {
                    if (!taskbar) return;
                    for (const auto& bounds : wincal::TaskbarClockBounds(taskbar))
                        if (bounds.right > bounds.left && bounds.bottom > bounds.top)
                            discovered.emplace_back(taskbar, bounds);
                };
                append(FindWindowW(L"Shell_TrayWnd", nullptr));
                HWND secondary{};
                while (!stop.stop_requested() && (secondary = FindWindowExW(
                    nullptr, secondary, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr)
                    append(secondary);
                lock.lock();
                if (stop.stop_requested()) break;
                pendingClockBounds_ = std::move(discovered);
                PostMessageW(window_, kClockBoundsUpdatedMessage, 0, 0);
                clockBoundsWake_.wait_for(lock, stop, std::chrono::seconds(2),
                    [this] { return clockBoundsRequested_; });
            }
            lock.unlock();
            CoUninitialize();
        });
    }

    void ApplyTaskbarClockBounds()
    {
        {
            const std::lock_guard lock(clockBoundsMutex_);
            taskbarClockBounds_ = pendingClockBounds_;
        }
        // Publish exact clock rectangles for the injected tooltip filter. This
        // avoids running UI Automation on Explorer's window-show path.
        SetPropW(window_, L"WinCal.ClockRectCount", nullptr);
        for (size_t i = 0; i < taskbarClockBounds_.size(); ++i)
        {
            const auto& rect = taskbarClockBounds_[i].second;
            const LONG coordinates[] = {rect.left, rect.top, rect.right, rect.bottom};
            for (size_t part = 0; part < 4; ++part)
            {
                const auto name = L"WinCal.ClockRect." + std::to_wstring(i) + L"." + std::to_wstring(part);
                SetPropW(window_, name.c_str(), reinterpret_cast<HANDLE>(static_cast<INT_PTR>(coordinates[part])));
            }
        }
        SetPropW(window_, L"WinCal.ClockRectCount", reinterpret_cast<HANDLE>(taskbarClockBounds_.size()));
    }

    bool IsCachedTaskbarClockPoint(POINT point) const
    {
        const std::lock_guard lock(clockBoundsMutex_);
        return std::any_of(taskbarClockBounds_.begin(), taskbarClockBounds_.end(),
            [point](const auto& entry) { return PtInRect(&entry.second, point) != FALSE; });
    }

    void StopGlobalMouseMonitor()
    {
        if (mouseHookThread_.joinable())
        {
            mouseHookThread_.request_stop();
            if (const DWORD id = mouseHookThreadId_.load()) PostThreadMessageW(id, WM_QUIT, 0, 0);
            mouseHookThread_.join();
            mouseHookThreadId_ = 0;
        }
    }

    bool HandleGlobalMouse(WPARAM message, const MSLLHOOKSTRUCT& mouse)
    {
        if (settingsLivePreview_) return false;
        if ((message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) && IsWindowVisible(contextMenuWindow_))
        {
            RECT menuRect{};
            if (GetWindowRect(contextMenuWindow_, &menuRect) && !PtInRect(&menuRect, mouse.pt))
                PostMessageW(window_, kDismissContextMenuMessage, 0, 0);
        }
        if (message != WM_LBUTTONDOWN)
            return false;
        if (wincal::TaskbarAtPoint(mouse.pt))
        {
            PostMessageW(window_, kTaskbarClockClickMessage,
                         static_cast<WPARAM>(mouse.pt.x), static_cast<LPARAM>(mouse.pt.y));
            return false;
        }
        if (!IsWindowVisible(window_))
            return false;

        RECT popupRect{};
        if (GetWindowRect(window_, &popupRect) && PtInRect(&popupRect, mouse.pt))
            return false;
        RECT toolsRect{};
        if (IsWindowVisible(toolsWindow_) && GetWindowRect(toolsWindow_, &toolsRect) &&
            PtInRect(&toolsRect, mouse.pt)) return false;
        if (IsWindowVisible(toolsWindow_))
        {
            RECT dropdown{};
            const HWND calendar = IsWindow(fortuneInput_) ? DateTime_GetMonthCal(fortuneInput_) : nullptr;
            if (IsWindowVisible(calendar) && GetWindowRect(calendar, &dropdown) && PtInRect(&dropdown, mouse.pt)) return false;
            COMBOBOXINFO combo{sizeof(combo)};
            for (const HWND choice : {toolInput_, fortuneGenderInput_})
                if (GetComboBoxInfo(choice, &combo) && IsWindowVisible(combo.hwndList) &&
                    GetWindowRect(combo.hwndList, &dropdown) && PtInRect(&dropdown, mouse.pt)) return false;
        }
        RECT weatherRect{};
        if (IsWindowVisible(weatherWindow_) && GetWindowRect(weatherWindow_, &weatherRect) &&
            PtInRect(&weatherRect, mouse.pt)) return false;
        RECT agendaRect{};
        if (IsWindowVisible(agendaWindow_) && GetWindowRect(agendaWindow_, &agendaRect) &&
            PtInRect(&agendaRect, mouse.pt)) return false;
        RECT detailRect{};
        if (IsWindow(detailWindow_) && GetWindowRect(detailWindow_, &detailRect) &&
            PtInRect(&detailRect, mouse.pt))
            return false;
        PostMessageW(window_, kScheduleOutsideHideMessage, 0, 0);
        return false;
    }

    static void CALLBACK WinEventProc(
        HWINEVENTHOOK,
        DWORD event,
        HWND eventWindow,
        LONG objectId,
        LONG,
        DWORD,
        DWORD)
    {
        if (activeInstance_)
            activeInstance_->HandleShellWinEvent(event, eventWindow, objectId);
    }

    void StartSystemCalendarInterceptor()
    {
        activeInstance_ = this;
        calendarHook_ = SetWinEventHook(
            EVENT_OBJECT_CREATE,
            kEventObjectUncloak,
            nullptr,
            WinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }

    void StopSystemCalendarInterceptor()
    {
        if (calendarHook_)
        {
            UnhookWinEvent(calendarHook_);
            calendarHook_ = nullptr;
        }

        const std::lock_guard lock(hiddenCalendarWindowsMutex_);
        for (const auto hiddenWindow : hiddenCalendarWindows_)
        {
            if (IsWindow(hiddenWindow))
                ShowWindow(hiddenWindow, SW_SHOW);
        }
        hiddenCalendarWindows_.clear();
        activeInstance_ = nullptr;
    }

    static bool IsCalendarShellProcess(HWND eventWindow)
    {
        DWORD processId{};
        GetWindowThreadProcessId(eventWindow, &processId);
        if (!processId)
            return false;

        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!process)
            return false;

        wchar_t path[MAX_PATH]{};
        DWORD length = ARRAYSIZE(path);
        const bool queried = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
        CloseHandle(process);
        if (!queried)
            return false;

        const wchar_t* fileName = PathFindFileNameW(path);
        return _wcsicmp(fileName, L"ShellExperienceHost.exe") == 0 ||
               _wcsicmp(fileName, L"ShellHost.exe") == 0;
    }

    static bool IsFlyoutAlignedWithTaskbar(HWND eventWindow)
    {
        RECT flyout{};
        if (!GetWindowRect(eventWindow, &flyout))
            return false;

        const int width = flyout.right - flyout.left;
        const int height = flyout.bottom - flyout.top;
        if (width <= 200 || height <= 100)
            return false;

        const HMONITOR monitor = MonitorFromWindow(eventWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)};
        if (!monitor || !GetMonitorInfoW(monitor, &info))
            return false;

        const int leftInset = info.rcWork.left - info.rcMonitor.left;
        const int topInset = info.rcWork.top - info.rcMonitor.top;
        const int rightInset = info.rcMonitor.right - info.rcWork.right;
        const int bottomInset = info.rcMonitor.bottom - info.rcWork.bottom;
        const int tolerance = MulDiv(50, GetDpiForWindow(eventWindow), 96);

        const bool leftAligned = std::abs(flyout.left - info.rcWork.left) < tolerance;
        const bool topAligned = std::abs(flyout.top - info.rcWork.top) < tolerance;
        const bool rightAligned = std::abs(flyout.right - info.rcWork.right) < tolerance;
        const bool bottomAligned = std::abs(flyout.bottom - info.rcWork.bottom) < tolerance;

        if (topInset >= leftInset && topInset >= rightInset && topInset >= bottomInset && topInset > 0)
            return rightAligned && topAligned;
        if (leftInset >= topInset && leftInset >= rightInset && leftInset >= bottomInset && leftInset > 0)
            return leftAligned && bottomAligned;
        if (rightInset >= leftInset && rightInset >= topInset && rightInset >= bottomInset && rightInset > 0)
            return rightAligned && bottomAligned;
        return rightAligned && bottomAligned;
    }

    void HandleShellWinEvent(DWORD event, HWND eventWindow, LONG objectId)
    {
        // Only suppress the shell flyout produced by a confirmed clock click.
        // Other tray buttons can create similar right-aligned shell windows.
        if (!lastClockClickTick_ || GetTickCount64() - lastClockClickTick_ > 1500)
            return;
        if (objectId != OBJID_WINDOW || !eventWindow)
            return;
        if (event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_SHOW &&
            event != EVENT_OBJECT_STATECHANGE && event != EVENT_OBJECT_NAMECHANGE &&
            event != kEventObjectUncloak)
            return;
        if (!IsCalendarShellProcess(eventWindow))
            return;

        wchar_t className[128]{};
        GetClassNameW(eventWindow, className, ARRAYSIZE(className));
        constexpr wchar_t coreWindowClass[] = L"Windows.UI.Core.CoreWindow";
        if (_wcsnicmp(className, coreWindowClass, wcslen(coreWindowClass)) != 0)
            return;
        if (!IsFlyoutAlignedWithTaskbar(eventWindow))
            return;

        ShowWindow(eventWindow, SW_HIDE);
        {
            const std::lock_guard lock(hiddenCalendarWindowsMutex_);
            if (std::find(hiddenCalendarWindows_.begin(), hiddenCalendarWindows_.end(), eventWindow) ==
                hiddenCalendarWindows_.end())
            {
                hiddenCalendarWindows_.push_back(eventWindow);
            }
        }

        // The posted clock-click message already toggles WinCal exactly once.
    }

    LRESULT HandleContextMenuMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        const int padding = MulDiv(4, GetDpiForWindow(window), 96);
        const int dpi = GetDpiForWindow(window);
        const int rowHeight = MulDiv(32, dpi, 96);
        const int width = MulDiv(196, dpi, 96);
        const int top = MulDiv(8, dpi, 96);
        const int separatorGap = MulDiv(16, dpi, 96);
        const int height = top + rowHeight * 2 + separatorGap;
        const int secondRowTop = top + rowHeight + separatorGap;
        const int separatorY = top + rowHeight + separatorGap / 2;
        switch (message)
        {
        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            const int y = GET_Y_LPARAM(lParam);
            const int item = y >= top && y < top + rowHeight ? 0 :
                y >= secondRowTop && y < secondRowTop + rowHeight ? 1 : -1;
            if (item != contextMenuHover_)
            {
                contextMenuHover_ = item;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            contextMenuHover_ = -1;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
        {
            const int y = GET_Y_LPARAM(lParam);
            const int item = y >= top && y < top + rowHeight ? kMenuSettings :
                y >= secondRowTop && y < secondRowTop + rowHeight ? kMenuExit : 0;
            ShowWindow(window, SW_HIDE);
            if (item == kMenuSettings) ShowSettingsWindow();
            else if (item == kMenuExit) RequestExit();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { ShowWindow(window, SW_HIDE); return 0; }
            if (wParam == VK_RETURN)
            {
                ShowWindow(window, SW_HIDE);
                if (contextMenuHover_ == 0) ShowSettingsWindow();
                else if (contextMenuHover_ == 1) RequestExit();
                return 0;
            }
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT bounds{0, 0, width, height};
            HBRUSH background = CreateSolidBrush(RGB(43, 43, 43));
            FillRect(dc, &bounds, background);
            DeleteObject(background);
            const auto drawRow = [&](int index, const wchar_t* text)
            {
                const int rowTop = index == 0 ? top : secondRowTop;
                RECT row{padding, rowTop, width - padding, rowTop + rowHeight};
                if (contextMenuHover_ == index)
                {
                    HBRUSH hover = CreateSolidBrush(RGB(61, 61, 61));
                    HGDIOBJ oldBrush = SelectObject(dc, hover);
                    HGDIOBJ oldOutline = SelectObject(dc, GetStockObject(NULL_PEN));
                    const int radius = MulDiv(4, dpi, 96);
                    RoundRect(dc, row.left, row.top, row.right, row.bottom, radius * 2, radius * 2);
                    SelectObject(dc, oldOutline);
                    SelectObject(dc, oldBrush);
                    DeleteObject(hover);
                }
                SetTextColor(dc, RGB(255, 255, 255));
                SetBkMode(dc, TRANSPARENT);
                NONCLIENTMETRICSW metrics{sizeof(metrics)};
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
                HFONT font = CreateFontIndirectW(&metrics.lfMenuFont);
                HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
                RECT textRect = row;
                textRect.left += MulDiv(12, GetDpiForWindow(window), 96);
                ::DrawTextW(dc, text, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                if (oldFont) SelectObject(dc, oldFont);
                if (font) DeleteObject(font);
            };
            drawRow(0, L"设置");
            const int divider = separatorY;
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(72, 72, 72));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, padding + MulDiv(10, GetDpiForWindow(window), 96), divider, nullptr);
            LineTo(dc, width - padding - MulDiv(10, GetDpiForWindow(window), 96), divider);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            drawRow(1, L"退出");
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCDESTROY:
            if (contextMenuWindow_ == window) contextMenuWindow_ = nullptr;
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void ShowContextMenu()
    {
        if (IsWindowVisible(window_)) HidePopup(HideReason::ContextMenu);
        POINT point{};
        GetCursorPos(&point);
        RECT anchor{};
        RECT taskbarRect{};
        int width = MulDiv(196, GetDpiForWindow(window_), 96);
        int height = MulDiv(88, GetDpiForWindow(window_), 96);
        for (const auto& entry : taskbarClockBounds_)
        {
            if (!PtInRect(&entry.second, point)) continue;
            anchor = entry.second;
            MONITORINFO monitor{sizeof(monitor)};
            if (!GetWindowRect(entry.first, &taskbarRect) ||
                !GetMonitorInfoW(MonitorFromWindow(entry.first, MONITOR_DEFAULTTONEAREST), &monitor)) break;
            const bool horizontal = taskbarRect.right - taskbarRect.left >= taskbarRect.bottom - taskbarRect.top;
            if (horizontal) point.x = anchor.right - width;
            else point.y = anchor.top;
            if (horizontal) point.y = taskbarRect.top <= monitor.rcMonitor.top ? taskbarRect.bottom : taskbarRect.top - height;
            else point.x = taskbarRect.left <= monitor.rcMonitor.left ? taskbarRect.right : taskbarRect.left - width;
            point.x = std::clamp(point.x, monitor.rcMonitor.left, monitor.rcMonitor.right - width);
            point.y = std::clamp(point.y, monitor.rcMonitor.top, monitor.rcMonitor.bottom - height);
            break;
        }
        if (IsWindow(contextMenuWindow_)) DestroyWindow(contextMenuWindow_);
        contextMenuHover_ = -1;
        contextMenuWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kContextMenuWindowClass, L"WinCal context menu", WS_POPUP,
            point.x, point.y, width, height, nullptr, nullptr, instance_, this);
        if (contextMenuWindow_)
        {
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(contextMenuWindow_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            const COLORREF border = RGB(64, 64, 64);
            DwmSetWindowAttribute(contextMenuWindow_, DWMWA_BORDER_COLOR, &border, sizeof(border));
            DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
            DwmSetWindowAttribute(contextMenuWindow_, DWMWA_WINDOW_CORNER_PREFERENCE,
                &preference, sizeof(preference));
            ShowWindow(contextMenuWindow_, SW_SHOWNOACTIVATE);
            UpdateWindow(contextMenuWindow_);
        }
    }

    void ShowSettingsWindow()
    {
        if (IsWindow(settingsWindow_))
        {
            ShowWindow(settingsWindow_, SW_RESTORE);
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            PositionLivePreview();
            SetForegroundWindow(settingsWindow_);
            return;
        }
        settingsDraft_ = wincal::SettingsStore::Load();
        settingsBeforeEdit_ = settings_;
        settingsSaveAccepted_ = false;
        settingsDpi_ = GetDpiForWindow(window_);
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        RECT bounds = SettingsRect(0, 0, kSettingsLogicalWidth, kSettingsLogicalHeight);
        AdjustWindowRectExForDpi(&bounds, style, FALSE, WS_EX_APPWINDOW, settingsDpi_);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = std::min(static_cast<int>(bounds.right - bounds.left),
                                   static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
        const int height = std::min(static_cast<int>(bounds.bottom - bounds.top),
                                    static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
        const int pairWidth = width + SettingsPx(kLogicalWidth + 20);
        HWND window = CreateWindowExW(
            WS_EX_APPWINDOW,
            kSettingsWindowClass,
            L"WinCal 设置",
            style,
            monitor.rcWork.left + std::max(0L, (monitor.rcWork.right - monitor.rcWork.left - pairWidth) / 2),
            monitor.rcWork.top + std::max(0L, (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2),
            width, height,
            window_, nullptr, instance_, this);
        if (window)
        {
            settingsLivePreview_ = true;
            ShowPopup(false);
            ShowWindow(window, SW_SHOW);
            ApplyLiveSettingsPreview(window);
            PositionLivePreview();
            SetForegroundWindow(window);
            UpdateWindow(window);
        }
    }

    bool ApplyAutoStartup(bool enable) const
    {
        HKEY key{};
        const LSTATUS openResult = RegCreateKeyExW(
            HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (openResult != ERROR_SUCCESS)
            return false;

        LSTATUS result{};
        if (enable)
        {
            wchar_t executable[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
            if (length == 0 || length >= ARRAYSIZE(executable))
            {
                RegCloseKey(key);
                return false;
            }
            const std::wstring command = L"\"" + std::wstring(executable) + L"\"";
            result = RegSetValueExW(
                key, L"miniCal", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            result = RegDeleteValueW(key, L"miniCal");
            if (result == ERROR_FILE_NOT_FOUND)
                result = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }

    void StartConfigSync(HWND window, int action)
    {
        if (syncBusy_) return;
        const wincal::SyncCredentials credentials{ReadControlText(GetDlgItem(window, kSyncAccount)),
            ReadControlText(GetDlgItem(window, kSyncPassword))};
        if (credentials.account.empty() || credentials.password.empty())
        {
            syncStatus_ = L"请填写账号和第三方应用密码。";
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        if (action == 1 && MessageBoxW(window, L"上传已保存的配置将覆盖云端配置，是否继续？",
            L"配置同步", MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
        if (action == 2 && MessageBoxW(window, L"下载将替换设置页中的选项，保存后生效。是否继续？",
            L"配置同步", MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
        syncBusy_ = true;
        syncApplyDownload_ = true;
        syncStatus_ = L"正在连接坚果云…";
        for (int id : {kSyncTest, kSyncUpload, kSyncDownload}) EnableWindow(GetDlgItem(window, id), FALSE);
        InvalidateRect(window, nullptr, FALSE);
        const auto upload = action == 1 ? wincal::SettingsStore::Serialize(wincal::SettingsStore::Load()) : std::string{};
        syncThread_ = std::jthread([this, action, credentials, upload]
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            wincal::SyncResult result;
            try
            {
                result = wincal::SyncNutstore(action, credentials, upload);
                if (result.success && !wincal::SaveSyncCredentials(credentials))
                    result.message += L" 凭据保存失败，下次需重新填写。";
            }
            catch (...) { result.message = L"同步失败，本地配置未修改。"; }
            { const std::lock_guard lock(syncMutex_); syncResult_ = std::move(result); }
            PostMessageW(window_, kSyncCompletedMessage, action, 0);
            CoUninitialize();
        });
    }

    void SaveSettingsFromControls(HWND window)
    {
        const int theme = SettingSelection(window, kSettingTheme);
        settingsDraft_.themeMode = theme == 1 ? L"Light" : theme == 2 ? L"Dark" : L"FollowSystem";
        BOOL valid{};
        const int offset = static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, &valid, TRUE));
        settingsDraft_.fontSizeOffset = std::clamp(valid ? offset : 0, -2, 10);
        settingsDraft_.autoStartup = SettingSelection(window, kSettingAutoStartup) != 0;
        const int source = SettingSelection(window, kSettingDataSource);
        settingsDraft_.dataSource = source == 1 ? L"IcsUrl" : source == 2 ? L"Both" : L"SystemCalendar";
        settingsDraft_.icsUrls = LinesFromText(ReadControlText(GetDlgItem(window, kSettingIcsUrls)));
        settingsDraft_.icsAliases.resize(settingsDraft_.icsUrls.size());
        constexpr std::array<int, 5> refreshValues{10, 30, 60, 120, 1440};
        const int refresh = SettingSelection(window, kSettingRefresh);
        settingsDraft_.icsRefreshMinutes = refreshValues[std::clamp(refresh, 0, 4)];
        settingsDraft_.weekStartDay = SettingSelection(window, kSettingWeekStart) == 1 ? L"Monday" : L"Sunday";
        settingsDraft_.monthPaging = SettingSelection(window, kSettingMonthPaging) != 0;
        settingsDraft_.weatherAutoLocation = SettingSelection(window, kSettingWeatherLocation) == 0;
        settingsDraft_.weatherUseWttr = SettingSelection(window, kSettingWeatherProvider) == 1;
        settingsDraft_.taskbarStatus = SettingSelection(window, kSettingTaskbarStatus) != 0;
        settingsDraft_.taskbarFormat = wincal::NormalizeTaskbarFormat(ReadControlText(GetDlgItem(window, kSettingTaskbarFormat)));
        if (settingsDraft_.taskbarFormat.empty() ||
            std::count(settingsDraft_.taskbarFormat.begin(), settingsDraft_.taskbarFormat.end(), L'\n') > 1 ||
            (settingsDraft_.taskbarFormat.find(L"\\n") != std::wstring::npos &&
             settingsDraft_.taskbarFormat.find(L"\\n", settingsDraft_.taskbarFormat.find(L"\\n") + 2) != std::wstring::npos))
        {
            MessageBoxW(window, L"任务栏格式不能为空，最多支持两行。", L"WinCal", MB_OK | MB_ICONWARNING);
            return;
        }
        wincal::StartupSettingSnapshot startupBeforeSave;
        if (!startupBeforeSave.Capture() || !ApplyAutoStartup(settingsDraft_.autoStartup))
        {
            MessageBoxW(window, L"更新开机启动项失败。", L"WinCal", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!wincal::SettingsStore::Save(settingsDraft_))
        {
            const bool restored = startupBeforeSave.Restore();
            MessageBoxW(window, restored ? L"保存设置失败，原设置和开机启动项已保留。" :
                L"保存设置失败，且无法恢复开机启动项，请检查开机启动设置。", L"WinCal", MB_OK | MB_ICONWARNING);
            return;
        }
        settingsSaveAccepted_ = true;
        PostMessageW(window_, kSettingsChangedMessage, 0, 0);
        DestroyWindow(window);
    }

    LRESULT HandleSettingsMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            CreateSettingsControls(window);
            ApplySettingsWindowAppearance(window);
            return 0;
        case WM_DPICHANGED:
        {
            settingsDpi_ = HIWORD(wParam);
            DeleteObject(settingsTitleFont_);
            DeleteObject(settingsBodyFont_);
            DeleteObject(settingsLabelFont_);
            DeleteObject(settingsCaptionFont_);
            settingsTitleFont_ = settingsBodyFont_ = settingsLabelFont_ = settingsCaptionFont_ = nullptr;
            EnsureSettingsFonts(window);
            const auto* bounds = reinterpret_cast<RECT*>(lParam);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromRect(bounds, MONITOR_DEFAULTTONEAREST), &monitor);
            const LONG width = std::min(bounds->right - bounds->left, monitor.rcWork.right - monitor.rcWork.left);
            const LONG height = std::min(bounds->bottom - bounds->top, monitor.rcWork.bottom - monitor.rcWork.top);
            SetWindowPos(window, nullptr,
                         std::clamp(bounds->left, monitor.rcWork.left, monitor.rcWork.right - width),
                         std::clamp(bounds->top, monitor.rcWork.top, monitor.rcWork.bottom - height),
                         width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            for (const auto& control : settingsControls_)
            {
                RECT rect = SettingsRect(control.x, control.y, control.width, control.height);
                SetWindowPos(control.window, nullptr, rect.left, rect.top, rect.right - rect.left,
                             rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
                SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(settingsBodyFont_), FALSE);
            }
            UpdateSettingsFontControls(window);
            LayoutSettingsViewport(window);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return 0;
        }
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) LayoutSettingsViewport(window);
            return 0;
        case WM_MOVE:
            if (settingsLivePreview_) PositionLivePreview();
            return 0;
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        {
            if (message == WM_MOUSEWHEEL)
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(window, &point);
                const int navLeft = SettingsPx(28) - settingsScrollX_;
                const int navTop = SettingsPx(136) - settingsScrollY_;
                const int navRight = navLeft + SettingsPx(118);
                const int panelLeft = SettingsPx(162) - settingsScrollX_;
                const int panelRight = SettingsPx(652) - settingsScrollX_;
                const int panelBottom = SettingsPx(500) - settingsScrollY_;
                const bool overNavigation = point.x >= navLeft && point.x < navRight &&
                    point.y >= navTop && point.y < panelBottom;
                const bool overContent = point.x >= panelLeft && point.x < panelRight &&
                    point.y >= navTop && point.y < panelBottom;
                if (overNavigation || overContent)
                {
                    const int currentPage = settingsPage_ == 4 ? 2 : settingsPage_ == 5 ? 4 : settingsPage_;
                    const int step = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
                    const int next = std::clamp(currentPage + step, 0, 4);
                    SelectSettingsPage(window, next == 4 ? 5 : next);
                    return 0;
                }
            }
            const bool horizontal = message == WM_HSCROLL;
            const int bar = horizontal ? SB_HORZ : SB_VERT;
            SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
            GetScrollInfo(window, bar, &info);
            int position = info.nPos;
            if (message == WM_MOUSEWHEEL)
                position -= GET_WHEEL_DELTA_WPARAM(wParam) * SettingsPx(48) / WHEEL_DELTA;
            else
            {
                switch (LOWORD(wParam))
                {
                case SB_LINEUP: position -= SettingsPx(24); break;
                case SB_LINEDOWN: position += SettingsPx(24); break;
                case SB_PAGEUP: position -= info.nPage; break;
                case SB_PAGEDOWN: position += info.nPage; break;
                case SB_THUMBTRACK: position = info.nTrackPos; break;
                default: return 0;
                }
            }
            if (horizontal) settingsScrollX_ = position;
            else settingsScrollY_ = position;
            LayoutSettingsViewport(window);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintSettingsWindow(window);
            return 0;
        case WM_CTLCOLORSTATIC:
        {
            const auto palette = GetSettingsPalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), palette.secondary);
            SetBkColor(reinterpret_cast<HDC>(wParam), palette.input);
            return reinterpret_cast<LRESULT>(settingsInputBrush_);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            const auto palette = GetSettingsPalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), palette.primary);
            SetBkColor(reinterpret_cast<HDC>(wParam), palette.input);
            return reinterpret_cast<LRESULT>(settingsInputBrush_);
        }
        case WM_CTLCOLORBTN:
        {
            const auto palette = GetSettingsPalette();
            SetTextColor(reinterpret_cast<HDC>(wParam), palette.primary);
            SetBkColor(reinterpret_cast<HDC>(wParam), palette.card);
            return reinterpret_cast<LRESULT>(settingsCardBrush_);
        }
        case WM_DRAWITEM:
            if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                item && item->CtlType == ODT_BUTTON)
                return DrawSettingsButton(*item);
            break;
        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kSettingPageSync)
            { SelectSettingsPage(window, 5); return 0; }
            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) >= kSyncTest && LOWORD(wParam) <= kSyncDownload)
            { StartConfigSync(window, LOWORD(wParam) - kSyncTest); return 0; }
            if (LOWORD(wParam) == kSettingTaskbarFormat && HIWORD(wParam) == EN_CHANGE)
            { InvalidateRect(window, nullptr, FALSE); return 0; }
            if (LOWORD(wParam) == kSettingTaskbarFormatPage && HIWORD(wParam) == BN_CLICKED)
            { SelectSettingsPage(window, 4); return 0; }
            if (LOWORD(wParam) == kSettingFontOffset && HIWORD(wParam) == EN_CHANGE)
            {
                UpdateSettingsFontControls(window);
                return 0;
            }
            if (LOWORD(wParam) == kSettingFontOffset && HIWORD(wParam) == EN_KILLFOCUS)
            {
                BOOL valid{};
                const int value = static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, &valid, TRUE));
                SetDlgItemInt(window, kSettingFontOffset, static_cast<UINT>(std::clamp(valid ? value : 0, -2, 10)), TRUE);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED)
            {
                const int id = LOWORD(wParam);
                if (id >= kSettingPageAppearance && id <= kSettingPageWeather)
                {
                    SelectSettingsPage(window, id - kSettingPageAppearance);
                    return 0;
                }
                if (id == kSettingTheme || id == kSettingThemeLight || id == kSettingThemeDark)
                {
                    SetSettingSelection(window, kSettingTheme, id == kSettingTheme ? 0 : id == kSettingThemeLight ? 1 : 2);
                    ApplySettingsWindowAppearance(window);
                    return 0;
                }
                if (id == kSettingFontMinus || id == kSettingFontPlus)
                {
                    BOOL valid{};
                    const int value = static_cast<int>(GetDlgItemInt(window, kSettingFontOffset, &valid, TRUE));
                    SetDlgItemInt(window, kSettingFontOffset, static_cast<UINT>(
                        std::clamp((valid ? value : 0) + (id == kSettingFontPlus ? 1 : -1), -2, 10)), TRUE);
                    return 0;
                }
                if (id == kSettingAutoStartup || id == kSettingMonthPaging || id == kSettingTaskbarStatus)
                {
                    SetSettingSelection(window, id, !SettingSelection(window, id));
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                const auto options = SettingOptions(id);
                if (!options.empty())
                {
                    HMENU menu = CreatePopupMenu();
                    for (size_t i = 0; i < options.size(); ++i)
                        AppendMenuW(menu, MF_STRING | (SettingSelection(window, id) == static_cast<int>(i) ? MF_CHECKED : 0),
                                    i + 1, options[i]);
                    RECT anchor{};
                    GetWindowRect(GetDlgItem(window, id), &anchor);
                    const int result = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                       anchor.left, anchor.bottom, window, nullptr);
                    DestroyMenu(menu);
                    if (result)
                    {
                        SetSettingSelection(window, id, result - 1);
                        UpdateSettingsSourceControls(window);
                        ApplyLiveSettingsPreview(window);
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    return 0;
                }
            }
            if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == kSettingSave || LOWORD(wParam) == IDOK))
            {
                SaveSettingsFromControls(window);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == kSettingDefaults)
            {
                settingsDraft_ = {};
                PopulateSettingsControls(window);
                UpdateSettingsFontControls(window);
                UpdateSettingsSourceControls(window);
                ApplySettingsWindowAppearance(window);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == kSettingCancel || LOWORD(wParam) == IDCANCEL))
            {
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_NCDESTROY:
            FinishLiveSettingsPreview();
            syncApplyDownload_ = false;
            if (settingsTitleFont_) DeleteObject(settingsTitleFont_);
            if (settingsBodyFont_) DeleteObject(settingsBodyFont_);
            if (settingsLabelFont_) DeleteObject(settingsLabelFont_);
            if (settingsCaptionFont_) DeleteObject(settingsCaptionFont_);
            if (settingsInputBrush_) DeleteObject(settingsInputBrush_);
            if (settingsCardBrush_) DeleteObject(settingsCardBrush_);
            settingsTitleFont_ = nullptr;
            settingsBodyFont_ = nullptr;
            settingsLabelFont_ = nullptr;
            settingsCaptionFont_ = nullptr;
            settingsControls_.clear();
            settingsControls_.shrink_to_fit();
            settingsHotControl_ = nullptr;
            settingsScrollX_ = settingsScrollY_ = 0;
            settingsInputBrush_ = nullptr;
            settingsCardBrush_ = nullptr;
            settingsWindow_ = nullptr;
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void ResetContinuousScroll()
    {
        continuousAnchor_ = WeekRowStartDays(displayYear_, displayMonth_);
        continuousPosition_ = continuousTarget_ = 0.0;
        continuousAnimating_ = false;
        continuousInitialized_ = !settings_.monthPaging;
    }

    void ScrollContinuously(int wheelDelta)
    {
        if (!continuousInitialized_) ResetContinuousScroll();
        // Preserve high-resolution wheel deltas and fractional rows at rest.
        continuousTarget_ -= static_cast<double>(wheelDelta) / WHEEL_DELTA * 0.75;
        continuousAnimating_ = true;
        scrollAnimating_ = slideAnimating_ = false;
        transition_ = 1.0f;
        hoveredCell_ = -1;
        SetTimer(window_, kAnimationTimer, 16, nullptr);
    }

    Date ContinuousDateForCell(int index) const
    {
        const auto firstRow = static_cast<long long>(std::floor(continuousPosition_));
        return CivilFromDays(continuousAnchor_ + (firstRow + index / 7) * 7 + index % 7);
    }

    void AdvanceContinuousScroll()
    {
        continuousPosition_ += (continuousTarget_ - continuousPosition_) * 0.30;
        if (std::abs(continuousTarget_ - continuousPosition_) < 0.001)
        {
            continuousPosition_ = continuousTarget_;
            continuousAnimating_ = false;
            KillTimer(window_, kAnimationTimer);
        }
        // Keep offsets small while preserving the fractional position and target.
        const auto wholeRows = static_cast<long long>(std::floor(continuousPosition_));
        continuousAnchor_ += wholeRows * 7;
        continuousPosition_ -= wholeRows;
        continuousTarget_ -= wholeRows;
        const auto middle = CivilFromDays(continuousAnchor_ + 17);
        if (middle.year != displayYear_ || middle.month != displayMonth_)
        {
            displayYear_ = middle.year;
            displayMonth_ = middle.month;
            RequestSystemCalendarMonth(displayYear_, displayMonth_);
            UpdateAgenda();
        }
        PresentAgenda();
        hoveredCell_ = -1;
    }

    int HitTestNavigation(int x, int y) const
    {
        if (HitTestTools(x, y)) return -1;
        const float px = static_cast<float>(x) / Scale(1);
        const float py = static_cast<float>(y) / Scale(1) - kClockHeight;
        if (px >= 286 && px < 416 && py >= -kClockHeight + 8 && py < -8) return -6;
        if (py >= 18 && py < 66)
        {
            if (px >= 18 && px < NavigationTitleRight() + 28) return -2;
            if (px >= 296 && px < 330) return -3;
            if (px >= 332 && px < 366) return -4;
            if (px >= 372 && px < 414) return -5;
        }
        if (pickerMode_ != DatePickerMode::Closed)
        {
            for (int i = 0; i < 12; ++i)
            {
                const float left = 22.0f + i % 4 * 96.0f;
                const float top = 108.0f + i / 4 * 92.0f;
                if (px >= left && px < left + 90 && py >= top && py < top + 76) return i;
            }
        }
        return -1;
    }

    void OpenDatePicker()
    {
        pickerKeyboardFocus_ = false;
        if (pickerMode_ == DatePickerMode::Closed)
        {
            pickerMode_ = DatePickerMode::Months;
            pickerYear_ = displayYear_;
            pickerFocus_ = displayMonth_ - 1;
            continuousAnimating_ = scrollAnimating_ = slideAnimating_ = false;
            transition_ = 1.0f;
            KillTimer(window_, kAnimationTimer);
            CloseDetailWindow();
        }
        else if (pickerMode_ == DatePickerMode::Months)
        {
            pickerMode_ = DatePickerMode::Years;
            pickerFirstYear_ = std::clamp(pickerYear_ / 12 * 12, 1601, 9988);
            pickerFocus_ = pickerYear_ - pickerFirstYear_;
        }
        else
            pickerMode_ = DatePickerMode::Months;
        navigationHot_ = -1;
        PresentAgenda();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void JumpToMonth(int year, int month)
    {
        displayYear_ = std::clamp(year, 1601, 9999);
        displayMonth_ = month;
        selected_ = {displayYear_, month, 1};
        const auto today = Today();
        if (today.year == year && today.month == month) selected_ = today;
        pickerMode_ = DatePickerMode::Closed;
        navigationHot_ = -1;
        hoveredCell_ = -1;
        agendaScroll_ = 0;
        continuousAnimating_ = scrollAnimating_ = slideAnimating_ = false;
        slidePos_ = slideTarget_ = 0.0f;
        ResetContinuousScroll();
        RequestSystemCalendarMonth(displayYear_, displayMonth_);
        UpdateAgenda();
        BeginTransition();
    }

    void ChangePickerPage(int delta)
    {
        if (pickerMode_ == DatePickerMode::Months)
            pickerYear_ = std::clamp(pickerYear_ + delta, 1601, 9999);
        else
            pickerFirstYear_ = std::clamp(pickerFirstYear_ + delta * 12, 1601, 9988);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void SelectPickerCell(int cell)
    {
        if (pickerMode_ == DatePickerMode::Years)
        {
            pickerYear_ = pickerFirstYear_ + cell;
            pickerMode_ = DatePickerMode::Months;
            pickerFocus_ = displayMonth_ - 1;
            navigationHot_ = -1;
            InvalidateRect(window_, nullptr, FALSE);
        }
        else
            JumpToMonth(pickerYear_, cell + 1);
    }

    void ChangeMonth(int delta)
    {
        const int fromYear = displayYear_;
        const int fromMonth = displayMonth_;
        const auto destination = !settings_.monthPaging && continuousInitialized_ && continuousAnimating_
            ? CivilFromDays(continuousAnchor_ + static_cast<long long>(std::floor(continuousTarget_)) * 7 + 17)
            : Date{displayYear_, displayMonth_, 1};
        int value = destination.year * 12 + destination.month - 1 + delta;
        displayYear_ = value / 12;
        displayMonth_ = value % 12 + 1;
        if (displayMonth_ <= 0)
        {
            displayMonth_ += 12;
            --displayYear_;
        }
        RequestSystemCalendarMonth(displayYear_, displayMonth_);
        UpdateAgenda();
        if (!settings_.monthPaging)
        {
            if (!continuousInitialized_) ResetContinuousScroll();
            continuousTarget_ = static_cast<double>(WeekRowStartDays(displayYear_, displayMonth_) - continuousAnchor_) / 7.0;
            continuousAnimating_ = true;
            scrollAnimating_ = slideAnimating_ = false;
            SetTimer(window_, kAnimationTimer, 16, nullptr);
        }
        else
            BeginMonthSlide(delta > 0 ? 1 : -1, fromYear, fromMonth);
    }

    void ChangeYear(int delta)
    {
        displayYear_ += delta;
        ResetContinuousScroll();
        RequestSystemCalendarMonth(displayYear_, displayMonth_);
        UpdateAgenda();
        BeginTransition();
    }

    void BeginTransition()
    {
        slideAnimating_ = false;
        slidePos_ = 0.0f;
        slideTarget_ = 0.0f;
        transition_ = 0.0f;
        hoveredCell_ = -1;
        SetTimer(window_, kAnimationTimer, 16, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void BeginScrollTransition(int dir, int fromYear, int fromMonth)
    {
        scrollDir_ = dir;
        scrollFromYear_ = fromYear;
        scrollFromMonth_ = fromMonth;
        const long long delta =
            WeekRowStartDays(displayYear_, displayMonth_) - WeekRowStartDays(fromYear, fromMonth);
        const long long rows = (delta < 0 ? -delta : delta) / 7;
        scrollRows_ = static_cast<int>(std::clamp(rows, 1LL, 6LL));
        scrollProgress_ = 0.0f;
        scrollAnimating_ = true;
        transition_ = 1.0f;
        hoveredCell_ = -1;
        SetTimer(window_, kAnimationTimer, 16, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
    }

    // 整月翻滚：虚拟滚动轨道。滚轮/按键不断累积目标位置（slideTarget_，单位 =
    // 一页网格高），轨道位置 slidePos_ 每帧指数逼近目标；连续滚动时月份像列表
    // 一样连续经过，而不是每刻度一次独立动画。
    void BeginMonthSlide(int delta, int fromYear, int fromMonth)
    {
        if (!slideAnimating_)
        {
            scrollFromYear_ = fromYear;
            scrollFromMonth_ = fromMonth;
            slidePos_ = 0.0f;
            slideTarget_ = 0.0f;
        }
        scrollDir_ = delta > 0 ? 1 : -1;
        slideTarget_ += delta;
        slideAnimating_ = true;
        transition_ = 1.0f;
        hoveredCell_ = -1;
        SetTimer(window_, kAnimationTimer, 16, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
    }

    // 轨道索引 → 年月（索引可为负，基于链起点月份）。
    void MonthTrackIndex(int index, int& year, int& month) const
    {
        const int total = scrollFromYear_ * 12 + scrollFromMonth_ - 1 + index;
        year = total / 12;
        month = total % 12 + 1;
    }

    float Scale(float value) const
    {
        return value * static_cast<float>(dpi_) / 96.0f;
    }

    void UpdateWindowSize()
    {
        SetWindowPos(
            window_,
            nullptr,
            0,
            0,
            static_cast<int>(Scale(kLogicalWidth)),
            static_cast<int>(Scale(kLogicalHeight)),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int HitTestCell(int x, int y) const
    {
        const float logicalX = x * 96.0f / dpi_;
        const float logicalY = y * 96.0f / dpi_ - kClockHeight;
        const int column = static_cast<int>((logicalX - kGridLeft) / kCellWidth);
        const bool continuous = !settings_.monthPaging && continuousInitialized_;
        const double fraction = continuous ? continuousPosition_ - std::floor(continuousPosition_) : 0.0;
        const int row = static_cast<int>(std::floor((logicalY - kGridTop) / kCellHeight + fraction));
        if (logicalY < kGridTop || logicalY >= kGridTop + 6 * kCellHeight ||
            logicalX < kGridLeft || column < 0 || column >= 7 || row < 0)
            return -1;
        return row * 7 + column;
    }

    static LRESULT CALLBACK AgendaWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->agendaWindow_ = window;
        }
        return app ? app->HandleAgendaMessage(window, message, wParam, lParam)
                   : DefWindowProcW(window, message, wParam, lParam);
    }

    bool RegisterAgendaWindowClass() const
    {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = AgendaWindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kAgendaWindowClass;
        return RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    int AgendaPx(int value) const { return MulDiv(value, agendaDpi_, 96); }
    int AgendaHeight() const { return kLogicalHeight; }

    void PositionAgenda()
    {
        if (!IsWindow(agendaWindow_)) return;
#ifdef WINCAL_SETTINGS_PREVIEW
        if (agendaPreview_) return;
#endif
        RECT anchor{};
        if (!GetWindowRect(window_, &anchor)) return;
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        agendaDpi_ = GetDpiForWindow(window_);
        const int width = AgendaPx(kAgendaWidth), height = AgendaPx(AgendaHeight());
        const int gap = AgendaPx(12);
        LONG x = anchor.left - width - gap, y = anchor.top;
        if (x < monitor.rcWork.left) x = anchor.right + gap;
        x = std::clamp(x, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
        y = std::clamp(y, monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
        SetWindowPos(agendaWindow_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    void UpdateAgenda()
    {
        PresentAgenda();
    }

    void PresentAgenda()
    {
        const auto selectedDay = DaysFromCivil(selected_.year, selected_.month, selected_.day);
        const auto firstVisibleDay = continuousAnchor_ + static_cast<long long>(std::floor(continuousPosition_)) * 7;
        const bool outsideContinuousView = !settings_.monthPaging && continuousInitialized_ &&
            (selectedDay < firstVisibleDay || selectedDay >= firstVisibleDay +
                (continuousPosition_ == std::floor(continuousPosition_) ? 42 : 49));
        if (toolsOpen_ || weatherPanelOpen_ || pickerMode_ != DatePickerMode::Closed || outsideContinuousView || !IsWindowVisible(window_) || !dateDetailsOpen_ ||
            (settings_.monthPaging && (selected_.year != displayYear_ || selected_.month != displayMonth_)))
        {
            if (IsWindow(agendaWindow_)) ShowWindow(agendaWindow_, SW_HIDE);
            CloseDetailWindow();
            return;
        }
        if (!IsWindow(agendaWindow_))
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kAgendaWindowClass, L"日期详情",
                WS_POPUP, -30000, -30000, 1, 1, window_, nullptr, instance_, this);
        if (!IsWindow(agendaWindow_)) return;
        const BOOL dark = dark_;
        DwmSetWindowAttribute(agendaWindow_, 20, &dark, sizeof(dark));
        const DWORD corner = 2;
        DwmSetWindowAttribute(agendaWindow_, 33, &corner, sizeof(corner));
        PositionAgenda();
        ShowWindow(agendaWindow_, SW_SHOWNOACTIVATE);
        InvalidateRect(agendaWindow_, nullptr, FALSE);
    }

    void DrawAgenda(HDC dc, RECT client)
    {
        const COLORREF background = dark_ ? RGB(23,25,29) : RGB(241,244,248);
        const COLORREF surface = dark_ ? RGB(34,37,43) : RGB(255,255,255);
        const COLORREF primary = dark_ ? RGB(239,242,247) : RGB(32,40,54);
        const COLORREF secondary = dark_ ? RGB(175,185,200) : RGB(94,108,127);
        const COLORREF accent = dark_ ? RGB(116,177,255) : RGB(40,108,221);
        const COLORREF green = dark_ ? RGB(117,216,175) : RGB(25,125,87);
        const COLORREF red = dark_ ? RGB(255,156,165) : RGB(190,53,72);
        HBRUSH backgroundBrush = CreateSolidBrush(background);
        FillRect(dc, &client, backgroundBrush);
        DeleteObject(backgroundBrush);
        const auto makeFont = [&](float size, int weight)
        {
            return CreateFontW(-static_cast<int>(std::lround(size * CalendarFontScale(settings_.fontSizeOffset) * agendaDpi_ / 96.0f)),
                0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, kCalendarFontFamily);
        };
        const float fontScale = CalendarFontScale(settings_.fontSizeOffset);
        HFONT title = makeFont(std::min(21.0f, 27.0f / fontScale), FW_SEMIBOLD), body = makeFont(15, FW_NORMAL);
        HFONT caption = makeFont(12, FW_NORMAL), badge = makeFont(15, FW_SEMIBOLD);
        HFONT dayFont = makeFont(32, FW_SEMIBOLD);
        const auto almanac = wincal::AlmanacForDate(selected_.year, selected_.month, selected_.day);
        const std::wstring suitable = almanac ? almanac->suitable : L"该日期暂无黄历数据";
        const std::wstring avoid = almanac ? almanac->avoid : L"该日期暂无黄历数据";
        auto observances = lunarCalendar_.ObservancesForDate(selected_.year, selected_.month, selected_.day);
        const auto holiday = calendarData_.HolidayNameForDate(selected_.year, selected_.month, selected_.day);
        if (!holiday.empty() && std::find(observances.begin(), observances.end(), holiday) == observances.end())
            observances.push_back(holiday);
        std::wstring names;
        for (const auto& name : observances) { if (!names.empty()) names += L" · "; names += name; }
        const auto measure = [&](const std::wstring& value, int width)
        {
            RECT rect{0, 0, AgendaPx(width), 0};
            const auto oldFont = SelectObject(dc, body);
            ::DrawTextW(dc, value.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, oldFont);
            return std::max(24, MulDiv(rect.bottom + AgendaPx(4), 96, agendaDpi_));
        };
        const int bodyLine = std::max(28, static_cast<int>(std::ceil(21 * fontScale)));
        const int captionLine = std::max(22, static_cast<int>(std::ceil(18 * fontScale)));
        const int titleBottom = 24 + std::max(32, static_cast<int>(std::ceil(26 * fontScale)));
        const int weekdayTop = titleBottom + 4, weekdayBottom = weekdayTop + bodyLine;
        const int ordinalBottom = weekdayBottom + captionLine;
        const int lunarTop = std::max(110, ordinalBottom + 8), lunarBottom = lunarTop + bodyLine;
        const int headerBottom = lunarBottom + captionLine + 8;
        const int almanacTop = headerBottom + 12, yiTop = almanacTop + 16;
        const int badgeHeight = std::max(30, static_cast<int>(std::ceil(22 * fontScale)));
        const int yiHeight = std::max(badgeHeight, measure(suitable, 264));
        const int jiHeight = std::max(badgeHeight, measure(avoid, 264));
        const int almanacBottom = yiTop + yiHeight + 16 + jiHeight + 16;
        const int festivalTop = almanacBottom + 12;
        const int festivalTextTop = festivalTop + 12 + captionLine;
        const auto reading = wincal::SolarTermReadingForDate(selected_.year, selected_.month, selected_.day);
        const int explanationTop = festivalTextTop + measure(names.empty() ? L"当天暂无节气或节日" : names, 304) + 12;
        const int verseTop = reading ? explanationTop + measure(std::wstring{reading->explanation}, 304) + 24 : explanationTop;
        const int sourceTop = reading ? verseTop + measure(std::wstring{reading->verse}, 280) + 8 : verseTop;
        const int festivalBottom = reading ? sourceTop + measure(std::wstring{reading->source}, 280) + 24 : explanationTop;
        const int contentBottom = festivalBottom + 16;
        agendaScrollMax_ = std::max(0, contentBottom - (AgendaHeight() - 32));
        agendaScroll_ = std::clamp(agendaScroll_, 0, agendaScrollMax_);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, 0, 0, client.right, AgendaPx(AgendaHeight() - 32));
        const auto rounded = [&](int left, int top, int right, int bottom, COLORREF color, int radius = 14)
        {
            HBRUSH brush = CreateSolidBrush(color);
            const auto oldBrush = SelectObject(dc, brush);
            const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, AgendaPx(left), AgendaPx(top - agendaScroll_), AgendaPx(right),
                AgendaPx(bottom - agendaScroll_), AgendaPx(radius), AgendaPx(radius));
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        };
        const auto text = [&](const std::wstring& value, int left, int top, int right, int bottom, HFONT font, COLORREF color,
                              UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX)
        {
            DetailText(dc, value.c_str(), {AgendaPx(left), AgendaPx(top - agendaScroll_),
                AgendaPx(right), AgendaPx(bottom - agendaScroll_)}, font, color, flags);
        };
        rounded(12, 12, 348, headerBottom, surface);
        rounded(24, 30, 94, 100, dark_ ? RGB(37,61,94) : RGB(230,240,255), 18);
        text(std::to_wstring(selected_.day), 24, 30, 94, 100, dayFont, accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(std::to_wstring(selected_.year) + L"年" + std::to_wstring(selected_.month) + L"月",
            108, 24, 338, titleBottom, title, primary);
        const auto selectedDay = DaysFromCivil(selected_.year, selected_.month, selected_.day);
        const auto today = Today();
        const auto distance = selectedDay - DaysFromCivil(today.year, today.month, today.day);
        static constexpr const wchar_t* weekdays[]{L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
        const auto weekday = static_cast<size_t>((selectedDay + 4) % 7 + 7) % 7;
        const auto relative = distance == 0 ? L"今天" : std::to_wstring(distance < 0 ? -distance : distance) +
            (distance < 0 ? L"天前" : L"天后");
        text(std::wstring(weekdays[weekday]) + L" · " + relative, 108, weekdayTop, 338, weekdayBottom, body, secondary);
        const auto ordinal = selectedDay - DaysFromCivil(selected_.year, 1, 1) + 1;
        const auto schedule = calendarData_.ScheduleForDate(selected_.year, selected_.month, selected_.day);
        text(L"第 " + std::to_wstring(ordinal) + L" 天" + (schedule == wincal::ScheduleLabel::Rest ? L" · 休息日" :
            schedule == wincal::ScheduleLabel::Work ? L" · 补班日" : L""), 108, weekdayBottom, 338, ordinalBottom, caption, secondary);
        text(L"农历 " + lunarCalendar_.FullTextForDate(selected_.year, selected_.month, selected_.day),
            24, lunarTop, 336, lunarBottom, body, primary);
        if (almanac) text(almanac->monthPillar + L"月 · " + almanac->dayPillar + L"日", 24, lunarBottom, 336, headerBottom - 8, caption, secondary);
        rounded(12, almanacTop, 348, almanacBottom, surface);
        rounded(24, yiTop, 58, yiTop + badgeHeight, dark_ ? RGB(30,65,53) : RGB(229,246,237), 12);
        text(L"宜", 24, yiTop, 58, yiTop + badgeHeight, badge, green, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(suitable, 72, yiTop, 336, yiTop + yiHeight, body, primary, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        const int jiTop = yiTop + yiHeight + 16;
        rounded(24, jiTop, 58, jiTop + badgeHeight, dark_ ? RGB(76,39,49) : RGB(255,235,239), 12);
        text(L"忌", 24, jiTop, 58, jiTop + badgeHeight, badge, red, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        text(avoid, 72, jiTop, 336, jiTop + jiHeight, body, primary, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        rounded(12, festivalTop, 348, festivalBottom, surface);
        text(L"节气与节日", 24, festivalTop + 8, 336, festivalTextTop - 4, caption, secondary);
        text(names.empty() ? L"当天暂无节气或节日" : names, 24, festivalTextTop, 328, explanationTop - 8,
            body, names.empty() ? secondary : accent, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        if (reading)
        {
            text(std::wstring{reading->explanation}, 24, explanationTop, 328, verseTop - 24,
                body, primary, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            rounded(24, verseTop - 10, 336, festivalBottom - 12, dark_ ? RGB(30,45,65) : RGB(239,245,255), 12);
            text(std::wstring{reading->verse}, 36, verseTop, 324, sourceTop - 8,
                body, accent, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            text(std::wstring{reading->source}, 36, sourceTop, 324, festivalBottom - 24,
                caption, secondary, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        RestoreDC(dc, saved);
        if (agendaScrollMax_ > 0)
        {
            const int track = AgendaHeight() - 52;
            const int thumb = std::max(28, track * (AgendaHeight() - 32) / contentBottom);
            const int y = 10 + (track - thumb) * agendaScroll_ / agendaScrollMax_;
            HBRUSH brush = CreateSolidBrush(dark_ ? RGB(95,108,125) : RGB(169,182,199));
            RECT rect{AgendaPx(354), AgendaPx(y), AgendaPx(357), AgendaPx(y + thumb)};
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        }
        const wchar_t* footer = agendaScrollMax_ > 0 ? L"滚轮查看更多 · 点击日期切换" : L"点击其他日期查看详情";
        DetailText(dc, footer, {AgendaPx(20), AgendaPx(AgendaHeight() - 28), AgendaPx(344), AgendaPx(AgendaHeight() - 4)},
            caption, secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        DeleteObject(title);
        DeleteObject(body);
        DeleteObject(caption);
        DeleteObject(badge);
        DeleteObject(dayFont);
    }

    LRESULT HandleAgendaMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            DrawAgenda(dc, client);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEWHEEL:
            agendaScroll_ = std::clamp(agendaScroll_ - MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), 48, WHEEL_DELTA),
                0, agendaScrollMax_);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) HandleEscape(lParam);
            return 0;
        case WM_CLOSE:
            HidePopup(HideReason::EscapeKey);
            return 0;
        case WM_DPICHANGED:
            agendaDpi_ = HIWORD(wParam);
            PositionAgenda();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            agendaWindow_ = nullptr;
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    int DetailPx(int value) const
    {
        return MulDiv(value, detailDpi_ ? detailDpi_ : 96, 96);
    }

    void EnsureDetailFonts(HWND window)
    {
        const UINT dpi = GetDpiForWindow(window);
        const int offset = settings_.fontSizeOffset;
        if (detailTitleFont_ && detailFontOffset_ == offset && detailDpi_ == dpi) return;
        if (detailTitleFont_) DeleteObject(detailTitleFont_);
        if (detailBodyFont_) DeleteObject(detailBodyFont_);
        if (detailCaptionFont_) DeleteObject(detailCaptionFont_);
        detailDpi_ = dpi;
        detailFontOffset_ = offset;
        const auto makeFont = [&](float size, int weight)
        {
            return CreateFontW(-static_cast<int>(std::lround(size * CalendarFontScale(offset) * dpi / 96.0f)),
                0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, kCalendarFontFamily);
        };
        detailTitleFont_ = makeFont(kCalendarTitleFontSize, FW_SEMIBOLD);
        detailBodyFont_ = makeFont(kCalendarBodyFontSize, FW_NORMAL);
        detailCaptionFont_ = makeFont(kCalendarCaptionFontSize, FW_NORMAL);
    }

    std::pair<std::wstring, std::wstring> DetailWhen() const
    {
        const auto date = [](int y, int m, int d)
        {
            return std::to_wstring(y) + L"年" + std::to_wstring(m) + L"月" + std::to_wstring(d) + L"日";
        };
        const auto time = [](int h, int m)
        {
            wchar_t value[16]{};
            swprintf_s(value, L"%02d:%02d", h, m);
            return std::wstring(value);
        };
        const auto& e = detailEvent_;
        const auto startDate = date(e.startYear, e.startMonth, e.startDay);
        const auto start = DaysFromCivil(e.startYear, e.startMonth, e.startDay);
        const auto end = e.endYear ? DaysFromCivil(e.endYear, e.endMonth, e.endDay) : start;
        if (e.allDay)
        {
            const auto last = CivilFromDays(std::max(start, end - 1));
            return {end > start + 1 ? startDate + L" – " + date(last.year, last.month, last.day) : startDate, L"全天"};
        }
        if (end > start)
            return {startDate + L"  " + time(e.startHour, e.startMinute) + L" 开始",
                date(e.endYear, e.endMonth, e.endDay) + L"  " + time(e.endHour, e.endMinute) + L" 结束"};
        const bool hasEnd = e.endYear &&
            std::pair{e.endHour, e.endMinute} > std::pair{e.startHour, e.startMinute};
        return {startDate, time(e.startHour, e.startMinute) +
            (hasEnd ? L" – " + time(e.endHour, e.endMinute) : L"")};
    }

    int MeasureDetailText(HDC dc, const std::wstring& text, HFONT font) const
    {
        RECT rect{0, 0, DetailPx(kDetailLogicalWidth - 48), 0};
        const auto previous = SelectObject(dc, font);
        ::DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(dc, previous);
        return rect.bottom;
    }

    int DetailContentHeight(HDC dc) const
    {
        const auto [date, time] = DetailWhen();
        return DetailPx(56 + 20 + 8 + 24) + MeasureDetailText(dc, detailEvent_.title, detailTitleFont_) +
            MeasureDetailText(dc, date, detailBodyFont_) + MeasureDetailText(dc, time, detailBodyFont_);
    }

    RECT DetailCloseRect() const
    {
        return {DetailPx(kDetailLogicalWidth - 44), DetailPx(12),
            DetailPx(kDetailLogicalWidth - 12), DetailPx(44)};
    }

    bool DetailCloseHit(LPARAM point) const
    {
        const auto rect = DetailCloseRect();
        return PtInRect(&rect, {GET_X_LPARAM(point), GET_Y_LPARAM(point)}) != FALSE;
    }

    void DetailText(HDC dc, const wchar_t* text, RECT rect, HFONT font,
                    COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) const
    {
        const auto oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        ::DrawTextW(dc, text, -1, &rect, flags | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }

    void DrawDetailCard(HDC dc, RECT client)
    {
        const COLORREF background = dark_ ? RGB(28,30,38) : RGB(250,251,254);
        const COLORREF primary = dark_ ? RGB(237,238,245) : RGB(33,38,57);
        const COLORREF secondary = dark_ ? RGB(164,169,186) : RGB(113,121,144);
        const COLORREF accent = dark_ ? RGB(162,155,255) : RGB(103,91,218);
        HBRUSH brush = CreateSolidBrush(background);
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        const auto close = DetailCloseRect();
        if (detailCloseHot_)
        {
            brush = CreateSolidBrush(dark_ ? RGB(42,44,56) : RGB(238,237,250));
            const auto oldBrush = SelectObject(dc, brush);
            const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, close.left, close.top, close.right, close.bottom, DetailPx(8), DetailPx(8));
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
        }
        const auto pen = CreatePen(PS_SOLID, std::max(1, DetailPx(1)), secondary);
        const auto oldPen = SelectObject(dc, pen);
        const int x = (close.left + close.right) / 2, y = (close.top + close.bottom) / 2;
        MoveToEx(dc, x - DetailPx(4), y - DetailPx(4), nullptr);
        LineTo(dc, x + DetailPx(4), y + DetailPx(4));
        MoveToEx(dc, x + DetailPx(4), y - DetailPx(4), nullptr);
        LineTo(dc, x - DetailPx(4), y + DetailPx(4));
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        detailScroll_ = std::clamp(detailScroll_, 0, std::max(0, DetailContentHeight(dc) - static_cast<int>(client.bottom)));
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, DetailPx(24), DetailPx(52), client.right - DetailPx(24), client.bottom - DetailPx(20));
        int top = DetailPx(56) - detailScroll_;
        const auto draw = [&](const std::wstring& value, HFONT font, COLORREF color)
        {
            const int height = MeasureDetailText(dc, value, font);
            DetailText(dc, value.c_str(), {DetailPx(24), top, client.right - DetailPx(24), top + height},
                font, color, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
            top += height;
        };
        draw(detailEvent_.title, detailTitleFont_, primary);
        top += DetailPx(20);
        const auto [date, time] = DetailWhen();
        draw(date, detailBodyFont_, secondary);
        top += DetailPx(8);
        draw(time, detailBodyFont_, accent);
        RestoreDC(dc, saved);
    }

    void PaintDetailWindow(HWND window)
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        DrawDetailCard(dc, client);
        EndPaint(window, &paint);
    }

    void PositionDetailWindow()
    {
        if (!IsWindow(detailWindow_)) return;
        RECT mainRect{};
        GetWindowRect(IsWindowVisible(agendaWindow_) ? agendaWindow_ : window_, &mainRect);
        HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{sizeof(monitorInfo)};
        GetMonitorInfoW(monitor, &monitorInfo);
        const int width = DetailPx(kDetailLogicalWidth);
        EnsureDetailFonts(detailWindow_);
        HDC dc = GetDC(detailWindow_);
        const int height = std::min(DetailContentHeight(dc),
            std::max(DetailPx(120), static_cast<int>(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - DetailPx(24)));
        ReleaseDC(detailWindow_, dc);
        const int gap = DetailPx(12);
        LONG x = mainRect.right + gap;
        if (x + width > monitorInfo.rcWork.right)
            x = mainRect.left - width - gap;
        x = std::clamp(x, monitorInfo.rcWork.left, std::max(monitorInfo.rcWork.left, monitorInfo.rcWork.right - width));
        LONG y = std::clamp(mainRect.top, monitorInfo.rcWork.top, std::max(monitorInfo.rcWork.top, monitorInfo.rcWork.bottom - height));
        SetWindowPos(detailWindow_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    void HandleEscape(LPARAM keyState)
    {
        // Do not let key auto-repeat dismiss the parent after closing details.
        if (keyState & (static_cast<LPARAM>(1) << 30)) return;
        if (toolsOpen_) { ToggleTools(); return; }
        if (weatherPanelOpen_) { ToggleWeatherForecast(); return; }
        if (pickerMode_ != DatePickerMode::Closed)
        {
            pickerMode_ = DatePickerMode::Closed;
            navigationHot_ = -1;
            ResetContinuousScroll();
            UpdateAgenda();
            InvalidateRect(window_, nullptr, FALSE);
        }
        else if (IsWindowVisible(detailWindow_))
            CloseDetailWindow();
        else
            HidePopup(HideReason::EscapeKey);
    }

    void CloseDetailWindow()
    {
        if (IsWindow(detailWindow_))
            DestroyWindow(detailWindow_);
    }

    LRESULT HandleDetailMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            detailDpi_ = GetDpiForWindow(window);
            EnsureDetailFonts(window);
            const DWORD corner = 2;
            DwmSetWindowAttribute(window, 33, &corner, sizeof(corner));
            BOOL darkValue = dark_ ? TRUE : FALSE;
            DwmSetWindowAttribute(window, 20, &darkValue, sizeof(darkValue));
            return 0;
        }
        case WM_PAINT:
            PaintDetailWindow(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            const bool hot = DetailCloseHit(lParam);
            SetCursor(LoadCursorW(nullptr, hot ? IDC_HAND : IDC_ARROW));
            if (hot != detailCloseHot_)
            {
                detailCloseHot_ = hot;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            detailCloseHot_ = false;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (DetailCloseHit(lParam))
                CloseDetailWindow();
            return 0;
        case WM_MOUSEWHEEL:
            detailScroll_ = std::max(0, detailScroll_ + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -DetailPx(36) : DetailPx(36)));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            CloseDetailWindow();
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                HandleEscape(lParam);
                return 0;
            }
            break;
        case WM_DPICHANGED:
        {
            detailDpi_ = HIWORD(wParam);
            if (detailTitleFont_) DeleteObject(detailTitleFont_);
            if (detailBodyFont_) DeleteObject(detailBodyFont_);
            if (detailCaptionFont_) DeleteObject(detailCaptionFont_);
            detailTitleFont_ = detailBodyFont_ = detailCaptionFont_ = nullptr;
            EnsureDetailFonts(window);
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            PositionDetailWindow();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_NCDESTROY:
            if (detailTitleFont_) DeleteObject(detailTitleFont_);
            if (detailBodyFont_) DeleteObject(detailBodyFont_);
            if (detailCaptionFont_) DeleteObject(detailCaptionFont_);
            detailTitleFont_ = detailBodyFont_ = detailCaptionFont_ = nullptr;
            detailWindow_ = nullptr;
            detailCloseHot_ = false;
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    Date DateForCell(int year, int month, int index) const
    {
        const long long first = DaysFromCivil(year, static_cast<unsigned>(month), 1);
        const int mondayBasedWeekday = static_cast<int>((first + 3) % 7 + 7) % 7;
        const int firstColumn = weekStartsMonday_ ? mondayBasedWeekday : (mondayBasedWeekday + 1) % 7;
        return CivilFromDays(first - firstColumn + index);
    }

    // 月份网格第一行的真实日期（配置周起始日的当周第一天），逐周滚动的锚点。
    long long WeekRowStartDays(int year, int month) const
    {
        const long long first = DaysFromCivil(year, static_cast<unsigned>(month), 1);
        const int mondayBasedWeekday = static_cast<int>((first + 3) % 7 + 7) % 7;
        const int backToWeekStart = weekStartsMonday_ ? mondayBasedWeekday : (mondayBasedWeekday + 1) % 7;
        return first - backToWeekStart;
    }

    void DrawCalendarCell(const Theme& theme, const Date& today, const Date& date,
                          bool inMonth, int column, float topLogical, bool hovered, float alpha)
    {
        const float left = Scale(static_cast<float>(kGridLeft + column * kCellWidth));
        const float top = Scale(topLogical);
        const auto rect = D2D1::RectF(
            left + Scale(3), top + Scale(3), left + Scale(kCellWidth - 3), top + Scale(kCellHeight - 3));
        const bool isToday = date == today;
        const bool selected = date == selected_;
        const bool weekend = weekStartsMonday_ ? column >= 5 : column == 0 || column == 6;
        const bool hasEvents = calendarData_.HasEvents(date.year, date.month, date.day);
        const auto schedule = calendarData_.ScheduleForDate(date.year, date.month, date.day);

        if (hovered)
        {
            const auto hover = Brush(theme.hover, alpha);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, Scale(9), Scale(9)), hover.Get());
        }
        if (selected)
        {
            const auto accent = Brush(theme.accent, alpha);
            renderTarget_->FillEllipse(D2D1::Ellipse(
                {(rect.left + rect.right) / 2, rect.top + Scale(16)}, Scale(14), Scale(14)), accent.Get());
        }
        else if (isToday)
        {
            const auto accent = Brush(theme.accent, alpha);
            renderTarget_->DrawEllipse(D2D1::Ellipse(
                {(rect.left + rect.right) / 2, rect.top + Scale(16)}, Scale(14), Scale(14)), accent.Get(), Scale(1.5f));
        }

        wchar_t number[4]{};
        swprintf_s(number, L"%d", date.day);
        DrawText(
            number,
            dayFormat_.Get(),
            D2D1::RectF(rect.left, rect.top, rect.right, rect.top + Scale(32)),
            selected ? D2D1::ColorF(0xFFFFFF) : (weekend ? theme.weekend : inMonth ? theme.primary : theme.muted),
            alpha * (!selected && weekend && !inMonth ? 0.4f : 1.0f));

        const auto holiday = calendarData_.HolidayNameForDate(date.year, date.month, date.day);
        const auto observances = lunarCalendar_.ObservancesForDate(date.year, date.month, date.day);
        const bool specialDay = !observances.empty() || !holiday.empty();
        const auto lunar = !observances.empty() ? observances.front() :
            !holiday.empty() ? holiday : lunarCalendar_.TextForDate(date.year, date.month, date.day);
        if (!lunar.empty())
        {
            DrawText(
                lunar.c_str(),
                lunarFormat_.Get(),
                D2D1::RectF(rect.left + Scale(1), rect.top + Scale(32), rect.right - Scale(1), rect.bottom - Scale(5)),
                inMonth ? (specialDay || selected ? theme.accent : theme.secondary) : theme.muted,
                alpha);
        }

        if (schedule != wincal::ScheduleLabel::None)
        {
            const bool isRest = schedule == wincal::ScheduleLabel::Rest;
            const auto badgeRect = D2D1::RectF(
                rect.right - Scale(17), rect.top + Scale(1),
                rect.right - Scale(1), rect.top + Scale(17));
            const auto badgeBackground = Brush(
                isRest ? theme.restBadgeBackground : theme.workBadgeBackground,
                alpha);
            renderTarget_->FillRoundedRectangle(
                D2D1::RoundedRect(badgeRect, Scale(4), Scale(4)), badgeBackground.Get());
            DrawText(
                isRest ? L"休" : L"班",
                badgeFormat_.Get(), badgeRect,
                isRest ? theme.restBadgeForeground : theme.workBadgeForeground,
                alpha);
        }

        if (hasEvents)
        {
            const auto dot = Brush(
                selected ? D2D1::ColorF(0xFFFFFF) : theme.eventDot,
                alpha);
            renderTarget_->FillEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F((rect.left + rect.right) / 2, rect.bottom - Scale(3)),
                    Scale(1.75f), Scale(1.75f)),
                dot.Get());
        }
    }

    void DrawMonthGrid(const Theme& theme, const Date& today, int year, int month, float alpha,
                       float yTranslate = 0.0f, bool enableHover = true)
    {
        for (int index = 0; index < 42; ++index)
        {
            const int row = index / 7;
            const int column = index % 7;
            const auto date = DateForCell(year, month, index);
            DrawCalendarCell(
                theme, today, date, date.month == month, column,
                static_cast<float>(kGridTop + row * kCellHeight) + yTranslate,
                enableHover && index == hoveredCell_, alpha);
        }
    }

    // 连续周列表保留小数行偏移；旧翻月预览仍可使用动画位置。
    void DrawWeekRows(const Theme& theme, const Date& today)
    {
        const bool continuous = !settings_.monthPaging && continuousInitialized_;
        const float position = continuous ? static_cast<float>(continuousPosition_) :
            scrollDir_ * scrollRows_ * EaseOutCubic(scrollProgress_);
        const long long anchor = continuous ? continuousAnchor_ : WeekRowStartDays(scrollFromYear_, scrollFromMonth_);
        const int firstRow = static_cast<int>(std::floor(position));
        const float gridBottom = static_cast<float>(kGridTop + 6 * kCellHeight);
        const auto clip = D2D1::RectF(0, Scale(kGridTop), Scale(kLogicalWidth), Scale(gridBottom));
        renderTarget_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        for (int row = firstRow; row <= firstRow + 7; ++row)
        {
            const float top = kGridTop + (row - position) * kCellHeight;
            if (top >= gridBottom || top + kCellHeight <= kGridTop)
                continue;
            for (int column = 0; column < 7; ++column)
            {
                const Date date = CivilFromDays(anchor + row * 7 + column);
                DrawCalendarCell(theme, today, date, date.month == displayMonth_,
                                 column, top, continuous && (row - firstRow) * 7 + column == hoveredCell_, 1.0f);
            }
        }
        renderTarget_->PopAxisAlignedClip();
    }

    // 整月翻滚：按轨道位置渲染相邻两页单月网格，轨道连续平移。
    void DrawMonthSlide(const Theme& theme, const Date& today)
    {
        const float gridHeight = 6 * kCellHeight;
        const float pos = slidePos_;
        const auto clip = D2D1::RectF(
            0, Scale(kGridTop), Scale(kLogicalWidth), Scale(kGridTop + gridHeight));
        renderTarget_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        const int first = static_cast<int>(std::floor(pos));
        for (int m = first; m <= first + 1; ++m)
        {
            int year = 0;
            int month = 0;
            MonthTrackIndex(m, year, month);
            DrawMonthGrid(theme, today, year, month, 1.0f,
                          (m - pos) * gridHeight, false);
        }
        renderTarget_->PopAxisAlignedClip();
    }

    void ReportRenderFailure(const wchar_t* stage, HRESULT result)
    {
        wchar_t message[96]{};
        swprintf_s(message, L"WinCal - %s 0x%08X", stage, static_cast<unsigned>(result));
        SetWindowTextW(window_, message);
    }

    bool EnsureDeviceResources()
    {
        if (!factory_)
        {
            const HRESULT result = D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                factory_.ReleaseAndGetAddressOf());
            if (FAILED(result))
            {
                ReportRenderFailure(L"D2D factory", result);
                return false;
            }
        }
        if (!writeFactory_)
        {
            const HRESULT result = DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(writeFactory_.ReleaseAndGetAddressOf()));
            if (FAILED(result))
            {
                ReportRenderFailure(L"DWrite factory", result);
                return false;
            }
        }

        if (!renderTarget_)
        {
            RECT rect{};
            GetClientRect(window_, &rect);
            const D2D1_SIZE_U size{static_cast<UINT32>(rect.right), static_cast<UINT32>(rect.bottom)};
            ComPtr<ID2D1HwndRenderTarget> hwndTarget;
            const HRESULT result = factory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                    D2D1::PixelFormat(),
                    96.0f,
                    96.0f),
                D2D1::HwndRenderTargetProperties(window_, size),
                hwndTarget.ReleaseAndGetAddressOf());
            if (FAILED(result))
            {
                ReportRenderFailure(L"render target", result);
                return false;
            }
            renderTarget_ = hwndTarget;
        }

        if (!titleFormat_)
        {
            HRESULT result = writeFactory_->CreateTextFormat(
                kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, TextScale(kCalendarTitleFontSize), L"zh-CN",
                titleFormat_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) result = writeFactory_->CreateTextFormat(
                kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, TextScale(kCalendarBodyFontSize), L"zh-CN",
                bodyFormat_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) result = writeFactory_->CreateTextFormat(
                kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, TextScale(18), L"zh-CN",
                dayFormat_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) result = writeFactory_->CreateTextFormat(
                kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, TextScale(kCalendarCaptionFontSize), L"zh-CN",
                lunarFormat_.ReleaseAndGetAddressOf());
            if (SUCCEEDED(result)) result = writeFactory_->CreateTextFormat(
                kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, TextScale(kCalendarCaptionFontSize), L"zh-CN",
                badgeFormat_.ReleaseAndGetAddressOf());
            if (FAILED(result))
            {
                ReportRenderFailure(L"text format", result);
                return false;
            }
            bodyFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            bodyFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            dayFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            dayFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            lunarFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            lunarFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            lunarFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(writeFactory_->CreateEllipsisTrimmingSign(lunarFormat_.Get(), &ellipsis)))
            {
                const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                lunarFormat_->SetTrimming(&trimming, ellipsis.Get());
            }
            badgeFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            badgeFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return true;
    }

    ComPtr<ID2D1SolidColorBrush> Brush(const D2D1_COLOR_F& color, float opacity = 1.0f)
    {
        ComPtr<ID2D1SolidColorBrush> brush;
        auto actual = color;
        actual.a *= opacity;
        renderTarget_->CreateSolidColorBrush(actual, brush.ReleaseAndGetAddressOf());
        return brush;
    }

    void DrawText(
        const wchar_t* text,
        IDWriteTextFormat* format,
        const D2D1_RECT_F& rect,
        const D2D1_COLOR_F& color,
        float opacity = 1.0f)
    {
        const auto brush = Brush(color, opacity);
        renderTarget_->DrawTextW(
            text,
            static_cast<UINT32>(wcslen(text)),
            format,
            rect,
            brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    std::wstring NavigationTitle() const
    {
        if (pickerMode_ == DatePickerMode::Months) return std::to_wstring(pickerYear_) + L"年";
        if (pickerMode_ == DatePickerMode::Years)
            return std::to_wstring(pickerFirstYear_) + L" – " + std::to_wstring(pickerFirstYear_ + 11);
        return std::to_wstring(displayYear_) + L"年" + std::to_wstring(displayMonth_) + L"月";
    }

    float NavigationTitleRight() const
    {
        if (!writeFactory_ || !titleFormat_) return 210.0f;
        const auto title = NavigationTitle();
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(writeFactory_->CreateTextLayout(title.c_str(), static_cast<UINT32>(title.size()),
            titleFormat_.Get(), Scale(240), Scale(44), &layout))) return 210.0f;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return 22.0f + std::min(metrics.width / Scale(1), 232.0f);
    }

    void DrawNavigation(const Theme& theme)
    {
        ComPtr<ID2D1StrokeStyle> stroke;
        auto style = D2D1::StrokeStyleProperties();
        style.startCap = style.endCap = D2D1_CAP_STYLE_ROUND;
        style.lineJoin = D2D1_LINE_JOIN_ROUND;
        factory_->CreateStrokeStyle(style, nullptr, 0, &stroke);
        const auto drawButton = [&](int hit, float center)
        {
            const auto rect = D2D1::RectF(Scale(center - 16), Scale(20), Scale(center + 16), Scale(52));
            if (navigationHot_ == hit)
            {
                const auto brush = Brush(theme.hover);
                renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, Scale(16), Scale(16)), brush.Get());
            }
            const auto ink = Brush(navigationHot_ == hit ? theme.accent : theme.secondary);
            const float x = Scale(center), y = Scale(36), width = Scale(1.8f);
            if (hit == -5)
            {
                renderTarget_->DrawEllipse(D2D1::Ellipse({x, y}, Scale(6), Scale(6)), ink.Get(), width);
                renderTarget_->FillEllipse(D2D1::Ellipse({x, y}, Scale(2), Scale(2)), ink.Get());
            }
            else
            {
                const float direction = hit == -3 ? -1.0f : 1.0f;
                renderTarget_->DrawLine({x - direction * Scale(2.5f), y - Scale(5)},
                    {x + direction * Scale(2.5f), y}, ink.Get(), width, stroke.Get());
                renderTarget_->DrawLine({x + direction * Scale(2.5f), y},
                    {x - direction * Scale(2.5f), y + Scale(5)}, ink.Get(), width, stroke.Get());
            }
        };
        drawButton(-3, 313);
        drawButton(-4, 349);
        drawButton(-5, 393);
        const auto brush = Brush(theme.secondary);
        const float x = Scale(NavigationTitleRight() + 14), y = Scale(36);
        const float direction = pickerMode_ == DatePickerMode::Years ? -1.0f : 1.0f;
        renderTarget_->DrawLine({x - Scale(3), y - direction * Scale(1.5f)},
            {x, y + direction * Scale(1.5f)}, brush.Get(), Scale(1.5f), stroke.Get());
        renderTarget_->DrawLine({x, y + direction * Scale(1.5f)},
            {x + Scale(3), y - direction * Scale(1.5f)}, brush.Get(), Scale(1.5f), stroke.Get());
    }

    void DrawWeatherIcon(const Theme&, int code, bool daylight, float cx, float cy, bool animate = true)
    {
#ifdef WINCAL_SETTINGS_PREVIEW
        animate = false;
#endif
        const float phase = animate ? static_cast<float>(GetTickCount64() % 120000) / 1000.0f : 0.0f;
        const auto point = [&](float x, float y) { return D2D1::Point2F(Scale(cx + x), Scale(cy + y)); };
        const auto gold = Brush(D2D1::ColorF(0xF5B634));
        const auto blue = Brush(D2D1::ColorF(0x65A9EF));
        ComPtr<ID2D1StrokeStyle> stroke;
        auto style = D2D1::StrokeStyleProperties();
        style.startCap = style.endCap = D2D1_CAP_STYLE_ROUND;
        style.lineJoin = D2D1_LINE_JOIN_ROUND;
        factory_->CreateStrokeStyle(style, nullptr, 0, &stroke);
        const auto gradient = [&](D2D1_COLOR_F top, D2D1_COLOR_F bottom, float y1, float y2)
        {
            D2D1_GRADIENT_STOP stops[]{{0, top}, {1, bottom}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> brush;
            if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(stops, 2, &collection)))
                renderTarget_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(point(0, y1), point(0, y2)),
                    collection.Get(), &brush);
            return brush;
        };
        if (code <= 2 && daylight)
        {
            const float sx = code ? -5.5f : 0, sy = code ? -5.5f : 0;
            const auto glow = Brush(D2D1::ColorF(0xFFD778, 0.14f));
            renderTarget_->FillEllipse(D2D1::Ellipse(point(sx, sy), Scale(14), Scale(14)), glow.Get());
            for (int i = 0; i < 8; ++i)
            {
                const float angle = i * 3.14159265f / 4 + phase * 0.16f;
                renderTarget_->DrawLine(point(sx + std::cos(angle) * 10, sy + std::sin(angle) * 10),
                    point(sx + std::cos(angle) * 12.5f, sy + std::sin(angle) * 12.5f), gold.Get(), Scale(1.7f), stroke.Get());
            }
            auto sun = gradient(D2D1::ColorF(0xFFE28A), D2D1::ColorF(0xF6AC31), sy - 7, sy + 7);
            renderTarget_->FillEllipse(D2D1::Ellipse(point(sx, sy), Scale(7), Scale(7)), sun ? static_cast<ID2D1Brush*>(sun.Get()) : gold.Get());
        }
        else if (code <= 2)
        {
            // Closed crescent path keeps the background visible, including on hover.
            ComPtr<ID2D1PathGeometry> moon;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(&moon)) && SUCCEEDED(moon->Open(&sink)))
            {
                sink->BeginFigure(point(3, -11), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddBezier(D2D1::BezierSegment(point(-16, -10), point(-13, 14), point(7, 9)));
                sink->AddBezier(D2D1::BezierSegment(point(-3, 8), point(-7, -3), point(3, -11)));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                renderTarget_->FillGeometry(moon.Get(), gold.Get());
            }
            const auto star = Brush(D2D1::ColorF(0xB8C9F2, 0.65f + 0.3f * std::sin(phase * 1.3f)));
            renderTarget_->FillEllipse(D2D1::Ellipse(point(9, -8), Scale(1.3f), Scale(1.3f)), star.Get());
        }
        if (code == 0) return;
        const float drift = std::sin(phase * 0.85f) * 1.2f;
        const auto cp = [&](float x, float y) { return point(x + drift, y); };
        ComPtr<ID2D1PathGeometry> cloud;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(&cloud)) && SUCCEEDED(cloud->Open(&sink)))
        {
            sink->BeginFigure(cp(-9, 10), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddBezier(D2D1::BezierSegment(cp(-19, 9), cp(-18, -2), cp(-9, -3)));
            sink->AddBezier(D2D1::BezierSegment(cp(-9, -14), cp(6, -14), cp(8, -4)));
            sink->AddBezier(D2D1::BezierSegment(cp(18, -6), cp(21, 9), cp(11, 10)));
            sink->AddLine(cp(-9, 10));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            auto fill = gradient(dark_ ? D2D1::ColorF(0xE0E8F5) : D2D1::ColorF(0xFFFFFF),
                dark_ ? D2D1::ColorF(0x8DACC9) : D2D1::ColorF(0xA7C3DF), -10, 12);
            renderTarget_->FillGeometry(cloud.Get(), fill ? static_cast<ID2D1Brush*>(fill.Get()) : blue.Get());
        }
        if (code == 45 || code == 48)
        {
            for (int i = 0; i < 2; ++i)
                renderTarget_->DrawLine(cp(-12 + i * 3.0f, 14 + i * 4.0f), cp(12 - i * 3.0f, 14 + i * 4.0f),
                    blue.Get(), Scale(1.5f), stroke.Get());
        }
        else if (code >= 51)
        {
            const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
            for (int i = 0; i < 3; ++i)
            {
                const float fall = std::fmod(phase * (snow ? 5 : 13) + i * 3.3f, 10.0f);
                const float x = -8 + i * 8.0f, y = 12 + fall;
                if (snow)
                {
                    renderTarget_->DrawLine(cp(x - 2, y), cp(x + 2, y), blue.Get(), Scale(1.2f), stroke.Get());
                    renderTarget_->DrawLine(cp(x, y - 2), cp(x, y + 2), blue.Get(), Scale(1.2f), stroke.Get());
                }
                else
                    renderTarget_->DrawLine(cp(x + 1, y), cp(x - 0.5f, y + 3), blue.Get(), Scale(1.7f), stroke.Get());
            }
            if (code >= 95)
            {
                renderTarget_->DrawLine(cp(3, 7), cp(-1, 13), gold.Get(), Scale(2.3f), stroke.Get());
                renderTarget_->DrawLine(cp(-1, 13), cp(3, 13), gold.Get(), Scale(2.3f), stroke.Get());
                renderTarget_->DrawLine(cp(3, 13), cp(-1, 19), gold.Get(), Scale(2.3f), stroke.Get());
            }
        }
    }

    bool EnsureClockFormats()
    {
        if (!clockFormat_)
        {
            writeFactory_->CreateTextFormat(kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Scale(32), L"zh-CN", &clockFormat_);
            writeFactory_->CreateTextFormat(kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Scale(10), L"zh-CN", &clockCaptionFormat_);
            writeFactory_->CreateTextFormat(kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Scale(21), L"zh-CN", &temperatureFormat_);
            if (!clockFormat_ || !clockCaptionFormat_ || !temperatureFormat_) return false;
            clockFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            clockCaptionFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            temperatureFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        return true;
    }

    void DrawClockAndWeather(const Theme& theme)
    {
        if (!EnsureClockFormats()) return;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        auto weather = weatherData_.Snapshot();
#ifdef WINCAL_SETTINGS_PREVIEW
        now = {2026, 11, 1, 2, 21, 30, 5, 0};
        weather.location = {39.9042, 116.4074, L"北京", L"网络定位"};
        weather.temperature = 12.0;
        weather.code = 2;
        weather.daylight = true;
        weather.available = true;
        weather.failed = false;
        weather.updatedAt = std::chrono::system_clock::now();
#endif
        wchar_t time[24]{};
        swprintf_s(time, L"%02d:%02d:%02d", now.wHour, now.wMinute, now.wSecond);
        DrawText(time, clockFormat_.Get(), D2D1::RectF(Scale(22), Scale(9), Scale(280), Scale(53)), theme.primary);
        constexpr std::array<const wchar_t*, 7> weekdays{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
        wchar_t date[96]{};
        const auto lunar = lunarCalendar_.TextForDate(now.wYear, now.wMonth, now.wDay);
        swprintf_s(date, L"%d年%d月%d日  %s  %s", now.wYear, now.wMonth, now.wDay,
            weekdays[now.wDayOfWeek], lunar.c_str());
        DrawText(date, clockCaptionFormat_.Get(), D2D1::RectF(Scale(23), Scale(55), Scale(280), Scale(73)), theme.secondary);
        if (navigationHot_ == -6)
        {
            const auto hover = Brush(theme.hover);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(Scale(286), Scale(8), Scale(416), Scale(78)), Scale(10), Scale(10)), hover.Get());
        }
        clockCaptionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (weather.available)
        {
            DrawWeatherIcon(theme, weather.code, weather.daylight, 311, 31);
            wchar_t temperature[24]{};
            swprintf_s(temperature, L"%.0f°C", weather.temperature);
            DrawText(temperature, temperatureFormat_.Get(),
                D2D1::RectF(Scale(334), Scale(16), Scale(416), Scale(50)), theme.primary);
            const bool stale = weather.failed || std::chrono::system_clock::now() - weather.updatedAt > std::chrono::hours(1);
            const std::wstring caption = weather.location.city + L" · " +
                (stale ? L"缓存" : weather.condition.empty() ? wincal::WeatherDescription(weather.code) : weather.condition);
            DrawText(caption.c_str(), clockCaptionFormat_.Get(),
                D2D1::RectF(Scale(286), Scale(57), Scale(416), Scale(75)), theme.secondary);
        }
        else
        {
            DrawText(weather.failed ? L"天气暂不可用" : L"正在获取天气", clockCaptionFormat_.Get(),
                D2D1::RectF(Scale(286), Scale(24), Scale(416), Scale(42)), theme.secondary);
            const std::wstring caption = weather.location.city + L" · 点击设置";
            DrawText(caption.c_str(), clockCaptionFormat_.Get(),
                D2D1::RectF(Scale(286), Scale(51), Scale(416), Scale(71)), theme.muted);
        }
        clockCaptionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const auto separator = Brush(theme.separator);
        renderTarget_->DrawLine({Scale(18), Scale(kClockHeight)}, {Scale(412), Scale(kClockHeight)}, separator.Get(), Scale(1));
    }

    void DrawWeatherForecast(const Theme& theme)
    {
        if (!EnsureClockFormats()) return;
        auto weather = weatherData_.Snapshot();
#ifdef WINCAL_SETTINGS_PREVIEW
        weather.location.city = L"北京";
        weather.available = true;
        weather.temperature = 14;
        weather.feelsLike = 12;
        weather.humidity = 48;
        weather.windSpeed = 18;
        weather.uvIndex = 3.4;
        weather.uvDailyMaximum = true;
        weather.updatedAt = std::chrono::system_clock::now();
        weather.days = {{L"2026-11-02", 14, 3, 2}, {L"2026-11-03", 16, 5, 0},
            {L"2026-11-04", 15, 6, 3}, {L"2026-11-05", 11, 4, 61},
            {L"2026-11-06", 10, 1, 71}, {L"2026-11-07", 13, 2, 2}, {L"2026-11-08", 15, 4, 0}};
        if (weatherWttrPreview_)
        {
            weather.provider = L"wttr.in";
            weather.days.resize(3);
            weather.uvDailyMaximum = false;
        }
#endif
        DrawText(L"天气详情", titleFormat_.Get(), D2D1::RectF(Scale(22), Scale(20), Scale(330), Scale(58)), theme.primary);
        DrawText(L"刷新", bodyFormat_.Get(), D2D1::RectF(Scale(354), Scale(62), Scale(410), Scale(86)), theme.accent);
        const auto close = Brush(theme.secondary);
        renderTarget_->DrawLine({Scale(390), Scale(25)}, {Scale(402), Scale(37)}, close.Get(), Scale(1.7f));
        renderTarget_->DrawLine({Scale(402), Scale(25)}, {Scale(390), Scale(37)}, close.Get(), Scale(1.7f));
        const auto todayDate = Today();
        wchar_t todayStamp[16]{};
        swprintf_s(todayStamp, L"%04d-%02d-%02d", todayDate.year, todayDate.month, todayDate.day);
        const bool stale = weather.failed || std::chrono::system_clock::now() - weather.updatedAt > std::chrono::hours(1) ||
            (!weather.days.empty() && weather.days.front().date != todayStamp);
        const std::wstring subtitle = weather.location.city + (stale ? L" · 缓存预报" : L" · 今天起的天气趋势");
        DrawText(subtitle.c_str(), clockCaptionFormat_.Get(), D2D1::RectF(Scale(23), Scale(64), Scale(407), Scale(84)), theme.secondary);
        const auto metric = [](const std::optional<double>& value, const wchar_t* unit)
        {
            if (!value) return std::wstring{L"—"};
            wchar_t text[40]{};
            swprintf_s(text, L"%.1f%s", *value, unit);
            return std::wstring{text};
        };
        const auto summary = [&](const std::wstring& value, float top, float bottom, const D2D1_COLOR_F& color)
        {
            DrawText(value.c_str(), clockCaptionFormat_.Get(),
                {Scale(23), Scale(top), Scale(407), Scale(bottom)}, color);
        };
        summary(weather.available ? (weather.condition.empty() ? wincal::WeatherDescription(weather.code) : weather.condition) + L"  ·  气温 " +
            metric(weather.temperature, L"℃") + L"  ·  体感 " + metric(weather.feelsLike, L"℃") : L"正在获取天气…", 100, 126, theme.primary);
        summary(L"湿度 " + metric(weather.humidity, L"%") + L"  ·  风速 " + metric(weather.windSpeed, L" km/h"), 130, 154, theme.secondary);
        summary((weather.uvDailyMaximum ? std::wstring{L"紫外线（今日最高） "} : std::wstring{L"紫外线（当前） "}) +
            metric(weather.uvIndex, L"") + (weather.uvIndex ? L" · " + wincal::UvLevel(*weather.uvIndex) : L""), 158, 182, theme.secondary);
        summary(L"穿衣建议：" + wincal::ClothingAdvice(weather), 186, 208, theme.primary);
        summary(L"根据天气生成 · 请结合个人体感调整", 210, 228, theme.muted);
        if (weather.days.empty())
        {
            DrawText(weather.failed ? L"暂时无法获取预报，点击右上角重试" : L"正在获取天气预报…",
                bodyFormat_.Get(), D2D1::RectF(Scale(34), Scale(190), Scale(405), Scale(250)), theme.secondary);
            return;
        }
        const int count = static_cast<int>(weather.days.size());
        const auto warm = D2D1::ColorF(dark_ ? 0xFFBA75 : 0xDA873C);
        const auto cool = D2D1::ColorF(dark_ ? 0x7CBDF4 : 0x4D95D4);
        const auto grid = Brush(theme.separator);
        const auto card = Brush(theme.card);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(Scale(16), Scale(230), Scale(414), Scale(548)), Scale(14), Scale(14)), card.Get());
        double minimum = weather.days[0].low, maximum = weather.days[0].high;
        for (const auto& day : weather.days) { minimum = std::min(minimum, day.low); maximum = std::max(maximum, day.high); }
        const double range = std::max(12.0, maximum - minimum);
        const auto xy = [&](int i, double value)
        {
            return D2D1::Point2F(Scale(46 + (count > 1 ? i * 338.0f / (count - 1) : 169.0f)), Scale(490 - static_cast<float>((value - minimum) / range) * 112));
        };
        clockCaptionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        constexpr std::array<const wchar_t*, 7> weekdays{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
        for (int i = 0; i < count; ++i)
        {
            const float x = 46 + (count > 1 ? i * 338.0f / (count - 1) : 169.0f);
            const auto& day = weather.days[i];
            int year{}, month{}, date{};
            swscanf_s(day.date.c_str(), L"%d-%d-%d", &year, &month, &date);
            const auto weekday = static_cast<size_t>((DaysFromCivil(year, month, date) + 4) % 7 + 7) % 7;
            const auto today = Today();
            const auto delta = DaysFromCivil(year, month, date) - DaysFromCivil(today.year, today.month, today.day);
            const std::wstring label = delta == 0 ? L"今天" : delta == 1 ? L"明天" : weekdays[weekday];
            const auto textRect = [&](float y, float h) { return D2D1::RectF(Scale(x - 27), Scale(y), Scale(x + 27), Scale(y + h)); };
            DrawText(label.c_str(), clockCaptionFormat_.Get(), textRect(240, 20), i == 0 ? theme.accent : theme.primary);
            wchar_t stamp[24]{};
            swprintf_s(stamp, L"%02d/%02d", month, date);
            DrawText(stamp, clockCaptionFormat_.Get(), textRect(262, 18), theme.muted);
            DrawWeatherIcon(theme, day.code, true, x, 300, false);
            DrawText(wincal::WeatherDescription(day.code).c_str(), clockCaptionFormat_.Get(), textRect(324, 20), theme.secondary);
        }
        for (int i = 0; i < 3; ++i)
            renderTarget_->DrawLine({Scale(32), Scale(370 + i * 52.0f)}, {Scale(398), Scale(370 + i * 52.0f)}, grid.Get(), Scale(0.6f));
        for (bool high : {true, false})
        {
            ComPtr<ID2D1PathGeometry> curve;
            ComPtr<ID2D1GeometrySink> sink;
            const auto value = [&](int i) { return high ? weather.days[i].high : weather.days[i].low; };
            const auto ink = Brush(high ? warm : cool);
            if (SUCCEEDED(factory_->CreatePathGeometry(&curve)) && SUCCEEDED(curve->Open(&sink)))
            {
                sink->BeginFigure(xy(0, value(0)), D2D1_FIGURE_BEGIN_HOLLOW);
                for (int i = 1; i < count; ++i)
                {
                    const auto a = xy(i - 1, value(i - 1)), b = xy(i, value(i));
                    const float middle = (a.x + b.x) / 2;
                    sink->AddBezier(D2D1::BezierSegment({middle, a.y}, {middle, b.y}, b));
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                renderTarget_->DrawGeometry(curve.Get(), ink.Get(), Scale(2));
            }
            for (int i = 0; i < count; ++i)
            {
                const auto p = xy(i, value(i));
                renderTarget_->FillEllipse(D2D1::Ellipse(p, Scale(3.4f), Scale(3.4f)), card.Get());
                renderTarget_->DrawEllipse(D2D1::Ellipse(p, Scale(3.4f), Scale(3.4f)), ink.Get(), Scale(1.7f));
                wchar_t temperature[16]{};
                swprintf_s(temperature, L"%.0f°", value(i));
                const float top = p.y + Scale(high ? -25.0f : 8.0f);
                DrawText(temperature, clockCaptionFormat_.Get(), {p.x - Scale(25), top, p.x + Scale(25), top + Scale(18)}, high ? warm : cool);
            }
        }
        clockCaptionFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const auto warmBrush = Brush(warm), coolBrush = Brush(cool);
        renderTarget_->DrawLine({Scale(109), Scale(561)}, {Scale(127), Scale(561)}, warmBrush.Get(), Scale(2));
        renderTarget_->DrawLine({Scale(232), Scale(561)}, {Scale(250), Scale(561)}, coolBrush.Get(), Scale(2));
        DrawText(L"最高温度", clockCaptionFormat_.Get(), D2D1::RectF(Scale(136), Scale(550), Scale(221), Scale(572)), theme.secondary);
        DrawText(L"最低温度", clockCaptionFormat_.Get(), D2D1::RectF(Scale(259), Scale(550), Scale(349), Scale(572)), theme.secondary);
        const auto stamp = std::chrono::system_clock::to_time_t(weather.updatedAt);
        std::tm parts{};
        localtime_s(&parts, &stamp);
        wchar_t footer[96]{};
        swprintf_s(footer, L"%s  ·  %02d:%02d 更新%s", weather.provider.c_str(), parts.tm_hour, parts.tm_min,
            weather.failed ? L" · 刷新失败，显示缓存" : L"");
        DrawText(footer, clockCaptionFormat_.Get(), D2D1::RectF(Scale(23), Scale(580), Scale(413), Scale(604)), theme.muted);
    }

    void DrawDatePicker(const Theme& theme)
    {
        const auto today = Today();
        for (int i = 0; i < 12; ++i)
        {
            const float left = 22.0f + i % 4 * 96.0f;
            const float top = 108.0f + i / 4 * 92.0f;
            const auto rect = D2D1::RectF(Scale(left), Scale(top), Scale(left + 90), Scale(top + 76));
            const bool months = pickerMode_ == DatePickerMode::Months;
            const int value = months ? i + 1 : pickerFirstYear_ + i;
            const bool selected = months ? pickerYear_ == displayYear_ && value == displayMonth_ : value == pickerYear_;
            const bool current = months ? pickerYear_ == today.year && value == today.month : value == today.year;
            const auto pill = D2D1::RectF(rect.left + Scale(8), rect.top + Scale(12),
                rect.right - Scale(8), rect.bottom - Scale(12));
            if (selected || current || navigationHot_ == i)
            {
                const auto background = Brush(selected ? theme.accent : current ? theme.accent : theme.hover,
                    current && !selected && navigationHot_ != i ? 0.10f : 1.0f);
                renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(pill, Scale(26), Scale(26)), background.Get());
            }
            if (current && !selected)
            {
                const auto accent = Brush(theme.accent);
                renderTarget_->FillEllipse(D2D1::Ellipse({(rect.left + rect.right) / 2, rect.bottom - Scale(7)},
                    Scale(2), Scale(2)), accent.Get());
            }
            wchar_t label[24]{};
            swprintf_s(label, months ? L"%d月" : L"%d", value);
            DrawText(label, dayFormat_.Get(), rect, selected ? D2D1::ColorF(0xFFFFFF) : current ? theme.accent : theme.primary);
            if (pickerKeyboardFocus_ && GetFocus() == window_ && pickerFocus_ == i)
            {
                const auto focus = Brush(selected ? D2D1::ColorF(0xFFFFFF) : theme.accent);
                const auto inner = D2D1::RectF(pill.left - Scale(3), pill.top - Scale(3), pill.right + Scale(3), pill.bottom + Scale(3));
                renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(inner, Scale(28), Scale(28)), focus.Get(), Scale(1));
            }
        }
        const auto footer = D2D1::RectF(Scale(22), Scale(412), Scale(408), Scale(450));
        DrawText(pickerMode_ == DatePickerMode::Months ? L"点击上方年份，可切换年份" : L"选择年份后，再选择月份",
            bodyFormat_.Get(), footer, theme.muted);
    }

    int ToolPx(int value) const { return MulDiv(value, toolDpi_, 96); }

    void LayoutToolInput()
    {
        if (!IsWindow(toolInput_) && !IsWindow(fortuneInput_)) return;
        if (IsWindow(fortuneInput_))
            SetWindowPos(fortuneInput_, nullptr, ToolPx(144), ToolPx(58), ToolPx(168), ToolPx(30), SWP_NOZORDER | SWP_NOACTIVATE);
        if (IsWindow(fortuneTimeInput_))
            SetWindowPos(fortuneTimeInput_, nullptr, ToolPx(332), ToolPx(58), ToolPx(102), ToolPx(30), SWP_NOZORDER | SWP_NOACTIVATE);
        if (IsWindow(fortuneGenderInput_))
            SetWindowPos(fortuneGenderInput_, nullptr, ToolPx(382), ToolPx(10), ToolPx(130), ToolPx(140), SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(toolInput_, nullptr, ToolPx(158), ToolPx(102), ToolPx(330), ToolPx(240),
            SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(toolTimeInput_, nullptr, ToolPx(158), ToolPx(102), ToolPx(330), ToolPx(32),
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (toolInputFont_) DeleteObject(toolInputFont_);
        toolInputFont_ = CreateFontW(-ToolPx(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, kCalendarFontFamily);
        SendMessageW(toolInput_, WM_SETFONT, reinterpret_cast<WPARAM>(toolInputFont_), TRUE);
        SendMessageW(fortuneInput_, WM_SETFONT, reinterpret_cast<WPARAM>(toolInputFont_), TRUE);
        SendMessageW(fortuneTimeInput_, WM_SETFONT, reinterpret_cast<WPARAM>(toolInputFont_), TRUE);
        SendMessageW(fortuneGenderInput_, WM_SETFONT, reinterpret_cast<WPARAM>(toolInputFont_), TRUE);
        SendMessageW(toolTimeInput_, WM_SETFONT, reinterpret_cast<WPARAM>(toolInputFont_), TRUE);
    }

    void ShowShutdownInputs()
    {
        ShowWindow(toolInput_, shutdownToolOpen_ && !shutdownAtTime_ ? SW_SHOW : SW_HIDE);
        ShowWindow(toolTimeInput_, shutdownToolOpen_ && shutdownAtTime_ ? SW_SHOW : SW_HIDE);
    }

    std::wstring ShutdownInputValue() const
    {
        if (!shutdownAtTime_) return ReadControlText(toolInput_);
        SYSTEMTIME time{};
        if (DateTime_GetSystemtime(toolTimeInput_, &time) != GDT_VALID) return {};
        wchar_t value[6]{};
        swprintf_s(value, L"%02u:%02u", time.wHour, time.wMinute);
        return value;
    }

    void PositionTools()
    {
        if (!IsWindow(toolsWindow_)) return;
        RECT calendar{};
        GetWindowRect(window_, &calendar);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        toolDpi_ = dpi_;
        const int width = ToolPx(560), height = calendar.bottom - calendar.top, gap = ToolPx(10);
        LONG x = calendar.left - width - gap;
        if (x < monitor.rcWork.left) x = calendar.right + gap;
        x = std::clamp(x, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
        const LONG y = std::clamp(calendar.top, monitor.rcWork.top,
            std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
        SetWindowPos(toolsWindow_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
        LayoutToolInput();
    }

    void OpenShutdownTool()
    {
        fortuneToolOpen_ = false;
        if (IsWindow(fortuneInput_)) ShowWindow(fortuneInput_, SW_HIDE);
        if (IsWindow(fortuneTimeInput_)) ShowWindow(fortuneTimeInput_, SW_HIDE);
        if (IsWindow(fortuneGenderInput_)) ShowWindow(fortuneGenderInput_, SW_HIDE);
        shutdownToolOpen_ = true;
        if (!IsWindow(toolInput_))
        {
            toolInput_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
                0, 0, 0, 0, toolsWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShutdownInput)), instance_, nullptr);
            for (const auto minutes : {1, 5, 10, 15, 30, 45, 60, 90, 120, 180, 240, 480, 720, 1440, 10080})
            {
                const auto label = std::to_wstring(minutes);
                SendMessageW(toolInput_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendMessageW(toolInput_, CB_LIMITTEXT, 5, 0);
            SetWindowTextW(toolInput_, L"30");
            SetWindowSubclass(toolInput_, ToolInputProc, 1, reinterpret_cast<DWORD_PTR>(this));
            COMBOBOXINFO combo{sizeof(combo)};
            if (GetComboBoxInfo(toolInput_, &combo))
                SetWindowSubclass(combo.hwndItem, ToolInputProc, 1, reinterpret_cast<DWORD_PTR>(this));
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES};
            InitCommonControlsEx(&controls);
            toolTimeInput_ = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_TABSTOP | DTS_TIMEFORMAT,
                0, 0, 0, 0, toolsWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShutdownTimeInput)), instance_, nullptr);
            SendMessageW(toolTimeInput_, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH':'mm"));
            SYSTEMTIME time{};
            GetLocalTime(&time);
            time.wHour = 23; time.wMinute = 30; time.wSecond = 0;
            DateTime_SetSystemtime(toolTimeInput_, GDT_VALID, &time);
            SetWindowSubclass(toolTimeInput_, ToolInputProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        LayoutToolInput();
        ShowShutdownInputs();
        SetFocus(shutdownAtTime_ ? toolTimeInput_ : toolInput_);
        InvalidateRect(toolsWindow_, nullptr, FALSE);
    }

    static LRESULT CALLBACK FortuneInputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR, DWORD_PTR data)
    {
        auto* app = reinterpret_cast<App*>(data);
        if (message == WM_KEYDOWN && wParam == VK_ESCAPE && !DateTime_GetMonthCal(window)) { app->BackFromTool(); return 0; }
        if (message == WM_KEYDOWN && wParam == VK_RETURN) { app->GenerateFortune(); return 0; }
        if (message == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE)) return 0;
        return DefSubclassProc(window, message, wParam, lParam);
    }

    void OpenFortuneTool()
    {
        shutdownToolOpen_ = false;
        if (IsWindow(toolInput_)) ShowWindow(toolInput_, SW_HIDE);
        if (IsWindow(toolTimeInput_)) ShowWindow(toolTimeInput_, SW_HIDE);
        fortuneToolOpen_ = true;
        if (!IsWindow(fortuneInput_))
        {
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES};
            InitCommonControlsEx(&controls);
            fortuneInput_ = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                WS_CHILD | WS_TABSTOP | DTS_SHOWNONE | DTS_SHORTDATEFORMAT, 0, 0, 0, 0, toolsWindow_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFortuneInput)), instance_, nullptr);
            SendMessageW(fortuneInput_, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"yyyy'-'MM'-'dd"));
            SYSTEMTIME birth{};
            if (const auto saved = wincal::ParseFortuneBirthday(settings_.fortuneBirthday))
            {
                const std::chrono::year_month_day date{*saved};
                birth.wYear = static_cast<WORD>(static_cast<int>(date.year()));
                birth.wMonth = static_cast<WORD>(static_cast<unsigned>(date.month()));
                birth.wDay = static_cast<WORD>(static_cast<unsigned>(date.day()));
                DateTime_SetSystemtime(fortuneInput_, GDT_VALID, &birth);
            }
            else DateTime_SetSystemtime(fortuneInput_, GDT_NONE, &birth);
            SetWindowSubclass(fortuneInput_, FortuneInputProc, 1, reinterpret_cast<DWORD_PTR>(this));
            fortuneTimeInput_ = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | WS_TABSTOP | DTS_TIMEFORMAT | DTS_SHOWNONE,
                0, 0, 0, 0, toolsWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFortuneTimeInput)), instance_, nullptr);
            SendMessageW(fortuneTimeInput_, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH':'mm"));
            SYSTEMTIME time{};
            GetLocalTime(&time);
            const auto& savedTime = settings_.fortuneBirthTime;
            if (savedTime.size() == 5 && savedTime[2] == L':' &&
                savedTime[0] >= L'0' && savedTime[0] <= L'2' && savedTime[1] >= L'0' && savedTime[1] <= L'9' &&
                savedTime[3] >= L'0' && savedTime[3] <= L'5' && savedTime[4] >= L'0' && savedTime[4] <= L'9' &&
                (savedTime[0] - L'0') * 10 + savedTime[1] - L'0' < 24)
            {
                time.wHour = static_cast<WORD>((savedTime[0] - L'0') * 10 + savedTime[1] - L'0');
                time.wMinute = static_cast<WORD>((savedTime[3] - L'0') * 10 + savedTime[4] - L'0');
                DateTime_SetSystemtime(fortuneTimeInput_, GDT_VALID, &time);
            }
            else DateTime_SetSystemtime(fortuneTimeInput_, GDT_NONE, &time);
            SetWindowSubclass(fortuneTimeInput_, FortuneInputProc, 1, reinterpret_cast<DWORD_PTR>(this));
            fortuneGenderInput_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
                0, 0, 0, 0, toolsWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFortuneGenderInput)), instance_, nullptr);
            for (const auto label : {L"性别未填", L"男", L"女"})
                SendMessageW(fortuneGenderInput_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            SendMessageW(fortuneGenderInput_, CB_SETCURSEL,
                settings_.fortuneGender == L"Male" ? 1 : settings_.fortuneGender == L"Female" ? 2 : 0, 0);
        }
        SYSTEMTIME range[2]{};
        range[0].wYear = 1900; range[0].wMonth = 1; range[0].wDay = 1;
        GetLocalTime(&range[1]);
        DateTime_SetRange(fortuneInput_, GDTR_MIN | GDTR_MAX, range);
        LayoutToolInput();
        ShowWindow(fortuneInput_, SW_SHOW);
        ShowWindow(fortuneTimeInput_, SW_SHOW);
        ShowWindow(fortuneGenderInput_, SW_SHOW);
        SetFocus(fortuneInput_);
        GenerateFortune();
        InvalidateRect(toolsWindow_, nullptr, FALSE);
    }

    void GenerateFortune()
    {
        SYSTEMTIME date{};
        wchar_t value[11]{};
        if (DateTime_GetSystemtime(fortuneInput_, &date) == GDT_VALID)
            swprintf_s(value, L"%04u-%02u-%02u", date.wYear, date.wMonth, date.wDay);
        SYSTEMTIME birthTime{};
        wchar_t timeValue[6]{};
        if (DateTime_GetSystemtime(fortuneTimeInput_, &birthTime) == GDT_VALID)
            swprintf_s(timeValue, L"%02u:%02u", birthTime.wHour, birthTime.wMinute);
        const auto birth = wincal::ParseFortuneBirthday(value);
        const auto selection = SendMessageW(fortuneGenderInput_, CB_GETCURSEL, 0, 0);
        const std::wstring genderValue = selection == 1 ? L"Male" : selection == 2 ? L"Female" : L"Unknown";
        const auto gender = selection == 1 ? wincal::FortuneGender::Male : selection == 2 ?
            wincal::FortuneGender::Female : wincal::FortuneGender::Unknown;
        const auto today = Today();
        const auto start = std::chrono::sys_days{std::chrono::year{today.year}/static_cast<unsigned>(today.month)/static_cast<unsigned>(today.day)};
        if (!birth || *birth > start)
        {
            fortuneStatus_ = L"请选择公历生日，不能晚于今天。";
            fortuneBirth_.reset();
            fortuneChart_.reset();
        }
        else
        {
            fortuneBirth_ = birth;
            fortuneChart_ = wincal::BuildFortuneChart(*birth, wincal::ParseFortuneTime(timeValue), 0, gender);
            fortuneStart_ = start;
            fortuneSelected_ = 0;
            fortuneStatus_.clear();
#ifndef WINCAL_SETTINGS_PREVIEW
            if (settings_.fortuneBirthday != value || settings_.fortuneBirthTime != timeValue || settings_.fortuneGender != genderValue)
            {
                auto saved = wincal::SettingsStore::Load();
                saved.fortuneBirthday = value;
                saved.fortuneBirthTime = timeValue;
                saved.fortuneGender = genderValue;
                if (wincal::SettingsStore::Save(saved))
                {
                    settings_.fortuneBirthday = value;
                    settingsDraft_.fortuneBirthday = value;
                    settings_.fortuneBirthTime = timeValue;
                    settingsDraft_.fortuneBirthTime = timeValue;
                    settings_.fortuneGender = genderValue;
                    settingsDraft_.fortuneGender = genderValue;
                }
                else fortuneStatus_ = L"本次结果已生成，生日保存失败。";
            }
#endif
        }
        InvalidateRect(toolsWindow_, nullptr, FALSE);
    }

    void DrawFortunePanel(const Theme& theme)
    {
        ComPtr<IDWriteTextFormat> normal, captionFormat, large;
        const auto create = [&](float size, IDWriteTextFormat** format)
        {
            return writeFactory_->CreateTextFormat(kCalendarFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Scale(size), L"zh-CN", format);
        };
        if (FAILED(create(14, normal.GetAddressOf())) || FAILED(create(12, captionFormat.GetAddressOf())) ||
            FAILED(create(36, large.GetAddressOf()))) return;
        const auto text = [&](const std::wstring& value, IDWriteTextFormat* font, float left, float top, float right, float bottom,
                              D2D1_COLOR_F color)
        {
            DrawText(value.c_str(), font, {Scale(left), Scale(top), Scale(right), Scale(bottom)}, color);
        };
        const auto card = [&](float left, float top, float right, float bottom, D2D1_COLOR_F color)
        {
            const auto brush = Brush(color);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
                {Scale(left), Scale(top), Scale(right), Scale(bottom)}, Scale(10), Scale(10)), brush.Get());
        };
        text(L"运势 · 出生日期 / 时间", normal.Get(), 140, 12, 372, 40, theme.primary);
        if (!IsWindow(fortuneGenderInput_))
        {
            card(382, 8, 512, 38, theme.hover);
            text(settings_.fortuneGender == L"Male" ? L"性别：男" : settings_.fortuneGender == L"Female" ? L"性别：女" : L"性别未填",
                normal.Get(), 390, 12, 510, 40, theme.primary);
        }
        card(138, 50, 318, 94, theme.hover);
        card(326, 50, 440, 94, theme.hover);
        if (!IsWindow(fortuneInput_))
        {
            std::wstring profile = L"出生日期";
            if (fortuneBirth_)
            {
                const std::chrono::year_month_day born{*fortuneBirth_};
                wchar_t value[16]{};
                swprintf_s(value, L"%04d-%02u-%02u", static_cast<int>(born.year()),
                    static_cast<unsigned>(born.month()), static_cast<unsigned>(born.day()));
                profile = value;
            }
            text(profile, normal.Get(), 144, 61, 312, 90, theme.primary);
        }
        if (!IsWindow(fortuneTimeInput_))
            text(settings_.fortuneBirthTime.empty() ? L"出生时间" : settings_.fortuneBirthTime,
                normal.Get(), 332, 61, 434, 90, theme.primary);
        card(448, 50, 538, 94, theme.accent);
        text(L"更新", normal.Get(), 474, 61, 536, 89, D2D1::ColorF(0xFFFFFF));
        if (!fortuneChart_)
        {
            text(fortuneStatus_.empty() ? L"输入公历出生日期，查看当天评分及未来 15 天走势。" : fortuneStatus_,
                normal.Get(), 146, 120, 526, 204, theme.primary);
            text(L"四柱流日规则 v3\n结合月令旺衰、通根透干，分层计算大运、流年、流月与流日。\n\n出生时间留空时省略时柱。大运需填写时间和性别。\n生辰与性别随配置同步至坚果云。",
                normal.Get(), 146, 224, 526, 456, theme.secondary);
            return;
        }
        std::array<wincal::FortuneDay, 15> scores;
        for (int i = 0; i < 15; ++i) scores[i] = wincal::FortuneForDate(*fortuneChart_, fortuneStart_ + std::chrono::days{i});
        const auto& selected = scores[fortuneSelected_];
        const std::chrono::year_month_day date{fortuneStart_ + std::chrono::days{fortuneSelected_}};
        const auto stamp = std::to_wstring(static_cast<unsigned>(date.month())) + L"月" +
            std::to_wstring(static_cast<unsigned>(date.day())) + L"日";
        const auto scoreColor = selected.overall >= 75 ? D2D1::ColorF(0x25B598) : selected.overall >= 55 ?
            theme.accent : D2D1::ColorF(dark_ ? 0xF6BE76 : 0xB87527);
        card(138, 106, 538, 200, theme.card);
        text(std::to_wstring(selected.overall), large.Get(), 154, 112, 234, 170, scoreColor);
        text(stamp + (fortuneSelected_ == 0 ? L" · 今天" : L""), normal.Get(), 246, 120, 520, 146, theme.primary);
        text(std::wstring{wincal::FortuneLabel(selected.overall)}, normal.Get(), 246, 150, 520, 178, scoreColor);
        text(L"综合评分 / 100", captionFormat.Get(), 154, 174, 330, 196, theme.secondary);
        constexpr const wchar_t* labels[]{L"人际", L"行动", L"节奏"};
        const int values[]{selected.social, selected.work, selected.vitality};
        for (int i = 0; i < 3; ++i)
        {
            const float x = 146.0f + i * 132;
            text(std::wstring{labels[i]} + L" " + std::to_wstring(values[i]), normal.Get(), x, 208, x + 120, 235, theme.secondary);
            card(x, 239, x + 112, 244, theme.hover);
            card(x, 239, x + 112 * values[i] / 100, 244, scoreColor);
        }
        text(L"未来 15 天 · 点击曲线节点切换", captionFormat.Get(), 146, 260, 530, 284, theme.secondary);
        card(138, 288, 538, 446, theme.card);
        const auto xy = [&](int i)
        {
            return D2D1::Point2F(Scale(158 + i * 26.0f), Scale(414 - scores[i].overall * 1.08f));
        };
        ComPtr<ID2D1PathGeometry> area;
        ComPtr<ID2D1GeometrySink> areaSink;
        if (SUCCEEDED(factory_->CreatePathGeometry(&area)) && SUCCEEDED(area->Open(&areaSink)))
        {
            areaSink->BeginFigure({Scale(158), Scale(414)}, D2D1_FIGURE_BEGIN_FILLED);
            areaSink->AddLine(xy(0));
            for (int i = 1; i < 15; ++i)
            {
                const auto a = xy(i - 1), b = xy(i);
                const float middle = (a.x + b.x) / 2;
                areaSink->AddBezier(D2D1::BezierSegment({middle, a.y}, {middle, b.y}, b));
            }
            areaSink->AddLine({Scale(522), Scale(414)});
            areaSink->EndFigure(D2D1_FIGURE_END_CLOSED);
            areaSink->Close();
            const D2D1_GRADIENT_STOP stops[]{{0, D2D1::ColorF(0xE6A44F, 0.3f)}, {1, D2D1::ColorF(0xE6A44F, 0.02f)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> gradient;
            if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(stops, 2, &collection)) &&
                SUCCEEDED(renderTarget_->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties({Scale(158), Scale(306)}, {Scale(158), Scale(414)}), collection.Get(), &gradient)))
                renderTarget_->FillGeometry(area.Get(), gradient.Get());
        }
        const auto grid = Brush(theme.separator);
        for (int value : {0, 50, 100})
        {
            const float y = Scale(414 - value * 1.08f);
            renderTarget_->DrawLine({Scale(150), y}, {Scale(526), y}, grid.Get(), Scale(0.7f));
        }
        ComPtr<ID2D1PathGeometry> curve;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(&curve)) && SUCCEEDED(curve->Open(&sink)))
        {
            sink->BeginFigure(xy(0), D2D1_FIGURE_BEGIN_HOLLOW);
            for (int i = 1; i < 15; ++i)
            {
                const auto a = xy(i - 1), b = xy(i);
                const float middle = (a.x + b.x) / 2;
                sink->AddBezier(D2D1::BezierSegment({middle, a.y}, {middle, b.y}, b));
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            const auto line = Brush(D2D1::ColorF(dark_ ? 0xF5BC76 : 0xD8913E));
            renderTarget_->DrawGeometry(curve.Get(), line.Get(), Scale(2.2f));
        }
        const auto marker = Brush(scoreColor), surface = Brush(theme.card);
        const auto nodeOutline = Brush(D2D1::ColorF(dark_ ? 0xF5BC76 : 0xD8913E));
        const auto position = xy(fortuneSelected_);
        renderTarget_->DrawLine({position.x, Scale(298)}, {position.x, Scale(414)}, marker.Get(), Scale(1));
        for (int i = 0; i < 15; ++i)
        {
            renderTarget_->FillEllipse(D2D1::Ellipse(xy(i), Scale(i == fortuneSelected_ ? 5.0f : 2.4f),
                Scale(i == fortuneSelected_ ? 5.0f : 2.4f)), i == fortuneSelected_ ? marker.Get() : surface.Get());
            if (i != fortuneSelected_)
                renderTarget_->DrawEllipse(D2D1::Ellipse(xy(i), Scale(2.4f), Scale(2.4f)), nodeOutline.Get(), Scale(1));
            if (i == 0 || i == 7 || i == 14)
            {
                const std::chrono::year_month_day d{fortuneStart_ + std::chrono::days{i}};
                captionFormat->SetTextAlignment(i == 14 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
                const float left = i == 14 ? 476.0f : 146 + i * 26.0f;
                text(std::to_wstring(static_cast<unsigned>(d.month())) + L"/" + std::to_wstring(static_cast<unsigned>(d.day())),
                    captionFormat.Get(), left, 420, i == 14 ? 528.0f : left + 65, 442, theme.secondary);
                captionFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
        const auto low = static_cast<int>(std::min_element(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.overall < b.overall; }) - scores.begin());
        const auto high = static_cast<int>(std::max_element(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.overall < b.overall; }) - scores.begin());
        for (const int index : {low, high})
        {
            const auto point = xy(index);
            const auto left = std::clamp(158 + index * 26.0f - 18, 146.0f, 498.0f);
            text(std::to_wstring(scores[index].overall), captionFormat.Get(), left,
                point.y / Scale(1) - 22, left + 40, point.y / Scale(1) - 2, theme.secondary);
        }
        text(L"当日提示 · " + stamp, normal.Get(), 146, 456, 530, 480, theme.primary);
        text(wincal::FortuneAdvice(selected), captionFormat.Get(), 146, 484, 532, 532, theme.secondary);
        const auto signedValue = [](int value) { return (value >= 0 ? L"+" : L"") + std::to_wstring(value); };
        text(fortuneChart_->Text() + L" · " + (fortuneChart_->strength > 0 ? L"偏强" : fortuneChart_->strength < 0 ? L"偏弱" : L"中和") +
            L"\n运 " + signedValue(selected.luckBonus) + L" · 年 " + signedValue(selected.yearBonus) + L" · 月 " + signedValue(selected.monthBonus) +
            L" · 日支 " + signedValue(selected.branchBonus) + L"（点击依据）", captionFormat.Get(), 146, 536, 532, 578, theme.secondary);
        text(fortuneStatus_.empty() ? (fortuneChart_->gender == wincal::FortuneGender::Unknown ? L"未填性别，未计大运 · 北京时间 · 娱乐参考" :
            !fortuneChart_->hasTime ? L"未填时间，未计大运 · 北京时间 · 娱乐参考" : L"月令与运年月日规则 v3 · 北京时间 · 娱乐参考") : fortuneStatus_,
            captionFormat.Get(), 146, 582, 532, 608, theme.muted);
    }

    void ToggleTools()
    {
        toolsOpen_ = !toolsOpen_;
        if (toolsOpen_)
        {
            CloseWeatherForecast();
            CloseDetailWindow();
            shutdownToolOpen_ = false;
            fortuneToolOpen_ = false;
            if (!IsWindow(toolsWindow_))
            {
                WNDCLASSW wc{};
                wc.lpfnWndProc = ToolsWindowProc;
                wc.hInstance = instance_;
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.lpszClassName = L"WinCalNativeTools";
                RegisterClassW(&wc);
                toolsWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"WinCal 工具",
                    WS_POPUP | WS_CLIPCHILDREN, 0, 0, ToolPx(560), ToolPx(254), window_, nullptr, instance_, this);
            }
            PositionTools();
            const BOOL dark = dark_;
            DwmSetWindowAttribute(toolsWindow_, 20, &dark, sizeof(dark));
            const DWORD corner = 2;
            DwmSetWindowAttribute(toolsWindow_, 33, &corner, sizeof(corner));
        }
        if (IsWindow(toolInput_)) ShowWindow(toolInput_, SW_HIDE);
        if (IsWindow(toolTimeInput_)) ShowWindow(toolTimeInput_, SW_HIDE);
        if (IsWindow(fortuneInput_)) ShowWindow(fortuneInput_, SW_HIDE);
        if (IsWindow(fortuneTimeInput_)) ShowWindow(fortuneTimeInput_, SW_HIDE);
        if (IsWindow(fortuneGenderInput_)) ShowWindow(fortuneGenderInput_, SW_HIDE);
        ShowWindow(toolsWindow_, toolsOpen_ ? SW_SHOW : SW_HIDE);
        SetFocus(toolsOpen_ ? toolsWindow_ : window_);
        PresentAgenda();
        InvalidateRect(window_, nullptr, FALSE);
    }

    static LRESULT CALLBACK ToolsWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->toolsWindow_ = window;
        }
        return app ? app->HandleToolsMessage(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT HandleToolsMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            PaintTools();
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            return HandleMessage(message, wParam, lParam);
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == fortuneInput_ &&
                reinterpret_cast<NMHDR*>(lParam)->code == DTN_DROPDOWN)
            {
                KillTimer(window_, kOutsideClickTimer);
                const HWND calendar = DateTime_GetMonthCal(fortuneInput_);
                MonthCal_SetFirstDayOfWeek(calendar, 0);
            }
            break;
        case WM_CLOSE:
            if (toolsOpen_) ToggleTools();
            return 0;
        case WM_SIZE:
            toolsRenderTarget_.Reset();
            return 0;
        case WM_DPICHANGED:
            toolsRenderTarget_.Reset();
            PositionTools();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) BackFromTool();
            return 0;
        case WM_LBUTTONUP:
        {
            const int localX = MulDiv(GET_X_LPARAM(lParam), 96, toolDpi_);
            const int localY = MulDiv(GET_Y_LPARAM(lParam), 96, toolDpi_);
            if (localX >= 520 && localY < 40) { ToggleTools(); return 0; }
            if (localX >= 16 && localX < 100 && localY >= 50 && localY < 138)
            {
                OpenShutdownTool();
                return 0;
            }
            if (localX >= 16 && localX < 100 && localY >= 150 && localY < 238)
            {
                OpenFortuneTool();
                return 0;
            }
            if (fortuneToolOpen_)
            {
                if (localX >= 448 && localX < 538 && localY >= 50 && localY < 94) GenerateFortune();
                else if (localX >= 140 && localX <= 538 && localY >= 536 && localY < 580)
                {
                    if (fortuneChart_)
                    {
                        const auto selected = wincal::FortuneForDate(*fortuneChart_, fortuneStart_ + std::chrono::days{fortuneSelected_});
                        MessageBoxW(window, wincal::FortuneRuleDetails(*fortuneChart_, selected).c_str(),
                            L"四柱流日评分依据 v3", MB_OK | MB_ICONINFORMATION);
                    }
                }
                else if (fortuneBirth_ && localX >= 150 && localX <= 530 && localY >= 288 && localY <= 432)
                {
                    fortuneSelected_ = std::clamp(static_cast<int>(std::lround((localX - 158) * 14.0 / 364)), 0, 14);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            if (!shutdownToolOpen_ || localX < 130 || localX >= 520) return 0;
            const int x = localX - 110;
            const int y = localY + 250;
            if (y >= 300 && y < 334 && !shutdownDue_)
            {
                shutdownAtTime_ = x >= 215;
                ShowShutdownInputs();
                toolStatus_.clear();
            }
            else if (y >= 446 && y < 480)
            {
                if (shutdownDue_) CancelShutdownSchedule();
                else StartShutdownSchedule();
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_NCDESTROY:
            toolsRenderTarget_.Reset();
            toolsWindow_ = nullptr;
            toolInput_ = nullptr;
            toolTimeInput_ = nullptr;
            fortuneInput_ = nullptr;
            fortuneTimeInput_ = nullptr;
            fortuneGenderInput_ = nullptr;
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void BackFromTool()
    {
        if (!shutdownToolOpen_ && !fortuneToolOpen_) { ToggleTools(); return; }
        shutdownToolOpen_ = false;
        fortuneToolOpen_ = false;
        if (IsWindow(fortuneInput_)) ShowWindow(fortuneInput_, SW_HIDE);
        if (IsWindow(fortuneTimeInput_)) ShowWindow(fortuneTimeInput_, SW_HIDE);
        if (IsWindow(fortuneGenderInput_)) ShowWindow(fortuneGenderInput_, SW_HIDE);
        ShowWindow(toolInput_, SW_HIDE);
        ShowWindow(toolTimeInput_, SW_HIDE);
        SetFocus(toolsWindow_);
        InvalidateRect(toolsWindow_, nullptr, FALSE);
    }

    void PaintTools()
    {
        if (!EnsureDeviceResources()) return;
        if (!toolsRenderTarget_)
        {
            RECT client{};
            GetClientRect(toolsWindow_, &client);
            if (FAILED(factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), 96, 96),
                D2D1::HwndRenderTargetProperties(toolsWindow_, D2D1::SizeU(client.right, client.bottom)),
                toolsRenderTarget_.ReleaseAndGetAddressOf()))) return;
        }
        const auto calendarTarget = renderTarget_;
        renderTarget_ = toolsRenderTarget_;
        const auto theme = GetTheme(dark_);
        renderTarget_->BeginDraw();
        renderTarget_->Clear(theme.background);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Translation(-Scale(20), -Scale(250)));
        DrawToolPanel(theme);
        if (renderTarget_->EndDraw() == D2DERR_RECREATE_TARGET) toolsRenderTarget_.Reset();
        renderTarget_ = calendarTarget;
    }

    static LRESULT CALLBACK ToolInputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR data)
    {
        auto* app = reinterpret_cast<App*>(data);
        if (message == WM_KEYDOWN && wParam == VK_ESCAPE)
        {
            if (SendMessageW(app->toolInput_, CB_GETDROPPEDSTATE, 0, 0))
                SendMessageW(app->toolInput_, CB_SHOWDROPDOWN, FALSE, 0);
            else app->BackFromTool();
            return 0;
        }
        // Only the explicit start button arms a shutdown.
        return DefSubclassProc(window, message, wParam, lParam);
    }

    void StartShutdownSchedule()
    {
        if (shutdownDue_) return;
        if (shutdownProcess_) return;
        const auto due = wincal::ShutdownDeadline(ShutdownInputValue(), shutdownAtTime_, std::time(nullptr));
        if (!due)
        {
            toolStatus_ = shutdownAtTime_ ? L"请输入有效时刻，例如 23:30。" : L"请输入 1～10080 分钟。";
        }
        else if (!SetTimer(window_, kShutdownTimer, 1000, nullptr))
            toolStatus_ = L"无法创建计时器，请重试。";
        else
        {
            shutdownDue_ = due;
            toolStatus_.clear();
            EnableWindow(toolInput_, FALSE);
            EnableWindow(toolTimeInput_, FALSE);
        }
        if (IsWindow(toolsWindow_)) InvalidateRect(toolsWindow_, nullptr, FALSE);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void CancelShutdownSchedule()
    {
        shutdownDue_.reset();
        KillTimer(window_, kShutdownTimer);
        EnableWindow(toolInput_, TRUE);
        EnableWindow(toolTimeInput_, TRUE);
        toolStatus_ = L"定时关机已取消。";
        if (IsWindow(toolsWindow_)) InvalidateRect(toolsWindow_, nullptr, FALSE);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void TickShutdownSchedule()
    {
        if (!shutdownDue_) return;
        if (std::time(nullptr) >= *shutdownDue_)
        {
            shutdownDue_.reset();
            EnableWindow(toolInput_, TRUE);
            EnableWindow(toolTimeInput_, TRUE);
            wchar_t systemDir[MAX_PATH]{};
            GetSystemDirectoryW(systemDir, ARRAYSIZE(systemDir));
            const auto executable = std::wstring{systemDir} + L"\\shutdown.exe";
            // No /f: allow Windows and applications to handle unsaved work normally.
            wchar_t command[] = L"shutdown.exe /s /t 0";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            if (CreateProcessW(executable.c_str(), command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, systemDir, &startup, &process))
            {
                shutdownProcess_ = process.hProcess;
                CloseHandle(process.hThread);
                toolStatus_ = L"已向 Windows 发送关机请求。";
            }
            else
            {
                KillTimer(window_, kShutdownTimer);
                toolStatus_ = L"关机请求失败，请检查系统权限。";
            }
        }
        if (IsWindow(toolsWindow_)) InvalidateRect(toolsWindow_, nullptr, FALSE);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void RequestExit()
    {
        if (shutdownDue_ && MessageBoxW(window_, L"定时关机正在计时。退出将取消任务，是否退出？",
            L"WinCal", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
        DestroyWindow(window_);
    }

    // All rectangles here use the popup's absolute logical coordinates.
    int HitTestTools(int x, int y) const
    {
        const float px = x / Scale(1), py = y / Scale(1);
        return py >= kLogicalHeight - 42 && py < kLogicalHeight - 6 && px >= 20 && px < 56 ? 2 : 0;
    }

    void DrawTools(const Theme& theme)
    {
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
        const float top = static_cast<float>(kLogicalHeight - 38);
        const auto rect = D2D1::RectF(Scale(22), Scale(top), Scale(54), Scale(top + 30));
        const auto fill = Brush(theme.hover);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, Scale(7), Scale(7)), fill.Get());
        const auto ink = Brush(theme.primary);
        renderTarget_->DrawLine({Scale(31), Scale(top + 15)}, {Scale(45), Scale(top + 15)}, ink.Get(), Scale(1.8f));
        renderTarget_->DrawLine({Scale(38), Scale(top + 8)}, {Scale(38), Scale(top + 22)}, ink.Get(), Scale(1.8f));
        if (shutdownDue_)
            DrawText(L"定时关机计时中", lunarFormat_.Get(),
                {Scale(66), Scale(top), Scale(264), Scale(top + 30)}, theme.secondary);
    }

    void DrawToolPanel(const Theme& theme)
    {
        const auto button = [&](const wchar_t* text, float left, float top, float right, float bottom, bool active)
        {
            const auto rect = D2D1::RectF(Scale(left), Scale(top), Scale(right), Scale(bottom));
            const auto fill = Brush(active ? theme.accent : theme.hover);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, Scale(7), Scale(7)), fill.Get());
            DrawText(text, bodyFormat_.Get(), rect, active ? D2D1::ColorF(0xFFFFFF) : theme.primary);
        };
        {
            DrawText(L"工具", dayFormat_.Get(), {Scale(36), Scale(260), Scale(130), Scale(290)}, theme.primary);
            DrawText(L"×", bodyFormat_.Get(), {Scale(540), Scale(258), Scale(572), Scale(290)}, theme.secondary);
            if (shutdownToolOpen_)
            {
                const auto selected = Brush(theme.hover);
                renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
                    {Scale(36), Scale(300), Scale(120), Scale(388)}, Scale(8), Scale(8)), selected.Get());
            }
            const auto ink = Brush(theme.accent);
            renderTarget_->DrawEllipse(D2D1::Ellipse({Scale(78), Scale(326)}, Scale(12), Scale(12)),
                ink.Get(), Scale(2));
            renderTarget_->DrawLine({Scale(78), Scale(317)}, {Scale(78), Scale(326)}, ink.Get(), Scale(2));
            renderTarget_->DrawLine({Scale(78), Scale(326)}, {Scale(85), Scale(326)}, ink.Get(), Scale(2));
            DrawText(L"定时关机", bodyFormat_.Get(),
                {Scale(36), Scale(350), Scale(120), Scale(378)}, theme.primary);
            if (shutdownDue_)
                renderTarget_->FillEllipse(D2D1::Ellipse({Scale(100), Scale(310)}, Scale(3), Scale(3)), ink.Get());
            if (fortuneToolOpen_)
            {
                const auto selected = Brush(theme.hover);
                renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
                    {Scale(36), Scale(400), Scale(120), Scale(488)}, Scale(8), Scale(8)), selected.Get());
            }
            renderTarget_->DrawLine({Scale(54), Scale(440)}, {Scale(70), Scale(420)}, ink.Get(), Scale(2));
            renderTarget_->DrawLine({Scale(70), Scale(420)}, {Scale(84), Scale(431)}, ink.Get(), Scale(2));
            renderTarget_->DrawLine({Scale(84), Scale(431)}, {Scale(103), Scale(410)}, ink.Get(), Scale(2));
            DrawText(L"运势", bodyFormat_.Get(), {Scale(36), Scale(450), Scale(120), Scale(478)}, theme.primary);
        }
        const auto divider = Brush(theme.separator);
        renderTarget_->DrawLine({Scale(138), Scale(266)}, {Scale(138), Scale(kLogicalHeight + 234)},
            divider.Get(), Scale(1));
        if (fortuneToolOpen_)
        {
            renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
            DrawFortunePanel(theme);
            return;
        }
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Translation(Scale(110), -Scale(250)));
        if (!shutdownToolOpen_)
        {
            DrawText(L"选择左侧工具查看配置", bodyFormat_.Get(),
                {Scale(36), Scale(300), Scale(394), Scale(360)}, theme.secondary);
            return;
        }
        const auto panel = D2D1::RectF(Scale(20), Scale(250), Scale(410), Scale(504));
        const auto fill = Brush(theme.background);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(panel, Scale(10), Scale(10)), fill.Get());

        DrawText(L"定时关机", dayFormat_.Get(), {Scale(36), Scale(260), Scale(185), Scale(290)}, theme.primary);
        button(L"倒计时", 36, 300, 210, 332, !shutdownAtTime_);
        button(L"指定时刻", 220, 300, 394, 332, shutdownAtTime_);
        const auto input = D2D1::RectF(Scale(36), Scale(344), Scale(394), Scale(388));
        const auto inputBrush = Brush(theme.hover);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(input, Scale(6), Scale(6)), inputBrush.Get());
        if (!IsWindow(toolInput_))
            DrawText(shutdownAtTime_ ? L"23:30" : L"30", bodyFormat_.Get(), input, theme.primary);
        std::wstring status = toolStatus_.empty() ?
            (shutdownAtTime_ ? L"选择小时和分钟；已过的时刻安排在明天。" : L"选择或输入分钟数，支持 1 分钟至 7 天。") : toolStatus_;
        if (shutdownDue_)
        {
            std::tm local{};
            const auto due = *shutdownDue_;
            localtime_s(&local, &due);
            const auto remaining = std::max<std::time_t>(0, due - std::time(nullptr));
            wchar_t text[128]{};
            swprintf_s(text, L"%02d月%02d日 %02d:%02d 关机 · 剩余 %lld:%02lld:%02lld",
                local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                static_cast<long long>(remaining / 3600), static_cast<long long>(remaining / 60 % 60),
                static_cast<long long>(remaining % 60));
            status = text;
        }
        DrawText(status.c_str(), lunarFormat_.Get(), {Scale(30), Scale(398), Scale(400), Scale(436)}, theme.secondary);
        button(shutdownDue_ ? L"取消定时关机" : L"开始计时", 36, 446, 394, 478, !shutdownDue_);
        DrawText(L"隐藏日历后继续计时，退出 WinCal 将取消任务。", lunarFormat_.Get(),
            {Scale(30), Scale(480), Scale(400), Scale(502)}, theme.muted);
    }

    void Paint()
    {
        if (!EnsureDeviceResources() || !renderTarget_)
            return;

        const auto theme = GetTheme(dark_);
        renderTarget_->BeginDraw();
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
        renderTarget_->Clear(theme.background);
#ifdef WINCAL_SETTINGS_PREVIEW
        if (weatherForecastPreview_) DrawWeatherForecast(theme);
        else
#endif
        {
        DrawClockAndWeather(theme);
        renderTarget_->SetTransform(D2D1::Matrix3x2F::Translation(0, Scale(kClockHeight)));

        wchar_t title[64]{};
        swprintf_s(title, L"%s", NavigationTitle().c_str());
        const auto titleRect = D2D1::RectF(Scale(22), Scale(kHeaderTop), Scale(260), Scale(62));
        if (navigationHot_ == -2)
        {
            const auto hover = Brush(theme.hover);
            renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(Scale(16), Scale(18), Scale(NavigationTitleRight() + 28), Scale(54)), Scale(8), Scale(8)), hover.Get());
        }
        if (scrollAnimating_ || slideAnimating_)
        {
            float linear = scrollProgress_;
            if (slideAnimating_)
            {
                // 轨道行进比例：|pos|/|target|，链式滚动时随轨道逼近目标而淡入新标题。
                const float denominator = std::fabs(slideTarget_);
                linear = denominator > 0.0f ? std::fabs(slidePos_) / denominator : 1.0f;
                linear = std::clamp(linear, 0.0f, 1.0f);
            }
            const float progress = EaseOutCubic(linear);
            wchar_t fromTitle[64]{};
            swprintf_s(fromTitle, L"%d年%d月", scrollFromYear_, scrollFromMonth_);
            if (progress < 1.0f)
                DrawText(fromTitle, titleFormat_.Get(), titleRect, theme.primary, 1.0f - progress);
            if (progress > 0.0f)
                DrawText(title, titleFormat_.Get(), titleRect, theme.primary, progress);
        }
        else
        {
            DrawText(title, titleFormat_.Get(), titleRect, theme.primary);
        }
        DrawNavigation(theme);

        const auto today = Today();

        const auto separator = Brush(theme.separator);
        renderTarget_->DrawLine(
            D2D1::Point2F(Scale(18), Scale(78)),
            D2D1::Point2F(Scale(412), Scale(78)),
            separator.Get(),
            Scale(1));

        if (pickerMode_ != DatePickerMode::Closed)
            DrawDatePicker(theme);
        else
        {
        static constexpr std::array<const wchar_t*, 7> mondayWeekdays{
            L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
        static constexpr std::array<const wchar_t*, 7> sundayWeekdays{
            L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
        const auto& weekdays = weekStartsMonday_ ? mondayWeekdays : sundayWeekdays;
        for (int column = 0; column < 7; ++column)
        {
            const float left = Scale(static_cast<float>(kGridLeft + column * kCellWidth));
            DrawText(
                weekdays[column],
                bodyFormat_.Get(),
                D2D1::RectF(left, Scale(kWeekTop), left + Scale(kCellWidth), Scale(kGridTop)),
                (weekStartsMonday_ ? column >= 5 : column == 0 || column == 6) ? theme.weekend : theme.secondary);
        }

        if ((!settings_.monthPaging && continuousInitialized_) || scrollAnimating_)
            DrawWeekRows(theme, today);
        else if (slideAnimating_)
            DrawMonthSlide(theme, today);
        else
            DrawMonthGrid(theme, today, displayYear_, displayMonth_, 0.45f + transition_ * 0.55f);
        }

        DrawTools(theme);
        }
        const HRESULT result = renderTarget_->EndDraw();
#ifdef WINCAL_SETTINGS_PREVIEW
        if (FAILED(result))
            fwprintf_s(stderr, L"Paint EndDraw hr=0x%08X\n", static_cast<unsigned>(result));
#endif
        if (result == D2DERR_RECREATE_TARGET)
            DiscardDeviceResources();
    }

    void DiscardDeviceResources()
    {
        toolsRenderTarget_.Reset();
        if (IsWindowVisible(toolsWindow_)) InvalidateRect(toolsWindow_, nullptr, FALSE);
        weatherRenderTarget_.Reset();
        if (IsWindowVisible(weatherWindow_)) InvalidateRect(weatherWindow_, nullptr, FALSE);
        clockFormat_.Reset();
        clockCaptionFormat_.Reset();
        temperatureFormat_.Reset();
        titleFormat_.Reset();
        bodyFormat_.Reset();
        dayFormat_.Reset();
        lunarFormat_.Reset();
        badgeFormat_.Reset();
        renderTarget_.Reset();
        writeFactory_.Reset();
        factory_.Reset();
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == taskbarCreatedMessage_)
        {
            UpdateTaskbarStatus();
            RefreshTaskbarClockBounds();
            return 0;
        }

        switch (message)
        {
        case WM_CLOSE:
            RequestExit();
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        {
            if (reinterpret_cast<HWND>(lParam) != toolInput_ && reinterpret_cast<HWND>(lParam) != fortuneInput_) break;
            const auto theme = GetTheme(dark_);
            const auto color = [](D2D1_COLOR_F c) { return RGB(static_cast<int>(c.r * 255),
                static_cast<int>(c.g * 255), static_cast<int>(c.b * 255)); };
            SetTextColor(reinterpret_cast<HDC>(wParam), color(theme.primary));
            SetBkColor(reinterpret_cast<HDC>(wParam), color(theme.hover));
            SetDCBrushColor(reinterpret_cast<HDC>(wParam), color(theme.hover));
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(window_, &paint);
            Paint();
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOVE:
            PositionAgenda();
            PositionTools();
            PositionWeatherForecast();
            return 0;
        case WM_SIZE:
            LayoutToolInput();
            if (renderTarget_)
                static_cast<ID2D1HwndRenderTarget*>(renderTarget_.Get())
                    ->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            return 0;
        case WM_DPICHANGED:
        {
            dpi_ = HIWORD(wParam);
            LayoutToolInput();
            const auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(
                window_, HWND_TOPMOST, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOACTIVATE);
            DiscardDeviceResources();
            PositionWeatherForecast();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_DISPLAYCHANGE:
            RefreshTaskbarClockBounds();
            return 0;
        case WM_SETTINGCHANGE:
            RefreshTaskbarClockBounds();
            if (!settingsLivePreview_) settings_ = wincal::SettingsStore::Load();
            ApplyDwmAppearance();
            DiscardDeviceResources();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case kSettingsChangedMessage:
        {
            settings_ = wincal::SettingsStore::Load();
            UpdateTaskbarStatus();
            taskbarStatus_.ConfigureFormat(settings_.taskbarFormat);
            const bool locationChanged = weatherAutoLocation_.exchange(settings_.weatherAutoLocation) != settings_.weatherAutoLocation;
            const bool providerChanged = weatherUseWttr_.exchange(settings_.weatherUseWttr) != settings_.weatherUseWttr;
            if (locationChanged || providerChanged)
                StartWeatherRefresh();
            ApplyDwmAppearance();
            DiscardDeviceResources();
            calendarData_.Load();
            UpdateAgenda();
            StartCalendarRefresh();
            UpdateCalendarDiagnostics();
            RequestSystemCalendarMonth(displayYear_, displayMonth_);
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
            TrackMouseEvent(&tracking);
            const int navigation = HitTestNavigation(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const int hit = pickerMode_ == DatePickerMode::Closed ? HitTestCell(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)) : -1;
            if (hit != hoveredCell_ || navigation != navigationHot_)
            {
                pickerKeyboardFocus_ = false;
                navigationHot_ = navigation;
                hoveredCell_ = hit;
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            navigationHot_ = -1;
            hoveredCell_ = -1;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
        {
            const int tool = HitTestTools(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (tool) { ToggleTools(); return 0; }
            const int navigation = HitTestNavigation(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (navigation == -6) { ToggleWeatherForecast(); return 0; }
            if (navigation == -2) { OpenDatePicker(); return 0; }
            if (navigation == -5) { const auto today = Today(); JumpToMonth(today.year, today.month); return 0; }
            if (navigation == -3 || navigation == -4)
            {
                const int delta = navigation == -3 ? -1 : 1;
                if (pickerMode_ == DatePickerMode::Closed) ChangeMonth(delta);
                else ChangePickerPage(delta);
                return 0;
            }
            if (pickerMode_ != DatePickerMode::Closed)
            {
                if (navigation >= 0) SelectPickerCell(navigation);
                return 0;
            }
            const int hit = HitTestCell(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (hit >= 0)
            {
                if (toolsOpen_) ToggleTools();
                if (weatherPanelOpen_) CloseWeatherForecast();
                dateDetailsOpen_ = true;
                CloseDetailWindow();
                const bool continuous = !settings_.monthPaging && continuousInitialized_;
                selected_ = continuous ? ContinuousDateForCell(hit) : DateForCell(displayYear_, displayMonth_, hit);
                agendaScroll_ = 0;
                if (!continuous && (selected_.year != displayYear_ || selected_.month != displayMonth_))
                {
                    const int fromYear = displayYear_;
                    const int fromMonth = displayMonth_;
                    displayYear_ = selected_.year;
                    displayMonth_ = selected_.month;
                    const int monthDelta =
                        (selected_.year * 12 + selected_.month) - (fromYear * 12 + fromMonth);
                    const bool adjacent = monthDelta == 1 || monthDelta == -1;
                    if (adjacent && !settings_.monthPaging)
                        BeginScrollTransition(monthDelta > 0 ? 1 : -1, fromYear, fromMonth);
                    else if (adjacent)
                        BeginMonthSlide(monthDelta > 0 ? 1 : -1, fromYear, fromMonth);
                    else
                        BeginTransition();
                }
                RequestSystemCalendarMonth(displayYear_, displayMonth_);
                UpdateAgenda();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            if (pickerMode_ != DatePickerMode::Closed)
            {
                ChangePickerPage(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
                return 0;
            }
            if (!settings_.monthPaging)
                ScrollContinuously(GET_WHEEL_DELTA_WPARAM(wParam));
            else
                ChangeMonth(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
            return 0;
        }
        case WM_KEYDOWN:
            if (toolsOpen_ && wParam == VK_ESCAPE)
            {
                ToggleTools();
                return 0;
            }
            if (pickerMode_ != DatePickerMode::Closed)
            {
                pickerKeyboardFocus_ = true;
                if (wParam == VK_ESCAPE) HandleEscape(lParam);
                else if (wParam == VK_RETURN || wParam == VK_SPACE) SelectPickerCell(pickerFocus_);
                else if (wParam == VK_PRIOR || wParam == VK_NEXT) ChangePickerPage(wParam == VK_PRIOR ? -1 : 1);
                else if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN)
                {
                    const int delta = wParam == VK_LEFT ? -1 : wParam == VK_RIGHT ? 1 : wParam == VK_UP ? -4 : 4;
                    pickerFocus_ = (pickerFocus_ + delta + 12) % 12;
                    InvalidateRect(window_, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == VK_LEFT) ChangeMonth(-1);
            else if (wParam == VK_RIGHT) ChangeMonth(1);
            else if (wParam == VK_UP) ChangeYear(-1);
            else if (wParam == VK_DOWN) ChangeYear(1);
            else if (wParam == VK_ESCAPE) HandleEscape(lParam);
            else return DefWindowProcW(window_, message, wParam, lParam);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window_, &point);
                if (HitTestNavigation(point.x, point.y) != -1 || HitTestTools(point.x, point.y))
                {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            return DefWindowProcW(window_, message, wParam, lParam);
        case WM_TIMER:
            if (wParam == kShutdownTimer)
            {
                if (shutdownProcess_)
                {
                    DWORD code{};
                    if (GetExitCodeProcess(shutdownProcess_, &code) && code != STILL_ACTIVE)
                    {
                        CloseHandle(shutdownProcess_);
                        shutdownProcess_ = nullptr;
                        KillTimer(window_, kShutdownTimer);
                        if (code != 0) toolStatus_ = L"Windows 拒绝了关机请求，请检查权限。";
                        InvalidateRect(window_, nullptr, FALSE);
                    }
                }
                else TickShutdownSchedule();
                return 0;
            }
            if (wParam == kClockTimer)
            {
                const RECT clockRect{0, 0, static_cast<LONG>(Scale(kLogicalWidth)), static_cast<LONG>(Scale(kClockHeight))};
                InvalidateRect(window_, &clockRect, FALSE);
                return 0;
            }
            if (wParam == kCalendarRefreshTimer)
            {
                RequestSystemCalendarMonth(displayYear_, displayMonth_);
                return 0;
            }
            if (wParam == kAnimationTimer)
            {
                if (continuousAnimating_)
                {
                    AdvanceContinuousScroll();
                }
                else if (scrollAnimating_)
                {
                    scrollProgress_ = std::min(1.0f, scrollProgress_ + 0.058f);
                    if (scrollProgress_ >= 1.0f)
                    {
                        scrollAnimating_ = false;
                        KillTimer(window_, kAnimationTimer);
                    }
                }
                else if (slideAnimating_)
                {
                    slidePos_ += (slideTarget_ - slidePos_) * 0.30f;
                    // |pos| 过 1 就把基准月推进对应格数，保持索引和数值都小。
                    const int shift = static_cast<int>(std::llround(slidePos_));
                    if (std::abs(shift) >= 1)
                    {
                        MonthTrackIndex(shift, scrollFromYear_, scrollFromMonth_);
                        slidePos_ -= shift;
                        slideTarget_ -= shift;
                    }
                    if (std::fabs(slideTarget_ - slidePos_) < 0.004f)
                    {
                        slidePos_ = 0.0f;
                        slideTarget_ = 0.0f;
                        slideAnimating_ = false;
                        KillTimer(window_, kAnimationTimer);
                    }
                }
                else
                {
                    transition_ = std::min(1.0f, transition_ + 0.055f);
                    if (transition_ >= 1.0f)
                        KillTimer(window_, kAnimationTimer);
                }
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            if (wParam == kOutsideClickTimer)
            {
                KillTimer(window_, kOutsideClickTimer);
                if (IsWindowVisible(window_))
                    HidePopup(HideReason::OutsideClick);
                return 0;
            }
            break;
        case WM_ACTIVATE:
            // 弹窗收起由全局鼠标监听处理，避免点击托盘时的焦点消息与
            // Shell_NotifyIcon 回调交错，造成一次点击执行两次显示切换。
            return 0;
        case kShowPopupMessage:
            ShowPopup(true);
            return 0;
        case kTaskbarClockContextMenuMessage:
            ShowContextMenu();
            return 0;
        case kDismissContextMenuMessage:
            if (IsWindow(contextMenuWindow_)) ShowWindow(contextMenuWindow_, SW_HIDE);
            return 0;
        case kTaskbarClockClickMessage:
            if (IsCachedTaskbarClockPoint({static_cast<LONG>(wParam), static_cast<LONG>(lParam)}))
            {
                PostMessageW(window_, kConfirmedClockClickMessage, wParam, lParam);
            }
            else if (!settingsLivePreview_ && IsWindowVisible(window_))
                SetTimer(window_, kOutsideClickTimer, 120, nullptr);
            return 0;
        case kConfirmedClockClickMessage:
        {
            const auto now = GetTickCount64();
            if (!lastClockClickTick_ || now - lastClockClickTick_ > GetDoubleClickTime())
            {
                lastClockClickTick_ = now;
                TogglePopup();
            }
            return 0;
        }
        case kGlobalMouseMessage:
        {
            MSLLHOOKSTRUCT mouse{};
            mouse.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            HandleGlobalMouse(wParam, mouse);
            return 0;
        }
        case kClockBoundsUpdatedMessage:
            ApplyTaskbarClockBounds();
            return 0;
        case kSyncCompletedMessage:
        {
            wincal::SyncResult result;
            { const std::lock_guard lock(syncMutex_); result = std::move(syncResult_); }
            syncBusy_ = false;
            syncStatus_ = result.message;
            if (IsWindow(settingsWindow_))
            {
                if (result.success && wParam == 2 && syncApplyDownload_)
                {
                    settingsDraft_ = wincal::SettingsStore::Deserialize(result.downloaded);
                    PopulateSettingsControls(settingsWindow_);
                    UpdateSettingsFontControls(settingsWindow_);
                    ApplySettingsWindowAppearance(settingsWindow_);
                }
                for (int id : {kSyncTest, kSyncUpload, kSyncDownload}) EnableWindow(GetDlgItem(settingsWindow_, id), TRUE);
                InvalidateRect(settingsWindow_, nullptr, FALSE);
            }
            return 0;
        }
        case kScheduleOutsideHideMessage:
            if (!settingsLivePreview_) SetTimer(window_, kOutsideClickTimer, 120, nullptr);
            return 0;
        case kCalendarDataUpdatedMessage:
            UpdateCalendarDiagnostics();
            UpdateAgenda();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case kWeatherUpdatedMessage:
            InvalidateRect(window_, nullptr, FALSE);
            if (IsWindowVisible(weatherWindow_)) InvalidateRect(weatherWindow_, nullptr, FALSE);
            return 0;
        case kShowSettingsMessage:
            ShowSettingsWindow();
            return 0;
        case WM_DESTROY:
            if (syncThread_.joinable()) syncThread_.join();
            if (clockBoundsThread_.joinable()) clockBoundsThread_.request_stop();
            KillTimer(window_, kShutdownTimer);
            if (shutdownProcess_) CloseHandle(shutdownProcess_);
            if (IsWindow(toolsWindow_)) DestroyWindow(toolsWindow_);
            if (toolInputFont_) DeleteObject(toolInputFont_);
            taskbarStatus_.Stop();
            if (IsWindow(weatherWindow_)) DestroyWindow(weatherWindow_);
            if (IsWindow(agendaWindow_)) DestroyWindow(agendaWindow_);
            CloseDetailWindow();
            if (calendarRefreshThread_.joinable()) calendarRefreshThread_.request_stop();
            if (weatherThread_.joinable()) weatherThread_.request_stop();
            if (systemCalendarThread_.joinable())
                systemCalendarThread_.request_stop();
            RemovePropW(window_, L"WinCal.LastHideReason");
            RemovePropW(window_, L"WinCal.EventCount");
            RemovePropW(window_, L"WinCal.IcsEventCount");
            RemovePropW(window_, L"WinCal.SystemEventCount");
            RemovePropW(window_, L"WinCal.SystemLoadedMonthCount");
            RemovePropW(window_, L"WinCal.SystemCalendarState");
            RemovePropW(window_, L"WinCal.TodayEventCount");
            RemovePropW(window_, L"WinCal.TomorrowEventCount");
            StopGlobalMouseMonitor();
            StopSystemCalendarInterceptor();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(window_, message, wParam, lParam);
    }

    HINSTANCE instance_{};
    HWND window_{};
    HWND contextMenuWindow_{};
    int contextMenuHover_{-1};
    HWND settingsWindow_{};
    std::mutex syncMutex_;
    std::jthread syncThread_;
    wincal::SyncResult syncResult_;
    bool syncBusy_{};
    bool syncApplyDownload_{};
    std::wstring syncStatus_;
    HWND toolsWindow_{};
    UINT toolDpi_{96};
    bool shutdownToolOpen_{};
    bool fortuneToolOpen_{};
    HWND fortuneInput_{};
    HWND fortuneTimeInput_{};
    HWND fortuneGenderInput_{};
    std::optional<std::chrono::sys_days> fortuneBirth_;
    std::optional<wincal::FortuneChart> fortuneChart_;
    std::chrono::sys_days fortuneStart_{};
    int fortuneSelected_{};
    std::wstring fortuneStatus_;
    ComPtr<ID2D1HwndRenderTarget> toolsRenderTarget_;
    bool toolsOpen_{};
    bool shutdownAtTime_{};
    HWND toolInput_{};
    HWND toolTimeInput_{};
    HFONT toolInputFont_{};
    std::optional<std::time_t> shutdownDue_;
    HANDLE shutdownProcess_{};
    std::wstring toolStatus_;
#ifdef WINCAL_SETTINGS_PREVIEW
    bool agendaPreview_{};
#endif
    HWND agendaWindow_{};
    bool dateDetailsOpen_{};
    int agendaScroll_{};
    int agendaScrollMax_{};
    UINT agendaDpi_{96};
    HWND detailWindow_{};
    int detailScroll_{};
    int detailFontOffset_{};
    HFONT detailTitleFont_{};
    HFONT detailBodyFont_{};
    HFONT detailCaptionFont_{};
    UINT detailDpi_{96};
    bool detailCloseHot_{};
    Date detailDate_{};
    wincal::CalendarEvent detailEvent_{};
#ifdef WINCAL_SETTINGS_PREVIEW
    UINT settingsPreviewDpi_{96};
#endif
    struct SettingControlLayout { HWND window; int x; int y; int width; int height; int page; };
    std::vector<SettingControlLayout> settingsControls_;
    UINT settingsDpi_{96};
    int settingsPage_{};
    HWND settingsHotControl_{};
    int settingsScrollX_{};
    int settingsScrollY_{};
    bool settingsLayoutActive_{};
    HFONT settingsCaptionFont_{};
    HFONT settingsTitleFont_{};
    HFONT settingsBodyFont_{};
    HFONT settingsLabelFont_{};
    HBRUSH settingsInputBrush_{};
    HBRUSH settingsCardBrush_{};

    HHOOK mouseHook_{};
    std::jthread mouseHookThread_;
    std::atomic<DWORD> mouseHookThreadId_{};
    std::vector<std::pair<HWND, RECT>> taskbarClockBounds_;
    std::vector<std::pair<HWND, RECT>> pendingClockBounds_;
    mutable std::mutex clockBoundsMutex_;
    std::condition_variable_any clockBoundsWake_;
    bool clockBoundsRequested_{};
    std::jthread clockBoundsThread_;
    HWINEVENTHOOK calendarHook_{};
    std::vector<HWND> hiddenCalendarWindows_;
    std::mutex hiddenCalendarWindowsMutex_;
    ULONGLONG lastClockClickTick_{};



    bool taskbarClockRightButtonPending_{};
    bool taskbarClockLeftButtonPending_{};
    UINT taskbarCreatedMessage_{};
    UINT dpi_{96};
    bool dark_{};
    bool settingsWindowDark_{};
    float fontScale_{1.0f};
    bool weekStartsMonday_{true};
    wincal::AppSettings settings_;
    wincal::AppSettings settingsDraft_;
    wincal::AppSettings settingsBeforeEdit_;
    std::atomic<bool> settingsLivePreview_{};
    bool settingsSaveAccepted_{};
    int displayYear_{};
    int displayMonth_{};
    DatePickerMode pickerMode_{DatePickerMode::Closed};
    int pickerYear_{};
    int pickerFirstYear_{};
    int pickerFocus_{};
    int navigationHot_{-1};
    bool pickerKeyboardFocus_{};
    Date selected_{};
    int hoveredCell_{-1};
    float transition_{1.0f};
    bool continuousInitialized_{};
    bool continuousAnimating_{};
    long long continuousAnchor_{};
    double continuousPosition_{};
    double continuousTarget_{};
    bool scrollAnimating_{};
    float scrollProgress_{};
    bool slideAnimating_{};
    float slidePos_{};
    float slideTarget_{};
    int scrollDir_{1};
    int scrollFromYear_{};
    int scrollFromMonth_{};
    int scrollRows_{};
#ifdef WINCAL_SETTINGS_PREVIEW
    ComPtr<ID2D1DeviceContext> previewContext_;
    ComPtr<ID2D1Bitmap1> previewBitmap_;
    ComPtr<ID2D1Bitmap1> previewTargetBitmap_;
#endif
    wincal::CalendarData calendarData_;
    wincal::LunarCalendar lunarCalendar_;
    std::mutex calendarRefreshMutex_;
    std::condition_variable_any calendarRefreshWake_;
    bool calendarRefreshRequested_{};
    std::jthread calendarRefreshThread_;
    std::jthread systemCalendarThread_;
    wincal::WeatherData weatherData_;
    wincal::ClockExtension taskbarStatus_;
    bool weatherPanelOpen_{};
    bool weatherForecastPreview_{};
    bool weatherWttrPreview_{};
    HWND weatherWindow_{};
    ComPtr<ID2D1HwndRenderTarget> weatherRenderTarget_;
    std::atomic<bool> weatherAutoLocation_{true};
    std::atomic<bool> weatherUseWttr_{};
    std::mutex weatherRefreshMutex_;
    std::condition_variable_any weatherWake_;
    bool weatherRefreshRequested_{};
    std::jthread weatherThread_;
    std::mutex systemCalendarRequestMutex_;
    std::optional<std::pair<int, int>> pendingSystemCalendarMonth_;
    bool systemCalendarWorkerRunning_{};

    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1RenderTarget> renderTarget_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<IDWriteTextFormat> titleFormat_;
    ComPtr<IDWriteTextFormat> bodyFormat_;
    ComPtr<IDWriteTextFormat> dayFormat_;
    ComPtr<IDWriteTextFormat> lunarFormat_;
    ComPtr<IDWriteTextFormat> badgeFormat_;
    ComPtr<IDWriteTextFormat> clockFormat_;
    ComPtr<IDWriteTextFormat> clockCaptionFormat_;
    ComPtr<IDWriteTextFormat> temperatureFormat_;

    inline static App* activeInstance_{};
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand)
{
    const bool openSettings = commandLine && wcsstr(commandLine, L"--settings");
    HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\WinCal.Native.Singleton");
    if (!singleInstance)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(L"WinCalNativePopup", nullptr))
        {
            PostMessageW(existing, openSettings ? WM_APP + 6 : WM_APP + 2, 0, 0);
            SetForegroundWindow(existing);
        }
        CloseHandle(singleInstance);
        return 0;
    }
    App app;
    const int result = app.Run(instance, showCommand, openSettings);
    CloseHandle(singleInstance);
    return result;
}
