#pragma once

#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include <string_view>
#include <string>
#include <vector>

namespace wincal
{
inline bool IsClockAutomationIdentity(std::wstring_view className, std::wstring_view id)
{
    return className == L"SystemTray.DateTimeIcon" || className == L"TrayClockWClass" ||
        className == L"360TrayClockWClass" ||
        id == L"ClockButton" || id == L"DateTimeButton";
}

inline std::wstring NormalizeClockDate(std::wstring_view value)
{
    std::wstring normalized;
    for (wchar_t c : value)
    {
        if (c <= L' ' || c == 0xa0 || c == 0x200e || c == 0x200f ||
            (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069)) continue;
        normalized += c;
    }
    return normalized;
}

inline bool IsClockAutomationElement(std::wstring_view className, std::wstring_view id,
                                      std::wstring_view name)
{
    if (IsClockAutomationIdentity(className, id)) return true;
    // Newer Windows 11 builds share OmniButton classes between clock, audio and
    // notifications. Only the clock's accessible name contains today's date.
    if (className != L"SystemTray.OmniButton" && className != L"SystemTray.OmniButtonRight" &&
        className != L"SystemTray.OmniButtonLeft")
        return false;
    const auto normalized = NormalizeClockDate(name);
    // The shell keeps the clock label even when a custom template omits dates.
    if (normalized.starts_with(L"时钟") || normalized.starts_with(L"Clock")) return true;
    SYSTEMTIME today{};
    GetLocalTime(&today);
    wchar_t isoDate[32]{};
    swprintf_s(isoDate, L"%04u-%02u-%02u", today.wYear, today.wMonth, today.wDay);
    if (normalized.find(isoDate) != std::wstring::npos) return true;
    for (DWORD format : {DATE_SHORTDATE, DATE_LONGDATE})
    {
        wchar_t date[256]{};
        if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, format, &today, nullptr, date, ARRAYSIZE(date), nullptr))
        {
            const auto formatted = NormalizeClockDate(date);
            if (!formatted.empty() && normalized.find(formatted) != std::wstring::npos) return true;
        }
    }
    // WinCal's taskbar extension uses this Chinese date independently of the
    // system's short/long date formats.
    wchar_t chineseDate[64]{};
    if (GetDateFormatEx(L"zh-CN", 0, &today, L"yyyy'年'M'月'd'日'",
                        chineseDate, ARRAYSIZE(chineseDate), nullptr) &&
        normalized.find(chineseDate) != std::wstring::npos) return true;
    return false;
}

inline bool IsClockAutomationNode(IUIAutomationElement* element)
{
    BSTR className{}, id{}, name{};
    element->get_CurrentClassName(&className);
    element->get_CurrentAutomationId(&id);
    element->get_CurrentName(&name);
    const bool clock = IsClockAutomationElement(className ? className : L"", id ? id : L"", name ? name : L"");
    SysFreeString(className);
    SysFreeString(id);
    SysFreeString(name);
    return clock;
}

inline HWND TaskbarAtPoint(POINT point)
{
    for (HWND window = WindowFromPoint(point); window; window = GetParent(window))
    {
        wchar_t name[128]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        if (_wcsicmp(name, L"Shell_TrayWnd") == 0 || _wcsicmp(name, L"Shell_SecondaryTrayWnd") == 0)
            return window;
    }
    return nullptr;
}

inline std::vector<RECT> TaskbarClockBounds(HWND taskbarWindow)
{
    using Microsoft::WRL::ComPtr;
    std::vector<RECT> bounds;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation)))) return bounds;
    ComPtr<IUIAutomationElement> taskbar;
    if (FAILED(automation->ElementFromHandle(taskbarWindow, &taskbar))) return bounds;
    constexpr PCWSTR classNames[]{L"SystemTray.OmniButton", L"SystemTray.OmniButtonRight",
                                  L"SystemTray.OmniButtonLeft"};
    for (const auto className : classNames)
    {
        VARIANT value{};
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocString(className);
        if (!value.bstrVal) continue;
        ComPtr<IUIAutomationCondition> condition;
        const HRESULT conditionResult = automation->CreatePropertyCondition(UIA_ClassNamePropertyId, value, &condition);
        VariantClear(&value);
        if (FAILED(conditionResult)) continue;
        ComPtr<IUIAutomationElementArray> descendants;
        if (FAILED(taskbar->FindAll(TreeScope_Descendants, condition.Get(), &descendants))) continue;
        int count{};
        descendants->get_Length(&count);
        for (int index = 0; index < count; ++index)
        {
            ComPtr<IUIAutomationElement> candidate;
            RECT rect{};
            if (SUCCEEDED(descendants->GetElement(index, &candidate)) &&
                SUCCEEDED(candidate->get_CurrentBoundingRectangle(&rect)) &&
                IsClockAutomationNode(candidate.Get())) bounds.push_back(rect);
        }
    }
    if (!bounds.empty()) return bounds;

    ComPtr<IUIAutomationCondition> allCondition;
    ComPtr<IUIAutomationElementArray> allDescendants;
    RECT taskbarBounds{};
    if (FAILED(automation->CreateTrueCondition(&allCondition)) ||
        FAILED(taskbar->get_CurrentBoundingRectangle(&taskbarBounds)) ||
        FAILED(taskbar->FindAll(TreeScope_Descendants, allCondition.Get(), &allDescendants))) return bounds;
    int fallbackCount{};
    allDescendants->get_Length(&fallbackCount);
    for (int index = 0; index < fallbackCount; ++index)
    {
        ComPtr<IUIAutomationElement> candidate;
        RECT rect{};
        RECT clipped{};
        if (SUCCEEDED(allDescendants->GetElement(index, &candidate)) &&
            SUCCEEDED(candidate->get_CurrentBoundingRectangle(&rect)) &&
            IntersectRect(&clipped, &rect, &taskbarBounds) && IsClockAutomationNode(candidate.Get()))
            bounds.push_back(clipped);
    }
    return bounds;
}

// Called from the posted UI message, never from the low-level mouse hook.
inline bool IsTaskbarClockPoint(POINT point)
{
    const HWND taskbarWindow = TaskbarAtPoint(point);
    if (!taskbarWindow) return false;
    for (HWND window = WindowFromPoint(point); window; window = GetParent(window))
    {
        wchar_t name[128]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        if (IsClockAutomationIdentity(name, L"")) return true;
    }
    using Microsoft::WRL::ComPtr;
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationElement> element;
    ComPtr<IUIAutomationTreeWalker> walker;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&automation))) &&
        SUCCEEDED(automation->ElementFromPoint(point, &element)) &&
        SUCCEEDED(automation->get_RawViewWalker(&walker)))
    {
        for (int depth = 0; element && depth < 16; ++depth)
        {
            if (IsClockAutomationNode(element.Get())) return true;
            BSTR className{};
            element->get_CurrentClassName(&className);
            const bool taskbar = className && (_wcsicmp(className, L"Shell_TrayWnd") == 0 ||
                                              _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0);
            SysFreeString(className);
            if (taskbar) break;
            ComPtr<IUIAutomationElement> parent;
            if (FAILED(walker->GetParentElement(element.Get(), &parent))) break;
            element = std::move(parent);
        }
    }
    for (const auto& bounds : TaskbarClockBounds(taskbarWindow))
        if (PtInRect(&bounds, point)) return true;
    return false;
}
}
