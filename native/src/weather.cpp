#include "weather.h"
#include "atomic_file.h"

#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>

namespace
{
struct HttpHandle
{
    HINTERNET handle{};
    ~HttpHandle() { if (handle) WinHttpCloseHandle(handle); }
};

std::string GetHttps(std::wstring_view host, const std::wstring& path, std::stop_token stop)
{
    if (stop.stop_requested()) return {};
    HttpHandle session{WinHttpOpen(L"WinCal/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.handle) return {};
    WinHttpSetTimeouts(session.handle, 3000, 3000, 5000, 5000);
    HttpHandle connection{WinHttpConnect(session.handle, std::wstring{host}.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.handle || stop.stop_requested()) return {};
    HttpHandle request{WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.handle || !WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || stop.stop_requested() ||
        !WinHttpReceiveResponse(request.handle, nullptr)) return {};
    DWORD status{}, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) || status != 200) return {};
    std::string content;
    while (!stop.stop_requested())
    {
        char buffer[4096];
        DWORD read{};
        if (!WinHttpReadData(request.handle, buffer, sizeof(buffer), &read)) return {};
        if (!read) return content;
        if (content.size() + read > 256 * 1024) return {};
        content.append(buffer, read);
    }
    return {};
}

bool ValidCoordinates(double latitude, double longitude)
{
    return std::isfinite(latitude) && std::isfinite(longitude) &&
        latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}

std::optional<wincal::WeatherLocation> SystemLocation(std::stop_token stop)
{
    try
    {
        using namespace winrt::Windows::Devices::Geolocation;
        Geolocator locator;
        if (locator.LocationStatus() == PositionStatus::Disabled ||
            locator.LocationStatus() == PositionStatus::NotAvailable) return std::nullopt;
        // The OS controls permission; a denial falls back to coarse network location.
        const auto operation = locator.GetGeopositionAsync(std::chrono::minutes(30), std::chrono::seconds(6));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(7);
        while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
        {
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline)
            {
                operation.Cancel();
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (stop.stop_requested()) return std::nullopt;
        const auto point = operation.get().Coordinate().Point().Position();
        if (!ValidCoordinates(point.Latitude, point.Longitude)) return std::nullopt;
        return wincal::WeatherLocation{point.Latitude, point.Longitude, L"当前位置", L"系统定位"};
    }
    catch (const winrt::hresult_error&) { return std::nullopt; }
}
}

namespace wincal
{
std::optional<WeatherLocation> ParseNetworkLocation(std::string_view json)
{
    try
    {
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        if (!JsonObject::TryParse(winrt::to_hstring(json), root) || !root.GetNamedBoolean(L"success", false))
            return std::nullopt;
        WeatherLocation location;
        location.latitude = root.GetNamedNumber(L"latitude");
        location.longitude = root.GetNamedNumber(L"longitude");
        location.city = root.GetNamedString(L"city");
        location.source = L"网络定位";
        if (!ValidCoordinates(location.latitude, location.longitude) || location.city.empty()) return std::nullopt;
        if (location.city == L"Beijing") location.city = L"北京";
        return location;
    }
    catch (const winrt::hresult_error&) { return std::nullopt; }
}

std::optional<WeatherSnapshot> ParseWeather(std::string_view json, const WeatherLocation& location)
{
    try
    {
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        if (!JsonObject::TryParse(winrt::to_hstring(json), root) || root.GetNamedBoolean(L"error", false))
            return std::nullopt;
        const auto current = root.GetNamedObject(L"current");
        WeatherSnapshot snapshot;
        snapshot.temperature = current.GetNamedNumber(L"temperature_2m");
        const auto optionalNumber = [&](const wchar_t* key, double low, double high) -> std::optional<double>
        {
            if (!current.HasKey(key)) return std::nullopt;
            const double value = current.GetNamedNumber(key);
            return std::isfinite(value) && value >= low && value <= high ? std::optional<double>{value} : std::nullopt;
        };
        snapshot.humidity = optionalNumber(L"relative_humidity_2m", 0, 100);
        snapshot.windSpeed = optionalNumber(L"wind_speed_10m", 0, 500);
        snapshot.windDirection = optionalNumber(L"wind_direction_10m", 0, 360);
        const double code = current.GetNamedNumber(L"weather_code");
        const double isDay = current.GetNamedNumber(L"is_day");
        if (!std::isfinite(snapshot.temperature) || snapshot.temperature < -100 || snapshot.temperature > 70 ||
            !std::isfinite(code) || code < 0 || code > 99 || code != std::floor(code) || (isDay != 0 && isDay != 1))
            return std::nullopt;
        snapshot.code = static_cast<int>(code);
        snapshot.daylight = isDay == 1;
        snapshot.location = location;
        snapshot.available = true;
        snapshot.updatedAt = std::chrono::system_clock::now();
        if (root.HasKey(L"daily"))
        {
            const auto daily = root.GetNamedObject(L"daily");
            const auto dates = daily.GetNamedArray(L"time");
            const auto highs = daily.GetNamedArray(L"temperature_2m_max");
            const auto lows = daily.GetNamedArray(L"temperature_2m_min");
            const auto codes = daily.GetNamedArray(L"weather_code");
            if (dates.Size() != 7 || highs.Size() != 7 || lows.Size() != 7 || codes.Size() != 7)
                return std::nullopt;
            for (uint32_t i = 0; i < 7; ++i)
            {
                WeatherDay day{std::wstring{dates.GetStringAt(i)}, highs.GetNumberAt(i), lows.GetNumberAt(i), 0};
                const double dayCode = codes.GetNumberAt(i);
                if (day.date.size() != 10 || day.date[4] != L'-' || day.date[7] != L'-' ||
                    !std::isfinite(day.high) || !std::isfinite(day.low) || day.low < -100 ||
                    day.high > 70 || day.low > day.high || !std::isfinite(dayCode) ||
                    dayCode < 0 || dayCode > 99 || dayCode != std::floor(dayCode)) return std::nullopt;
                for (size_t j = 0; j < day.date.size(); ++j)
                    if (j != 4 && j != 7 && (day.date[j] < L'0' || day.date[j] > L'9')) return std::nullopt;
                const auto dateValue = [](const std::wstring& text)
                {
                    return std::chrono::year_month_day{std::chrono::year{std::stoi(text.substr(0, 4))},
                        std::chrono::month{static_cast<unsigned>(std::stoi(text.substr(5, 2)))},
                        std::chrono::day{static_cast<unsigned>(std::stoi(text.substr(8, 2)))}};
                };
                const auto date = dateValue(day.date);
                if (!date.ok() || (i && std::chrono::sys_days{date} -
                    std::chrono::sys_days{dateValue(snapshot.days.back().date)} != std::chrono::days{1})) return std::nullopt;
                day.code = static_cast<int>(dayCode);
                snapshot.days.push_back(std::move(day));
            }
        }
        return snapshot;
    }
    catch (const winrt::hresult_error&) { return std::nullopt; }
}

std::wstring WeatherDescription(int code)
{
    if (code == 0) return L"晴";
    if (code == 1 || code == 2) return L"多云";
    if (code == 3) return L"阴";
    if (code == 45 || code == 48) return L"雾";
    if (code == 51 || code == 53 || code == 55) return L"毛毛雨";
    if (code == 56 || code == 57 || code == 66 || code == 67) return L"冻雨";
    if (code == 61 || code == 63 || code == 65) return L"雨";
    if (code == 71 || code == 73 || code == 75 || code == 77) return L"雪";
    if (code == 80 || code == 81 || code == 82) return L"阵雨";
    if (code == 85 || code == 86) return L"阵雪";
    if (code == 95 || code == 96 || code == 99) return L"雷雨";
    return L"天气";
}

void WeatherData::LoadCache(const std::filesystem::path& directory)
{
    if (directory.empty()) return;
    cachePath_ = directory / L"weather.json";
    try
    {
        using namespace winrt::Windows::Data::Json;
        std::ifstream stream(cachePath_, std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        JsonObject root;
        if (!JsonObject::TryParse(winrt::to_hstring(content), root)) return;
        WeatherLocation location{root.GetNamedNumber(L"latitude"), root.GetNamedNumber(L"longitude"),
            std::wstring{root.GetNamedString(L"city")}, std::wstring{root.GetNamedString(L"source")}};
        if (!ValidCoordinates(location.latitude, location.longitude)) return;
        auto cached = ParseWeather(winrt::to_string(root.GetNamedObject(L"response").Stringify()), location);
        if (!cached) return;
        const double seconds = root.GetNamedNumber(L"updatedAt");
        const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > now + 300 || now - seconds > 24 * 3600) return;
        cached->updatedAt = std::chrono::system_clock::time_point{
            std::chrono::seconds{static_cast<long long>(seconds)}};
        const std::lock_guard lock(mutex_);
        snapshot_ = std::move(*cached);
    }
    catch (const winrt::hresult_error&) { }
}

bool WeatherData::Refresh(std::stop_token stop, bool automaticLocation)
{
    WeatherLocation location;
    location.source = automaticLocation ? L"默认北京" : L"手动选择";
    if (automaticLocation)
    {
        if (!located_ || std::chrono::steady_clock::now() - locatedAt_ > std::chrono::hours(6))
        {
            auto found = SystemLocation(stop);
            if (!found && !stop.stop_requested())
                found = ParseNetworkLocation(GetHttps(L"ipwho.is", L"/", stop));
            if (found)
            {
                located_ = std::move(found);
                locatedAt_ = std::chrono::steady_clock::now();
            }
        }
        if (located_) location = *located_;
    }
    if (stop.stop_requested()) return false;
    std::wostringstream path;
    path.imbue(std::locale::classic());
    path << std::fixed << std::setprecision(4) << L"/v1/forecast?latitude=" << location.latitude
         << L"&longitude=" << location.longitude
         << L"&current=temperature_2m,weather_code,is_day,relative_humidity_2m,wind_speed_10m,wind_direction_10m"
         << L"&daily=temperature_2m_max,temperature_2m_min,weather_code&forecast_days=7&timezone=auto";
    const auto content = GetHttps(L"api.open-meteo.com", path.str(), stop);
    if (stop.stop_requested()) return false;
    auto weather = ParseWeather(content, location);
    if (weather && weather->days.size() != 7) weather.reset();
    if (!weather)
    {
        const std::lock_guard lock(mutex_);
        snapshot_.failed = true;
        if (!snapshot_.available) snapshot_.location = location;
        return true;
    }
    {
        const std::lock_guard lock(mutex_);
        snapshot_ = *weather;
    }
    if (!cachePath_.empty())
    {
        using namespace winrt::Windows::Data::Json;
        JsonObject cached;
        cached.Insert(L"latitude", JsonValue::CreateNumberValue(location.latitude));
        cached.Insert(L"longitude", JsonValue::CreateNumberValue(location.longitude));
        cached.Insert(L"city", JsonValue::CreateStringValue(location.city));
        cached.Insert(L"source", JsonValue::CreateStringValue(location.source));
        cached.Insert(L"updatedAt", JsonValue::CreateNumberValue(
            std::chrono::duration<double>(weather->updatedAt.time_since_epoch()).count()));
        cached.Insert(L"response", JsonObject::Parse(winrt::to_hstring(content)));
        const bool saved = WriteFileAtomically(cachePath_, winrt::to_string(cached.Stringify()));
        static_cast<void>(saved);
    }
    return true;
}

WeatherSnapshot WeatherData::Snapshot() const
{
    const std::lock_guard lock(mutex_);
    return snapshot_;
}
}
