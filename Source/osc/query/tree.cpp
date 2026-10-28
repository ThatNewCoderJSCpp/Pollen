#include "osc/query.hpp"

#include <fizmo/../../Source/Util Hpp/json.hpp>

namespace osc {
namespace query {

namespace {

std::vector<std::string> parts_of(std::string_view path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

fizmo::json::Value value_json(const Value& v) {
    if (v.is_bool()) return fizmo::json::Value(v.as_bool());
    if (v.is_integer()) return fizmo::json::Value(static_cast<double>(v.to_int64()));
    if (v.is_real()) return fizmo::json::Value(v.to_double());
    if (v.is_text()) return fizmo::json::Value(v.as_text());
    if (v.is_char()) return fizmo::json::Value(std::string(1, v.as_char()));
    return fizmo::json::Value();
}

fizmo::json::Value node_json(const Node& node) {
    fizmo::json::Value out = fizmo::json::Value::object();
    out.set("FULL_PATH", node.full_path);
    if (!node.description.empty()) out.set("DESCRIPTION", node.description);
    out.set("ACCESS", static_cast<int>(node.access));
    if (!node.type.empty()) out.set("TYPE", node.type);
    if (!node.value.empty()) {
        fizmo::json::Value values = fizmo::json::Value::array();
        for (const Value& v : node.value) values.push_back(value_json(v));
        out.set("VALUE", values);
    }
    if (!node.contents.empty()) {
        fizmo::json::Value contents = fizmo::json::Value::object();
        for (const auto& kv : node.contents) contents.set(kv.first, node_json(kv.second));
        out.set("CONTENTS", contents);
    }
    return out;
}

Value value_from(const fizmo::json::Value& j, char tag) {
    if (j.is_bool()) return Value(j.as_bool());
    if (j.is_number()) {
        if (tag == 'i') return Value(static_cast<std::int32_t>(j.as_int()));
        if (tag == 'h') return Value::int64(j.as_int());
        if (tag == 'd') return Value(j.as_number());
        if (tag == 'T' || tag == 'F') return Value(j.as_number() != 0.0);
        return Value(static_cast<float>(j.as_number()));
    }
    if (j.is_string()) return Value(j.as_string());
    return Value();
}

void node_from(const fizmo::json::Value& j, Node& node, const std::string& path, int depth) {
    node.full_path = j["FULL_PATH"].as_string(path);
    node.description = j["DESCRIPTION"].as_string();
    node.type = j["TYPE"].as_string();
    const int access = static_cast<int>(j["ACCESS"].as_number(0));
    node.access = access >= 0 && access <= 3 ? static_cast<Access>(access) : Access::None;
    std::size_t i = 0;
    for (const fizmo::json::Value& v : j["VALUE"].items()) {
        const char tag = i < node.type.size() ? node.type[i] : 'f';
        node.value.push_back(value_from(v, tag));
        ++i;
    }
    if (depth > 64) return;
    for (const auto& member : j["CONTENTS"].members()) {
        const std::string child_path = (path == "/" ? std::string("/") : path + "/") + member.first;
        node_from(member.second, node.contents[member.first], child_path, depth + 1);
    }
}

} // namespace

const Node* Node::find(std::string_view path) const {
    const Node* cur = this;
    for (const std::string& part : parts_of(path)) {
        const auto it = cur->contents.find(part);
        if (it == cur->contents.end()) return nullptr;
        cur = &it->second;
    }
    return cur;
}

void Node::for_each_leaf(const std::function<void(const Node&)>& fn) const {
    if (!type.empty()) fn(*this);
    for (const auto& kv : contents) kv.second.for_each_leaf(fn);
}

Tree::Tree() {
    m_root.full_path = "/";
    m_root.description = "root node";
}

void Tree::add(const std::string& path, const std::string& type, Access access, const std::string& description, std::vector<Value> value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Node* cur = &m_root;
    std::string full;
    for (const std::string& part : parts_of(path)) {
        full += "/" + part;
        Node& child = cur->contents[part];
        if (child.full_path.empty()) child.full_path = full;
        cur = &child;
    }
    cur->type = type;
    cur->access = access;
    cur->description = description;
    cur->value = std::move(value);
}

bool Tree::remove(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::vector<std::string> parts = parts_of(path);
    if (parts.empty()) return false;
    Node* cur = &m_root;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const auto it = cur->contents.find(parts[i]);
        if (it == cur->contents.end()) return false;
        cur = &it->second;
    }
    return cur->contents.erase(parts.back()) > 0;
}

bool Tree::set_value(const std::string& path, std::vector<Value> value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Node* cur = &m_root;
    for (const std::string& part : parts_of(path)) {
        const auto it = cur->contents.find(part);
        if (it == cur->contents.end()) return false;
        cur = &it->second;
    }
    cur->value = std::move(value);
    return true;
}

void Tree::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_root.contents.clear();
}

Node Tree::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_root;
}

std::optional<std::string> Tree::json(std::string_view path, std::string_view attribute) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const Node* node = m_root.find(path);
    if (!node) return std::nullopt;
    const fizmo::json::Value full = node_json(*node);
    if (attribute.empty()) return full.dump();
    fizmo::json::Value only = fizmo::json::Value::object();
    const std::string key(attribute);
    if (!full.has(key)) return std::nullopt;
    only.set(key, full[key]);
    return only.dump();
}

std::string node_to_json(const Node& node) {
    return node_json(node).dump();
}

std::optional<Node> node_from_json(std::string_view json, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json.data(), json.size(), root, &why) || !root.is_object()) {
        if (error) *error = why.empty() ? std::string("not an OSCQuery node") : why;
        return std::nullopt;
    }
    Node node;
    node_from(root, node, root["FULL_PATH"].as_string("/"), 0);
    return node;
}

std::string host_info_to_json(const HostInfo& info) {
    fizmo::json::Value out = fizmo::json::Value::object();
    out.set("NAME", info.name);
    fizmo::json::Value ext = fizmo::json::Value::object();
    ext.set("ACCESS", true);
    ext.set("CLIPMODE", false);
    ext.set("RANGE", false);
    ext.set("TYPE", true);
    ext.set("VALUE", true);
    out.set("EXTENSIONS", ext);
    out.set("OSC_IP", info.osc_ip);
    out.set("OSC_PORT", static_cast<int>(info.osc_port));
    out.set("OSC_TRANSPORT", info.osc_transport);
    return out.dump();
}

std::optional<HostInfo> host_info_from_json(std::string_view json, std::string* error) {
    fizmo::json::Value root;
    std::string why;
    if (!fizmo::json::parse(json.data(), json.size(), root, &why) || !root.is_object()) {
        if (error) *error = why.empty() ? std::string("not OSCQuery host info") : why;
        return std::nullopt;
    }
    HostInfo info;
    info.name = root["NAME"].as_string();
    info.osc_ip = root["OSC_IP"].as_string("127.0.0.1");
    const double port = root["OSC_PORT"].as_number(0);
    info.osc_port = port > 0 && port < 65536 ? static_cast<std::uint16_t>(port) : 0;
    info.osc_transport = root["OSC_TRANSPORT"].as_string("UDP");
    return info;
}

std::string type_tag_of(const Value& value) {
    if (value.is_bool()) return "T";
    if (value.is_array()) return "[]";
    return std::string(1, value.tag());
}

} // namespace query
} // namespace osc
