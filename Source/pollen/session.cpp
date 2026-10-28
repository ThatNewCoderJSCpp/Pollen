#include "pollen/session.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace pollen {

namespace fs = std::filesystem;
namespace vrc = osc::vrchat;

namespace {

constexpr std::size_t log_limit = 2000;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string show_value(const osc::Value& v) {
    if (v.is_real()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", v.to_double());
        return buf;
    }
    return v.to_string();
}

bool parse_token(const std::string& token, bool quoted, osc::Value& out, std::string* error) {
    if (quoted) {
        out = osc::Value(token);
        return true;
    }
    const std::size_t colon = token.find(':');
    if (colon == 1) {
        const char t = token[0];
        const std::string rest = token.substr(2);
        char* end = nullptr;
        switch (t) {
        case 's': out = osc::Value(rest); return true;
        case 'S': out = osc::Value(osc::Symbol(rest)); return true;
        case 'i': {
            const long long v = std::strtoll(rest.c_str(), &end, 0);
            if (*end) break;
            out = osc::Value(static_cast<std::int32_t>(v));
            return true;
        }
        case 'h': {
            const long long v = std::strtoll(rest.c_str(), &end, 0);
            if (*end) break;
            out = osc::Value::int64(v);
            return true;
        }
        case 'f': {
            const double v = std::strtod(rest.c_str(), &end);
            if (*end) break;
            out = osc::Value(static_cast<float>(v));
            return true;
        }
        case 'd': {
            const double v = std::strtod(rest.c_str(), &end);
            if (*end) break;
            out = osc::Value(v);
            return true;
        }
        case 'c':
            if (rest.size() != 1) break;
            out = osc::Value(rest[0]);
            return true;
        case 'b': {
            const osc::Value b = osc::Value(rest == "1" || lower(rest) == "true");
            out = b;
            return true;
        }
        default: break;
        }
        if (error) *error = "can't read \"" + token + "\"";
        return false;
    }
    const std::string l = lower(token);
    if (l == "true" || l == "t") {
        out = osc::Value(true);
        return true;
    }
    if (l == "false" || l == "f") {
        out = osc::Value(false);
        return true;
    }
    if (l == "nil" || l == "null") {
        out = osc::Value(osc::Nil{});
        return true;
    }
    if (l == "impulse" || l == "bang") {
        out = osc::Value(osc::Impulse{});
        return true;
    }
    char* end = nullptr;
    const long long i = std::strtoll(token.c_str(), &end, 10);
    if (!token.empty() && *end == '\0') {
        if (i >= INT32_MIN && i <= INT32_MAX) out = osc::Value(static_cast<std::int32_t>(i));
        else out = osc::Value::int64(i);
        return true;
    }
    const double d = std::strtod(token.c_str(), &end);
    if (!token.empty() && *end == '\0') {
        out = osc::Value(static_cast<float>(d));
        return true;
    }
    out = osc::Value(token);
    return true;
}

} // namespace

std::optional<osc::Message> parse_message_line(const std::string& line, std::string* error) {
    std::vector<std::pair<std::string, bool>> tokens;
    std::string cur;
    bool in_quote = false, have = false, was_quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quote) {
            if (c == '\\' && i + 1 < line.size()) {
                const char n = line[++i];
                cur += n == 'n' ? '\n' : (n == 't' ? '\t' : n);
            } else if (c == '"') {
                in_quote = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            in_quote = true;
            have = true;
            was_quoted = true;
        } else if (c == ' ' || c == '\t') {
            if (have) tokens.emplace_back(cur, was_quoted);
            cur.clear();
            have = false;
            was_quoted = false;
        } else {
            cur += c;
            have = true;
        }
    }
    if (in_quote) {
        if (error) *error = "a quote is never closed";
        return std::nullopt;
    }
    if (have) tokens.emplace_back(cur, was_quoted);
    if (tokens.empty()) {
        if (error) *error = "type an address like /avatar/parameters/Name, then the values";
        return std::nullopt;
    }
    if (!osc::is_valid_address(tokens[0].first)) {
        if (error) *error = "\"" + tokens[0].first + "\" isn't an OSC address (they start with / and have no spaces)";
        return std::nullopt;
    }
    osc::Message m(tokens[0].first);
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        osc::Value v;
        if (!parse_token(tokens[i].first, tokens[i].second, v, error)) return std::nullopt;
        m.add(std::move(v));
    }
    return m;
}

Session::Session(fs::path settings_path) : m_settings_path(std::move(settings_path)) {
    m_started = std::chrono::steady_clock::now();
    m_last_stats = m_started;
    std::error_code ec;
    if (fs::exists(m_settings_path, ec)) m_settings.load(m_settings_path, &m_load_error);
    m_status.set_template(m_settings.status_template);
    m_connection.on_receive = [this](const osc::Received& r) {
        r.packet.for_each_message([&](const osc::Message& m, osc::TimeTag) {
            if (!m_settings.log_parameters && vrc::is_parameter_address(m.address())) return;
            log('i', m.to_string());
        });
    };
    m_connection.on_send = [this](const osc::Message& m) {
        if (!m_settings.log_parameters && vrc::is_parameter_address(m.address())) return;
        log('o', m.to_string());
    };
    m_connection.avatar().on_avatar_change = [this](const std::string& id) {
        log('s', "avatar changed to " + (id.empty() ? std::string("(none)") : id));
        if (m_link.running()) m_link.request_avatar();
    };
    m_media.start(2.0);
    m_sensors.start(1.0);
    m_weather.configure(weather_config(m_settings.modules));
    if (m_settings.auto_connect) connect();
}

Session::~Session() {
    if (m_typing) typing(false);
    m_link.stop();
    m_weather.stop();
    m_sensors.stop();
    m_media.stop();
    m_api.stop();
    m_connection.stop();
}

void Session::start_vrchat(const std::string& user_agent) {
    std::vector<fs::path> extra;
    if (!m_settings.osc_folder.empty()) extra.emplace_back(m_settings.osc_folder);
    m_vrclog.set_folders(vrchat_log_folders(extra));
    m_api.start(m_settings_path.parent_path(), user_agent);
}

std::string Session::world_name() {
    const WorldSnapshot& w = m_vrclog.world();
    if (!w.world_name.empty()) return w.world_name;
    if (w.info.valid) return m_api.world_name(w.info.world_id);
    return std::string();
}

void Session::add_activity(const std::string& kind, const std::string& text, const std::string& user_id, bool friend_related, std::chrono::system_clock::time_point time) {
    Activity a;
    a.seq = ++m_activity_seq;
    a.time = time;
    a.kind = kind;
    a.text = text;
    a.user_id = user_id;
    a.friend_related = friend_related;
    m_activity.push_back(std::move(a));
    while (m_activity.size() > 1000) m_activity.pop_front();
}

std::vector<Activity> Session::activity_since(std::uint64_t seq, std::size_t max) const {
    std::vector<Activity> out;
    for (const Activity& a : m_activity) {
        if (a.seq > seq) out.push_back(a);
    }
    if (out.size() > max) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(max));
    return out;
}

void Session::clear_activity() {
    m_activity.clear();
}

void Session::poll_vrchat() {
    for (const LogEvent& e : m_vrclog.poll()) {
        const WorldSnapshot& w = m_vrclog.world();
        switch (e.kind) {
        case LogKind::Entering: {
            const std::string type = w.info.valid ? " (" + access_label(w.info.access) + ", " + region_label(w.info.region) + ")" : std::string();
            add_activity("world", "You joined " + e.text + type, std::string(), false, e.time);
            break;
        }
        case LogKind::PlayerJoined: {
            const bool friend_related = !e.user_id.empty() && m_api.is_friend(e.user_id);
            m_last_joined = e.text;
            add_activity("join", (friend_related ? "Your friend " : std::string()) + e.text + " joined your instance", e.user_id, friend_related, e.time);
            break;
        }
        case LogKind::PlayerLeft: {
            const bool friend_related = !e.user_id.empty() && m_api.is_friend(e.user_id);
            m_last_left = e.text;
            add_activity("leave", (friend_related ? "Your friend " : std::string()) + e.text + " left your instance", e.user_id, friend_related, e.time);
            break;
        }
        case LogKind::LeftRoom:
            add_activity("world", "You left the world", std::string(), false, e.time);
            break;
        case LogKind::Joining:
            break;
        }
    }
    for (const SocialEvent& e : m_api.take_events()) {
        const bool friend_related = e.kind != "you";
        if (friend_related && e.kind != "request" && e.kind != "invite") m_friend_news = e.text;
        add_activity(e.kind, e.text, e.user_id, friend_related, e.time);
    }
}

bool Session::save_settings(std::string* error) {
    std::string why;
    const bool ok = m_settings.save(m_settings_path, &why);
    if (!ok) {
        m_error = why;
        if (error) *error = why;
    }
    return ok;
}

bool Session::connect(std::string* error) {
    std::string why;
    const vrc::ConnectionSettings c = m_settings.connection();
    m_link.stop();
    if (!m_connection.start(c, &why)) {
        bool recovered = false;
        if (m_connection.sending() && m_settings.oscquery && c.receive_port != 0) {
            std::string second;
            if (m_connection.receiver().listen(0, c.listen_host, &second)) {
                log('e', why);
                log('s', "listening on port " + std::to_string(m_connection.receiver().port()) + " instead; VRChat finds it through OSCQuery");
                recovered = true;
            }
        }
        if (!recovered) {
            m_error = why;
            log('e', why);
            if (error) *error = why;
            if (m_connection.sending() && m_settings.oscquery) start_link();
            return false;
        }
    }
    m_error.clear();
    m_status.reset();
    const bool listening = m_connection.listening();
    log('s', "sending to " + osc::Endpoint(c.host, c.send_port).to_string() + (listening ? ", listening on " + osc::Endpoint(c.listen_host, m_connection.receiver().port()).to_string() : std::string(", not listening")));
    if (m_settings.oscquery) start_link();
    return true;
}

void Session::start_link() {
    const vrc::ConnectionSettings c = m_settings.connection();
    const std::uint16_t port = m_connection.listening() ? m_connection.receiver().port() : 0;
    m_vrchat_revision = 0;
    m_auto_target = c.host == "127.0.0.1" || c.host == "localhost";
    m_link_error.clear();
    std::string why;
    if (!m_link.start("Pollen", c.listen_host, port, &why)) {
        m_link_error = why;
        log('e', "OSCQuery: " + why);
        return;
    }
    if (!why.empty()) {
        m_link_error = why;
        log('e', "OSCQuery: " + why + ", so VRChat won't find Pollen by itself");
    } else {
        log('s', "OSCQuery: advertising as " + m_link.instance() + " on port " + std::to_string(m_link.http_port()));
    }
}

void Session::apply_avatar(vrc::AvatarSnapshot snapshot) {
    vrc::AvatarState& avatar = m_connection.avatar();
    if (!snapshot.avatar_id.empty() && snapshot.avatar_id != avatar.avatar_id()) {
        avatar.set_avatar_id(snapshot.avatar_id);
        m_connection.reload_avatar_config();
    }
    if (!avatar.config() || avatar.config()->parameters.empty()) {
        snapshot.config.id = avatar.avatar_id();
        avatar.set_config(snapshot.config);
    }
    for (const auto& kv : snapshot.values) avatar.remember_sent(kv.first, kv.second);
    log('s', "OSCQuery: read " + std::to_string(snapshot.config.parameters.size()) + " parameters from VRChat");
}

std::string Session::oscquery_status() const {
    if (!m_settings.oscquery) return "off";
    if (!m_link.running()) return m_link_error.empty() ? std::string("not running") : m_link_error;
    if (const auto v = m_link.vrchat()) return "found " + v->name + ", which listens on " + osc::Endpoint(v->osc_ip, v->osc_port).to_string();
    if (!m_link.advertising()) return "mDNS isn't available (" + m_link.mdns_error() + "), so VRChat can't find Pollen by itself";
    return "looking for VRChat as " + m_link.instance();
}

void Session::disconnect() {
    m_link.stop();
    if (!m_connection.sending()) return;
    if (m_typing) typing(false);
    m_connection.stop();
    log('s', "disconnected");
}

void Session::tick() {
    m_connection.poll();
    if (m_link.running()) {
        const std::uint64_t rev = m_link.vrchat_revision();
        if (rev != m_vrchat_revision) {
            m_vrchat_revision = rev;
            if (const auto v = m_link.vrchat()) {
                const osc::Endpoint there(v->osc_ip, v->osc_port);
                log('s', "OSCQuery: found " + v->name + ", which listens on " + there.to_string());
                if (m_auto_target && v->osc_port != 0 && m_connection.sender().target() != there) {
                    m_connection.sender().set_target(there);
                    log('s', "sending to " + there.to_string() + " now");
                }
            }
        }
        if (std::optional<vrc::AvatarSnapshot> snap = m_link.take_avatar()) apply_avatar(std::move(*snap));
    }
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - m_last_stats).count() >= 1.0) {
        m_stats.update();
        m_last_stats = now;
    }
    if (std::chrono::duration<double>(now - m_last_vrchat_poll).count() >= 1.0) {
        m_last_vrchat_poll = now;
        poll_vrchat();
    }
    const bool hearing = m_connection.hearing_vrchat();
    if (hearing && !m_heard) m_heard_since = now;
    m_heard = hearing;
    if (m_settings.status_enabled && m_connection.sending()) {
        const double interval = m_settings.status_interval < 2.0 ? 2.0 : m_settings.status_interval;
        if (m_status.due(now, interval)) {
            const std::string text = m_status.next(status_values(), now);
            if (!text.empty()) m_connection.chatbox(text, true, m_settings.status_notify);
        }
    }
}

bool Session::set_parameter(const std::string& name, const std::string& type, const std::string& text, std::string* error) {
    if (name.empty()) {
        if (error) *error = "the parameter needs a name";
        return false;
    }
    vrc::ParamType t = vrc::param_type_from(type);
    if (t == vrc::ParamType::Unknown) {
        if (const auto& config = m_connection.avatar().config()) {
            if (const vrc::ParameterInfo* info = config->find(name)) t = info->type;
        }
    }
    if (t == vrc::ParamType::Unknown) {
        if (const auto live = m_connection.avatar().value(name)) t = vrc::param_type_of(*live);
    }
    const std::optional<osc::Value> v = vrc::make_param_value(t, text);
    if (!v) {
        if (error) *error = "\"" + text + "\" isn't a " + vrc::to_string(t) + " value";
        return false;
    }
    if (!m_connection.set_parameter(name, *v)) {
        const std::string why = m_connection.sending() ? m_connection.sender().last_error() : std::string("not connected");
        if (error) *error = why;
        return false;
    }
    return true;
}

bool Session::chatbox(const std::string& text, bool notify) {
    m_typing = false;
    return m_connection.chatbox(text, true, notify);
}

bool Session::typing(bool on) {
    m_typing = on;
    return m_connection.typing(on);
}

bool Session::send_text(const std::string& line, std::string* error) {
    const std::optional<osc::Message> m = parse_message_line(line, error);
    if (!m) return false;
    if (!m_connection.send(*m)) {
        if (error) *error = m_connection.sending() ? m_connection.sender().last_error() : std::string("not connected");
        return false;
    }
    if (vrc::is_parameter_address(m->address()) && !m->empty()) m_connection.avatar().remember_sent(vrc::parameter_name(m->address()), (*m)[0]);
    return true;
}

void Session::set_status_enabled(bool on) {
    m_settings.status_enabled = on;
    m_status.reset();
}

void Session::set_status_template(const std::string& text) {
    m_settings.status_template = text;
    m_status.set_template(text);
}

StatusValues Session::status_values() {
    const ModuleInputs in = module_inputs();
    StatusValues values = all_module_values(m_settings.modules, in);
    add_modified_values(m_settings.status_template, m_settings.modules, in, values);
    return values;
}

std::string Session::placeholder_problems(const std::string& text) const {
    StatusValues values;
    std::vector<std::string> problems;
    add_modified_values(text, m_settings.modules, ModuleInputs(), values, &problems);
    std::string out;
    for (const std::string& p : problems) out += (out.empty() ? "" : "\n") + p;
    return out;
}

double Session::connected_seconds() const {
    if (!m_heard) return -1.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_heard_since).count();
}

ModuleInputs Session::module_inputs() const {
    ModuleInputs in;
    in.sensors = m_sensors.snapshot();
    in.media = m_media.now_playing();
    in.weather = m_weather.snapshot();
    in.avatar_name = avatar_name();
    in.avatar_id = m_connection.avatar().avatar_id();
    const auto& config = m_connection.avatar().config();
    in.params = config && !config->parameters.empty() ? config->parameters.size() : m_connection.avatar().live().size();
    in.connected_seconds = connected_seconds();
    in.app_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
    in.now = std::chrono::system_clock::now();
    in.world = m_vrclog.world();
    in.account = m_api.account();
    in.last_joined = m_last_joined;
    in.last_left = m_last_left;
    in.friend_news = m_friend_news;
    for (const VrcUser& f : m_api.friends()) {
        ++in.friends_total;
        if (f.state == "online") ++in.friends_online;
        else if (f.state == "active") ++in.friends_web;
    }
    for (const InstancePlayer& p : in.world.players) {
        if (!p.id.empty() && m_api.is_friend(p.id)) in.friends_here.push_back(p.name);
    }
    return in;
}

std::string Session::module_preview(const std::string& module, const std::string& text) const {
    const ModuleDef* def = find_module(module);
    if (!def) return std::string();
    StatusValues values;
    const ModuleInputs in = module_inputs();
    if (module == "units") values = all_module_values(m_settings.modules, in);
    else module_values(*def, m_settings.modules, in, values);
    add_modified_values(text, m_settings.modules, in, values);
    return render_status(text, values);
}

std::string Session::module_status(const std::string& module) const {
    if (module == "world") {
        if (!m_vrclog.found()) return "Pollen hasn't found VRChat's log yet. It looks next to VRChat's OSC folder, so set that folder in Settings if VRChat is somewhere unusual";
        if (!m_vrclog.world().in_world) return "Not in a world right now (reading " + m_vrclog.file().filename().string() + ")";
        return "Reading " + m_vrclog.file().filename().string();
    }
    if (module == "friends") {
        const AccountSnapshot a = m_api.account();
        if (a.state != LoginState::LoggedIn) return "Log in to VRChat under Settings to use this module";
        return "Logged in as " + a.me.name + (a.pipeline ? ", getting live updates" : ", waiting for live updates");
    }
    if (module == "weather") {
        const WeatherSnapshot w = m_weather.snapshot();
        if (w.ok) return "Weather for " + w.place;
        if (w.loading && w.error.empty()) return "Checking the weather...";
        return w.error.empty() ? std::string("Set a city or a latitude and longitude") : "Weather: " + w.error;
    }
    const SensorSnapshot s = m_sensors.snapshot();
    if (module == "gpu" || module == "vram") {
        std::string out = s.gpu_name.empty() ? std::string("No GPU readings found") : "Reading " + s.gpu_name + (s.gpu_source.empty() ? std::string() : " through " + s.gpu_source);
        if (!s.vrchat_running) out += ". VRChat isn't running, so its values are blank";
        if (!s.notes.empty()) out += ". Note: " + s.notes;
        return out;
    }
    if (module == "cpu") {
        std::string out = s.cpu_name.empty() ? std::string() : "Reading " + s.cpu_name;
        if (!s.cpu_temp) out += std::string(out.empty() ? "" : ". ") + "No temperature sensor found";
        return out;
    }
    if (module == "ram" && !s.vrchat_running) return "VRChat isn't running, so its value is blank";
    if (module == "media") {
        const NowPlaying np = m_media.now_playing();
        if (!np.available || !np.has_song()) return np.reason;
    }
    return std::string();
}

std::string Session::module_warning(const std::string& module) const {
    const SensorSnapshot s = m_sensors.snapshot();
    if (module == "cpu" && option_on(m_settings.modules, "cpu", "temp")) return s.cpu_temp_help;
    if (module == "gpu" && option_on(m_settings.modules, "gpu", "temp")) return s.gpu_temp_help;
    return std::string();
}

bool Session::set_module_option(const std::string& module, const std::string& option, const std::string& value, std::string* error) {
    if (!set_option(m_settings.modules, module, option, value, error)) return false;
    if (module == "weather") m_weather.configure(weather_config(m_settings.modules));
    return save_settings(error);
}

std::string Session::module_template(const std::string& module) const {
    const auto it = m_settings.module_templates.find(module);
    if (it != m_settings.module_templates.end()) return it->second;
    const ModuleDef* def = find_module(module);
    return def ? def->example : std::string();
}

bool Session::set_module_template(const std::string& module, const std::string& text, std::string* error) {
    if (!find_module(module)) {
        if (error) *error = "no module called " + module;
        return false;
    }
    m_settings.module_templates[module] = text;
    return save_settings(error);
}

std::string Session::status_preview() {
    m_status.set_template(m_settings.status_template);
    const double interval = m_settings.status_interval < 2.0 ? 2.0 : m_settings.status_interval;
    const bool sending = m_settings.status_enabled && m_connection.sending();
    return vrc::chatbox_text(m_status.showing(status_values(), std::chrono::steady_clock::now(), interval, sending));
}

bool Session::send_status_now() {
    m_status.set_template(m_settings.status_template);
    const std::string text = m_status.next(status_values(), std::chrono::steady_clock::now());
    if (text.empty()) return false;
    return m_connection.chatbox(text, true, m_settings.status_notify);
}

std::vector<ParamRow> Session::parameters() const {
    std::vector<ParamRow> rows;
    const vrc::AvatarState& avatar = m_connection.avatar();
    const auto now = std::chrono::steady_clock::now();
    auto fill_live = [&](ParamRow& row) {
        const auto it = avatar.live().find(row.name);
        if (it == avatar.live().end()) return;
        row.live = true;
        row.value = show_value(it->second.value);
        row.age = std::chrono::duration<double>(now - it->second.updated).count();
        if (row.type == vrc::ParamType::Unknown) row.type = it->second.type;
    };
    if (const auto& config = avatar.config()) {
        for (const vrc::ParameterInfo& p : config->parameters) {
            ParamRow row;
            row.name = p.name;
            row.type = p.type;
            row.known = true;
            row.writable = p.writable();
            fill_live(row);
            rows.push_back(std::move(row));
        }
    }
    for (const auto& kv : avatar.live()) {
        const bool listed = std::any_of(rows.begin(), rows.end(), [&](const ParamRow& r) { return r.name == kv.first; });
        if (listed) continue;
        ParamRow row;
        row.name = kv.first;
        fill_live(row);
        rows.push_back(std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const ParamRow& a, const ParamRow& b) { return lower(a.name) < lower(b.name); });
    return rows;
}

std::string Session::avatar_name() const {
    const vrc::AvatarState& avatar = m_connection.avatar();
    if (avatar.config() && !avatar.config()->name.empty()) return avatar.config()->name;
    return avatar.avatar_id();
}

bool Session::reload_avatar() {
    if (m_link.running()) m_link.request_avatar();
    const bool ok = m_connection.reload_avatar_config() || found_vrchat();
    if (!avatar_id().empty()) log('s', ok ? "loaded the parameters of " + avatar_name() : "couldn't find the OSC file for " + avatar_id());
    return ok;
}

void Session::log(char kind, const std::string& text) {
    LogLine line;
    line.seq = ++m_seq;
    line.kind = kind;
    line.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
    line.text = text;
    m_log.push_back(std::move(line));
    while (m_log.size() > log_limit) m_log.pop_front();
}

std::vector<LogLine> Session::log_since(std::uint64_t seq, std::size_t max) const {
    std::vector<LogLine> out;
    for (auto it = m_log.rbegin(); it != m_log.rend() && it->seq > seq && out.size() < max; ++it) out.push_back(*it);
    std::reverse(out.begin(), out.end());
    return out;
}

void Session::clear_log() {
    m_log.clear();
}

std::string Session::last_error() const {
    if (!m_error.empty()) return m_error;
    std::string e = m_connection.receiver().last_error();
    if (!e.empty()) return e;
    return m_connection.sender().last_error();
}

std::vector<fs::path> Session::osc_folders() const {
    return vrc::osc_folders(m_settings.connection().extra_osc_folders);
}

} // namespace pollen
