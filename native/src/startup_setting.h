#pragma once

#include <windows.h>
#include <vector>

namespace wincal
{
// Keep the exact old command, including legacy paths, if configuration saving fails.
class StartupSettingSnapshot
{
public:
    explicit StartupSettingSnapshot(const wchar_t* key = DefaultKey) : key_(key) {}

    bool Capture()
    {
        existed_ = false;
        data_.clear();
        DWORD size{};
        const auto status = RegGetValueW(HKEY_CURRENT_USER, key_, L"miniCal", RRF_RT_ANY | RRF_NOEXPAND,
                                         &type_, nullptr, &size);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return true;
        if (status != ERROR_SUCCESS) return false;
        existed_ = true;
        data_.resize(size);
        const auto read = RegGetValueW(HKEY_CURRENT_USER, key_, L"miniCal", RRF_RT_ANY | RRF_NOEXPAND,
                                      &type_, data_.data(), &size);
        if (read == ERROR_SUCCESS) data_.resize(size);
        return read == ERROR_SUCCESS;
    }

    bool Restore() const
    {
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key_, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
            != ERROR_SUCCESS) return false;
        const auto status = existed_ ?
            RegSetValueExW(key, L"miniCal", 0, type_, data_.data(), static_cast<DWORD>(data_.size())) :
            RegDeleteValueW(key, L"miniCal");
        RegCloseKey(key);
        return status == ERROR_SUCCESS || (!existed_ && status == ERROR_FILE_NOT_FOUND);
    }

private:
    static constexpr wchar_t DefaultKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* key_;
    bool existed_{};
    DWORD type_{};
    std::vector<BYTE> data_;
};
}
