#include "pollen/settings.hpp"

#include <fizmo/../../Source/Util Hpp/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pollen {

namespace fs = std::filesystem;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

int port_or(const fizmo::json::Value& v, int fallback) {
    if (!v.is_number()) return fallback;
    const double d = v.as_number();
    if (d < 0 || d > 65535) return fallback;
    return static_cast<int>(d);
}

} // namespace

osc::vrchat::ConnectionSettings Settings::connection() const {
    osc::vrchat::ConnectionSettings c;
    c.host = host.empty() ? std::string("127.0.0.1") : host;
    c.send_port = static_cast<std::uint16_t>(send_port);
    c.receive_port = static_cast<std::uint16_t>(receive_port);
    c.listen_host = listen_host.empty() ? std::string("127.0.0.1") : listen_host;
    if (!osc_folder.empty()) c.extra_osc_folders.emplace_back(osc_folder);
    return c;
}

fs::path Settings::config_dir() {
    const std::string over = env("POLLEN_CONFIG_DIR");
    if (!over.empty()) return over;
#if defined(_WIN32)
    const std::string appdata = env("APPDATA");
    if (!appdata.empty()) return fs::path(appdata) / "Pollen";
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    if (!home.empty()) return fs::path(home) / "Library" / "Application Support" / "Pollen";
#else
    const std::string xdg = env("XDG_CONFIG_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "pollen";
    const std::string home = env("HOME");
    if (!home.empty()) return fs::path(home) / ".config" / "pollen";
#endif
    return fs::current_path() / "pollen-config";
}

fs::path Settings::default_path() {
    return config_dir() / "settings.json";
}

std::string Settings::to_json() const {
    fizmo::json::Value root = fizmo::json::Value::object();
    root.set("host", host);
    root.set("send_port", send_port);
    root.set("receive_port", receive_port);
    root.set("listen_host", listen_host);
    root.set("osc_folder", osc_folder);
    root.set("auto_connect", auto_connect);
    root.set("oscquery", oscquery);
    root.set("status_enabled", status_enabled);
    root.set("status_template", status_template);
    root.set("status_interval", status_interval);
    root.set("status_notify", status_notify);
    root.set("log_parameters", log_parameters);
    fizmo::json::Value mods = fizmo::json::Value::object();
    for (const auto& m : modules) {
        fizmo::json::Value opts = fizmo::json::Value::object();
        for (const auto& o : m.second) opts.set(o.first, o.second);
        mods.set(m.first, opts);
    }
    root.set("modules", mods);
    fizmo::json::Value templates = fizmo::json::Value::object();
    for (const auto& t : module_templates) templates.set(t.first, t.second);
    root.set("module_templates", templates);
    return root.dump(2) + "\n";
}

bool Settings::from_json(const std::string& text, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(text, root, &why) || !root.is_object()) {
        if (error) *error = why.empty() ? std::string("the settings file doesn't hold a JSON object") : why;
        return false;
    }
    host = root["host"].as_string(host);
    send_port = port_or(root["send_port"], send_port);
    receive_port = port_or(root["receive_port"], receive_port);
    listen_host = root["listen_host"].as_string(listen_host);
    osc_folder = root["osc_folder"].as_string(osc_folder);
    auto_connect = root["auto_connect"].as_bool(auto_connect);
    oscquery = root["oscquery"].as_bool(oscquery);
    status_enabled = root["status_enabled"].as_bool(status_enabled);
    status_template = root["status_template"].as_string(status_template);
    status_interval = root["status_interval"].as_number(status_interval);
    if (status_interval < 2.0) status_interval = 2.0;
    status_notify = root["status_notify"].as_bool(status_notify);
    log_parameters = root["log_parameters"].as_bool(log_parameters);
    for (const auto& m : root["modules"].members()) {
        for (const auto& o : m.second.members()) {
            if (o.second.is_string()) modules[m.first][o.first] = o.second.as_string();
            else if (o.second.is_bool()) modules[m.first][o.first] = o.second.as_bool() ? "1" : "0";
        }
    }
    for (const auto& t : root["module_templates"].members()) {
        if (t.second.is_string()) module_templates[t.first] = t.second.as_string();
    }
    return true;
}

bool Settings::load(const fs::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "couldn't open " + path.string();
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return from_json(text.str(), error);
}

bool Settings::save(const fs::path& path, std::string* error) const {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "couldn't write " + temp.string();
            return false;
        }
        out << to_json();
        if (!out) {
            if (error) *error = "couldn't write " + temp.string();
            return false;
        }
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(temp, path, ec);
    }
    if (ec) {
        if (error) *error = "couldn't replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace pollen
