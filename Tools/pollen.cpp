#include <pollen/session.hpp>

#include <grove/grove.hpp>
#include <grove/native.hpp>
#include <grove/runtime/runtime.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef POLLEN_VERSION_STRING
#define POLLEN_VERSION_STRING "0.1.0"
#endif

namespace fs = std::filesystem;
namespace vrc = osc::vrchat;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string clean(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

std::string escape_lines(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c != '\r') out += c;
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> fields_of(const std::vector<std::string>& args) {
    if (args.size() == 1) return split(args[0], '\t');
    return args;
}

std::string arg(const std::vector<std::string>& args, std::size_t i) {
    return i < args.size() ? args[i] : std::string();
}

std::string number(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string clock_text(std::chrono::system_clock::time_point t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    int h = tm.tm_hour % 12;
    if (h == 0) h = 12;
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

std::string since_text(std::chrono::system_clock::time_point t) {
    const double s = std::chrono::duration<double>(std::chrono::system_clock::now() - t).count();
    if (s < 60) return "just now";
    if (s < 3600) return std::to_string(static_cast<int>(s / 60)) + " min";
    const int h = static_cast<int>(s / 3600);
    const int m = static_cast<int>(s / 60) % 60;
    return std::to_string(h) + " h " + std::to_string(m) + " min";
}

bool open_in_browser(const std::string& url) {
    if (url.compare(0, 19, "https://vrchat.com/") != 0) return false;
    for (char c : url) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && std::string(":/?&=._-%").find(c) == std::string::npos) return false;
    }
#if defined(_WIN32)
    const std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    const std::string command = "open '" + url + "'";
#else
    const std::string command = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
    return std::system(command.c_str()) == 0;
}

std::string find_ui() {
    std::vector<fs::path> candidates;
    const std::string over = env("POLLEN_UI_DIR");
    if (!over.empty()) candidates.push_back(over);
    const fs::path exe = fs::path(grove::executable_path()).parent_path();
    candidates.push_back(exe / "ui");
    candidates.push_back(exe.parent_path() / "share" / "pollen" / "ui");
#ifdef POLLEN_SOURCE_UI_DIR
    candidates.push_back(POLLEN_SOURCE_UI_DIR);
#endif
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c / "pollen.grove", ec)) return (c / "pollen.grove").string();
    }
    return std::string();
}

class App {
public:
    explicit App(grove::Project& project) : m_doc(project.document) {
        register_natives();
    }

    void install() {
        m_session.start_vrchat(std::string("Pollen/") + POLLEN_VERSION_STRING + " https://github.com/ThatNewCoderJSCpp/pollen");
        m_doc.events().set_interval(33.0, [this] { m_session.tick(); });
    }

private:
    void native(const std::string& name, std::function<std::string(const std::vector<std::string>&)> fn) {
        grove::register_native(m_doc, "pollen." + name, std::move(fn));
    }

    std::string state() {
        const osc::Stats in = m_session.received_stats();
        const osc::Stats out = m_session.sent_stats();
        const vrc::Connection& c = m_session.connection();
        const vrc::ConnectionSettings& cs = c.settings();
        std::vector<std::string> f;
        f.push_back(m_session.connected() ? "1" : "0");
        f.push_back(m_session.hearing_vrchat() ? "1" : "0");
        f.push_back(m_session.connected() ? c.sender().target().to_string() : std::string());
        f.push_back(c.listening() ? osc::Endpoint(cs.listen_host, c.receiver().port()).to_string() : std::string());
        f.push_back(std::to_string(in.messages));
        f.push_back(std::to_string(out.messages));
        f.push_back(std::to_string(in.errors + out.errors));
        f.push_back(in.any ? number(in.seconds_since_last(), 1) : std::string("-1"));
        f.push_back(clean(m_session.avatar_name()));
        f.push_back(clean(m_session.avatar_id()));
        f.push_back(std::to_string(m_session.parameter_revision()));
        f.push_back(std::to_string(m_session.value_revision()));
        f.push_back(clean(m_session.last_error()));
        f.push_back(m_session.settings().status_enabled ? "1" : "0");
        f.push_back(std::to_string(m_session.connection().avatar().live().size()));
        const auto& config = m_session.connection().avatar().config();
        f.push_back(config ? std::to_string(config->parameters.size()) : std::string("0"));
        f.push_back(clean(m_session.oscquery_status()));
        f.push_back(m_session.found_vrchat() ? "1" : "0");
        std::string line;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (i) line += '\t';
            line += f[i];
        }
        return line;
    }

    std::string system() {
        const pollen::SystemSnapshot& s = m_session.system();
        const pollen::NowPlaying np = m_session.now_playing();
        std::vector<std::string> f;
        f.push_back(s.cpu_known ? number(s.cpu_percent, 0) + "%" : std::string("-"));
        f.push_back(s.memory_known ? number(s.memory_percent(), 0) + "%" : std::string("-"));
        f.push_back(s.memory_known ? pollen::format_bytes(s.memory_used) + " of " + pollen::format_bytes(s.memory_total) : std::string());
        f.push_back(pollen::format_duration(s.uptime_seconds));
        f.push_back(clean(np.has_song() ? np.song() : std::string()));
        f.push_back(np.playing ? "playing" : (np.paused ? "paused" : "stopped"));
        f.push_back(clean(np.player));
        f.push_back(clean(np.reason));
        const pollen::SensorSnapshot sn = m_session.sensors();
        const pollen::Units units = pollen::units_of(m_session.settings().modules);
        f.push_back(sn.gpu_percent ? number(*sn.gpu_percent, 0) + "%" : std::string("-"));
        f.push_back(sn.vram_used && sn.vram_total ? pollen::format_bytes(*sn.vram_used, units) + " of " + pollen::format_bytes(*sn.vram_total, units) : std::string());
        f.push_back(clean(sn.gpu_name));
        f.push_back(clean(sn.cpu_name));
        f.push_back(s.memory_known ? pollen::format_bytes(static_cast<double>(s.memory_used), units) + " of " + pollen::format_bytes(static_cast<double>(s.memory_total), units) : std::string());
        std::string warning = m_session.module_warning("cpu");
        const std::string gpu_warning = m_session.module_warning("gpu");
        if (!gpu_warning.empty()) warning += (warning.empty() ? "" : " ") + gpu_warning;
        f.push_back(clean(warning));
        std::string line;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (i) line += '\t';
            line += f[i];
        }
        return line;
    }

    std::string settings_text() {
        const pollen::Settings& s = m_session.settings();
        std::string out;
        out += "host\t" + clean(s.host) + "\n";
        out += "send_port\t" + std::to_string(s.send_port) + "\n";
        out += "receive_port\t" + std::to_string(s.receive_port) + "\n";
        out += "listen_host\t" + clean(s.listen_host) + "\n";
        out += "osc_folder\t" + clean(s.osc_folder) + "\n";
        out += "auto_connect\t" + std::string(s.auto_connect ? "1" : "0") + "\n";
        out += "oscquery\t" + std::string(s.oscquery ? "1" : "0") + "\n";
        out += "status_enabled\t" + std::string(s.status_enabled ? "1" : "0") + "\n";
        out += "status_interval\t" + number(s.status_interval, 1) + "\n";
        out += "status_notify\t" + std::string(s.status_notify ? "1" : "0") + "\n";
        out += "log_parameters\t" + std::string(s.log_parameters ? "1" : "0") + "\n";
        out += "status_template\t" + escape_lines(s.status_template) + "\n";
        out += "settings_path\t" + clean(m_session.settings_path().string()) + "\n";
        return out;
    }

    std::string set_setting(const std::string& key, const std::string& value) {
        pollen::Settings& s = m_session.settings();
        auto as_port = [](const std::string& v, int& out) {
            char* end = nullptr;
            const long p = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str() || *end || p < 0 || p > 65535) return false;
            out = static_cast<int>(p);
            return true;
        };
        const bool on = value == "1" || value == "true";
        if (key == "host") s.host = value;
        else if (key == "listen_host") s.listen_host = value;
        else if (key == "osc_folder") s.osc_folder = value;
        else if (key == "send_port") {
            if (!as_port(value, s.send_port)) return "error\tports go from 1 to 65535";
        } else if (key == "receive_port") {
            if (!as_port(value, s.receive_port)) return "error\tports go from 0 (don't listen) to 65535";
        } else if (key == "auto_connect") s.auto_connect = on;
        else if (key == "oscquery") s.oscquery = on;
        else if (key == "status_enabled") m_session.set_status_enabled(on);
        else if (key == "status_notify") s.status_notify = on;
        else if (key == "log_parameters") s.log_parameters = on;
        else if (key == "status_interval") {
            const double v = std::strtod(value.c_str(), nullptr);
            if (!(v >= 2.0) || !std::isfinite(v)) return "error\tVRChat ignores chatbox messages sent faster than every 2 seconds";
            s.status_interval = v;
        } else if (key == "status_template") m_session.set_status_template(value);
        else return "error\tno setting called " + key;
        std::string why;
        if (!m_session.save_settings(&why)) return "error\t" + clean(why);
        return "ok";
    }

    std::string account_line() {
        const pollen::AccountSnapshot a = m_session.vrchat().account();
        const char* state = "loggedout";
        switch (a.state) {
        case pollen::LoginState::LoggedOut: state = "loggedout"; break;
        case pollen::LoginState::Working: state = "working"; break;
        case pollen::LoginState::NeedsCode: state = "code"; break;
        case pollen::LoginState::WaitingForApproval: state = "approve"; break;
        case pollen::LoginState::LoggedIn: state = "loggedin"; break;
        }
        std::vector<std::string> f = { state, clean(a.message), clean(a.code_kind), clean(a.device_code), clean(a.device_url), clean(a.me.name), clean(a.me.status),
            clean(pollen::status_label(a.me.status)), clean(a.me.status_text), a.pipeline ? "1" : "0", clean(a.action_note),
            std::to_string(a.friends_revision), std::to_string(a.collection_revision), std::to_string(m_session.activity_seq()) };
        std::string line;
        for (std::size_t i = 0; i < f.size(); ++i) line += (i ? "\t" : "") + f[i];
        return line;
    }

    std::string world_text() {
        const pollen::WorldSnapshot w = m_session.world();
        std::string out;
        auto put = [&](const std::string& k, const std::string& v) { out += k + "\t" + clean(v) + "\n"; };
        put("found", m_session.found_vrchat_log() ? "1" : "0");
        put("file", m_session.vrchat_log_file());
        put("in_world", w.in_world ? "1" : "0");
        put("world", m_session.world_name());
        put("type", w.info.valid ? pollen::access_label(w.info.access) : std::string());
        put("region", w.info.valid ? pollen::region_label(w.info.region) : std::string());
        put("instance", w.info.instance);
        put("players", std::to_string(w.players.size()));
        put("since", w.in_world ? clock_text(w.entered) + " (" + since_text(w.entered) + ")" : std::string());
        put("location", w.location);
        return out;
    }

    std::string friends_text(const std::string& filter, const std::string& search) {
        std::vector<pollen::VrcUser> list = m_session.vrchat().friends();
        const pollen::WorldSnapshot w = m_session.world();
        std::map<std::string, bool> here;
        for (const pollen::InstancePlayer& p : w.players) {
            if (!p.id.empty()) here[p.id] = true;
        }
        auto rank = [&](const pollen::VrcUser& u) {
            if (here.count(u.id)) return 0;
            if (u.state == "online") return 1;
            if (u.state == "active") return 2;
            return 3;
        };
        std::sort(list.begin(), list.end(), [&](const pollen::VrcUser& a, const pollen::VrcUser& b) {
            const int ra = rank(a), rb = rank(b);
            if (ra != rb) return ra < rb;
            std::string x = a.name, y = b.name;
            for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return x < y;
        });
        std::string needle = search;
        for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string out;
        for (const pollen::VrcUser& u : list) {
            const int r = rank(u);
            if (filter == "online" && r > 1) continue;
            if (filter == "web" && r != 2) continue;
            if (filter == "offline" && r != 3) continue;
            if (filter == "here" && r != 0) continue;
            if (!needle.empty()) {
                std::string n = u.name;
                for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (n.find(needle) == std::string::npos) continue;
            }
            std::string where;
            std::string extra;
            if (r == 0) where = "In your instance";
            else if (r == 2) where = "On the VRChat website";
            else if (r == 3) where = u.last_login.empty() ? std::string("Offline") : "Offline, last seen " + u.last_login.substr(0, 10);
            else {
                where = m_session.vrchat().describe_location(u.location);
                const pollen::LocationInfo info = pollen::parse_location(u.location);
                if (info.valid) m_session.vrchat().world_name(info.world_id);
                where[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(where[0])));
                if (info.special == "private" && !u.last_public_location.empty()) extra = "last seen in " + m_session.vrchat().describe_location(u.last_public_location);
            }
            if (r == 1 && u.location_since_known) where += " for " + since_text(u.location_since);
            const std::string status = r == 3 ? std::string("offline") : u.status;
            out += clean(u.id) + "\t" + clean(u.name) + "\t" + std::to_string(r) + "\t" + clean(status) + "\t" + clean(pollen::status_label(status)) + "\t" + clean(u.status_text) + "\t" + clean(where) + "\t" + clean(extra) + "\n";
        }
        return out;
    }

    void register_vrchat_natives() {
        native("vrc_account", [this](const std::vector<std::string>&) { return account_line(); });
        native("vrc_login", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string password = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) password += "\t" + f[i];
            if (arg(f, 0).empty() || password.empty()) return std::string("error\ttype your username and password");
            m_session.vrchat().login(arg(f, 0), password);
            return std::string("ok");
        });
        native("vrc_verify", [this](const std::vector<std::string>& args) {
            m_session.vrchat().verify(arg(args, 0));
            return std::string("ok");
        });
        native("vrc_login_code", [this](const std::vector<std::string>&) {
            m_session.vrchat().login_with_code();
            return std::string("ok");
        });
        native("vrc_cancel", [this](const std::vector<std::string>&) {
            m_session.vrchat().cancel_login();
            return std::string("ok");
        });
        native("vrc_logout", [this](const std::vector<std::string>&) {
            m_session.vrchat().logout();
            return std::string("ok");
        });
        native("vrc_refresh", [this](const std::vector<std::string>&) {
            m_session.vrchat().refresh_friends();
            return std::string("ok");
        });
        native("vrc_open_login", [this](const std::vector<std::string>&) {
            const std::string url = m_session.vrchat().account().device_url;
            if (!open_in_browser(url)) return std::string("error\tcouldn't open a browser; copy the link instead");
            return std::string("ok");
        });
        native("vrc_copy_login", [this](const std::vector<std::string>&) {
            const std::string url = m_session.vrchat().account().device_url;
            if (url.empty() || !grove::set_clipboard_text(m_doc, url)) return std::string("error\tcouldn't copy the link");
            return std::string("ok");
        });
        native("vrc_world", [this](const std::vector<std::string>&) { return world_text(); });
        native("vrc_players", [this](const std::vector<std::string>&) {
            std::string out;
            for (const pollen::InstancePlayer& p : m_session.world().players) {
                out += clean(p.name) + "\t" + clean(p.id) + "\t" + (m_session.vrchat().is_friend(p.id) ? "1" : "0") + "\t" + clock_text(p.joined) + "\n";
            }
            return out;
        });
        native("vrc_friends", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            return friends_text(arg(f, 0), arg(f, 1));
        });
        native("vrc_activity", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            const std::uint64_t since = std::strtoull(arg(f, 0).c_str(), nullptr, 10);
            const std::string flags = arg(f, 1) + "11111";
            const bool friends_only = flags[0] == '1';
            std::string out;
            for (const pollen::Activity& a : m_session.activity_since(since)) {
                const std::string& k = a.kind;
                bool show = true;
                if ((k == "join" || k == "leave") && (flags[1] != '1' || (friends_only && !a.friend_related))) show = false;
                if ((k == "online" || k == "offline" || k == "active") && flags[2] != '1') show = false;
                if (k == "location" && flags[3] != '1') show = false;
                if ((k == "status" || k == "you") && flags[4] != '1') show = false;
                out += std::to_string(a.seq) + "\t" + clock_text(a.time) + "\t" + k + "\t" + (show ? "1" : "0") + "\t" + clean(a.text) + "\n";
            }
            return out;
        });
        native("vrc_activity_clear", [this](const std::vector<std::string>&) {
            m_session.clear_activity();
            return std::string("ok");
        });
        native("vrc_load", [this](const std::vector<std::string>& args) {
            m_session.vrchat().load_collection(arg(args, 0));
            return std::string("ok");
        });
        native("vrc_collection", [this](const std::vector<std::string>& args) {
            const auto fields = fields_of(args);
            const std::string kind = arg(fields, 0);
            std::string needle = arg(fields, 1);
            const bool show_archived = arg(fields, 2) == "1";
            for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const std::string type = kind == "avatars" ? "avatar" : (kind == "worlds" ? "world" : "friend");
            std::map<std::string, std::string> names;
            for (const pollen::FavoriteGroup& g : m_session.vrchat().favorite_groups(type)) names[g.name] = g.display_name.empty() ? g.name : g.display_name;
            std::string out;
            for (const pollen::CollectionRow& r : m_session.vrchat().collection(kind)) {
                if (r.archived && !show_archived) continue;
                if (!needle.empty()) {
                    std::string hay = r.name + " " + r.detail;
                    for (char& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (hay.find(needle) == std::string::npos) continue;
                }
                const auto n = names.find(r.group);
                const std::string group = n == names.end() ? r.group : n->second;
                out += clean(r.id) + "\t" + clean(r.target_id) + "\t" + clean(r.name) + "\t" + clean(r.detail) + "\t" + clean(group) + "\t" + clean(r.group) + "\t" + (r.archived ? "1" : "0") + "\t" + (r.custom ? "1" : "0") + "\n";
            }
            return out;
        });
        native("vrc_collection_status", [this](const std::vector<std::string>& args) { return m_session.vrchat().collection_status(arg(args, 0)); });
        native("vrc_groups", [this](const std::vector<std::string>& args) {
            std::string out;
            for (const pollen::FavoriteGroup& g : m_session.vrchat().favorite_groups(arg(args, 0))) out += (out.empty() ? "" : "|") + g.name + "=" + (g.display_name.empty() ? g.name : g.display_name);
            return clean(out);
        });
        native("vrc_act", [this](const std::vector<std::string>& args) {
            std::vector<std::string> f = fields_of(args);
            if (arg(f, 0) == "invite") f = { "invite", arg(f, 1), m_session.world().location };
            m_session.vrchat().act(f);
            return std::string("ok");
        });
    }

    void register_natives() {
        register_vrchat_natives();
        native("version", [](const std::vector<std::string>&) { return std::string(POLLEN_VERSION_STRING); });
        native("state", [this](const std::vector<std::string>&) { return state(); });
        native("system", [this](const std::vector<std::string>&) { return system(); });
        native("connect", [this](const std::vector<std::string>&) {
            std::string why;
            if (!m_session.connect(&why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("disconnect", [this](const std::vector<std::string>&) {
            m_session.disconnect();
            return std::string("ok");
        });
        native("settings", [this](const std::vector<std::string>&) { return settings_text(); });
        native("set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string value = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) value += "\t" + f[i];
            return set_setting(arg(f, 0), value);
        });
        native("params", [this](const std::vector<std::string>& args) {
            const std::string filter = arg(args, 0);
            std::string out;
            for (const pollen::ParamRow& r : m_session.parameters()) {
                if (!filter.empty()) {
                    std::string a = r.name, b = filter;
                    for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (a.find(b) == std::string::npos) continue;
                }
                out += clean(r.name) + "\t" + vrc::to_string(r.type) + "\t" + clean(r.value) + "\t" + (r.writable ? "1" : "0") + "\t" + (r.known ? "1" : "0") + "\t" + (r.live ? "1" : "0") + "\n";
            }
            return out;
        });
        native("values", [this](const std::vector<std::string>&) {
            std::string out;
            for (const auto& kv : m_session.connection().avatar().live()) {
                const osc::Value& v = kv.second.value;
                std::string shown;
                if (v.is_real()) shown = number(v.to_double(), 3);
                else shown = v.to_string();
                out += clean(kv.first) + "\t" + clean(shown) + "\n";
            }
            return out;
        });
        native("set_param", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string why;
            if (!m_session.set_parameter(arg(f, 0), arg(f, 1), arg(f, 2), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("chatbox", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string text = arg(f, 0);
            for (char& c : text) {
                if (c == '\r') c = ' ';
            }
            if (!m_session.chatbox(text, arg(f, 1) == "1")) return std::string("error\tnot connected");
            return std::string("ok");
        });
        native("chat_preview", [](const std::vector<std::string>& args) {
            std::string text = arg(args, 0);
            for (std::size_t i = 1; i < args.size(); ++i) text += "\t" + args[i];
            return vrc::chatbox_text(text);
        });
        native("chat_drop_last", [](const std::vector<std::string>& args) {
            std::string text = arg(args, 0);
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u) text.pop_back();
            if (!text.empty()) text.pop_back();
            return text;
        });
        native("chat_length", [](const std::vector<std::string>& args) {
            return std::to_string(vrc::utf8_length(arg(args, 0)));
        });
        native("typing", [this](const std::vector<std::string>& args) {
            m_session.typing(arg(args, 0) == "1");
            return std::string("ok");
        });
        native("status_preview", [this](const std::vector<std::string>&) { return m_session.status_preview(); });
        native("status_send", [this](const std::vector<std::string>&) {
            if (!m_session.send_status_now()) return std::string("error\tnothing to send (is it connected, and is the template empty?)");
            return std::string("ok");
        });
        native("placeholders", [this](const std::vector<std::string>&) {
            std::string out;
            const pollen::ModuleOptions& opts = m_session.settings().modules;
            for (const pollen::ModuleDef& m : pollen::module_defs()) {
                for (const pollen::PlaceholderDef& p : m.placeholders) {
                    if (pollen::placeholder_enabled(opts, m, p)) out += "{" + p.name + "}\t" + clean(m.title + ": " + p.description) + "\n";
                }
            }
            return out;
        });
        native("modules", [this](const std::vector<std::string>&) {
            std::string out;
            const pollen::ModuleOptions& opts = m_session.settings().modules;
            for (const pollen::ModuleDef& m : pollen::module_defs()) {
                std::size_t on = 0;
                for (const pollen::PlaceholderDef& p : m.placeholders) on += pollen::placeholder_enabled(opts, m, p) ? 1 : 0;
                std::string sample = m_session.module_preview(m.id, m_session.module_template(m.id));
                if (sample.empty()) sample = "(nothing to show right now)";
                out += m.id + "\t" + m.icon + "\t" + clean(m.title) + "\t" + clean(m.description) + "\t" + escape_lines(sample) + "\t" + std::to_string(on) + "\t" + std::to_string(m.placeholders.size()) + "\n";
            }
            return out;
        });
        native("module_options", [this](const std::vector<std::string>& args) {
            const pollen::ModuleDef* m = pollen::find_module(arg(args, 0));
            if (!m) return std::string();
            std::string out;
            for (const pollen::OptionDef& o : m->options) {
                if (!pollen::option_visible(m_session.settings().modules, *m, o)) continue;
                const char* kind = "bool";
                switch (o.kind) {
                case pollen::OptionKind::Bool: kind = "bool"; break;
                case pollen::OptionKind::Choice: kind = "choice"; break;
                case pollen::OptionKind::Text: kind = "text"; break;
                case pollen::OptionKind::Integer: kind = "integer"; break;
                case pollen::OptionKind::Float: kind = "float"; break;
                }
                std::string choices;
                for (const auto& c : o.choices) choices += (choices.empty() ? "" : "|") + c.first + "=" + c.second;
                out += std::string(kind) + "\t" + o.id + "\t" + clean(o.label) + "\t" + clean(pollen::option_value(m_session.settings().modules, m->id, o.id)) + "\t" + clean(o.hint) + "\t" + clean(choices) + "\t" + number(o.min, 6) + "\t" + number(o.max, 6) + "\n";
            }
            return out;
        });
        native("module_set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string why;
            if (!m_session.set_module_option(arg(f, 0), arg(f, 1), arg(f, 2), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("module_placeholders", [this](const std::vector<std::string>& args) {
            const pollen::ModuleDef* m = pollen::find_module(arg(args, 0));
            if (!m) return std::string();
            std::string out;
            for (const pollen::PlaceholderDef& p : m->placeholders) {
                const bool on = pollen::placeholder_enabled(m_session.settings().modules, *m, p);
                out += p.name + "\t" + clean(p.description) + "\t" + (on ? "1" : "0") + "\t" + clean(pollen::requirement_text(*m, p)) + "\n";
            }
            return out;
        });
        native("placeholder_options", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            const pollen::ModuleDef* m = pollen::find_module(arg(f, 0));
            const pollen::PlaceholderDef* p = m ? pollen::find_placeholder(*m, arg(f, 1)) : nullptr;
            if (!p) return std::string();
            const pollen::ModuleOptions& opts = m_session.settings().modules;
            std::string out;
            for (const pollen::OptionDef& o : pollen::visible_placeholder_settings(opts, *m, *p)) {
                const std::string kind = o.kind == pollen::OptionKind::Bool ? "bool" : "choice";
                std::string value = pollen::option_value(opts, m->id, pollen::placeholder_key(p->name, o.id));
                if (value.empty()) value = o.fallback;
                std::string choices;
                for (const auto& c : o.choices) {
                    std::string label = c.second;
                    if (c.first == "default") label = "Default (" + pollen::choice_label(o, pollen::placeholder_default(opts, *m, *p, o.id)) + ")";
                    choices += (choices.empty() ? "" : "|") + c.first + "=" + label;
                }
                out += kind + "\t" + o.id + "\t" + clean(o.label) + "\t" + clean(value) + "\t" + clean(o.hint) + "\t" + clean(choices) + "\n";
            }
            return out;
        });
        native("placeholder_set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string why;
            if (!m_session.set_module_option(arg(f, 0), pollen::placeholder_key(arg(f, 1), arg(f, 2)), arg(f, 3), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("module_title", [](const std::vector<std::string>& args) {
            const pollen::ModuleDef* m = pollen::find_module(arg(args, 0));
            return m ? m->icon + "\t" + m->title + "\t" + m->description : std::string();
        });
        native("module_template", [this](const std::vector<std::string>& args) {
            return escape_lines(m_session.module_template(arg(args, 0)));
        });
        native("module_template_set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string text = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) text += "\t" + f[i];
            std::string why;
            if (!m_session.set_module_template(arg(f, 0), text, &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("module_preview", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string text = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) text += "\t" + f[i];
            return m_session.module_preview(arg(f, 0), text);
        });
        native("module_status", [this](const std::vector<std::string>& args) {
            return m_session.module_status(arg(args, 0));
        });
        native("module_warning", [this](const std::vector<std::string>& args) {
            return m_session.module_warning(arg(args, 0));
        });
        native("status_append", [this](const std::vector<std::string>& args) {
            std::string text = arg(args, 0);
            for (std::size_t i = 1; i < args.size(); ++i) text += "\t" + args[i];
            std::string tpl = m_session.settings().status_template;
            while (!tpl.empty() && tpl.back() == '\n') tpl.pop_back();
            tpl += tpl.empty() ? text : "\n" + text;
            m_session.set_status_template(tpl);
            std::string why;
            if (!m_session.save_settings(&why)) return "error\t" + clean(why);
            return escape_lines(tpl);
        });
        native("log", [this](const std::vector<std::string>& args) {
            const std::uint64_t since = std::strtoull(arg(args, 0).c_str(), nullptr, 10);
            std::string out;
            for (const pollen::LogLine& l : m_session.log_since(since, 300)) {
                out += std::to_string(l.seq) + "\t" + std::string(1, l.kind) + "\t" + number(l.seconds, 2) + "\t" + clean(l.text) + "\n";
            }
            return out;
        });
        native("log_clear", [this](const std::vector<std::string>&) {
            m_session.clear_log();
            return std::string("ok");
        });
        native("send", [this](const std::vector<std::string>& args) {
            std::string why;
            if (!m_session.send_text(arg(args, 0), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("folders", [this](const std::vector<std::string>&) {
            std::string out;
            for (const fs::path& p : m_session.osc_folders()) out += clean(p.string()) + "\n";
            return out;
        });
        native("reload_avatar", [this](const std::vector<std::string>&) {
            return std::string(m_session.reload_avatar() ? "ok" : "error\tno OSC file for this avatar yet; turn OSC on in VRChat's action menu and switch avatars once");
        });
    }

    grove::Document& m_doc;
    pollen::Session m_session;
};

int bundle_ui(const std::string& output, bool portable) {
    const std::string ui = find_ui();
    if (ui.empty()) {
        std::cerr << "pollen: can't find the UI (pollen.grove) to bundle; set POLLEN_UI_DIR to the folder that holds it\n";
        return 1;
    }
    grove::AppBuildOptions options;
    options.output = output;
    options.stub = grove::executable_path();
    options.tools = grove::default_script_tools(ui);
    options.portable = portable;
    grove::Diagnostics diagnostics;
    const grove::AppBuildResult result = grove::build_app(ui, options, diagnostics, &std::cout);
    if (!result.ok) {
        diagnostics.print(std::cerr);
        std::cerr << "pollen: couldn't bundle the UI into " << output << "\n";
        return 1;
    }
    std::cout << "pollen: wrote " << result.output << " (" << result.files << " files, " << result.modules << " compiled scripts)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version") {
            std::cout << "pollen " << POLLEN_VERSION_STRING << "\n";
            return 0;
        }
        if (a == "--help") {
            std::cout << "usage: pollen [--check] [--bundle OUTPUT [--portable]]\n\n"
                         "  --check          load the window and its script without opening it, then exit\n"
                         "  --bundle OUTPUT  write a copy of pollen with the window and its compiled script built in\n"
                         "  --portable       with --bundle, link the script with the static C++ runtime (when the compiler has one)\n\n"
                         "Set POLLEN_UI_DIR to use a different UI folder, POLLEN_CONFIG_DIR to keep settings somewhere else,\n"
                         "and VRCHAT_OSC_DIR if Pollen can't find VRChat's OSC folder by itself.\n";
            return 0;
        }
        if (a == "--bundle") {
            if (i + 1 >= argc) {
                std::cerr << "pollen: --bundle needs an output path\n";
                return 2;
            }
            bool portable = false;
            for (int j = 1; j < argc; ++j) portable = portable || std::string(argv[j]) == "--portable";
            return bundle_ui(argv[i + 1], portable);
        }
        if (a == "--check") check = true;
    }

    grove::LoadedApp loaded;
    grove::Project from_files;
    grove::Project* project = nullptr;
    grove::runtime::RunOptions options;
    std::optional<grove::Bundle> bundle;
    if (env("POLLEN_UI_DIR").empty()) bundle = grove::read_embedded_bundle(grove::executable_path());
    if (bundle) {
        std::string error;
        if (!grove::load_app(*bundle, loaded, error)) {
            std::cerr << "pollen: " << error << "\n";
            return 1;
        }
        project = &loaded.project;
        options.window_id = loaded.window_id;
        options.prebuilt = &loaded.scripts;
        if (check) {
            std::cout << "pollen " << POLLEN_VERSION_STRING << ": built-in window ok, " << loaded.scripts.runnable_count() << " compiled script(s)\n";
            return loaded.scripts.ok() ? 0 : 1;
        }
    } else {
        const std::string ui = find_ui();
        if (ui.empty()) {
            std::cerr << "pollen: can't find the UI (pollen.grove); set POLLEN_UI_DIR to the folder that holds it\n";
            return 1;
        }
        from_files = grove::load_project(ui);
        if (!from_files.diagnostics.ok()) {
            from_files.diagnostics.print(std::cerr);
            return 1;
        }
        if (check) {
            std::cout << "pollen " << POLLEN_VERSION_STRING << ": window ok (" << ui << ")\n";
            return 0;
        }
        project = &from_files;
        options.tools = grove::default_script_tools(ui);
        options.tools.cache_dir = (pollen::Settings::config_dir() / "script-cache").string();
    }
    auto app = std::make_unique<App>(*project);
    app->install();
    return grove::runtime::run(*project, options);
}
