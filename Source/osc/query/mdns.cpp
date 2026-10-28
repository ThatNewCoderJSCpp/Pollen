#include "osc/query.hpp"
#include "osc/socket.hpp"

#include <fizmo/../networking.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <thread>

namespace osc {
namespace query {

namespace net = fizmo::networking::core;

namespace {

constexpr const char* group = "224.0.0.251";
constexpr std::uint16_t mdns_port = 5353;
constexpr std::uint32_t record_ttl = 120;

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string trim_dot(std::string s) {
    while (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

class Writer {
public:
    std::vector<std::uint8_t> out;

    void u8(std::uint8_t v) { out.push_back(v); }
    void u16(std::uint16_t v) {
        out.push_back(std::uint8_t(v >> 8));
        out.push_back(std::uint8_t(v));
    }
    void u32(std::uint32_t v) {
        u16(std::uint16_t(v >> 16));
        u16(std::uint16_t(v));
    }
    void name(const std::string& n) {
        std::string label;
        const std::string full = trim_dot(n);
        for (std::size_t i = 0; i <= full.size(); ++i) {
            if (i == full.size() || full[i] == '.') {
                const std::size_t len = std::min<std::size_t>(label.size(), 63);
                u8(static_cast<std::uint8_t>(len));
                out.insert(out.end(), label.begin(), label.begin() + static_cast<std::ptrdiff_t>(len));
                label.clear();
            } else {
                label += full[i];
            }
        }
        u8(0);
    }
};

class Reader {
public:
    Reader(const std::uint8_t* d, std::size_t n) : data(d), size(n) {}

    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
    bool bad = false;

    std::uint8_t u8() {
        if (pos + 1 > size) {
            bad = true;
            return 0;
        }
        return data[pos++];
    }
    std::uint16_t u16() {
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>((hi << 8) | u8());
    }
    std::uint32_t u32() {
        const std::uint32_t hi = u16();
        return (hi << 16) | u16();
    }
    std::string name_at(std::size_t& at, int depth) {
        std::string out;
        int jumps = 0;
        bool jumped = false;
        std::size_t p = at;
        while (true) {
            if (p >= size) {
                bad = true;
                return out;
            }
            const std::uint8_t len = data[p];
            if (len == 0) {
                ++p;
                break;
            }
            if ((len & 0xC0) == 0xC0) {
                if (p + 1 >= size || ++jumps > 32 || depth > 8) {
                    bad = true;
                    return out;
                }
                const std::size_t target = (static_cast<std::size_t>(len & 0x3F) << 8) | data[p + 1];
                if (!jumped) at = p + 2;
                jumped = true;
                p = target;
                continue;
            }
            if (p + 1 + len > size) {
                bad = true;
                return out;
            }
            if (!out.empty()) out += '.';
            out.append(reinterpret_cast<const char*>(data + p + 1), len);
            p += 1 + len;
        }
        if (!jumped) at = p;
        return out;
    }
    std::string name() { return name_at(pos, 0); }
};

void write_record(Writer& w, const MdnsRecord& r) {
    w.name(r.name);
    w.u16(r.type);
    const bool unique = r.type != mdns_type_ptr;
    w.u16(static_cast<std::uint16_t>(unique ? 0x8001 : 0x0001));
    w.u32(r.ttl);
    Writer rdata;
    switch (r.type) {
    case mdns_type_ptr: rdata.name(r.target); break;
    case mdns_type_srv:
        rdata.u16(0);
        rdata.u16(0);
        rdata.u16(r.port);
        rdata.name(r.target);
        break;
    case mdns_type_txt: {
        const std::string txt = r.target.empty() ? std::string("txtvers=1") : r.target;
        rdata.u8(static_cast<std::uint8_t>(std::min<std::size_t>(txt.size(), 255)));
        rdata.out.insert(rdata.out.end(), txt.begin(), txt.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(txt.size(), 255)));
        break;
    }
    case mdns_type_a: {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (std::sscanf(r.address.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            rdata.u8(static_cast<std::uint8_t>(a));
            rdata.u8(static_cast<std::uint8_t>(b));
            rdata.u8(static_cast<std::uint8_t>(c));
            rdata.u8(static_cast<std::uint8_t>(d));
        }
        break;
    }
    default: break;
    }
    w.u16(static_cast<std::uint16_t>(rdata.out.size()));
    w.out.insert(w.out.end(), rdata.out.begin(), rdata.out.end());
}

} // namespace

std::vector<std::uint8_t> mdns_encode(const MdnsMessage& message) {
    Writer w;
    w.u16(0);
    w.u16(static_cast<std::uint16_t>(message.response ? 0x8400 : 0x0000));
    w.u16(static_cast<std::uint16_t>(message.questions.size()));
    w.u16(static_cast<std::uint16_t>(message.response ? message.records.size() : 0));
    w.u16(0);
    w.u16(static_cast<std::uint16_t>(message.response ? 0 : message.records.size()));
    for (const MdnsQuestion& q : message.questions) {
        w.name(q.name);
        w.u16(q.type);
        w.u16(1);
    }
    for (const MdnsRecord& r : message.records) write_record(w, r);
    return w.out;
}

std::optional<MdnsMessage> mdns_decode(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 12) return std::nullopt;
    Reader r(data, size);
    MdnsMessage m;
    r.u16();
    const std::uint16_t flags = r.u16();
    m.response = (flags & 0x8000) != 0;
    const std::uint16_t qd = r.u16(), an = r.u16(), ns = r.u16(), ar = r.u16();
    for (std::uint16_t i = 0; i < qd && !r.bad; ++i) {
        MdnsQuestion q;
        q.name = r.name();
        q.type = r.u16();
        r.u16();
        m.questions.push_back(std::move(q));
    }
    const std::size_t total = static_cast<std::size_t>(an) + ns + ar;
    for (std::size_t i = 0; i < total && !r.bad; ++i) {
        MdnsRecord rec;
        rec.name = r.name();
        rec.type = r.u16();
        r.u16();
        rec.ttl = r.u32();
        const std::uint16_t len = r.u16();
        if (r.bad || r.pos + len > size) return std::nullopt;
        const std::size_t end = r.pos + len;
        switch (rec.type) {
        case mdns_type_ptr: rec.target = r.name(); break;
        case mdns_type_srv:
            r.u16();
            r.u16();
            rec.port = r.u16();
            rec.target = r.name();
            break;
        case mdns_type_a:
            if (len == 4) {
                char buf[20];
                std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", data[r.pos], data[r.pos + 1], data[r.pos + 2], data[r.pos + 3]);
                rec.address = buf;
            }
            break;
        case mdns_type_txt:
            if (len > 1) rec.target.assign(reinterpret_cast<const char*>(data + r.pos + 1), std::min<std::size_t>(data[r.pos], len - 1));
            break;
        default: break;
        }
        r.pos = end;
        if (!r.bad) m.records.push_back(std::move(rec));
    }
    if (r.bad) return std::nullopt;
    return m;
}

std::vector<Service> mdns_services(const MdnsMessage& message, const std::string& type) {
    std::vector<Service> out;
    const std::string type_name = trim_dot(type) + ".local";
    for (const MdnsRecord& ptr : message.records) {
        if (ptr.type != mdns_type_ptr || !same_name(ptr.name, type_name)) continue;
        Service s;
        s.type = trim_dot(type);
        const std::string full = trim_dot(ptr.target);
        s.instance = full.size() > type_name.size() + 1 && same_name(full.substr(full.size() - type_name.size()), type_name) ? full.substr(0, full.size() - type_name.size() - 1) : full;
        for (const MdnsRecord& srv : message.records) {
            if (srv.type == mdns_type_srv && same_name(trim_dot(srv.name), full)) {
                s.port = srv.port;
                s.host = trim_dot(srv.target);
            }
        }
        for (const MdnsRecord& a : message.records) {
            if (a.type == mdns_type_a && !s.host.empty() && same_name(trim_dot(a.name), s.host)) s.address = a.address;
        }
        s.seen = std::chrono::steady_clock::now();
        out.push_back(std::move(s));
    }
    return out;
}

struct Mdns::Impl {
    struct Advert {
        std::string instance;
        std::string type;
        std::uint16_t port;
        std::string address;
        std::string host;
        int announced = 0;
        std::chrono::steady_clock::time_point next;
    };

    mutable std::mutex mutex;
    std::unique_ptr<net::UDPSocket> socket;
    std::thread thread;
    std::atomic<bool> running{ false };
    std::vector<Advert> adverts;
    std::vector<std::string> browsing;
    int browse_round = 0;
    std::chrono::steady_clock::time_point next_browse;
    std::map<std::string, std::set<std::string>> ptrs;
    std::map<std::string, std::pair<std::string, std::uint16_t>> srvs;
    std::map<std::string, std::string> hosts;
    std::map<std::string, std::string> sources;
    std::map<std::string, Service> services;
    std::string error;
    Mdns* owner = nullptr;

    void send(const MdnsMessage& m, const net::NetworkAddress& to) {
        const std::vector<std::uint8_t> bytes = mdns_encode(m);
        socket->send(bytes.data(), bytes.size(), to);
    }

    void send_group(const MdnsMessage& m) {
        send(m, net::NetworkAddress(group, mdns_port, net::AddressFamily::IPv4));
    }

    static std::string full_name(const Advert& a) { return a.instance + "." + a.type + ".local"; }

    void add_records(MdnsMessage& m, const Advert& a, std::uint32_t ttl, bool with_ptr) {
        if (with_ptr) {
            MdnsRecord ptr;
            ptr.name = a.type + ".local";
            ptr.type = mdns_type_ptr;
            ptr.ttl = ttl;
            ptr.target = full_name(a);
            m.records.push_back(ptr);
        }
        MdnsRecord srv;
        srv.name = full_name(a);
        srv.type = mdns_type_srv;
        srv.ttl = ttl;
        srv.port = a.port;
        srv.target = a.host;
        m.records.push_back(srv);
        MdnsRecord txt;
        txt.name = full_name(a);
        txt.type = mdns_type_txt;
        txt.ttl = ttl;
        txt.target = "txtvers=1";
        m.records.push_back(txt);
        MdnsRecord addr;
        addr.name = a.host;
        addr.type = mdns_type_a;
        addr.ttl = ttl;
        addr.address = a.address;
        m.records.push_back(addr);
    }

    void answer(const MdnsMessage& q, const net::NetworkAddress& from) {
        MdnsMessage reply;
        reply.response = true;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const MdnsQuestion& question : q.questions) {
                for (const Advert& a : adverts) {
                    const bool any = question.type == mdns_type_any;
                    if (same_name(trim_dot(question.name), a.type + ".local") && (any || question.type == mdns_type_ptr)) add_records(reply, a, record_ttl, true);
                    else if (same_name(trim_dot(question.name), full_name(a)) && (any || question.type == mdns_type_srv || question.type == mdns_type_txt)) add_records(reply, a, record_ttl, false);
                    else if (same_name(trim_dot(question.name), a.host) && (any || question.type == mdns_type_a)) add_records(reply, a, record_ttl, false);
                }
            }
        }
        if (reply.records.empty()) return;
        if (from.port() != mdns_port) send(reply, from);
        else send_group(reply);
    }

    void learn(const MdnsMessage& m, const std::string& source) {
        std::vector<Service> fresh;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const MdnsRecord& r : m.records) {
                const std::string name = trim_dot(r.name);
                if (r.type == mdns_type_ptr) {
                    if (r.ttl == 0) ptrs[name].erase(trim_dot(r.target));
                    else ptrs[name].insert(trim_dot(r.target));
                } else if (r.type == mdns_type_srv) {
                    srvs[name] = { trim_dot(r.target), r.port };
                    sources[name] = source;
                } else if (r.type == mdns_type_a && !r.address.empty()) {
                    hosts[name] = r.address;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            for (const std::string& type : browsing) {
                const std::string type_name = type + ".local";
                for (const auto& kv : ptrs) {
                    if (!same_name(kv.first, type_name)) continue;
                    for (const std::string& full : kv.second) {
                        const auto srv = srvs.find(full);
                        if (srv == srvs.end()) continue;
                        Service s;
                        s.type = type;
                        s.instance = full.size() > type_name.size() + 1 ? full.substr(0, full.size() - type_name.size() - 1) : full;
                        s.host = srv->second.first;
                        s.port = srv->second.second;
                        const auto h = hosts.find(s.host);
                        s.address = h != hosts.end() ? h->second : sources[full];
                        s.seen = now;
                        const auto old = services.find(s.key());
                        const bool changed = old == services.end() || old->second.port != s.port || old->second.address != s.address;
                        services[s.key()] = s;
                        if (changed) fresh.push_back(s);
                    }
                }
            }
        }
        if (owner->on_found) {
            for (const Service& s : fresh) owner->on_found(s);
        }
    }

    void periodic() {
        const auto now = std::chrono::steady_clock::now();
        MdnsMessage announce;
        announce.response = true;
        MdnsMessage query;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (Advert& a : adverts) {
                if (now < a.next) continue;
                add_records(announce, a, record_ttl, true);
                ++a.announced;
                a.next = now + (a.announced < 3 ? std::chrono::seconds(a.announced) : std::chrono::seconds(60));
            }
            if (!browsing.empty() && now >= next_browse) {
                for (const std::string& type : browsing) query.questions.push_back(MdnsQuestion{ type + ".local", mdns_type_ptr });
                ++browse_round;
                const int wait = browse_round < 4 ? (1 << (browse_round - 1)) : 15;
                next_browse = now + std::chrono::seconds(wait);
            }
            for (auto it = services.begin(); it != services.end();) {
                if (now - it->second.seen > std::chrono::seconds(75)) it = services.erase(it);
                else ++it;
            }
        }
        if (!announce.records.empty()) send_group(announce);
        if (!query.questions.empty()) send_group(query);
    }

    void run() {
        std::vector<std::uint8_t> buffer(9000);
        while (running.load()) {
            periodic();
            net::NetworkAddress from;
            const int n = socket->receive(buffer.data(), buffer.size(), from);
            if (n <= 0) continue;
            const std::optional<MdnsMessage> m = mdns_decode(buffer.data(), static_cast<std::size_t>(n));
            if (!m) continue;
            if (m->response) learn(*m, from.host());
            else answer(*m, from);
        }
    }
};

Mdns::Mdns() : m(new Impl) {
    m->owner = this;
}

Mdns::~Mdns() {
    stop();
}

bool Mdns::start(std::string* error) {
    if (m->running.load()) return true;
    auto socket = std::make_unique<net::UDPSocket>(net::AddressFamily::IPv4);
    socket->set_log_all_errors(false);
    int yes = 1;
    socket->set_option(SOL_SOCKET, SO_REUSEADDR, yes);
#ifdef SO_REUSEPORT
    socket->set_option(SOL_SOCKET, SO_REUSEPORT, yes);
#endif
    std::string why;
    if (!socket->bind(net::NetworkAddress("0.0.0.0", mdns_port, net::AddressFamily::IPv4))) why = "couldn't share UDP port 5353 for mDNS";
    else if (!socket->join_multicast_group(group)) why = "couldn't join the mDNS multicast group";
    if (!why.empty()) {
        std::lock_guard<std::mutex> lock(m->mutex);
        m->error = why;
        if (error) *error = why;
        return false;
    }
    socket->set_multicast_loopback(true);
    socket->set_multicast_ttl(255);
    socket->set_receive_timeout(100);
    m->socket = std::move(socket);
    m->running.store(true);
    Impl* impl = m.get();
    m->thread = std::thread([impl] { impl->run(); });
    return true;
}

void Mdns::stop() {
    if (!m->running.exchange(false)) return;
    if (m->thread.joinable()) m->thread.join();
    MdnsMessage bye;
    bye.response = true;
    {
        std::lock_guard<std::mutex> lock(m->mutex);
        for (const Impl::Advert& a : m->adverts) m->add_records(bye, a, 0, true);
    }
    if (!bye.records.empty()) m->send_group(bye);
    m->socket->close();
    m->socket.reset();
}

bool Mdns::running() const noexcept {
    return m->running.load();
}

void Mdns::advertise(const std::string& instance, const std::string& type, std::uint16_t port, const std::string& address) {
    std::lock_guard<std::mutex> lock(m->mutex);
    for (Impl::Advert& a : m->adverts) {
        if (a.instance == instance && a.type == type) {
            a.port = port;
            a.address = address;
            a.announced = 0;
            a.next = std::chrono::steady_clock::now();
            return;
        }
    }
    Impl::Advert a;
    a.instance = instance;
    a.type = trim_dot(type);
    a.port = port;
    a.address = address;
    std::string host;
    for (char c : instance) host += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '-';
    a.host = host + ".local";
    a.next = std::chrono::steady_clock::now();
    m->adverts.push_back(a);
}

void Mdns::withdraw(const std::string& instance, const std::string& type) {
    MdnsMessage bye;
    bye.response = true;
    {
        std::lock_guard<std::mutex> lock(m->mutex);
        for (auto it = m->adverts.begin(); it != m->adverts.end(); ++it) {
            if (it->instance == instance && it->type == trim_dot(type)) {
                m->add_records(bye, *it, 0, true);
                m->adverts.erase(it);
                break;
            }
        }
    }
    if (!bye.records.empty() && m->running.load()) m->send_group(bye);
}

void Mdns::browse(const std::string& type) {
    std::lock_guard<std::mutex> lock(m->mutex);
    const std::string t = trim_dot(type);
    if (std::find(m->browsing.begin(), m->browsing.end(), t) == m->browsing.end()) m->browsing.push_back(t);
    m->browse_round = 0;
    m->next_browse = std::chrono::steady_clock::now();
}

std::vector<Service> Mdns::services(const std::string& type) const {
    std::lock_guard<std::mutex> lock(m->mutex);
    std::vector<Service> out;
    for (const auto& kv : m->services) {
        if (type.empty() || kv.second.type == trim_dot(type)) out.push_back(kv.second);
    }
    return out;
}

std::string Mdns::last_error() const {
    std::lock_guard<std::mutex> lock(m->mutex);
    return m->error;
}

} // namespace query
} // namespace osc
