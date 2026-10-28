#include "osc/vrchat/query_link.hpp"
#include "osc/vrchat/vrchat.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

namespace osc {
namespace vrchat {

namespace {

constexpr const char* service_json = "_oscjson._tcp";
constexpr const char* service_osc = "_osc._udp";

bool is_vrchat(const query::Service& s) {
    return s.instance.compare(0, 14, "VRChat-Client-") == 0;
}

ParamType type_from_tags(const std::string& tags) {
    if (tags.empty()) return ParamType::Unknown;
    switch (tags[0]) {
    case 'T':
    case 'F': return ParamType::Bool;
    case 'i': return ParamType::Int;
    case 'f': return ParamType::Float;
    default: return ParamType::Unknown;
    }
}

} // namespace

std::optional<AvatarSnapshot> avatar_from_query(const query::Node& avatar_node) {
    AvatarSnapshot snap;
    if (const query::Node* change = avatar_node.find("change")) {
        if (!change->value.empty() && change->value[0].is_text()) snap.avatar_id = change->value[0].as_text();
    }
    snap.config.id = snap.avatar_id;
    const query::Node* params = avatar_node.find("parameters");
    if (!params) return snap;
    const std::size_t prefix = std::strlen(parameter_prefix);
    params->for_each_leaf([&](const query::Node& leaf) {
        if (leaf.full_path.size() <= prefix || leaf.full_path.compare(0, prefix, parameter_prefix) != 0) return;
        ParameterInfo info;
        info.name = leaf.full_path.substr(prefix);
        info.type = type_from_tags(leaf.type);
        info.output_address = leaf.full_path;
        if (leaf.access == query::Access::Write || leaf.access == query::Access::ReadWrite) info.input_address = leaf.full_path;
        snap.config.parameters.push_back(info);
        if (!leaf.value.empty()) snap.values.emplace_back(info.name, coerce_param_value(info.type, leaf.value[0]));
    });
    return snap;
}

std::string random_instance_name(const std::string& app) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned> dist(0, 0xFFFFFF);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06X", dist(gen));
    return app + "-" + buf;
}

QueryLink::QueryLink() = default;

QueryLink::~QueryLink() {
    stop();
}

bool QueryLink::start(const std::string& app_name, const std::string& osc_ip, std::uint16_t osc_port, std::string* error) {
    stop();
    m_instance = random_instance_name(app_name);
    query::HostInfo info;
    info.name = m_instance;
    info.osc_ip = osc_ip;
    info.osc_port = osc_port;
    query::Tree& tree = m_server.tree();
    tree.clear();
    tree.add("/avatar/change", "s", query::Access::Write, "the avatar VRChat switched to");
    tree.add("/avatar/parameters", "", query::Access::None, "avatar parameters");
    if (!m_server.start(info, osc_ip == "0.0.0.0" ? std::string("0.0.0.0") : std::string("127.0.0.1"), 0, error)) return false;
    std::string why;
    m_mdns.on_found = [this](const query::Service& s) {
        if (!is_vrchat(s) || s.type != service_json) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back(s);
        m_wake.notify_all();
    };
    if (m_mdns.start(&why)) {
        const std::string advertised = osc_ip == "0.0.0.0" ? std::string("127.0.0.1") : osc_ip;
        m_mdns.advertise(m_instance, service_json, m_server.port(), advertised);
        if (osc_port != 0) m_mdns.advertise(m_instance, service_osc, osc_port, advertised);
        m_mdns.browse(service_json);
    } else if (error) {
        *error = why;
    }
    m_running.store(true);
    m_thread = std::thread([this] { run(); });
    return true;
}

void QueryLink::stop() {
    if (m_running.exchange(false)) {
        m_wake.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    m_mdns.stop();
    m_server.stop();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.clear();
    m_vrchat.reset();
    m_avatar.reset();
    m_want_avatar = false;
}

std::optional<VrchatService> QueryLink::vrchat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_vrchat;
}

void QueryLink::request_avatar() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_want_avatar = true;
    m_wake.notify_all();
}

std::optional<AvatarSnapshot> QueryLink::take_avatar() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::optional<AvatarSnapshot> out = std::move(m_avatar);
    m_avatar.reset();
    return out;
}

std::optional<AvatarSnapshot> QueryLink::fetch_avatar(std::string* error) const {
    std::optional<VrchatService> v = vrchat();
    if (!v) {
        if (error) *error = "VRChat hasn't been found over OSCQuery yet";
        return std::nullopt;
    }
    const query::HttpResult r = query::http_get(v->http_ip, v->http_port, "/avatar");
    if (!r.ok()) {
        if (error) *error = r.error;
        return std::nullopt;
    }
    std::optional<query::Node> node = query::node_from_json(r.body, error);
    if (!node) return std::nullopt;
    return avatar_from_query(*node);
}

void QueryLink::check(const query::Service& s) {
    const std::string ip = s.address.empty() ? std::string("127.0.0.1") : s.address;
    const query::HttpResult r = query::http_get(ip, s.port, "/?HOST_INFO");
    if (!r.ok()) return;
    std::optional<query::HostInfo> info = query::host_info_from_json(r.body);
    if (!info) return;
    VrchatService v;
    v.name = info->name.empty() ? s.instance : info->name;
    v.osc_ip = info->osc_ip.empty() || info->osc_ip == "0.0.0.0" ? ip : info->osc_ip;
    v.osc_port = info->osc_port;
    v.http_ip = ip;
    v.http_port = s.port;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool same = m_vrchat && m_vrchat->http_ip == v.http_ip && m_vrchat->http_port == v.http_port && m_vrchat->osc_port == v.osc_port && m_vrchat->osc_ip == v.osc_ip;
        if (same) return;
        m_vrchat = v;
        m_want_avatar = true;
    }
    ++m_vrchat_revision;
}

void QueryLink::run() {
    while (m_running.load()) {
        std::vector<query::Service> pending;
        bool want = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(500), [this] { return !m_running.load() || !m_pending.empty() || m_want_avatar; });
            pending.swap(m_pending);
            want = m_want_avatar && m_vrchat.has_value();
            if (want) m_want_avatar = false;
        }
        if (!m_running.load()) break;
        for (const query::Service& s : pending) check(s);
        if (!want) {
            std::lock_guard<std::mutex> lock(m_mutex);
            want = m_want_avatar && m_vrchat.has_value();
            if (want) m_want_avatar = false;
        }
        if (want) {
            std::optional<AvatarSnapshot> snap = fetch_avatar();
            if (snap) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_avatar = std::move(snap);
            }
        }
    }
}

} // namespace vrchat
} // namespace osc
