#include "../src/weather.h"
#include "../src/atomic_file.h"
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <iostream>

int main(int argc, char** argv)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    int failures{};
    const auto check = [&](bool condition, const char* name)
    {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    if (argc == 2 && (std::string_view{argv[1]} == "--live-beijing" || std::string_view{argv[1]} == "--live-auto" || std::string_view{argv[1]} == "--live-wttr"))
    {
        wincal::WeatherData data;
        const bool wttr = std::string_view{argv[1]} == "--live-wttr";
        check(data.Refresh({}, std::string_view{argv[1]} == "--live-auto", wttr), "live refresh completed");
        const auto snapshot = data.Snapshot();
        check(snapshot.available && !snapshot.failed, "live weather is available");
        check(wttr ? !snapshot.days.empty() : snapshot.days.size() == 7, "live forecast is available");
        std::cout << "city=" << winrt::to_string(snapshot.location.city)
                  << "; source=" << winrt::to_string(snapshot.location.source)
                  << "; temperature=" << snapshot.temperature << "; code=" << snapshot.code
                  << "; forecastDays=" << snapshot.days.size()
                  << "; feelsLike=" << snapshot.feelsLike.value_or(-999)
                  << "; uv=" << snapshot.uvIndex.value_or(-1)
                  << "; humidity=" << snapshot.humidity.value_or(-1)
                  << "; windKmph=" << snapshot.windSpeed.value_or(-1) << '\n';
        return failures ? 1 : 0;
    }
    const auto location = wincal::ParseNetworkLocation(R"({"success":true,"city":"Beijing",
        "latitude":39.9042,"longitude":116.4074})");
    check(location && location->city == L"北京" && location->source == L"网络定位", "coarse location parsing");
    check(!wincal::ParseNetworkLocation(R"({"success":false,"message":"rate limited"})"), "location denial rejected");
    check(!wincal::ParseNetworkLocation(R"({"success":true,"city":"X","latitude":91,"longitude":116})"),
          "invalid coordinates rejected");
    check(!wincal::ParseNetworkLocation(R"({"success":true,"city":"X","latitude":"39","longitude":116})"),
          "wrong location types rejected");
    const wincal::WeatherLocation beijing;
    const std::string wttrResponse = R"({"current_condition":[{"temp_C":"12","FeelsLikeC":"10","humidity":"68",
        "windspeedKmph":"18","winddirDegree":"270","uvIndex":"3","weatherCode":"143"}],
        "weather":[{"date":"2026-11-04","maxtempC":"15","mintempC":"5","hourly":[{"weatherCode":"116"}]},
                   {"date":"2026-11-05","maxtempC":"13","mintempC":"4","hourly":[{"weatherCode":"296"}]},
                   {"date":"2026-11-06","maxtempC":"9","mintempC":"0","hourly":[{"weatherCode":"326"}]}]})";
    const auto wttr = wincal::ParseWttrWeather(wttrResponse, beijing);
    check(wttr && wttr->provider == L"wttr.in" && wttr->temperature == 12 && wttr->feelsLike == 10 &&
        wttr->humidity == 68 && wttr->windSpeed == 18 && wttr->code == 45 && wttr->uvIndex == 3 &&
        !wttr->uvDailyMaximum && wttr->days.size() == 3 && wttr->days.back().code == 73,
        "wttr fields, units, fog mapping, current UV and three-day forecast");
    check(!wincal::ParseWttrWeather("{}", beijing) && !wincal::ParseWttrWeather("<html>error</html>", beijing),
        "wttr failure responses rejected");
    auto badWttr = wttrResponse;
    badWttr.replace(badWttr.find("\"12\""), 4, "\"12junk\"");
    check(!wincal::ParseWttrWeather(badWttr, beijing), "wttr rejects partially parsed temperature");
    auto hazeResponse = wttrResponse;
    hazeResponse.replace(hazeResponse.find("\"143\""), 5, "\"149\"");
    const auto haze = wincal::ParseWttrWeather(hazeResponse, beijing);
    check(haze && haze->condition == L"烟霾" && haze->code == 45, "wttr extended haze code retains its description");
    const auto uvWeather = wincal::ParseWeather(R"({"current":{"temperature_2m":20,"apparent_temperature":18,
        "weather_code":0,"is_day":1}})", beijing);
    check(uvWeather && uvWeather->feelsLike == 18, "Open-Meteo apparent temperature");
    check(wincal::UvLevel(2) == L"低" && wincal::UvLevel(3) == L"中等" && wincal::UvLevel(6) == L"高" &&
        wincal::UvLevel(8) == L"很高" && wincal::UvLevel(11) == L"极高", "UV level boundaries");
    if (wttr) check(wincal::ClothingAdvice(*wttr).find(L"夹克") != std::wstring::npos, "clothing uses feels-like temperature");
    const std::string response = R"({"current":{"temperature_2m":-3.5,"weather_code":71,"is_day":0}})";
    const auto weather = wincal::ParseWeather(response, beijing);
    const auto detailed = wincal::ParseWeather(R"({"current":{"temperature_2m":8,"weather_code":0,"is_day":1,
        "relative_humidity_2m":28,"wind_speed_10m":15,"wind_direction_10m":315}})", beijing);
    check(detailed && detailed->humidity == 28 && detailed->windSpeed == 15 && detailed->windDirection == 315,
          "taskbar humidity and wind values survive parsing");
    check(weather && !weather->humidity && !weather->windSpeed && !weather->windDirection,
          "old cached forecasts do not invent humidity or wind values");
    std::string forecastResponse = R"({"current":{"temperature_2m":-3.5,"weather_code":71,"is_day":0},"daily":{
        "time":["2026-11-02","2026-11-03","2026-11-04","2026-11-05","2026-11-06","2026-11-07","2026-11-08"],
        "temperature_2m_max":[0,1,2,3,4,5,6],"temperature_2m_min":[-10,-9,-8,-7,-6,-5,-4],
        "weather_code":[71,2,3,61,0,0,2]}})";
    const auto forecast = wincal::ParseWeather(forecastResponse, beijing);
    check(forecast && forecast->days.size() == 7 && forecast->days[0].low == -10 &&
        forecast->days[6].date == L"2026-11-08" && forecast->days[6].high == 6,
        "seven day forecast preserves aligned dates and subzero temperatures");
    auto uvResponse = forecastResponse;
    uvResponse.insert(uvResponse.find("\"time\""), "\"uv_index_max\":[6.2,5,4,3,2,1,0],");
    const auto dailyUv = wincal::ParseWeather(uvResponse, beijing);
    check(dailyUv && dailyUv->uvIndex == 6.2 && dailyUv->uvDailyMaximum, "Open-Meteo UV is marked as daily maximum");
    auto invalid = forecastResponse;
    invalid.replace(invalid.find("[-10,-9"), 4, "[10");
    check(!wincal::ParseWeather(invalid, beijing), "inverted daily temperature range rejected");
    invalid = forecastResponse;
    invalid.replace(invalid.find("[71,2,3,61,0,0,2]"), 17, "[71]");
    check(!wincal::ParseWeather(invalid, beijing), "unequal daily array lengths rejected");
    invalid = forecastResponse;
    invalid.replace(invalid.find("2026-11-03"), 10, "2026-11-09");
    check(!wincal::ParseWeather(invalid, beijing), "nonconsecutive forecast dates rejected");
    invalid = forecastResponse;
    invalid.replace(invalid.find("2026-11-02"), 10, "2026-11-32");
    check(!wincal::ParseWeather(invalid, beijing), "invalid forecast calendar date rejected");
    invalid = forecastResponse;
    invalid.replace(invalid.find("[0,1,2,3,4,5,6]"), 15, "[null,1,2,3,4,5,6]");
    check(!wincal::ParseWeather(invalid, beijing), "missing daily temperature rejected");
    check(weather && weather->temperature == -3.5 && weather->code == 71 && !weather->daylight &&
          weather->location.city == L"北京", "sub-zero night weather retains requested location");
    check(!wincal::ParseWeather(R"({"error":true,"reason":"invalid request"})", beijing), "API errors rejected");
    check(!wincal::ParseWeather(R"({"current":{"weather_code":0,"is_day":1}})", beijing), "missing temperature rejected");
    check(!wincal::ParseWeather(R"({"current":{"temperature_2m":1000,"weather_code":0,"is_day":1}})", beijing),
          "invalid temperatures rejected");
    check(!wincal::ParseWeather(R"({"current":{"temperature_2m":12,"weather_code":1.5,"is_day":1}})", beijing),
          "fractional weather codes rejected");
    check(!wincal::ParseWeather(R"({"current":{"temperature_2m":12,"weather_code":0,"is_day":2}})", beijing),
          "invalid daylight flag rejected");
    check(wincal::WeatherDescription(0) == L"晴" && wincal::WeatherDescription(2) == L"多云" &&
          wincal::WeatherDescription(45) == L"雾" && wincal::WeatherDescription(65) == L"雨" &&
          wincal::WeatherDescription(71) == L"雪" && wincal::WeatherDescription(95) == L"雷雨", "WMO descriptions");

    const auto directory = std::filesystem::temp_directory_path() /
        (L"WinCalWeatherTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const auto path = directory / L"weather.json";
    using namespace winrt::Windows::Data::Json;
    JsonObject cached;
    cached.Insert(L"latitude", JsonValue::CreateNumberValue(beijing.latitude));
    cached.Insert(L"longitude", JsonValue::CreateNumberValue(beijing.longitude));
    cached.Insert(L"city", JsonValue::CreateStringValue(L"北京"));
    cached.Insert(L"source", JsonValue::CreateStringValue(L"手动选择"));
    const double epoch = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    cached.Insert(L"updatedAt", JsonValue::CreateNumberValue(epoch - 3600));
    cached.Insert(L"response", JsonObject::Parse(winrt::to_hstring(forecastResponse)));
    check(wincal::WriteFileAtomically(path, winrt::to_string(cached.Stringify())), "prepare isolated weather cache");
    wincal::WeatherData data;
    data.LoadCache(directory);
    check(data.Snapshot().available && data.Snapshot().temperature == -3.5 &&
          data.Snapshot().days.size() == 7 &&
          std::chrono::system_clock::now() - data.Snapshot().updatedAt > std::chrono::minutes(59),
          "offline cache retains temperature and its original age");
    cached.Insert(L"provider", JsonValue::CreateStringValue(L"wttr.in"));
    cached.Insert(L"response", JsonObject::Parse(winrt::to_hstring(wttrResponse)));
    check(wincal::WriteFileAtomically(path, winrt::to_string(cached.Stringify())), "prepare wttr cache");
    wincal::WeatherData wttrCache;
    wttrCache.LoadCache(directory);
    check(wttrCache.Snapshot().provider == L"wttr.in" && wttrCache.Snapshot().feelsLike == 10 &&
        wttrCache.Snapshot().days.size() == 3, "wttr cache preserves provider and detailed fields");
    check(wincal::WriteFileAtomically(path, "{broken"), "prepare invalid cache");
    data.LoadCache(directory);
    check(data.Snapshot().available && data.Snapshot().temperature == -3.5, "bad cache keeps the last good snapshot");
    cached.Insert(L"updatedAt", JsonValue::CreateNumberValue(epoch - 25 * 3600));
    check(wincal::WriteFileAtomically(path, winrt::to_string(cached.Stringify())), "prepare expired cache");
    wincal::WeatherData expired;
    expired.LoadCache(directory);
    check(!expired.Snapshot().available, "cache older than one day is not shown as current weather");
    std::stop_source stop;
    stop.request_stop();
    check(!data.Refresh(stop.get_token(), false) && data.Snapshot().temperature == -3.5,
          "cancellation retains existing data without a network request");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::cout << "Weather regression failures: " << failures << '\n';
    return failures ? 1 : 0;
}
