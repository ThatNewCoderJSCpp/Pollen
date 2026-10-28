#include "osc/vrchat/avatar.hpp"
#include "osc/vrchat/vrchat.hpp"

#include <fizmo/../../Source/Util Hpp/json.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace osc {
namespace vrchat {

namespace {

bool same_word(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

} // namespace

const char* to_string(ParamType type) noexcept {
    switch (type) {
    case ParamType::Bool: return "bool";
    case ParamType::Int: return "int";
    case ParamType::Float: return "float";
    case ParamType::Unknown: break;
    }
    return "unknown";
}

ParamType param_type_from(std::string_view name) noexcept {
    if (same_word(name, "bool") || same_word(name, "boolean") || same_word(name, "b")) return ParamType::Bool;
    if (same_word(name, "int") || same_word(name, "integer") || same_word(name, "i")) return ParamType::Int;
    if (same_word(name, "float") || same_word(name, "f")) return ParamType::Float;
    return ParamType::Unknown;
}

ParamType param_type_of(const Value& value) noexcept {
    if (value.is_bool()) return ParamType::Bool;
    if (value.is_integer()) return ParamType::Int;
    if (value.is_real()) return ParamType::Float;
    return ParamType::Unknown;
}

std::optional<Value> make_param_value(ParamType type, std::string_view text) {
    const std::string t = trimmed(text);
    if (t.empty()) return std::nullopt;
    switch (type) {
    case ParamType::Bool: {
        if (same_word(t, "true") || same_word(t, "on") || same_word(t, "yes") || t == "1") return Value(true);
        if (same_word(t, "false") || same_word(t, "off") || same_word(t, "no") || t == "0") return Value(false);
        return std::nullopt;
    }
    case ParamType::Int: {
        char* end = nullptr;
        const double d = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || !std::isfinite(d)) return std::nullopt;
        long r = std::lround(d);
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        return Value(static_cast<std::int32_t>(r));
    }
    case ParamType::Float: {
        char* end = nullptr;
        const double d = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || !std::isfinite(d)) return std::nullopt;
        double c = d < -1.0 ? -1.0 : (d > 1.0 ? 1.0 : d);
        return Value(static_cast<float>(c));
    }
    case ParamType::Unknown: break;
    }
    if (same_word(t, "true") || same_word(t, "false")) return Value(same_word(t, "true"));
    char* end = nullptr;
    const long long i = std::strtoll(t.c_str(), &end, 10);
    if (*end == '\0') return Value(static_cast<std::int32_t>(i));
    const double d = std::strtod(t.c_str(), &end);
    if (*end == '\0') return Value(static_cast<float>(d));
    return Value(t);
}

Value coerce_param_value(ParamType type, const Value& value) {
    switch (type) {
    case ParamType::Bool: return Value(value.to_bool());
    case ParamType::Int: {
        std::int64_t r = value.to_int64();
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        return Value(static_cast<std::int32_t>(r));
    }
    case ParamType::Float: {
        double d = value.to_double();
        if (d < -1.0) d = -1.0;
        if (d > 1.0) d = 1.0;
        return Value(static_cast<float>(d));
    }
    case ParamType::Unknown: break;
    }
    return value;
}

const ParameterInfo* AvatarConfig::find(std::string_view parameter) const noexcept {
    for (const ParameterInfo& p : parameters) {
        if (p.name == parameter) return &p;
    }
    return nullptr;
}

std::optional<AvatarConfig> parse_avatar_config(std::string_view json, std::string* error) {
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF && static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF) json.remove_prefix(3);
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json.data(), json.size(), root, &why)) {
        if (error) *error = "the avatar file isn't valid JSON: " + why;
        return std::nullopt;
    }
    if (!root.is_object()) {
        if (error) *error = "the avatar file doesn't hold a JSON object";
        return std::nullopt;
    }
    AvatarConfig config;
    config.id = root["id"].as_string();
    config.name = root["name"].as_string();
    for (const fizmo::json::Value& p : root["parameters"].items()) {
        ParameterInfo info;
        info.name = p["name"].as_string();
        if (info.name.empty()) continue;
        const fizmo::json::Value& in = p["input"];
        const fizmo::json::Value& out = p["output"];
        info.input_address = in["address"].as_string();
        info.output_address = out["address"].as_string();
        info.type = param_type_from(in["type"].as_string());
        if (info.type == ParamType::Unknown) info.type = param_type_from(out["type"].as_string());
        config.parameters.push_back(std::move(info));
    }
    return config;
}

std::optional<AvatarConfig> load_avatar_config(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "couldn't open " + path.string();
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    std::optional<AvatarConfig> config = parse_avatar_config(text.str(), error);
    if (config) config->path = path;
    return config;
}

bool AvatarState::handle(const Message& message) {
    const std::string& address = message.address();
    if (address == avatar_change_address) {
        set_avatar_id(message.to<std::string>(0));
        if (on_avatar_change) on_avatar_change(m_avatar_id);
        return true;
    }
    if (!is_parameter_address(address)) return false;
    if (message.empty()) return true;
    store(parameter_name(address), message[0]);
    return true;
}

void AvatarState::set_avatar_id(std::string id) {
    if (id == m_avatar_id) return;
    m_avatar_id = std::move(id);
    m_live.clear();
    if (m_config && m_config->id != m_avatar_id) m_config.reset();
    ++m_revision;
    ++m_value_revision;
}

void AvatarState::set_config(std::optional<AvatarConfig> config) {
    m_config = std::move(config);
    ++m_revision;
}

std::optional<Value> AvatarState::value(std::string_view parameter) const {
    const auto it = m_live.find(std::string(parameter));
    if (it == m_live.end()) return std::nullopt;
    return it->second.value;
}

void AvatarState::remember_sent(std::string_view parameter, const Value& value) {
    store(std::string(parameter), value);
}

void AvatarState::store(std::string name, const Value& value) {
    auto it = m_live.find(name);
    const bool fresh = it == m_live.end();
    if (fresh) {
        it = m_live.emplace(std::move(name), Live{}).first;
        ++m_revision;
    }
    Live& live = it->second;
    if (fresh || live.value != value) {
        live.value = value;
        live.type = param_type_of(value);
        ++live.changes;
        ++m_value_revision;
    }
    live.updated = std::chrono::steady_clock::now();
}

void AvatarState::clear() {
    m_avatar_id.clear();
    m_config.reset();
    m_live.clear();
    ++m_revision;
    ++m_value_revision;
}

} // namespace vrchat
} // namespace osc
