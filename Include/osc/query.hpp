#ifndef OSC_QUERY_HPP
#define OSC_QUERY_HPP

#include "value.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osc {
namespace query {

enum class Access { None = 0, Read = 1, Write = 2, ReadWrite = 3 };

struct Node {
    std::string full_path;
    std::string description;
    std::string type;
    Access access = Access::None;
    std::vector<Value> value;
    std::map<std::string, Node> contents;

    bool is_leaf() const noexcept { return contents.empty() && !type.empty(); }
    const Node* find(std::string_view path) const;
    void for_each_leaf(const std::function<void(const Node&)>& fn) const;
};

struct HostInfo {
    std::string name;
    std::string osc_ip = "127.0.0.1";
    std::uint16_t osc_port = 0;
    std::string osc_transport = "UDP";
};

class Tree {
public:
    Tree();

    void add(const std::string& path, const std::string& type, Access access, const std::string& description = std::string(), std::vector<Value> value = {});
    bool remove(const std::string& path);
    bool set_value(const std::string& path, std::vector<Value> value);
    void clear();

    Node snapshot() const;
    std::optional<std::string> json(std::string_view path, std::string_view attribute = std::string_view()) const;

private:
    mutable std::mutex m_mutex;
    Node m_root;
};

std::string node_to_json(const Node& node);
std::optional<Node> node_from_json(std::string_view json, std::string* error = nullptr);
std::string host_info_to_json(const HostInfo& info);
std::optional<HostInfo> host_info_from_json(std::string_view json, std::string* error = nullptr);
std::string type_tag_of(const Value& value);

class Server {
public:
    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(const HostInfo& info, const std::string& http_host = "127.0.0.1", std::uint16_t http_port = 0, std::string* error = nullptr);
    void stop();
    bool running() const noexcept;
    std::uint16_t port() const noexcept;
    HostInfo host_info() const;
    void set_host_info(const HostInfo& info);

    Tree& tree() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

struct HttpResult {
    int status = 0;
    std::string body;
    std::string error;

    bool ok() const noexcept { return status >= 200 && status < 300; }
};

HttpResult http_get(const std::string& host, std::uint16_t port, const std::string& target, std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));

struct Service {
    std::string instance;
    std::string type;
    std::string host;
    std::string address;
    std::uint16_t port = 0;
    std::chrono::steady_clock::time_point seen;

    std::string key() const { return instance + "." + type; }
};

class Mdns {
public:
    Mdns();
    ~Mdns();

    Mdns(const Mdns&) = delete;
    Mdns& operator=(const Mdns&) = delete;

    bool start(std::string* error = nullptr);
    void stop();
    bool running() const noexcept;

    void advertise(const std::string& instance, const std::string& type, std::uint16_t port, const std::string& address = "127.0.0.1");
    void withdraw(const std::string& instance, const std::string& type);
    void browse(const std::string& type);

    std::vector<Service> services(const std::string& type = std::string()) const;
    std::string last_error() const;

    std::function<void(const Service&)> on_found;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

struct MdnsRecord {
    std::string name;
    std::uint16_t type = 0;
    std::uint32_t ttl = 0;
    std::string target;
    std::uint16_t port = 0;
    std::string address;
};

struct MdnsQuestion {
    std::string name;
    std::uint16_t type = 0;
};

struct MdnsMessage {
    bool response = false;
    std::vector<MdnsQuestion> questions;
    std::vector<MdnsRecord> records;
};

constexpr std::uint16_t mdns_type_a = 1;
constexpr std::uint16_t mdns_type_ptr = 12;
constexpr std::uint16_t mdns_type_txt = 16;
constexpr std::uint16_t mdns_type_srv = 33;
constexpr std::uint16_t mdns_type_any = 255;

std::vector<std::uint8_t> mdns_encode(const MdnsMessage& message);
std::optional<MdnsMessage> mdns_decode(const std::uint8_t* data, std::size_t size);
std::vector<Service> mdns_services(const MdnsMessage& message, const std::string& type);

} // namespace query
} // namespace osc

#endif // OSC_QUERY_HPP
