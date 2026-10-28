#include "osc/socket.hpp"
#include "osc/codec.hpp"

#include <fizmo/../networking.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace osc {

namespace net = fizmo::networking::core;

namespace {

bool is_ipv4_literal(const std::string& host) {
    int dots = 0, digits = 0;
    for (char c : host) {
        if (c == '.') {
            if (digits == 0) return false;
            ++dots;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 3) return false;
        } else {
            return false;
        }
    }
    return dots == 3 && digits > 0;
}

std::string socket_error(const net::SocketBase& s, const std::string& what) {
    const net::SocketError e = s.last_error();
    if (e.is_success()) return what;
    std::string message = e.message();
    if (message.empty()) message = net::SocketError::code_to_string(e.code());
    return what + ": " + message;
}

} // namespace

std::string Endpoint::to_string() const {
    if (host.find(':') != std::string::npos) return "[" + host + "]:" + std::to_string(port);
    return host + ":" + std::to_string(port);
}

std::string resolve_host(const std::string& host, std::string* error) {
    if (host.empty() || host == "localhost") return "127.0.0.1";
    if (is_ipv4_literal(host) || host.find(':') != std::string::npos) return host;
    net::DNSResolver resolver;
    const net::DNSResult result = resolver.resolve(host, net::AddressFamily::IPv4);
    if (!result.is_success()) {
        if (error) *error = "couldn't find the address of " + host;
        return std::string();
    }
    return result.first_ipv4();
}

double Stats::seconds_since_last() const {
    if (!any) return -1.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - last).count();
}

struct Sender::Impl {
    mutable std::mutex mutex;
    net::UDPSocket socket{ net::AddressFamily::IPv4 };
    Endpoint target{ "127.0.0.1", 9000 };
    std::string resolved = "127.0.0.1";
    Stats stats;
    std::string error;
    std::vector<std::uint8_t> buffer;

    Impl() {
        socket.set_log_all_errors(false);
        socket.set_max_errors(8);
    }

    bool send(const std::vector<std::uint8_t>& bytes, std::size_t messages) {
        std::lock_guard<std::mutex> lock(mutex);
        if (target.port == 0) {
            error = "no port to send to";
            ++stats.errors;
            return false;
        }
        if (bytes.size() > max_udp_payload) {
            error = "the packet is " + std::to_string(bytes.size()) + " bytes, more than fits in one UDP datagram";
            ++stats.errors;
            return false;
        }
        const net::NetworkAddress to(resolved, target.port, net::AddressFamily::IPv4);
        const int sent = socket.send(bytes.data(), bytes.size(), to);
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            error = socket_error(socket, "couldn't send to " + target.to_string());
            ++stats.errors;
            return false;
        }
        ++stats.packets;
        stats.messages += messages;
        stats.bytes += bytes.size();
        stats.any = true;
        stats.last = std::chrono::steady_clock::now();
        return true;
    }
};

Sender::Sender() : m(new Impl) {}

Sender::Sender(Endpoint target) : m(new Impl) {
    set_target(std::move(target));
}

Sender::~Sender() = default;
Sender::Sender(Sender&&) noexcept = default;
Sender& Sender::operator=(Sender&&) noexcept = default;

bool Sender::set_target(Endpoint target, std::string* error) {
    std::string why;
    const std::string resolved = resolve_host(target.host, &why);
    std::lock_guard<std::mutex> lock(m->mutex);
    m->target = std::move(target);
    if (resolved.empty()) {
        m->error = why;
        if (error) *error = why;
        return false;
    }
    m->resolved = resolved;
    return true;
}

const Endpoint& Sender::target() const noexcept {
    return m->target;
}

bool Sender::send(const Message& message) {
    try {
        return m->send(message.encode(), 1);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m->mutex);
        m->error = e.what();
        ++m->stats.errors;
        return false;
    }
}

bool Sender::send(const Bundle& bundle) {
    try {
        return m->send(bundle.encode(), bundle.message_count());
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m->mutex);
        m->error = e.what();
        ++m->stats.errors;
        return false;
    }
}

bool Sender::send(const Packet& packet) {
    if (packet.is_bundle()) return send(packet.bundle());
    return send(packet.message());
}

bool Sender::send_bytes(const std::vector<std::uint8_t>& bytes) {
    return m->send(bytes, 0);
}

Stats Sender::stats() const {
    std::lock_guard<std::mutex> lock(m->mutex);
    return m->stats;
}

std::string Sender::last_error() const {
    std::lock_guard<std::mutex> lock(m->mutex);
    return m->error;
}

struct Receiver::Impl {
    std::unique_ptr<net::UDPSocket> socket;
    std::thread thread;
    std::atomic<bool> running{ false };
    std::uint16_t port = 0;
    std::string host;

    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<Received> queue;
    std::size_t limit = 8192;
    Callback callback;
    Stats stats;
    std::string error;

    void run() {
        std::vector<std::uint8_t> buffer(65536);
        while (running.load(std::memory_order_acquire)) {
            net::NetworkAddress from;
            const int n = socket->receive(buffer.data(), buffer.size(), from);
            if (!running.load(std::memory_order_acquire)) break;
            if (n <= 0) continue;
            const auto now = std::chrono::steady_clock::now();
            DecodeResult decoded = try_decode(buffer.data(), static_cast<std::size_t>(n));
            Callback cb;
            {
                std::lock_guard<std::mutex> lock(mutex);
                stats.any = true;
                stats.last = now;
                stats.bytes += static_cast<std::uint64_t>(n);
                if (!decoded.ok()) {
                    ++stats.errors;
                    error = "a packet from " + from.to_string() + " isn't OSC: " + decoded.error;
                    continue;
                }
                ++stats.packets;
                stats.messages += decoded.packet->message_count();
                cb = callback;
            }
            Received r;
            r.packet = std::move(*decoded.packet);
            r.from = Endpoint(from.host(), from.port());
            r.bytes = static_cast<std::size_t>(n);
            r.when = now;
            if (cb) cb(r);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (queue.size() >= limit) {
                    queue.pop_front();
                    ++stats.dropped;
                }
                queue.push_back(std::move(r));
            }
            ready.notify_all();
        }
    }
};

Receiver::Receiver() : m(new Impl) {}

Receiver::~Receiver() {
    stop();
}

bool Receiver::listen(std::uint16_t port, const std::string& host, std::string* error) {
    stop();
    std::string why;
    const std::string resolved = resolve_host(host, &why);
    if (resolved.empty()) {
        if (error) *error = why;
        std::lock_guard<std::mutex> lock(m->mutex);
        m->error = why;
        return false;
    }
    auto socket = std::make_unique<net::UDPSocket>(net::AddressFamily::IPv4);
    socket->set_log_all_errors(false);
    socket->set_max_errors(8);
    if (!socket->is_valid()) {
        why = socket_error(*socket, "couldn't make a UDP socket");
    } else if (!socket->bind(net::NetworkAddress(resolved, port, net::AddressFamily::IPv4))) {
        why = socket_error(*socket, "couldn't listen on " + Endpoint(resolved, port).to_string());
        why += " (is another OSC app already using port " + std::to_string(port) + "?)";
    }
    if (!why.empty()) {
        if (error) *error = why;
        std::lock_guard<std::mutex> lock(m->mutex);
        m->error = why;
        return false;
    }
    socket->set_receive_timeout(200);
    const std::uint16_t bound = socket->get_local_address().port();
    m->socket = std::move(socket);
    m->port = bound != 0 ? bound : port;
    m->host = resolved;
    {
        std::lock_guard<std::mutex> lock(m->mutex);
        m->error.clear();
    }
    m->running.store(true, std::memory_order_release);
    Impl* impl = m.get();
    m->thread = std::thread([impl] { impl->run(); });
    return true;
}

void Receiver::stop() {
    if (!m->running.exchange(false)) return;
    if (m->thread.joinable()) m->thread.join();
    if (m->socket) m->socket->close();
    m->socket.reset();
    m->port = 0;
    m->ready.notify_all();
}

bool Receiver::listening() const noexcept {
    return m->running.load(std::memory_order_acquire);
}

std::uint16_t Receiver::port() const noexcept {
    return m->port;
}

const std::string& Receiver::host() const noexcept {
    return m->host;
}

void Receiver::set_callback(Callback callback) {
    std::lock_guard<std::mutex> lock(m->mutex);
    m->callback = std::move(callback);
}

void Receiver::set_queue_limit(std::size_t limit) {
    std::lock_guard<std::mutex> lock(m->mutex);
    m->limit = limit == 0 ? 1 : limit;
    while (m->queue.size() > m->limit) {
        m->queue.pop_front();
        ++m->stats.dropped;
    }
}

std::vector<Received> Receiver::take(std::size_t max) {
    std::lock_guard<std::mutex> lock(m->mutex);
    std::vector<Received> out;
    const std::size_t n = m->queue.size() < max ? m->queue.size() : max;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(std::move(m->queue.front()));
        m->queue.pop_front();
    }
    return out;
}

std::size_t Receiver::poll(const Dispatcher& dispatcher, std::size_t max) {
    std::vector<Received> batch = take(max);
    for (const Received& r : batch) dispatcher.dispatch(r.packet);
    return batch.size();
}

bool Receiver::wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m->mutex);
    return m->ready.wait_for(lock, timeout, [this] { return !m->queue.empty() || !m->running.load(); }) && !m->queue.empty();
}

Stats Receiver::stats() const {
    std::lock_guard<std::mutex> lock(m->mutex);
    return m->stats;
}

std::string Receiver::last_error() const {
    std::lock_guard<std::mutex> lock(m->mutex);
    return m->error;
}

} // namespace osc
