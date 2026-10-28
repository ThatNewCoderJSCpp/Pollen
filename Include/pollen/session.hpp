#ifndef POLLEN_SESSION_HPP
#define POLLEN_SESSION_HPP

#include "media.hpp"
#include "modules.hpp"
#include "sensors.hpp"
#include "weather.hpp"
#include "settings.hpp"
#include "status.hpp"
#include "system_stats.hpp"

#include <osc/osc.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace pollen {

struct LogLine {
    std::uint64_t seq = 0;
    char kind = 'i';
    double seconds = 0.0;
    std::string text;
};

struct ParamRow {
    std::string name;
    osc::vrchat::ParamType type = osc::vrchat::ParamType::Unknown;
    bool known = false;
    bool live = false;
    bool writable = true;
    std::string value;
    double age = -1.0;
};

class Session {
public:
    explicit Session(std::filesystem::path settings_path = Settings::default_path());
    ~Session();

    Settings& settings() noexcept { return m_settings; }
    const Settings& settings() const noexcept { return m_settings; }
    bool save_settings(std::string* error = nullptr);
    const std::filesystem::path& settings_path() const noexcept { return m_settings_path; }
    const std::string& load_error() const noexcept { return m_load_error; }

    bool connect(std::string* error = nullptr);
    void disconnect();
    bool connected() const noexcept { return m_connection.sending(); }
    bool hearing_vrchat() const { return m_connection.hearing_vrchat(); }

    void tick();

    bool set_parameter(const std::string& name, const std::string& type, const std::string& text, std::string* error = nullptr);
    bool chatbox(const std::string& text, bool notify);
    bool typing(bool on);
    bool send_text(const std::string& line, std::string* error = nullptr);

    void set_status_enabled(bool on);
    void set_status_template(const std::string& text);
    std::string status_preview();
    bool send_status_now();
    StatusValues status_values();

    std::vector<ParamRow> parameters() const;
    std::uint64_t parameter_revision() const noexcept { return m_connection.avatar().revision(); }
    std::uint64_t value_revision() const noexcept { return m_connection.avatar().value_revision(); }
    std::string avatar_name() const;
    const std::string& avatar_id() const noexcept { return m_connection.avatar().avatar_id(); }
    bool reload_avatar();

    std::vector<LogLine> log_since(std::uint64_t seq, std::size_t max = 400) const;
    void clear_log();
    void log(char kind, const std::string& text);

    osc::Stats sent_stats() const { return m_connection.sender().stats(); }
    osc::Stats received_stats() const { return m_connection.receiver().stats(); }
    std::string last_error() const;
    std::vector<std::filesystem::path> osc_folders() const;

    std::string oscquery_status() const;
    bool found_vrchat() const { return m_link.vrchat().has_value(); }
    osc::vrchat::QueryLink& query_link() noexcept { return m_link; }

    osc::vrchat::Connection& connection() noexcept { return m_connection; }
    const SystemSnapshot& system() const noexcept { return m_stats.snapshot(); }
    NowPlaying now_playing() const { return m_media.now_playing(); }
    SensorSnapshot sensors() const { return m_sensors.snapshot(); }
    WeatherSnapshot weather() const { return m_weather.snapshot(); }

    ModuleInputs module_inputs() const;
    std::string module_preview(const std::string& module, const std::string& text) const;
    std::string module_status(const std::string& module) const;
    std::string module_warning(const std::string& module) const;
    bool set_module_option(const std::string& module, const std::string& option, const std::string& value, std::string* error = nullptr);
    std::string module_template(const std::string& module) const;
    bool set_module_template(const std::string& module, const std::string& text, std::string* error = nullptr);
    double connected_seconds() const;

private:
    void apply_avatar(osc::vrchat::AvatarSnapshot snapshot);
    void start_link();

    Settings m_settings;
    std::filesystem::path m_settings_path;
    std::string m_load_error;
    std::string m_error;
    osc::vrchat::Connection m_connection;
    SystemStats m_stats;
    Sensors m_sensors;
    Weather m_weather;
    Media m_media;
    StatusRotation m_status;
    osc::vrchat::QueryLink m_link;
    std::uint64_t m_vrchat_revision = 0;
    std::string m_link_error;
    bool m_auto_target = false;
    std::deque<LogLine> m_log;
    std::uint64_t m_seq = 0;
    std::chrono::steady_clock::time_point m_started;
    std::chrono::steady_clock::time_point m_last_stats;
    bool m_typing = false;
    bool m_heard = false;
    std::chrono::steady_clock::time_point m_heard_since;
};

std::optional<osc::Message> parse_message_line(const std::string& line, std::string* error = nullptr);

} // namespace pollen

#endif // POLLEN_SESSION_HPP
