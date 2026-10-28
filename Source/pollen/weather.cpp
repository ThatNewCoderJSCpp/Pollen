#include "pollen/weather.hpp"

#include <fizmo/../networking.hpp>

#include <cctype>
#include <cstdio>
#include <future>

namespace pollen {

namespace http = fizmo::networking::http;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

void ensure_loop() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (!fizmo::networking::background_running()) fizmo::networking::start_background(1);
    });
}

} // namespace

std::string weather_condition(int code) {
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: return "Fog";
    case 48: return "Freezing fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56: return "Light freezing drizzle";
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: return "Light freezing rain";
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: return "Light snow showers";
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: return "Thunderstorm with hail";
    case 99: return "Thunderstorm with heavy hail";
    default: return code < 0 ? std::string() : std::string("Unknown");
    }
}

std::string weather_icon(int code, bool is_day) {
    if (code < 0) return std::string();
    if (code == 0) return is_day ? "\xe2\x98\x80\xef\xb8\x8f" : "\xf0\x9f\x8c\x99";
    if (code <= 2) return is_day ? "\xe2\x9b\x85" : "\xe2\x98\x81\xef\xb8\x8f";
    if (code == 3) return "\xe2\x98\x81\xef\xb8\x8f";
    if (code == 45 || code == 48) return "\xf0\x9f\x8c\xab\xef\xb8\x8f";
    if (code >= 71 && code <= 77) return "\xf0\x9f\x8c\xa8\xef\xb8\x8f";
    if (code == 85 || code == 86) return "\xf0\x9f\x8c\xa8\xef\xb8\x8f";
    if (code >= 95) return "\xe2\x9b\x88\xef\xb8\x8f";
    return "\xf0\x9f\x8c\xa7\xef\xb8\x8f";
}

std::string url_encode(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::optional<Place> parse_geocode(const std::string& json, const std::string& query, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json, root, &why) || !root.is_object()) {
        if (error) *error = "the place search didn't answer with JSON";
        return std::nullopt;
    }
    const auto& results = root["results"].items();
    if (results.empty()) {
        if (error) *error = "couldn't find \"" + query + "\"";
        return std::nullopt;
    }
    std::string hint;
    const std::size_t comma = query.find(',');
    if (comma != std::string::npos) hint = lower(trim(query.substr(comma + 1)));
    const fizmo::json::Value* pick = &results[0];
    if (!hint.empty()) {
        for (const auto& r : results) {
            const std::string admin = lower(r["admin1"].as_string());
            const std::string country = lower(r["country"].as_string());
            const std::string code = lower(r["country_code"].as_string());
            if ((!admin.empty() && (admin.find(hint) != std::string::npos || hint.find(admin) != std::string::npos)) || country.find(hint) != std::string::npos || code == hint) {
                pick = &r;
                break;
            }
        }
    }
    Place p;
    p.latitude = (*pick)["latitude"].as_number();
    p.longitude = (*pick)["longitude"].as_number();
    p.name = (*pick)["name"].as_string();
    const std::string admin = (*pick)["admin1"].as_string();
    const std::string country = (*pick)["country_code"].as_string();
    if (!admin.empty()) p.name += ", " + admin;
    else if (!country.empty()) p.name += ", " + country;
    return p;
}

std::optional<WeatherSnapshot> parse_forecast(const std::string& json, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json, root, &why) || !root.is_object()) {
        if (error) *error = "the weather service didn't answer with JSON";
        return std::nullopt;
    }
    if (root["error"].as_bool(false)) {
        if (error) *error = root["reason"].as_string("the weather service refused the request");
        return std::nullopt;
    }
    const fizmo::json::Value& c = root["current"];
    if (!c.is_object() || !c["temperature_2m"].is_number()) {
        if (error) *error = "the weather answer has no current conditions";
        return std::nullopt;
    }
    WeatherSnapshot w;
    w.ok = true;
    w.temp_c = c["temperature_2m"].as_number();
    w.feels_c = c["apparent_temperature"].as_number(w.temp_c);
    w.humidity = c["relative_humidity_2m"].as_number();
    w.wind_kmh = c["wind_speed_10m"].as_number();
    w.code = static_cast<int>(c["weather_code"].as_number(-1));
    w.is_day = c["is_day"].as_number(1) != 0.0;
    w.updated = std::chrono::system_clock::now();
    return w;
}

Weather::Weather() = default;

Weather::~Weather() {
    stop();
}

void Weather::configure(const WeatherConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (config == m_config && m_running.load()) return;
    m_config = config;
    m_dirty = true;
    if (!config.usable()) {
        m_snapshot = WeatherSnapshot();
        m_snapshot.error = config.coordinates ? "set a latitude and longitude" : "set a city";
    } else {
        m_snapshot.loading = true;
    }
    if (!m_running.exchange(true)) m_thread = std::thread([this] { run(); });
    m_wake.notify_all();
}

void Weather::refresh() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirty = true;
    m_wake.notify_all();
}

void Weather::stop() {
    if (!m_running.exchange(false)) return;
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

WeatherSnapshot Weather::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

bool Weather::fetch(const std::string& url, std::string& body, std::string& error) {
    ensure_loop();
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string target = url;
        if (attempt == 1) {
            if (target.compare(0, 8, "https://") != 0) break;
            target = "http://" + target.substr(8);
        }
        auto promise = std::make_shared<std::promise<std::pair<std::string, int>>>();
        auto result = promise->get_future();
        http::FetchOptions options;
        options.timeout = std::chrono::milliseconds(12000);
        options.headers.set("User-Agent", "Pollen (VRChat OSC app)");
        http::fetch(target, options, [promise](const std::error_code& ec, http::FetchResponse r) {
            try {
                promise->set_value({ ec ? std::string("error: ") + ec.message() : r.body, ec ? -1 : r.status });
            } catch (...) {
            }
        });
        if (result.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            error = "the weather service didn't answer";
            continue;
        }
        const auto got = result.get();
        if (got.second == 200) {
            body = got.first;
            return true;
        }
        error = got.second < 0 ? "couldn't reach the weather service (" + got.first.substr(7) + ")" : "the weather service answered " + std::to_string(got.second);
    }
    return false;
}

void Weather::run() {
    std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
    while (m_running.load()) {
        WeatherConfig config;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_until(lock, next, [this] { return !m_running.load() || m_dirty; });
            if (!m_running.load()) break;
            m_dirty = false;
            config = m_config;
        }
        const double minutes = config.refresh_minutes < 5.0 ? 5.0 : config.refresh_minutes;
        next = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<long long>(minutes * 60.0));
        if (!config.usable()) continue;
        std::string error, body;
        Place place;
        if (config.coordinates) {
            place.latitude = config.latitude;
            place.longitude = config.longitude;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.3f, %.3f", config.latitude, config.longitude);
            place.name = buf;
        } else if (m_geocoded_city == config.city) {
            place = m_place;
        } else {
            std::string name = config.city;
            const std::size_t comma = name.find(',');
            if (comma != std::string::npos) name = trim(name.substr(0, comma));
            if (fetch(config.geocode_url + "?name=" + url_encode(name) + "&count=10&language=en&format=json", body, error)) {
                if (const auto p = parse_geocode(body, config.city, &error)) {
                    place = *p;
                    m_place = place;
                    m_geocoded_city = config.city;
                }
            }
            if (m_geocoded_city != config.city) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_snapshot.loading = false;
                m_snapshot.ok = false;
                m_snapshot.error = error;
                next = std::chrono::steady_clock::now() + std::chrono::minutes(2);
                continue;
            }
        }
        const std::string url = config.forecast_url + "?latitude=" + number(place.latitude) + "&longitude=" + number(place.longitude) +
            "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day&timezone=auto";
        std::optional<WeatherSnapshot> w;
        if (fetch(url, body, error)) w = parse_forecast(body, &error);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config != config) continue;
        if (w) {
            m_snapshot = *w;
            m_snapshot.place = place.name;
        } else {
            m_snapshot.loading = false;
            m_snapshot.error = error;
            next = std::chrono::steady_clock::now() + std::chrono::minutes(2);
        }
    }
}

} // namespace pollen
