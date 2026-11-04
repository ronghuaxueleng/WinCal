#pragma once
#include <windows.h>
#include <winhttp.h>
#include <wincred.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <string>
#include "app_settings.h"

namespace wincal
{
struct SyncCredentials { std::wstring account, password; };
inline SyncCredentials LoadSyncCredentials()
{
    PCREDENTIALW credential{};
    if (!CredReadW(L"WinCal/Nutstore", CRED_TYPE_GENERIC, 0, &credential)) return {};
    SyncCredentials result{credential->UserName ? credential->UserName : L"",
        std::wstring(reinterpret_cast<wchar_t*>(credential->CredentialBlob), credential->CredentialBlobSize / sizeof(wchar_t))};
    CredFree(credential);
    return result;
}
inline bool SaveSyncCredentials(const SyncCredentials& values)
{
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(L"WinCal/Nutstore");
    credential.UserName = const_cast<wchar_t*>(values.account.c_str());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(values.password.data()));
    credential.CredentialBlobSize = static_cast<DWORD>(values.password.size() * sizeof(wchar_t));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0) != FALSE;
}
struct SyncResult { bool success{}; std::wstring message; std::string downloaded; };
struct HttpHandle
{
    HINTERNET value{};
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
};
// Fixed HTTPS origin: credentials are never sent to a user-supplied or redirected host.
inline SyncResult SyncNutstore(int action, const SyncCredentials& credentials, const std::string& upload)
{
    SyncResult result;
    HttpHandle session{WinHttpOpen(L"WinCal/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return {false, L"无法初始化网络连接。"};
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 15000);
    HttpHandle connection{WinHttpConnect(session.value, L"dav.jianguoyun.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    const auto request = [&](const wchar_t* method, const wchar_t* path, const std::string& body,
                             std::string* response) -> DWORD
    {
        HttpHandle handle{WinHttpOpenRequest(connection.value, method, path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
        if (!handle.value) return 0;
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(handle.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
        WinHttpSetCredentials(handle.value, WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC,
            credentials.account.c_str(), credentials.password.c_str(), nullptr);
        const wchar_t* headers = action == 0 ? L"Depth: 0\r\n" : L"Content-Type: application/json; charset=utf-8\r\n";
        if (!WinHttpSendRequest(handle.value, headers, static_cast<DWORD>(-1),
            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
            !WinHttpReceiveResponse(handle.value, nullptr)) return 0;
        DWORD status{}, bytes = sizeof(status);
        WinHttpQueryHeaders(handle.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX);
        if (response && status == 200)
        {
            char buffer[8192];
            for (;;)
            {
                DWORD read{};
                if (!WinHttpReadData(handle.value, buffer, sizeof(buffer), &read)) return 0;
                if (!read) break;
                if (response->size() + read > 1024 * 1024) return 413;
                response->append(buffer, read);
            }
        }
        return status;
    };
    if (action == 1)
    {
        const auto folder = request(L"MKCOL", L"/dav/WinCal/", {}, nullptr);
        if (folder != 201 && folder != 405) return {false, L"无法创建 WinCal 同步目录（HTTP " + std::to_wstring(folder) + L"）。"};
    }
    const auto status = request(action == 0 ? L"PROPFIND" : action == 1 ? L"PUT" : L"GET",
        action == 0 ? L"/dav/" : L"/dav/WinCal/settings.json", action == 1 ? upload : std::string{},
        action == 2 ? &result.downloaded : nullptr);
    if (status < 200 || status >= 300)
    {
        result.message = status == 401 ? L"认证失败，请检查账号和第三方应用密码。" :
            status == 404 ? L"云端尚无配置，请先上传。" :
            status == 0 ? L"连接失败或超时，请检查网络。" : L"同步失败（HTTP " + std::to_wstring(status) + L"）。";
        return result;
    }
    if (action == 2)
    {
        try
        {
            const auto json = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(result.downloaded));
            if (!json.HasKey(L"ThemeMode") || !json.HasKey(L"TaskbarFormat"))
                return {false, L"云端文件不是有效的 WinCal 配置。"};
            json.GetNamedString(L"ThemeMode");
            json.GetNamedString(L"TaskbarFormat");
        }
        catch (...) { return {false, L"云端配置格式错误，本地配置未修改。"}; }
    }
    result.success = true;
    result.message = action == 0 ? L"连接成功。" : action == 1 ? L"已上传已保存的配置。" : L"已载入云端配置，点击保存设置后生效。";
    return result;
}
}
