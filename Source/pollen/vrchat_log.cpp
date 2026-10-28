#include "pollen/vrchat_log.hpp"

#include <osc/vrchat/paths.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>

namespace pollen {

namespace fs = std::filesystem;

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

std::string tag_value(const std::string& location, const std::string& tag) {
    const std::string key = "~" + tag + "(";
    const std::size_t at = location.find(key);
    if (at == std::string::npos) return std::string();
    const std::size_t start = at + key.size();
    const std::size_t end = location.find(')', start);
    return end == std::string::npos ? std::string() : location.substr(start, end - start);
}

bool has_tag(const std::string& location, const std::string& tag) {
    const std::string key = "~" + tag;
    std::size_t at = location.find(key);
    while (at != std::string::npos) {
        const std::size_t after = at + key.size();
        if (after == location.size() || location[after] == '(' || location[after] == '~') return true;
        at = location.find(key, after);
    }
    return false;
}

void split_name_id(const std::string& rest, std::string& name, std::string& id) {
    name = trim(rest);
    id.clear();
    if (name.size() > 2 && name.back() == ')') {
        const std::size_t open = name.rfind(" (");
        if (open != std::string::npos && name.compare(open + 2, 4, "usr_") == 0) {
            id = name.substr(open + 2, name.size() - open - 3);
            name = name.substr(0, open);
        }
    }
}

bool after(const std::string& line, const std::string& marker, std::string& rest) {
    const std::size_t at = line.find(marker);
    if (at == std::string::npos) return false;
    rest = line.substr(at + marker.size());
    return true;
}

} // namespace

LocationInfo parse_location(const std::string& location) {
    LocationInfo info;
    const std::string loc = trim(location);
    if (loc.empty() || loc == "offline" || loc == "private" || loc == "traveling" || loc == "traveling:traveling") {
        info.special = loc.empty() ? std::string("offline") : (loc.compare(0, 9, "traveling") == 0 ? std::string("traveling") : loc);
        return info;
    }
    const std::size_t colon = loc.find(':');
    if (loc.compare(0, 5, "wrld_") != 0 || colon == std::string::npos) {
        info.special = "unknown";
        return info;
    }
    info.valid = true;
    info.world_id = loc.substr(0, colon);
    const std::string rest = loc.substr(colon + 1);
    info.instance = rest.substr(0, rest.find('~'));
    info.region = tag_value(rest, "region");
    if (has_tag(rest, "hidden")) {
        info.access = "friends_plus";
        info.owner_id = tag_value(rest, "hidden");
    } else if (has_tag(rest, "friends")) {
        info.access = "friends";
        info.owner_id = tag_value(rest, "friends");
    } else if (has_tag(rest, "private")) {
        info.access = has_tag(rest, "canRequestInvite") ? "invite_plus" : "invite";
        info.owner_id = tag_value(rest, "private");
    } else if (has_tag(rest, "group")) {
        info.group_id = tag_value(rest, "group");
        const std::string type = tag_value(rest, "groupAccessType");
        info.access = type == "public" ? "group_public" : (type == "plus" ? "group_plus" : "group");
    } else {
        info.access = "public";
    }
    return info;
}

std::string access_label(const std::string& access) {
    if (access == "public") return "Public";
    if (access == "friends_plus") return "Friends+";
    if (access == "friends") return "Friends";
    if (access == "invite_plus") return "Invite+";
    if (access == "invite") return "Invite";
    if (access == "group_public") return "Group Public";
    if (access == "group_plus") return "Group+";
    if (access == "group") return "Group";
    return access;
}

std::string region_label(const std::string& region) {
    if (region.empty() || region == "us") return "US West";
    if (region == "use") return "US East";
    if (region == "eu") return "Europe";
    if (region == "jp") return "Japan";
    return region;
}

std::string location_label(const LocationInfo& info) {
    if (info.valid) return access_label(info.access);
    if (info.special == "private") return "Private";
    if (info.special == "traveling") return "Traveling";
    if (info.special == "offline") return "Offline";
    return std::string();
}

std::optional<std::chrono::system_clock::time_point> parse_log_time(const std::string& line) {
    if (line.size() < 19) return std::nullopt;
    for (std::size_t i : { 0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u, 17u, 18u }) {
        if (!std::isdigit(static_cast<unsigned char>(line[i]))) return std::nullopt;
    }
    if (line[4] != '.' || line[7] != '.' || line[10] != ' ' || line[13] != ':' || line[16] != ':') return std::nullopt;
    std::tm tm{};
    tm.tm_year = std::stoi(line.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(line.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(line.substr(8, 2));
    tm.tm_hour = std::stoi(line.substr(11, 2));
    tm.tm_min = std::stoi(line.substr(14, 2));
    tm.tm_sec = std::stoi(line.substr(17, 2));
    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
}

std::optional<LogEvent> parse_log_line(const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    std::string rest;
    if (!after(line, "[Behaviour] ", rest)) return std::nullopt;
    LogEvent e;
    if (const auto t = parse_log_time(line)) {
        e.time = *t;
        e.has_time = true;
    } else {
        e.time = std::chrono::system_clock::now();
    }
    if (rest.compare(0, 26, "Joining or Creating Room: ") == 0) {
        e.kind = LogKind::Entering;
        e.text = trim(rest.substr(26));
        return e;
    }
    if (rest.compare(0, 15, "Entering Room: ") == 0) {
        e.kind = LogKind::Entering;
        e.text = trim(rest.substr(15));
        return e;
    }
    if (rest.compare(0, 13, "Joining wrld_") == 0) {
        e.kind = LogKind::Joining;
        e.text = trim(rest.substr(8));
        return e;
    }
    if (rest.compare(0, 15, "OnPlayerJoined ") == 0) {
        e.kind = LogKind::PlayerJoined;
        split_name_id(rest.substr(15), e.text, e.user_id);
        return e.text.empty() ? std::nullopt : std::optional<LogEvent>(e);
    }
    if (rest.compare(0, 13, "OnPlayerLeft ") == 0) {
        e.kind = LogKind::PlayerLeft;
        split_name_id(rest.substr(13), e.text, e.user_id);
        return e.text.empty() ? std::nullopt : std::optional<LogEvent>(e);
    }
    if (trim(rest) == "OnLeftRoom") {
        e.kind = LogKind::LeftRoom;
        return e;
    }
    return std::nullopt;
}

void apply_log_event(WorldSnapshot& world, const LogEvent& e) {
    switch (e.kind) {
    case LogKind::Joining:
        world.in_world = true;
        world.location = e.text;
        world.info = parse_location(e.text);
        world.players.clear();
        world.entered = e.time;
        break;
    case LogKind::Entering:
        world.in_world = true;
        world.world_name = e.text;
        if (world.location.empty()) world.entered = e.time;
        break;
    case LogKind::PlayerJoined: {
        auto same = [&](const InstancePlayer& p) { return e.user_id.empty() ? p.name == e.text : p.id == e.user_id; };
        if (std::find_if(world.players.begin(), world.players.end(), same) == world.players.end()) world.players.push_back({ e.text, e.user_id, e.time });
        break;
    }
    case LogKind::PlayerLeft: {
        auto same = [&](const InstancePlayer& p) { return e.user_id.empty() ? p.name == e.text : p.id == e.user_id; };
        world.players.erase(std::remove_if(world.players.begin(), world.players.end(), same), world.players.end());
        break;
    }
    case LogKind::LeftRoom:
        world.in_world = false;
        world.players.clear();
        world.world_name.clear();
        world.location.clear();
        world.info = LocationInfo();
        break;
    }
}

std::vector<fs::path> vrchat_log_folders(const std::vector<fs::path>& extra_osc_folders) {
    std::vector<fs::path> out;
    for (const fs::path& osc : osc::vrchat::osc_folders(extra_osc_folders)) {
        const fs::path folder = osc.filename() == "OSC" ? osc.parent_path() : osc;
        if (std::find(out.begin(), out.end(), folder) == out.end()) out.push_back(folder);
    }
    return out;
}

std::optional<fs::path> newest_vrchat_log(const std::vector<fs::path>& folders) {
    std::optional<fs::path> best;
    fs::file_time_type best_time{};
    for (const fs::path& folder : folders) {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) continue;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.compare(0, 11, "output_log_") != 0 || it->path().extension() != ".txt") continue;
            std::error_code tec;
            const auto t = fs::last_write_time(it->path(), tec);
            if (tec) continue;
            if (!best || t > best_time) {
                best = it->path();
                best_time = t;
            }
        }
    }
    return best;
}

void VrchatLog::set_folders(std::vector<fs::path> folders) {
    m_folders = std::move(folders);
    m_last_scan = std::chrono::steady_clock::time_point();
}

std::vector<LogEvent> VrchatLog::read_new(bool replay) {
    std::vector<LogEvent> out;
    std::ifstream in(m_file, std::ios::binary);
    if (!in) return out;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) return out;
    if (static_cast<std::uintmax_t>(size) < m_offset) {
        m_offset = 0;
        m_partial.clear();
    }
    in.seekg(static_cast<std::streamoff>(m_offset));
    std::string chunk(1 << 16, '\0');
    while (in) {
        in.read(&chunk[0], static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        m_offset += static_cast<std::uintmax_t>(got);
        m_partial.append(chunk.data(), static_cast<std::size_t>(got));
        std::size_t start = 0;
        for (std::size_t nl = m_partial.find('\n'); nl != std::string::npos; nl = m_partial.find('\n', start)) {
            if (auto e = parse_log_line(m_partial.substr(start, nl - start))) {
                apply_log_event(m_world, *e);
                if (!replay) out.push_back(std::move(*e));
            }
            start = nl + 1;
        }
        m_partial.erase(0, start);
    }
    return out;
}

std::vector<LogEvent> VrchatLog::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (m_folders.empty() && !m_started) m_folders = vrchat_log_folders();
    if (m_last_scan == std::chrono::steady_clock::time_point() || now - m_last_scan > std::chrono::seconds(10)) {
        m_last_scan = now;
        const auto newest = newest_vrchat_log(m_folders);
        if (newest && *newest != m_file) {
            const bool replay = !m_started;
            m_file = *newest;
            m_offset = 0;
            m_partial.clear();
            m_world = WorldSnapshot();
            m_world.log_file = m_file.string();
            m_started = true;
            return read_new(replay);
        }
        m_started = true;
    }
    if (m_file.empty()) return {};
    return read_new(false);
}

} // namespace pollen
