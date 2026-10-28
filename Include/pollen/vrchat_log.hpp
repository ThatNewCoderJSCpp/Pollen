#ifndef POLLEN_VRCHAT_LOG_HPP
#define POLLEN_VRCHAT_LOG_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pollen {

struct LocationInfo {
    bool valid = false;
    std::string special;
    std::string world_id;
    std::string instance;
    std::string access;
    std::string region;
    std::string owner_id;
    std::string group_id;
};

LocationInfo parse_location(const std::string& location);
std::string access_label(const std::string& access);
std::string region_label(const std::string& region);
std::string location_label(const LocationInfo& info);

enum class LogKind { Joining, Entering, PlayerJoined, PlayerLeft, LeftRoom };

struct LogEvent {
    LogKind kind = LogKind::Joining;
    std::chrono::system_clock::time_point time;
    bool has_time = false;
    std::string text;
    std::string user_id;
};

std::optional<LogEvent> parse_log_line(const std::string& line);
std::optional<std::chrono::system_clock::time_point> parse_log_time(const std::string& line);

struct InstancePlayer {
    std::string name;
    std::string id;
    std::chrono::system_clock::time_point joined;
};

struct WorldSnapshot {
    bool in_world = false;
    std::string world_name;
    std::string location;
    LocationInfo info;
    std::vector<InstancePlayer> players;
    std::chrono::system_clock::time_point entered;
    std::string log_file;
};

void apply_log_event(WorldSnapshot& world, const LogEvent& event);

std::vector<std::filesystem::path> vrchat_log_folders(const std::vector<std::filesystem::path>& extra_osc_folders = {});
std::optional<std::filesystem::path> newest_vrchat_log(const std::vector<std::filesystem::path>& folders);

class VrchatLog {
public:
    void set_folders(std::vector<std::filesystem::path> folders);
    std::vector<LogEvent> poll();
    const WorldSnapshot& world() const noexcept { return m_world; }
    const std::filesystem::path& file() const noexcept { return m_file; }
    bool found() const noexcept { return !m_file.empty(); }

private:
    std::vector<LogEvent> read_new(bool replay);

    std::vector<std::filesystem::path> m_folders;
    std::filesystem::path m_file;
    std::uintmax_t m_offset = 0;
    std::string m_partial;
    WorldSnapshot m_world;
    bool m_started = false;
    std::chrono::steady_clock::time_point m_last_scan;
};

} // namespace pollen

#endif // POLLEN_VRCHAT_LOG_HPP
