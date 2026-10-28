#include "pollen/vrchat_api.hpp"

#include <fizmo/../../Source/Util Hpp/json.hpp>
#include <fizmo/../networking.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <future>
#include <sstream>

namespace pollen {

namespace fs = std::filesystem;
namespace http = fizmo::networking::http;
namespace ws = fizmo::networking::ws;
using fizmo::json::Value;

namespace {

void ensure_loop() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (!fizmo::networking::background_running()) fizmo::networking::start_background(1);
    });
}

bool parse_json(const std::string& text, Value& out) {
    return fizmo::json::parse(text, out, nullptr);
}

std::string str(const Value& v, const char* key) {
    return v[key].as_string(std::string());
}

Value content_of(const Value& message) {
    const Value& c = message["content"];
    if (c.is_string()) {
        Value inner;
        if (parse_json(c.as_string(), inner)) return inner;
        return c;
    }
    return c;
}

VrcUser user_from(const Value& u) {
    VrcUser out;
    out.id = str(u, "id");
    out.name = str(u, "displayName");
    out.status = str(u, "status");
    out.status_text = str(u, "statusDescription");
    out.location = str(u, "location");
    out.platform = str(u, "platform");
    if (out.platform.empty()) out.platform = str(u, "last_platform");
    out.last_login = str(u, "last_login");
    return out;
}

std::string first_tag(const Value& v) {
    for (const Value& t : v["tags"].items()) {
        if (t.is_string()) return t.as_string();
    }
    return std::string();
}

std::string json_string(const std::string& s) {
    return Value(s).dump();
}

bool write_private(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        out << text;
        if (!out) return false;
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(temp, path, ec);
    }
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return !ec;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string error_message(const std::string& body, int status) {
    Value v;
    if (parse_json(body, v)) {
        std::string m = v["error"]["message"].as_string(std::string());
        if (m.empty()) m = v["error"].as_string(std::string());
        while (!m.empty() && (m.front() == '"' || m.back() == '"')) {
            if (m.front() == '"') m.erase(0, 1);
            if (!m.empty() && m.back() == '"') m.pop_back();
        }
        if (!m.empty()) return m;
    }
    return "VRChat answered " + std::to_string(status);
}

} // namespace

std::string status_label(const std::string& status) {
    if (status == "join me") return "Join Me";
    if (status == "active") return "Online";
    if (status == "ask me") return "Ask Me";
    if (status == "busy") return "Do Not Disturb";
    if (status == "offline") return "Offline";
    return status;
}

std::string status_color(const std::string& status) {
    if (status == "join me") return "Blue";
    if (status == "active") return "Green";
    if (status == "ask me") return "Orange";
    if (status == "busy") return "Red";
    if (status == "offline") return "Gray";
    return std::string();
}

std::string status_icon(const std::string& status) {
    if (status == "join me") return "\xf0\x9f\x94\xb5";
    if (status == "active") return "\xf0\x9f\x9f\xa2";
    if (status == "ask me") return "\xf0\x9f\x9f\xa0";
    if (status == "busy") return "\xf0\x9f\x94\xb4";
    if (status == "offline") return "\xe2\x9a\xab";
    return std::string();
}

std::string base64_encode(const std::string& data) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) | static_cast<unsigned char>(data[i + 2]);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += table[v & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string form_encode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::optional<VrcUser> parse_vrc_user(const std::string& json) {
    Value v;
    if (!parse_json(json, v) || !v.is_object() || str(v, "id").empty()) return std::nullopt;
    return user_from(v);
}

const VrcUser* FriendBook::find(const std::string& id) const {
    const auto it = m_friends.find(id);
    return it == m_friends.end() ? nullptr : &it->second;
}

std::map<std::string, std::string> FriendBook::names() const {
    std::map<std::string, std::string> out;
    for (const auto& f : m_friends) out[f.first] = f.second.name;
    return out;
}

std::string FriendBook::describe_location(const std::string& location) const {
    const LocationInfo info = parse_location(location);
    if (!info.valid) {
        if (info.special == "private") return "a private world";
        if (info.special == "traveling") return "traveling";
        if (info.special == "offline") return "offline";
        return location;
    }
    std::string name = m_world_name ? m_world_name(info.world_id) : std::string();
    if (name.empty()) name = "a world";
    return name + " (" + access_label(info.access) + ")";
}

void FriendBook::set_location(VrcUser& f, const std::string& location, std::chrono::system_clock::time_point now) {
    if (location == f.location && f.location_since_known) return;
    if (location.compare(0, 9, "traveling") == 0) return;
    if (parse_location(f.location).valid) f.last_public_location = f.location;
    f.location = location;
    f.location_since = now;
    f.location_since_known = true;
    if (parse_location(location).valid) f.last_public_location = location;
}

void FriendBook::replace_all(const std::vector<VrcUser>& list, const std::map<std::string, std::string>& known_before, std::vector<SocialEvent>& events) {
    const auto now = std::chrono::system_clock::now();
    std::map<std::string, VrcUser> next;
    for (VrcUser u : list) {
        const auto old = m_friends.find(u.id);
        if (old != m_friends.end()) {
            u.last_public_location = old->second.last_public_location;
            if (old->second.location == u.location) {
                u.location_since = old->second.location_since;
                u.location_since_known = old->second.location_since_known;
            }
        }
        if (parse_location(u.location).valid) u.last_public_location = u.location;
        next[u.id] = u;
    }
    if (!known_before.empty()) {
        for (const auto& k : known_before) {
            if (next.count(k.first)) continue;
            events.push_back({ now, "removed", k.first, k.second, k.second + " is no longer on your friends list. They unfriended or blocked you, or were removed somewhere else; VRChat doesn't say which", std::string() });
        }
        for (const auto& n : next) {
            if (known_before.count(n.first)) continue;
            events.push_back({ now, "added", n.first, n.second.name, n.second.name + " is now your friend", std::string() });
        }
    }
    m_friends = std::move(next);
}

bool FriendBook::apply_pipeline(const std::string& message, VrcUser& me, std::vector<SocialEvent>& events) {
    Value m;
    if (!parse_json(message, m) || !m.is_object()) return false;
    const std::string type = str(m, "type");
    const Value c = content_of(m);
    const auto now = std::chrono::system_clock::now();
    std::string id = str(c, "userId");
    if (id.empty()) id = str(c, "userid");
    const Value& user = c["user"];

    auto name_of = [&](const std::string& uid) {
        if (const VrcUser* f = find(uid)) {
            if (!f->name.empty()) return f->name;
        }
        const std::string n = str(user, "displayName");
        return n.empty() ? uid : n;
    };
    auto update_profile = [&](VrcUser& f) -> std::vector<std::string> {
        std::vector<std::string> changes;
        if (!user.is_object()) return changes;
        const std::string name = str(user, "displayName");
        if (!name.empty()) f.name = name;
        if (user.has("status")) {
            const std::string s = str(user, "status");
            if (!s.empty() && s != f.status) {
                if (!f.status.empty()) changes.push_back("status");
                f.status = s;
            }
        }
        if (user.has("statusDescription")) {
            const std::string s = str(user, "statusDescription");
            if (s != f.status_text) {
                if (!f.status_text.empty() || !s.empty()) changes.push_back("text");
                f.status_text = s;
            }
        }
        return changes;
    };

    if (type == "friend-online" || type == "friend-active") {
        VrcUser& f = m_friends[id];
        f.id = id;
        update_profile(f);
        const bool was_on = f.state == "online";
        f.state = type == "friend-online" ? "online" : "active";
        const std::string platform = str(c, "platform");
        if (!platform.empty()) f.platform = platform;
        if (type == "friend-online") {
            if (f.status.empty() || f.status == "offline") f.status = "active";
            set_location(f, str(c, "location"), now);
            if (!was_on) events.push_back({ now, "online", id, f.name, f.name + " is online" + (parse_location(f.location).valid ? " in " + describe_location(f.location) : std::string()), f.location });
        } else {
            f.location = "offline";
            events.push_back({ now, "active", id, f.name, f.name + " is on the VRChat website", std::string() });
        }
        return true;
    }
    if (type == "friend-offline") {
        VrcUser& f = m_friends[id];
        f.id = id;
        if (f.name.empty()) f.name = id;
        f.state = "offline";
        set_location(f, "offline", now);
        events.push_back({ now, "offline", id, f.name, f.name + " went offline", std::string() });
        return true;
    }
    if (type == "friend-location") {
        VrcUser& f = m_friends[id];
        f.id = id;
        update_profile(f);
        f.state = "online";
        std::string location = str(c, "location");
        if (location.compare(0, 9, "traveling") == 0) return true;
        if (location == f.location) return true;
        set_location(f, location, now);
        events.push_back({ now, "location", id, f.name, f.name + " went to " + describe_location(location), location });
        return true;
    }
    if (type == "friend-update") {
        VrcUser& f = m_friends[id];
        f.id = id;
        for (const std::string& change : update_profile(f)) {
            if (change == "status") events.push_back({ now, "status", id, f.name, f.name + " is now " + status_label(f.status), std::string() });
            else events.push_back({ now, "status", id, f.name, f.name + "'s status says \"" + f.status_text + "\"", std::string() });
        }
        return true;
    }
    if (type == "friend-add") {
        VrcUser f = user.is_object() ? user_from(user) : VrcUser();
        f.id = id;
        if (f.name.empty()) f.name = id;
        if (f.state.empty()) f.state = "offline";
        m_friends[id] = f;
        events.push_back({ now, "added", id, f.name, f.name + " is now your friend", std::string() });
        return true;
    }
    if (type == "friend-delete") {
        const std::string name = name_of(id);
        m_friends.erase(id);
        events.push_back({ now, "removed", id, name, name + " is no longer your friend. They unfriended or blocked you, or you removed them; VRChat doesn't say which", std::string() });
        return true;
    }
    if (type == "user-update") {
        const std::string old_status = me.status;
        const std::string old_text = me.status_text;
        if (user.is_object()) {
            const std::string name = str(user, "displayName");
            if (!name.empty()) me.name = name;
            if (user.has("status")) me.status = str(user, "status");
            if (user.has("statusDescription")) me.status_text = str(user, "statusDescription");
        }
        if (!old_status.empty() && me.status != old_status) events.push_back({ now, "you", me.id, me.name, "Your status is now " + status_label(me.status), std::string() });
        else if (me.status_text != old_text) events.push_back({ now, "you", me.id, me.name, "Your status text is now \"" + me.status_text + "\"", std::string() });
        return true;
    }
    if (type == "user-location") {
        const std::string location = str(c, "location");
        if (location.compare(0, 9, "traveling") != 0) me.location = location;
        return true;
    }
    if (type == "notification") {
        const std::string kind = str(c, "type");
        const std::string sender = str(c, "senderUsername").empty() ? str(c, "senderUserId") : str(c, "senderUsername");
        const std::string sender_id = str(c, "senderUserId");
        if (kind == "friendRequest") events.push_back({ now, "request", sender_id, sender, sender + " sent you a friend request", std::string() });
        else if (kind == "invite") {
            std::string world = str(c["details"], "worldName");
            events.push_back({ now, "invite", sender_id, sender, sender + " invited you" + (world.empty() ? std::string() : " to " + world), str(c["details"], "worldId") });
        } else if (kind == "requestInvite") events.push_back({ now, "invite", sender_id, sender, sender + " asked you for an invite", std::string() });
        return true;
    }
    return false;
}

struct VrchatApi::Pipeline {
    std::mutex mutex;
    std::atomic<bool> alive{ true };
    VrchatApi* owner = nullptr;
    ws::WebSocket socket;
};

VrchatApi::VrchatApi() {
    m_book.set_world_namer([this](const std::string& world_id) {
        const auto it = m_world_names.find(world_id);
        if (it != m_world_names.end()) return it->second;
        if (std::find(m_world_queue.begin(), m_world_queue.end(), world_id) == m_world_queue.end()) m_world_queue.push_back(world_id);
        return std::string();
    });
}

VrchatApi::~VrchatApi() {
    stop();
}

fs::path VrchatApi::session_file(const fs::path& folder) {
    return folder / "vrchat-session.json";
}

void VrchatApi::set_endpoints(const std::string& api_base, const std::string& pipeline_url) {
    if (m_running.load()) return;
    m_api_base = api_base;
    m_pipeline_url = pipeline_url;
}

void VrchatApi::start(const fs::path& folder, const std::string& user_agent) {
    if (m_running.exchange(true)) return;
    m_folder = folder;
    m_user_agent = user_agent;
    load_session();
    m_thread = std::thread([this] { run(); });
    if (!m_auth.empty()) {
        post([this] {
            set_state(LoginState::Working, "Logging in with your saved session...");
            fetch_me(true);
        });
    }
}

void VrchatApi::stop() {
    if (!m_running.exchange(false)) return;
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
    close_pipeline();
}

void VrchatApi::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_all();
}

void VrchatApi::set_state(LoginState state, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_account.state = state;
    m_account.message = message;
}

void VrchatApi::note(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_account.action_note = text;
}

VrchatApi::Reply VrchatApi::request(const std::string& method, const std::string& path, const std::string& body, const std::string& authorization) {
    ensure_loop();
    Reply reply;
    const auto now = std::chrono::steady_clock::now();
    if (now < m_backoff_until) {
        reply.error = "VRChat asked Pollen to slow down; trying again in a minute";
        return reply;
    }
    const auto spacing = std::chrono::milliseconds(800);
    if (now - m_last_request < spacing) std::this_thread::sleep_for(spacing - (now - m_last_request));
    m_last_request = std::chrono::steady_clock::now();

    http::FetchOptions options;
    options.method = method;
    options.timeout = std::chrono::milliseconds(20000);
    options.headers.set("User-Agent", m_user_agent);
    options.headers.set("Accept", "application/json");
    std::string cookie;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_auth.empty()) cookie = "auth=" + m_auth;
        if (!m_two_factor.empty()) cookie += (cookie.empty() ? "" : "; ") + std::string("twoFactorAuth=") + m_two_factor;
    }
    if (!cookie.empty()) options.headers.set("Cookie", cookie);
    if (!authorization.empty()) options.headers.set("Authorization", authorization);
    if (!body.empty() || method == "POST" || method == "PUT") {
        options.headers.set("Content-Type", "application/json");
        options.body = body.empty() ? std::string("{}") : body;
    }
    auto promise = std::make_shared<std::promise<Reply>>();
    auto result = promise->get_future();
    http::fetch(m_api_base + path, options, [promise](const std::error_code& ec, http::FetchResponse r) {
        Reply out;
        if (ec) out.error = ec.message();
        else {
            out.status = r.status;
            out.body = r.body;
            out.cookies = r.headers.get_all("set-cookie");
        }
        try {
            promise->set_value(std::move(out));
        } catch (...) {
        }
    });
    if (result.wait_for(std::chrono::seconds(25)) != std::future_status::ready) {
        reply.error = "VRChat didn't answer";
        return reply;
    }
    reply = result.get();
    if (!reply.cookies.empty()) take_cookies(reply.cookies);
    if (reply.status == 429) {
        m_backoff_until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        reply.error = "VRChat asked Pollen to slow down; trying again in a minute";
    } else if (reply.status >= 400 || reply.status < 0) {
        if (reply.error.empty()) reply.error = error_message(reply.body, reply.status);
    }
    return reply;
}

void VrchatApi::take_cookies(const std::vector<std::string>& cookies) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const std::string& c : cookies) {
            const std::size_t eq = c.find('=');
            if (eq == std::string::npos) continue;
            const std::string name = c.substr(0, eq);
            const std::size_t semi = c.find(';', eq);
            const std::string value = c.substr(eq + 1, semi == std::string::npos ? std::string::npos : semi - eq - 1);
            std::string lower = c;
            for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            const bool expired = value.empty() || lower.find("max-age=0") != std::string::npos;
            if (name == "auth") {
                m_auth = expired ? std::string() : value;
                changed = true;
            } else if (name == "twoFactorAuth") {
                m_two_factor = expired ? std::string() : value;
                changed = true;
            }
        }
    }
    if (changed) save_session();
}

void VrchatApi::save_session() {
    std::string auth, two;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auth = m_auth;
        two = m_two_factor;
    }
    const fs::path path = session_file(m_folder);
    if (auth.empty() && two.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
        return;
    }
    Value v = Value::object();
    v.set("auth", auth);
    v.set("twoFactorAuth", two);
    write_private(path, v.dump(2) + "\n");
}

void VrchatApi::load_session() {
    Value v;
    if (!parse_json(read_file(session_file(m_folder)), v)) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_auth = str(v, "auth");
    m_two_factor = str(v, "twoFactorAuth");
}

void VrchatApi::save_friend_names() {
    Value v = Value::object();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& n : m_book.names()) v.set(n.first, n.second);
    }
    write_private(m_folder / "vrchat-friends.json", v.dump(1) + "\n");
}

std::map<std::string, std::string> VrchatApi::load_friend_names() {
    std::map<std::string, std::string> out;
    Value v;
    if (!parse_json(read_file(m_folder / "vrchat-friends.json"), v)) return out;
    for (const auto& m : v.members()) out[m.first] = m.second.as_string(m.first);
    return out;
}

bool VrchatApi::fetch_me(bool quiet) {
    const Reply r = request("GET", "/auth/user");
    if (r.status == 200) {
        Value v;
        if (parse_json(r.body, v) && v.has("requiresTwoFactorAuth")) {
            std::vector<std::string> methods;
            for (const Value& m : v["requiresTwoFactorAuth"].items()) methods.push_back(m.as_string());
            std::string kind = "app";
            if (std::find(methods.begin(), methods.end(), "emailOtp") != methods.end()) kind = "email";
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_code_methods = methods;
                m_account.code_kind = kind;
            }
            set_state(LoginState::NeedsCode, kind == "email" ? "VRChat emailed you a code. Type it in below" : "Type the code from your authenticator app (or a recovery code)");
            return false;
        }
        logged_in(r.body);
        return true;
    }
    if (r.status == 401) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_auth.clear();
            m_two_factor.clear();
        }
        save_session();
        set_state(LoginState::LoggedOut, quiet ? "Your saved VRChat login ran out. Log in again" : "Wrong username or password");
        return false;
    }
    set_state(LoginState::LoggedOut, "Couldn't reach VRChat: " + r.error);
    return false;
}

void VrchatApi::logged_in(const std::string& user_json) {
    const auto me = parse_vrc_user(user_json);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (me) m_account.me = *me;
        m_account.state = LoginState::LoggedIn;
        m_account.message = "Logged in as " + m_account.me.name;
        m_account.device_code.clear();
        m_account.device_url.clear();
        m_device_secret.clear();
    }
    save_session();
    fetch_groups();
    fetch_friends();
    open_pipeline();
}

void VrchatApi::login(const std::string& username, const std::string& password) {
    const std::string header = "Basic " + base64_encode(form_encode(username) + ":" + form_encode(password));
    post([this, header] {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_auth.clear();
            m_two_factor.clear();
        }
        set_state(LoginState::Working, "Logging in...");
        const Reply r = request("GET", "/auth/user", std::string(), header);
        if (r.status == 200) {
            Value v;
            if (parse_json(r.body, v) && v.has("requiresTwoFactorAuth")) {
                fetch_me(false);
                return;
            }
            logged_in(r.body);
            return;
        }
        set_state(LoginState::LoggedOut, r.status == 401 ? "Wrong username or password" : "Couldn't log in: " + r.error);
    });
}

void VrchatApi::verify(const std::string& raw) {
    std::string code;
    for (char c : raw) {
        if (!std::isspace(static_cast<unsigned char>(c))) code += c;
    }
    post([this, code] {
        std::vector<std::string> methods;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            methods = m_code_methods;
        }
        auto has = [&](const char* m) { return std::find(methods.begin(), methods.end(), m) != methods.end(); };
        std::string path = "/auth/twofactorauth/totp/verify";
        if (has("emailOtp")) path = "/auth/twofactorauth/emailotp/verify";
        else if (code.size() != 6 && has("otp")) path = "/auth/twofactorauth/otp/verify";
        set_state(LoginState::Working, "Checking the code...");
        const Reply r = request("POST", path, "{\"code\":" + json_string(code) + "}");
        Value v;
        if (r.status == 200 && parse_json(r.body, v) && v["verified"].as_bool(false)) {
            fetch_me(false);
            return;
        }
        set_state(LoginState::NeedsCode, r.status == 200 ? "That code didn't work. Try again" : "That code didn't work: " + r.error);
    });
}

void VrchatApi::login_with_code() {
    post([this] {
        set_state(LoginState::Working, "Asking VRChat for a login code...");
        const Reply r = request("POST", "/auth/device/code");
        Value v;
        if (r.status != 200 || !parse_json(r.body, v) || str(v, "deviceCode").empty()) {
            set_state(LoginState::LoggedOut, "VRChat didn't give a login code: " + (r.error.empty() ? std::string("unexpected answer") : r.error));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_device_secret = str(v, "deviceCode");
            m_account.device_code = str(v, "userCode");
            m_account.device_url = str(v, "verificationUrlComplete");
            if (m_account.device_url.empty()) m_account.device_url = str(v, "verificationUrl");
            m_device_interval = static_cast<int>(v["interval"].as_int(5));
            if (m_device_interval < 3) m_device_interval = 3;
            m_device_next = std::chrono::steady_clock::now() + std::chrono::seconds(m_device_interval);
            m_device_expires = std::chrono::steady_clock::now() + std::chrono::seconds(v["expiresIn"].as_int(300));
            m_account.state = LoginState::WaitingForApproval;
            m_account.message = "Open the link, check the code matches, and approve it while logged in to vrchat.com";
        }
    });
}

void VrchatApi::poll_device_code() {
    std::string secret;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_account.state != LoginState::WaitingForApproval || m_device_secret.empty()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_device_expires) {
            m_device_secret.clear();
            m_account.device_code.clear();
            m_account.device_url.clear();
            m_account.state = LoginState::LoggedOut;
            m_account.message = "The login code ran out before it was approved. Ask for a new one";
            return;
        }
        if (now < m_device_next) return;
        m_device_next = now + std::chrono::seconds(m_device_interval);
        secret = m_device_secret;
    }
    const Reply r = request("POST", "/auth/device/status", "{\"deviceCode\":" + json_string(secret) + "}");
    Value v;
    if (r.status == 200 && parse_json(r.body, v) && v["user"].is_object()) {
        logged_in(v["user"].dump());
        return;
    }
    if (r.status >= 400 && r.status != 429) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_device_secret.clear();
        m_account.state = LoginState::LoggedOut;
        m_account.message = "The login code stopped working: " + r.error;
    }
}

void VrchatApi::cancel_login() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device_secret.clear();
    m_account.device_code.clear();
    m_account.device_url.clear();
    if (m_account.state != LoginState::LoggedIn) {
        m_account.state = LoginState::LoggedOut;
        m_account.message.clear();
    }
}

void VrchatApi::logout() {
    post([this] {
        close_pipeline();
        request("PUT", "/logout");
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_auth.clear();
            m_two_factor.clear();
            m_account = AccountSnapshot();
            m_account.message = "Logged out";
            m_book.clear();
            m_friends_loaded = false;
            m_waiting.clear();
            m_collections.clear();
            m_groups.clear();
        }
        save_session();
    });
}

void VrchatApi::refresh_friends() {
    post([this] { fetch_friends(); });
}

void VrchatApi::fetch_friends() {
    std::vector<VrcUser> list;
    for (int pass = 0; pass < 2; ++pass) {
        const bool offline = pass == 1;
        for (int page = 0; page < 30; ++page) {
            const Reply r = request("GET", "/auth/user/friends?offline=" + std::string(offline ? "true" : "false") + "&n=100&offset=" + std::to_string(page * 100));
            Value v;
            if (r.status != 200 || !parse_json(r.body, v) || !v.is_array()) {
                note("Couldn't load your friends: " + r.error);
                return;
            }
            for (const Value& u : v.items()) {
                VrcUser f = user_from(u);
                if (f.id.empty()) continue;
                if (offline) {
                    f.state = "offline";
                } else {
                    const LocationInfo info = parse_location(f.location);
                    f.state = (!info.valid && (f.location.empty() || f.location == "offline")) ? "active" : "online";
                }
                list.push_back(f);
            }
            if (v.size() < 100) break;
        }
    }
    std::map<std::string, std::string> before = load_friend_names();
    std::vector<SocialEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (before.empty() && m_friends_loaded) before = m_book.names();
        m_book.replace_all(list, before, events);
        m_friends_loaded = true;
        ++m_account.friends_revision;
        m_next_friends = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    }
    save_friend_names();
    push_events(events);
}

void VrchatApi::push_events(std::vector<SocialEvent>& events) {
    if (events.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (SocialEvent& e : events) m_events.push_back(std::move(e));
    while (m_events.size() > 500) m_events.pop_front();
}

std::vector<SocialEvent> VrchatApi::take_events() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SocialEvent> out(m_events.begin(), m_events.end());
    m_events.clear();
    return out;
}

void VrchatApi::fetch_world(const std::string& world_id) {
    const Reply r = request("GET", "/worlds/" + world_id);
    Value v;
    std::string name = world_id;
    if (r.status == 200 && parse_json(r.body, v)) name = str(v, "name");
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_world_names[world_id] = name.empty() ? world_id : name;
        ++m_account.friends_revision;
    }
    release_waiting(false);
}

void VrchatApi::release_waiting(bool all) {
    std::vector<SocialEvent> ready;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = m_waiting.begin(); it != m_waiting.end();) {
            const LocationInfo info = parse_location(it->second.location);
            const auto name = m_world_names.find(info.world_id);
            const bool known = name != m_world_names.end();
            if (known || all || now - it->first > std::chrono::seconds(15)) {
                SocialEvent e = it->second;
                const std::size_t at = e.text.find("a world (");
                if (known && at != std::string::npos) e.text.replace(at, 7, name->second);
                ready.push_back(std::move(e));
                it = m_waiting.erase(it);
            } else {
                ++it;
            }
        }
    }
    push_events(ready);
}

std::string VrchatApi::world_name(const std::string& world_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_world_names.find(world_id);
    if (it != m_world_names.end()) return it->second;
    if (m_account.state == LoginState::LoggedIn && std::find(m_world_queue.begin(), m_world_queue.end(), world_id) == m_world_queue.end()) {
        m_world_queue.push_back(world_id);
        m_wake.notify_all();
    }
    return std::string();
}

std::string VrchatApi::describe_location(const std::string& location) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const LocationInfo info = parse_location(location);
    if (!info.valid) return m_book.describe_location(location);
    const auto it = m_world_names.find(info.world_id);
    const std::string name = it == m_world_names.end() ? std::string("a world") : it->second;
    return name + " (" + access_label(info.access) + ")";
}

void VrchatApi::open_pipeline() {
    close_pipeline();
    std::string auth;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auth = m_auth;
    }
    if (auth.empty()) return;
    ensure_loop();
    auto p = std::make_shared<Pipeline>();
    p->owner = this;
    ws::ClientOptions options;
    options.headers.set("User-Agent", m_user_agent);
    std::weak_ptr<Pipeline> weak = p;
    p->socket = ws::connect(m_pipeline_url + "?authToken=" + form_encode(auth), options, [weak] {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            if (!s->alive) return;
            std::lock_guard<std::mutex> state(s->owner->m_mutex);
            s->owner->m_account.pipeline = true;
        }
    });
    p->socket.on_text([weak](const std::string& text) {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            if (s->alive) s->owner->on_pipeline(text);
        }
    });
    auto lost = [weak] {
        if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex);
            if (!s->alive) return;
            s->alive = false;
            std::lock_guard<std::mutex> state(s->owner->m_mutex);
            s->owner->m_account.pipeline = false;
            s->owner->m_pipeline_retry = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            s->owner->m_wake.notify_all();
        }
    };
    p->socket.on_close([lost](std::uint16_t, const std::string&) { lost(); });
    p->socket.on_error([lost](const std::error_code&) { lost(); });
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pipeline = p;
    m_pipeline_retry = std::chrono::steady_clock::time_point();
}

void VrchatApi::close_pipeline() {
    std::shared_ptr<Pipeline> p;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        p = std::move(m_pipeline);
        m_account.pipeline = false;
    }
    if (!p) return;
    {
        std::lock_guard<std::mutex> lock(p->mutex);
        p->alive = false;
    }
    if (p->socket) p->socket.close();
}

void VrchatApi::on_pipeline(const std::string& text) {
    std::vector<SocialEvent> events;
    std::string removing;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_book.apply_pipeline(text, m_account.me, events)) ++m_account.friends_revision;
        removing = m_removing;
    }
    for (SocialEvent& e : events) {
        if (e.kind == "removed" && !removing.empty() && e.user_id == removing) {
            e.text = "You removed " + e.name + " from your friends";
        }
    }
    bool names_changed = false;
    for (const SocialEvent& e : events) names_changed = names_changed || e.kind == "added" || e.kind == "removed";
    std::vector<SocialEvent> now_events;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (SocialEvent& e : events) {
            const LocationInfo info = parse_location(e.location);
            if (info.valid && !m_world_names.count(info.world_id)) {
                m_waiting.emplace_back(std::chrono::steady_clock::now(), std::move(e));
                const auto queued = std::find(m_world_queue.begin(), m_world_queue.end(), info.world_id);
                if (queued != m_world_queue.end()) m_world_queue.erase(queued);
                m_world_queue.insert(m_world_queue.begin(), info.world_id);
            } else {
                now_events.push_back(std::move(e));
            }
        }
    }
    push_events(now_events);
    if (names_changed) post([this] { save_friend_names(); });
    m_wake.notify_all();
}

void VrchatApi::fetch_groups() {
    const Reply r = request("GET", "/favorite/groups?n=100");
    Value v;
    if (r.status != 200 || !parse_json(r.body, v)) return;
    std::vector<FavoriteGroup> groups;
    for (const Value& g : v.items()) groups.push_back({ str(g, "type"), str(g, "name"), str(g, "displayName") });
    std::lock_guard<std::mutex> lock(m_mutex);
    m_groups = groups;
}

std::vector<FavoriteGroup> VrchatApi::favorite_groups(const std::string& type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FavoriteGroup> out;
    for (const FavoriteGroup& g : m_groups) {
        if (type.empty() || g.type == type) out.push_back(g);
    }
    return out;
}

void VrchatApi::load_collection(const std::string& kind) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_collection_status[kind] = "Loading...";
    }
    post([this, kind] { fetch_collection(kind); });
}

void VrchatApi::fetch_collection(const std::string& kind) {
    std::vector<CollectionRow> rows;
    std::string error;
    auto paged = [&](const std::string& path, const std::function<void(const Value&)>& each, bool wrapped) {
        for (int page = 0; page < 10; ++page) {
            const char sep = path.find('?') == std::string::npos ? '?' : '&';
            const Reply r = request("GET", path + sep + "n=100&offset=" + std::to_string(page * 100));
            Value v;
            if (r.status != 200 || !parse_json(r.body, v)) {
                if (error.empty()) error = r.error;
                return;
            }
            const Value& list = wrapped ? v["data"] : v;
            for (const Value& item : list.items()) each(item);
            if (list.size() < 100) return;
        }
    };
    if (kind == "avatars" || kind == "worlds" || kind == "friends") {
        const std::string type = kind == "avatars" ? "avatar" : (kind == "worlds" ? "world" : "friend");
        std::map<std::string, std::pair<std::string, std::string>> names;
        if (kind == "avatars") paged("/avatars/favorites", [&](const Value& a) { names[str(a, "id")] = { str(a, "name"), str(a, "authorName") }; }, false);
        if (kind == "worlds") paged("/worlds/favorites", [&](const Value& w) { names[str(w, "id")] = { str(w, "name"), str(w, "authorName") }; }, false);
        paged("/favorites?type=" + type, [&](const Value& f) {
            CollectionRow row;
            row.kind = kind;
            row.id = str(f, "id");
            row.target_id = str(f, "favoriteId");
            row.group = first_tag(f);
            const auto it = names.find(row.target_id);
            if (it != names.end()) {
                row.name = it->second.first;
                if (!it->second.second.empty()) row.detail = "by " + it->second.second;
            }
            if (kind == "friends") {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (const VrcUser* u = m_book.find(row.target_id)) {
                    row.name = u->name;
                    row.detail = status_label(u->state == "offline" ? std::string("offline") : u->status);
                }
            }
            if (row.name.empty()) row.name = row.target_id;
            rows.push_back(row);
        }, false);
        if (kind == "worlds") {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& n : names) m_world_names[n.first] = n.second.first;
        }
        fetch_groups();
    } else if (kind == "stickers" || kind == "emojis" || kind == "items") {
        const std::string type = kind == "stickers" ? "sticker" : (kind == "emojis" ? "emoji" : "prop");
        paged("/inventory?types=" + type, [&](const Value& i) {
            CollectionRow row;
            row.kind = kind;
            row.id = str(i, "id");
            row.target_id = row.id;
            row.name = str(i, "name");
            row.detail = str(i, "itemTypeLabel");
            row.archived = i["isArchived"].as_bool(false);
            if (row.name.empty()) row.name = row.id;
            rows.push_back(row);
        }, true);
        std::vector<std::string> tags;
        if (kind == "stickers") tags = { "sticker" };
        if (kind == "emojis") tags = { "emoji", "emojianimated" };
        for (const std::string& tag : tags) {
            paged("/files?tag=" + tag, [&](const Value& f) {
                CollectionRow row;
                row.kind = kind;
                row.id = str(f, "id");
                row.target_id = row.id;
                row.name = str(f, "name");
                row.detail = tag == "emojianimated" ? "Your own animated upload" : "Your own upload";
                row.custom = true;
                if (row.name.empty()) row.name = row.id;
                rows.push_back(row);
            }, false);
        }
    } else {
        error = "Pollen doesn't know about " + kind;
    }
    std::sort(rows.begin(), rows.end(), [](const CollectionRow& a, const CollectionRow& b) {
        if (a.group != b.group) return a.group < b.group;
        return a.name < b.name;
    });
    std::lock_guard<std::mutex> lock(m_mutex);
    m_collections[kind] = rows;
    m_collection_status[kind] = error.empty() ? std::to_string(rows.size()) + " found" : "Couldn't load everything: " + error;
    ++m_account.collection_revision;
}

std::vector<CollectionRow> VrchatApi::collection(const std::string& kind) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_collections.find(kind);
    return it == m_collections.end() ? std::vector<CollectionRow>() : it->second;
}

std::string VrchatApi::collection_status(const std::string& kind) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_collection_status.find(kind);
    return it == m_collection_status.end() ? std::string() : it->second;
}

void VrchatApi::act(const std::vector<std::string>& args) {
    if (args.empty()) return;
    note("Working...");
    post([this, args] { do_action(args); });
}

void VrchatApi::do_action(const std::vector<std::string>& a) {
    auto arg = [&](std::size_t i) { return i < a.size() ? a[i] : std::string(); };
    const std::string action = arg(0);
    auto fav_type = [](const std::string& kind) { return kind == "avatars" ? std::string("avatar") : (kind == "worlds" ? std::string("world") : std::string("friend")); };
    auto done = [&](const Reply& r, const std::string& ok, const std::string& kind) {
        if (r.status >= 200 && r.status < 300) {
            note(ok);
            if (!kind.empty()) fetch_collection(kind);
            return true;
        }
        note("That didn't work: " + r.error);
        return false;
    };
    if (action == "fav-remove") {
        done(request("DELETE", "/favorites/" + form_encode(arg(1))), "Removed from favorites", arg(2));
    } else if (action == "fav-move") {
        const std::string kind = arg(1);
        Reply r = request("DELETE", "/favorites/" + form_encode(arg(2)));
        if (r.status < 200 || r.status >= 300) {
            done(r, std::string(), std::string());
            return;
        }
        const std::string body = "{\"type\":" + json_string(fav_type(kind)) + ",\"favoriteId\":" + json_string(arg(3)) + ",\"tags\":[" + json_string(arg(4)) + "]}";
        done(request("POST", "/favorites", body), "Moved", kind);
    } else if (action == "fav-add-friend") {
        const std::string group = arg(2).empty() ? std::string("group_0") : arg(2);
        const std::string body = "{\"type\":\"friend\",\"favoriteId\":" + json_string(arg(1)) + ",\"tags\":[" + json_string(group) + "]}";
        done(request("POST", "/favorites", body), "Added to your favorite friends", std::string());
    } else if (action == "inv-archive") {
        const bool on = arg(2) == "1";
        done(request("PUT", "/inventory/" + form_encode(arg(1)), std::string("{\"isArchived\":") + (on ? "true" : "false") + "}"), on ? "Archived" : "Brought back from the archive", arg(3));
    } else if (action == "inv-delete") {
        done(request("DELETE", "/inventory/" + form_encode(arg(1))), "Deleted", arg(2));
    } else if (action == "file-delete") {
        done(request("DELETE", "/file/" + form_encode(arg(1))), "Deleted your upload", arg(2));
    } else if (action == "request-invite") {
        done(request("POST", "/requestInvite/" + form_encode(arg(1)), "{}"), "Asked for an invite", std::string());
    } else if (action == "invite") {
        if (arg(2).empty() || !parse_location(arg(2)).valid) {
            note("Pollen doesn't know which instance you're in yet");
            return;
        }
        done(request("POST", "/invite/" + form_encode(arg(1)), "{\"instanceId\":" + json_string(arg(2)) + "}"), "Invite sent", std::string());
    } else if (action == "unfriend") {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_removing = arg(1);
        }
        const Reply r = request("DELETE", "/auth/user/friends/" + form_encode(arg(1)));
        if (done(r, "Removed from your friends", std::string())) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_book.forget(arg(1));
                ++m_account.friends_revision;
            }
            save_friend_names();
        }
    } else {
        note("Pollen doesn't know how to " + action);
    }
}

AccountSnapshot VrchatApi::account() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_account;
}

std::vector<VrcUser> VrchatApi::friends() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<VrcUser> out;
    for (const auto& f : m_book.friends()) out.push_back(f.second);
    return out;
}

bool VrchatApi::is_friend(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_book.is_friend(id);
}

void VrchatApi::run() {
    while (m_running.load()) {
        std::function<void()> job;
        std::string world;
        bool retry_pipeline = false;
        bool refresh = false;
        bool device = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::seconds(1), [this] { return !m_running.load() || !m_jobs.empty(); });
            if (!m_running.load()) break;
            const auto now = std::chrono::steady_clock::now();
            if (!m_jobs.empty()) {
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            } else if (m_account.state == LoginState::WaitingForApproval) {
                device = true;
            } else if (m_account.state == LoginState::LoggedIn) {
                if (!m_pipeline && m_pipeline_retry != std::chrono::steady_clock::time_point() && now >= m_pipeline_retry) retry_pipeline = true;
                else if (m_next_friends != std::chrono::steady_clock::time_point() && now >= m_next_friends) refresh = true;
                else if (!m_world_queue.empty()) {
                    world = m_world_queue.front();
                    m_world_queue.erase(m_world_queue.begin());
                    if (m_world_names.count(world)) world.clear();
                }
            }
            if (m_pipeline && !m_pipeline->alive) {
                m_pipeline.reset();
            }
        }
        release_waiting(false);
        if (job) job();
        else if (device) poll_device_code();
        else if (retry_pipeline) open_pipeline();
        else if (refresh) fetch_friends();
        else if (!world.empty()) fetch_world(world);
    }
}

} // namespace pollen
