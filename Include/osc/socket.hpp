#ifndef OSC_SOCKET_HPP
#define OSC_SOCKET_HPP

#include "dispatcher.hpp"
#include "message.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace osc {

struct Endpoint {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;

    Endpoint() = default;
    Endpoint(std::string h, std::uint16_t p) : host(std::move(h)), port(p) {}

    static Endpoint local(std::uint16_t port) { return Endpoint("127.0.0.1", port); }

    std::string to_string() const;

    bool operator==(const Endpoint& o) const noexcept { return host == o.host && port == o.port; }
    bool operator!=(const Endpoint& o) const noexcept { return !(*this == o); }
};

std::string resolve_host(const std::string& host, std::string* error = nullptr);

struct Received {
    Packet packet;
    Endpoint from;
    std::size_t bytes = 0;
    std::chrono::steady_clock::time_point when;
};

struct Stats {
    std::uint64_t packets = 0;
    std::uint64_t messages = 0;
    std::uint64_t bytes = 0;
    std::uint64_t errors = 0;
    std::uint64_t dropped = 0;
    bool any = false;
    std::chrono::steady_clock::time_point last;

    double seconds_since_last() const;
};

class Sender {
public:
    Sender();
    explicit Sender(Endpoint target);
    ~Sender();

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&&) noexcept;
    Sender& operator=(Sender&&) noexcept;

    bool set_target(Endpoint target, std::string* error = nullptr);
    const Endpoint& target() const noexcept;

    bool send(const Message& message);
    bool send(const Bundle& bundle);
    bool send(const Packet& packet);
    bool send_bytes(const std::vector<std::uint8_t>& bytes);

    template <typename First, typename... Rest>
    bool send(std::string address, First&& first, Rest&&... rest) {
        return send(Message(std::move(address), std::forward<First>(first), std::forward<Rest>(rest)...));
    }

    bool send(std::string address) { return send(Message(std::move(address))); }

    Stats stats() const;
    std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

class Receiver {
public:
    using Callback = std::function<void(const Received&)>;

    Receiver();
    ~Receiver();

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    bool listen(std::uint16_t port, const std::string& host = "127.0.0.1", std::string* error = nullptr);
    void stop();
    bool listening() const noexcept;
    std::uint16_t port() const noexcept;
    const std::string& host() const noexcept;

    void set_callback(Callback callback);
    void set_queue_limit(std::size_t limit);

    std::vector<Received> take(std::size_t max = std::numeric_limits<std::size_t>::max());
    std::size_t poll(const Dispatcher& dispatcher, std::size_t max = std::numeric_limits<std::size_t>::max());
    bool wait(std::chrono::milliseconds timeout);

    Stats stats() const;
    std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace osc

#endif // OSC_SOCKET_HPP
