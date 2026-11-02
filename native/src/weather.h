#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace wincal
{
struct WeatherLocation
{
    double latitude{39.9042};
    double longitude{116.4074};
    std::wstring city{L"北京"};
    std::wstring source{L"北京"};
};

struct WeatherDay
{
    std::wstring date;
    double high{};
    double low{};
    int code{};
};

struct WeatherSnapshot
{
    WeatherLocation location;
    double temperature{};
    std::optional<double> humidity, windSpeed, windDirection;
    int code{};
    bool daylight{true};
    bool available{};
    bool failed{};
    std::chrono::system_clock::time_point updatedAt{};
    std::vector<WeatherDay> days;
};

[[nodiscard]] std::optional<WeatherLocation> ParseNetworkLocation(std::string_view json);
[[nodiscard]] std::optional<WeatherSnapshot> ParseWeather(std::string_view json, const WeatherLocation& location);
[[nodiscard]] std::wstring WeatherDescription(int code);

class WeatherData
{
public:
    void LoadCache(const std::filesystem::path& directory);
    // Call from a COM-initialized worker. Last successful weather survives failures.
    [[nodiscard]] bool Refresh(std::stop_token stop, bool automaticLocation);
    [[nodiscard]] WeatherSnapshot Snapshot() const;

private:
    mutable std::mutex mutex_;
    WeatherSnapshot snapshot_;
    std::filesystem::path cachePath_;
    std::optional<WeatherLocation> located_;
    std::chrono::steady_clock::time_point locatedAt_{};
};
}
