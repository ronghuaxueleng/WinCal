#define WINCAL_SETTINGS_PREVIEW
#include "../src/main.cpp"
#include <winrt/base.h>
#include <fstream>

int wmain(int argc, wchar_t** argv)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    App app;
    if (argc == 2 && wcscmp(argv[1], L"--check-tools") == 0)
        return app.CheckShutdownTools(GetModuleHandleW(nullptr)) ? 0 : 1;
    if (argc == 5 && wcscmp(argv[1], L"--weather") == 0)
        return app.RenderCalendarPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, 1, 2026, 10, 0.5f, argv[4], false, false, {}, DatePickerMode::Closed, true) ? 0 : 1;
    if (argc == 2 && wcscmp(argv[1], L"--check-picker") == 0)
        return app.CheckDatePicker(GetModuleHandleW(nullptr)) ? 0 : 1;
    if (argc == 6 && wcscmp(argv[1], L"--picker") == 0)
        return app.RenderCalendarPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, 1, 2026, 10, 0.5f, argv[5], false, false, {},
            _wtoi(argv[4]) == 0 ? DatePickerMode::Months : DatePickerMode::Years) ? 0 : 1;
    if (argc == 9 && wcscmp(argv[1], L"--holidays") == 0)
    {
        std::ifstream source(std::filesystem::path{argv[7]}, std::ios::binary);
        const std::string json{std::istreambuf_iterator<char>{source}, std::istreambuf_iterator<char>{}};
        if (json.empty()) return 2;
        return app.RenderCalendarPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, _wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]),
            0.5f, argv[8], false, false, json) ? 0 : 1;
    }
    if (argc == 2 && wcscmp(argv[1], L"--check-continuous") == 0)
        return app.CheckContinuousScrolling(GetModuleHandleW(nullptr)) ? 0 : 1;
    if (argc == 9 && wcscmp(argv[1], L"--continuous") == 0)
        return app.RenderCalendarPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, _wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]),
            static_cast<float>(_wtof(argv[7])), argv[8], false, true) ? 0 : 1;
    if (argc == 6 && wcscmp(argv[1], L"--detail") == 0)
        return app.RenderDetailPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, _wtoi(argv[4]), argv[5]) ? 0 : 1;
    if (argc == 6 && wcscmp(argv[1], L"--agenda") == 0)
        return app.RenderAgendaPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, _wtoi(argv[4]), argv[5]) ? 0 : 1;
    if (argc == 9 && wcscmp(argv[1], L"--calendar") == 0)
    {
        return app.RenderCalendarPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, _wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]),
            static_cast<float>(_wtof(argv[7])), argv[8]) ? 0 : 1;
    }
    if (argc == 9 && wcscmp(argv[1], L"--slide") == 0)
    {
        return app.RenderCalendarPreview(GetModuleHandleW(nullptr), _wtoi(argv[2]),
            _wtoi(argv[3]) != 0, _wtoi(argv[4]), _wtoi(argv[5]), _wtoi(argv[6]),
            static_cast<float>(_wtof(argv[7])), argv[8], true) ? 0 : 1;
    }
    if (argc != 6) return 2;
    return app.RenderSettingsPreview(GetModuleHandleW(nullptr), _wtoi(argv[1]),
        _wtoi(argv[2]), _wtoi(argv[3]) != 0, _wtoi(argv[4]), argv[5]) ? 0 : 1;
}
