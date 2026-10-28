#ifndef OSC_VRCHAT_QUERY_LINK_HPP
#define OSC_VRCHAT_QUERY_LINK_HPP

#include "../query.hpp"
#include "avatar.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace osc {
namespace vrchat {

struct VrchatService {
    std::string name;
    std::string osc_ip;
    std::uint16_t osc_port = 0;
    std::string http_ip;
    std::uint16_t http_port = 0;
};

struct AvatarSnapshot {
    std::string avatar_id;
    AvatarConfig config;
    std::vector<std::pair<std::string, Value>> values;
};

std::optional<AvatarSnapshot> avatar_from_query(const query::Node& avatar_node);
std::string random_instance_name(const std::string& app);

class QueryLink {
public:
    QueryLink();
    ~QueryLink();

    QueryLink(const QueryLink&) = delete;
    QueryLink& operator=(const QueryLink&) = delete;

    bool start(const std::string& app_name, const std::string& osc_ip, std::uint16_t osc_port, std::string* error = nullptr);
    void stop();
    bool running() const noexcept { return m_running.load(); }

    const std::string& instance() const noexcept { return m_instance; }
    std::uint16_t http_port() const noexcept { return m_server.port(); }
    bool advertising() const noexcept { return m_mdns.running(); }
    std::string mdns_error() const { return m_mdns.last_error(); }

    std::optional<VrchatService> vrchat() const;
    std::uint64_t vrchat_revision() const noexcept { return m_vrchat_revision.load(); }

    void request_avatar();
    std::optional<AvatarSnapshot> take_avatar();

    std::optional<AvatarSnapshot> fetch_avatar(std::string* error = nullptr) const;

    query::Server& server() noexcept { return m_server; }
    query::Mdns& mdns() noexcept { return m_mdns; }

private:
    void run();
    void check(const query::Service& service);

    query::Server m_server;
    query::Mdns m_mdns;
    std::string m_instance;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<query::Service> m_pending;
    std::optional<VrchatService> m_vrchat;
    std::atomic<std::uint64_t> m_vrchat_revision{ 0 };
    bool m_want_avatar = false;
    std::optional<AvatarSnapshot> m_avatar;
};

} // namespace vrchat
} // namespace osc

#endif // OSC_VRCHAT_QUERY_LINK_HPP
