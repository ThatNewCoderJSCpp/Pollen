#include "osc/query.hpp"
#include "osc/socket.hpp"

#include <fizmo/../networking.hpp>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <thread>

namespace osc {
namespace query {

namespace net = fizmo::networking::core;

namespace {

std::string url_decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string status_text(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default: return "Error";
    }
}

std::string response(int status, const std::string& body, const std::string& type = "application/json") {
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + status_text(status) + "\r\n";
    out += "Content-Type: " + type + "; charset=utf-8\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Connection: close\r\n\r\n";
    return out + body;
}

bool send_all(net::TCPSocket& s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = s.send(data.data() + sent, data.size() - sent);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

struct Server::Impl {
    Tree tree;
    mutable std::mutex mutex;
    HostInfo info;
    std::unique_ptr<net::TCPSocket> listener;
    std::thread thread;
    std::atomic<bool> running{ false };
    std::uint16_t port = 0;

    std::string answer(const std::string& request_line) {
        std::size_t a = request_line.find(' ');
        std::size_t b = a == std::string::npos ? std::string::npos : request_line.find(' ', a + 1);
        if (a == std::string::npos) return response(400, "{\"ERROR\":\"bad request\"}");
        const std::string method = request_line.substr(0, a);
        std::string target = request_line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        if (method != "GET" && method != "HEAD") return response(405, "{\"ERROR\":\"only GET\"}");
        std::string query;
        const std::size_t q = target.find('?');
        if (q != std::string::npos) {
            query = target.substr(q + 1);
            target = target.substr(0, q);
        }
        const std::size_t hash = target.find('#');
        if (hash != std::string::npos) target = target.substr(0, hash);
        target = url_decode(target);
        if (target.empty()) target = "/";
        if (query == "HOST_INFO") {
            std::lock_guard<std::mutex> lock(mutex);
            return response(200, host_info_to_json(info));
        }
        std::string attribute;
        if (!query.empty()) {
            attribute = query;
            for (char& c : attribute) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (attribute != "VALUE" && attribute != "TYPE" && attribute != "ACCESS" && attribute != "DESCRIPTION" && attribute != "CONTENTS" && attribute != "FULL_PATH") return response(400, "{\"ERROR\":\"unknown attribute\"}");
        }
        const std::optional<std::string> body = tree.json(target, attribute);
        if (!body) {
            if (!attribute.empty() && tree.json(target)) return response(204, std::string());
            return response(404, "{\"ERROR\":\"no such OSC path\"}");
        }
        return response(200, *body);
    }

    void serve(std::unique_ptr<net::TCPSocket> client) {
        client->set_blocking(true);
        client->set_log_all_errors(false);
        client->set_receive_timeout(1000);
        std::string request;
        char buf[2048];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
            const int n = client->receive(buf, sizeof(buf));
            if (n <= 0) break;
            request.append(buf, static_cast<std::size_t>(n));
        }
        const std::size_t eol = request.find("\r\n");
        if (eol == std::string::npos) return;
        const std::string reply = answer(request.substr(0, eol));
        send_all(*client, reply);
        client->shutdown();
        client->close();
    }

    void run() {
        while (running.load()) {
            std::unique_ptr<net::TCPSocket> client = listener->accept();
            if (!client) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            serve(std::move(client));
        }
    }
};

Server::Server() : m(new Impl) {}

Server::~Server() {
    stop();
}

bool Server::start(const HostInfo& info, const std::string& http_host, std::uint16_t http_port, std::string* error) {
    stop();
    std::string why;
    const std::string host = resolve_host(http_host, &why);
    if (host.empty()) {
        if (error) *error = why;
        return false;
    }
    auto socket = std::make_unique<net::TCPSocket>(net::AddressFamily::IPv4);
    socket->set_log_all_errors(false);
#if !defined(_WIN32)
    int yes = 1;
    socket->set_option(SOL_SOCKET, SO_REUSEADDR, yes);
#endif
    if (!socket->bind(net::NetworkAddress(host, http_port, net::AddressFamily::IPv4)) || !socket->listen(16)) {
        if (error) *error = "couldn't start the OSCQuery server on " + Endpoint(host, http_port).to_string();
        return false;
    }
    socket->set_blocking(false);
    m->port = socket->get_local_address().port();
    m->listener = std::move(socket);
    set_host_info(info);
    m->running.store(true);
    Impl* impl = m.get();
    m->thread = std::thread([impl] { impl->run(); });
    return true;
}

void Server::stop() {
    if (!m->running.exchange(false)) return;
    if (m->thread.joinable()) m->thread.join();
    if (m->listener) m->listener->close();
    m->listener.reset();
    m->port = 0;
}

bool Server::running() const noexcept {
    return m->running.load();
}

std::uint16_t Server::port() const noexcept {
    return m->port;
}

HostInfo Server::host_info() const {
    std::lock_guard<std::mutex> lock(m->mutex);
    return m->info;
}

void Server::set_host_info(const HostInfo& info) {
    std::lock_guard<std::mutex> lock(m->mutex);
    m->info = info;
}

Tree& Server::tree() noexcept {
    return m->tree;
}

HttpResult http_get(const std::string& host, std::uint16_t port, const std::string& target, std::chrono::milliseconds timeout) {
    HttpResult result;
    std::string why;
    const std::string ip = resolve_host(host, &why);
    if (ip.empty()) {
        result.error = why;
        return result;
    }
    net::TCPSocket socket(net::AddressFamily::IPv4);
    socket.set_log_all_errors(false);
    socket.set_connect_timeout(static_cast<unsigned>(timeout.count()));
    socket.set_receive_timeout(static_cast<unsigned>(timeout.count()));
    socket.set_send_timeout(static_cast<unsigned>(timeout.count()));
    if (!socket.connect(net::NetworkAddress(ip, port, net::AddressFamily::IPv4))) {
        result.error = "couldn't connect to " + Endpoint(ip, port).to_string();
        return result;
    }
    const std::string request = "GET " + target + " HTTP/1.1\r\nHost: " + ip + ":" + std::to_string(port) + "\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
    if (!send_all(socket, request)) {
        result.error = "couldn't send the request to " + Endpoint(ip, port).to_string();
        return result;
    }
    std::string reply;
    char buf[8192];
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t length = std::string::npos;
    std::size_t header_end = std::string::npos;
    while (std::chrono::steady_clock::now() < deadline && reply.size() < (16u << 20)) {
        const int n = socket.receive(buf, sizeof(buf));
        if (n <= 0) break;
        reply.append(buf, static_cast<std::size_t>(n));
        if (header_end == std::string::npos) {
            header_end = reply.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string headers = reply.substr(0, header_end);
                for (char& c : headers) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const std::size_t at = headers.find("content-length:");
                if (at != std::string::npos) length = static_cast<std::size_t>(std::strtoull(headers.c_str() + at + 15, nullptr, 10));
            }
        }
        if (header_end != std::string::npos && length != std::string::npos && reply.size() >= header_end + 4 + length) break;
    }
    socket.close();
    if (reply.compare(0, 5, "HTTP/") != 0) {
        result.error = reply.empty() ? std::string("no answer from ") + Endpoint(ip, port).to_string() : std::string("not an HTTP answer");
        return result;
    }
    const std::size_t sp = reply.find(' ');
    result.status = sp == std::string::npos ? 0 : std::atoi(reply.c_str() + sp + 1);
    if (header_end == std::string::npos) header_end = reply.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        result.body = reply.substr(header_end + 4);
        if (length != std::string::npos && result.body.size() > length) result.body.resize(length);
    }
    std::string headers = reply.substr(0, header_end == std::string::npos ? reply.size() : header_end);
    for (char& c : headers) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (headers.find("transfer-encoding: chunked") != std::string::npos) {
        std::string body;
        std::size_t pos = 0;
        while (pos < result.body.size()) {
            const std::size_t eol = result.body.find("\r\n", pos);
            if (eol == std::string::npos) break;
            const std::size_t n = static_cast<std::size_t>(std::strtoull(result.body.substr(pos, eol - pos).c_str(), nullptr, 16));
            if (n == 0) break;
            body += result.body.substr(eol + 2, n);
            pos = eol + 2 + n + 2;
        }
        result.body = body;
    }
    if (!result.ok() && result.error.empty()) result.error = "HTTP " + std::to_string(result.status);
    return result;
}

} // namespace query
} // namespace osc
