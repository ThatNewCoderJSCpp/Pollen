#ifndef POLLEN_WEATHER_HPP
#define POLLEN_WEATHER_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace pollen {

struct WeatherConfig {
    bool coordinates = false;
    std::string city;
    double latitude = 0.0;
    double longitude = 0.0;
    double refresh_minutes = 15.0;
    std::string forecast_url = "https://api.open-meteo.com/v1/forecast";
    std::string geocode_url = "https://geocoding-api.open-meteo.com/v1/search";

    bool operator==(const WeatherConfig& o) const noexcept {
        return coordinates == o.coordinates && city == o.city && latitude == o.latitude && longitude == o.longitude && refresh_minutes == o.refresh_minutes && forecast_url == o.forecast_url && geocode_url == o.geocode_url;
    }
    bool operator!=(const WeatherConfig& o) const noexcept { return !(*this == o); }
    bool usable() const noexcept { return coordinates ? (latitude != 0.0 || longitude != 0.0) : !city.empty(); }
};

struct WeatherSnapshot {
    bool ok = false;
    bool loading = false;
    std::string error;
    std::string place;
    double temp_c = 0.0;
    double feels_c = 0.0;
    double humidity = 0.0;
    double wind_kmh = 0.0;
    int code = -1;
    bool is_day = true;
    std::chrono::system_clock::time_point updated;
};

struct Place {
    std::string name;
    double latitude = 0.0;
    double longitude = 0.0;
};

std::string weather_condition(int code);
std::string weather_icon(int code, bool is_day);
std::string url_encode(const std::string& text);
std::optional<Place> parse_geocode(const std::string& json, const std::string& query, std::string* error = nullptr);
std::optional<WeatherSnapshot> parse_forecast(const std::string& json, std::string* error = nullptr);

class Weather {
public:
    Weather();
    ~Weather();

    Weather(const Weather&) = delete;
    Weather& operator=(const Weather&) = delete;

    void configure(const WeatherConfig& config);
    void refresh();
    void stop();
    WeatherSnapshot snapshot() const;

private:
    void run();
    bool fetch(const std::string& url, std::string& body, std::string& error);

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    bool m_dirty = false;
    WeatherConfig m_config;
    WeatherSnapshot m_snapshot;
    std::string m_geocoded_city;
    Place m_place;
};

} // namespace pollen

#endif // POLLEN_WEATHER_HPP
