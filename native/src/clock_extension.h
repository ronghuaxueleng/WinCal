#pragma once

#include <winsock2.h>
#include <windows.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include "app_settings.h"

namespace wincal
{
// Supplies WinCal's existing data to Windhawk's native taskbar clock mod.
// The listener is loopback-only; the extension formats its own live clock.
class ClockExtension
{
public:
    using Lines = TaskbarFields;
    ~ClockExtension() { Stop(); }
    bool Running() const { return IsWindow(window_) != FALSE; }

    void Start(HINSTANCE instance, std::function<Lines()> text, std::wstring_view format)
    {
        if (Running()) return;
        config_ = SettingsStore::CacheDirectory().parent_path() / L"TaskbarExtension" / L"AppData" /
            L"Engine" / L"Mods" / L"taskbar-clock-customization.ini";
        if (!std::filesystem::exists(config_)) return;
        wchar_t modulePath[32768]{};
        GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
        const auto applicationDirectory = std::filesystem::path{modulePath}.parent_path();
        const auto library = applicationDirectory / L"WinCalTaskbarClock.dll";
        const auto host = applicationDirectory / L"WinCalTaskbarHost.exe";
        if (!std::filesystem::exists(library) || !std::filesystem::exists(host)) return;
        if (!CopyFileW(library.c_str(), (config_.parent_path() / L"64" / L"WinCalTaskbarClock.dll").c_str(), FALSE)) return;
        WritePrivateProfileStringW(L"Mod", L"LibraryFileName", L"WinCalTaskbarClock.dll", config_.c_str());
        WritePrivateProfileStringW(L"Settings", L"TooltipLine", L"WinCal 天气与农历", config_.c_str());
        ConfigureFormat(format);
        WSADATA sockets{};
        if (WSAStartup(MAKEWORD(2, 2), &sockets)) return;
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        BOOL exclusive = TRUE;
        setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(17864);
        if (socket_ == INVALID_SOCKET || bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ||
            listen(socket_, 4))
        {
            if (socket_ != INVALID_SOCKET) closesocket(socket_);
            socket_ = INVALID_SOCKET;
            WSACleanup();
            return;
        }
        text_ = std::move(text);
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"WinCalClockFeed";
        RegisterClassW(&wc);
        window_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        if (!window_) { closesocket(socket_); socket_ = INVALID_SOCKET; WSACleanup(); return; }
        Publish();
        SetTimer(window_, 1, 1000, nullptr);
        worker_ = std::jthread([this](std::stop_token stop) { Serve(stop); });
        WritePrivateProfileStringW(L"Mod", L"Disabled", L"0", config_.c_str());
        const auto root = config_.parent_path().parent_path().parent_path().parent_path();
        const std::wstring eventName = L"Local\\WinCalClockStop-" + std::to_wstring(GetCurrentProcessId());
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
        std::wstring command = L"\"" + host.wstring() + L"\" \"" + root.wstring() + L"\" " +
            std::to_wstring(GetCurrentProcessId()) + L" \"" + eventName + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (stopEvent_ && CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, applicationDirectory.c_str(), &startup, &process))
        {
            CloseHandle(process.hThread);
            hostProcess_ = process.hProcess;
        }
        else Stop();
    }

    void Stop()
    {
        if (config_.empty()) return;
        if (std::filesystem::exists(config_)) WritePrivateProfileStringW(L"Mod", L"Disabled", L"1", config_.c_str());
        if (stopEvent_) SetEvent(stopEvent_);
        if (hostProcess_) { WaitForSingleObject(hostProcess_, 5000); CloseHandle(hostProcess_); hostProcess_ = nullptr; }
        if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
        if (IsWindow(window_)) DestroyWindow(window_);
        window_ = nullptr;
        if (worker_.joinable()) { worker_.request_stop(); worker_.join(); }
        if (socket_ != INVALID_SOCKET) { closesocket(socket_); socket_ = INVALID_SOCKET; WSACleanup(); }
        config_.clear();
    }

    void ConfigureFormat(std::wstring_view format)
    {
        if (config_.empty()) return;
        const auto normalized = NormalizeTaskbarFormat(format);
        const auto split = normalized.find(L"\\n");
        const auto top = L"@WinCal:" + normalized.substr(0, split);
        const auto bottom = L"@WinCal:" + (split == normalized.npos ? std::wstring{} : normalized.substr(split + 2));
        WritePrivateProfileStringW(L"Settings", L"TopLine", top.c_str(), config_.c_str());
        WritePrivateProfileStringW(L"Settings", L"BottomLine", bottom.c_str(), config_.c_str());
        for (size_t i = 0; i < TaskbarFieldNames.size(); ++i)
        {
            const auto prefix = L"WebContentsItems[" + std::to_wstring(i) + L"].";
            const auto tag = L"field" + std::to_wstring(i);
            const auto write = [&](const wchar_t* key, const std::wstring& value)
                { WritePrivateProfileStringW(L"Settings", (prefix + key).c_str(), value.c_str(), config_.c_str()); };
            write(L"Url", L"http://127.0.0.1:17864/wincal");
            write(L"BlockStart", L""); write(L"Start", L"<" + tag + L">"); write(L"End", L"</" + tag + L">");
            write(L"MaxLength", L"100"); write(L"ContentMode", L"plainText");
        }
        const auto stamp = std::to_wstring(std::max<long long>(
            GetPrivateProfileIntW(L"Mod", L"SettingsChangeTime", 0, config_.c_str()) + 1LL,
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
        WritePrivateProfileStringW(L"Mod", L"SettingsChangeTime", stamp.c_str(), config_.c_str());
    }

private:
    HWND window_{};
    HANDLE hostProcess_{}, stopEvent_{};
    SOCKET socket_{INVALID_SOCKET};
    std::jthread worker_;
    std::function<Lines()> text_;
    std::filesystem::path config_;
    std::mutex mutex_;
    std::string body_;

    void Publish()
    {
        const auto lines = text_();
        std::wstring wide;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto tag = L"field" + std::to_wstring(i);
            wide += L"<" + tag + L">" + lines[i] + L"</" + tag + L">";
        }
        const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string body(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), body.data(), length, nullptr, nullptr);
        const std::lock_guard lock(mutex_);
        body_ = std::move(body);
    }

    void Serve(std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(socket_, &ready);
            timeval timeout{0, 200000};
            if (select(0, &ready, nullptr, nullptr, &timeout) <= 0) continue;
            const SOCKET client = accept(socket_, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            const DWORD milliseconds = 1000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
            char request[2048];
            const int count = recv(client, request, sizeof(request), 0);
            std::string body;
            const bool valid = count > 0 && std::string_view(request, count).starts_with("GET /wincal ");
            if (valid) { const std::lock_guard lock(mutex_); body = body_; }
            const std::string response = std::string(valid ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n") +
                "Content-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\n\r\n" + body;
            size_t sent{};
            while (sent < response.size() && !stop.stop_requested())
            {
                const int written = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
                if (written <= 0) break;
                sent += written;
            }
            closesocket(client);
        }
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<ClockExtension*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<ClockExtension*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_TIMER) { self->Publish(); return 0; }
        return DefWindowProcW(window, message, wp, lp);
    }
};
}
