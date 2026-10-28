#include "osc/dispatcher.hpp"

#include <algorithm>

namespace osc {

Dispatcher::Id Dispatcher::on(std::string pattern, Handler handler) {
    const Id id = m_next++;
    const bool literal = !is_pattern(pattern);
    m_routes.push_back(Route{ id, std::move(pattern), literal, false, std::move(handler) });
    return id;
}

Dispatcher::Id Dispatcher::on_unmatched(Handler handler) {
    const Id id = m_next++;
    m_routes.push_back(Route{ id, std::string(), true, true, std::move(handler) });
    return id;
}

bool Dispatcher::off(Id id) {
    const auto it = std::find_if(m_routes.begin(), m_routes.end(), [id](const Route& r) { return r.id == id; });
    if (it == m_routes.end()) return false;
    m_routes.erase(it);
    return true;
}

void Dispatcher::clear() noexcept {
    m_routes.clear();
}

std::size_t Dispatcher::dispatch(const Message& message) const {
    std::size_t hits = 0;
    for (std::size_t i = 0; i < m_routes.size(); ++i) {
        const Route& r = m_routes[i];
        if (r.fallback) continue;
        const bool hit = r.literal ? r.pattern == message.address() : matches(r.pattern, message.address());
        if (!hit) continue;
        ++hits;
        if (r.handler) {
            const Handler handler = r.handler;
            handler(message);
        }
    }
    if (hits == 0) {
        for (std::size_t i = 0; i < m_routes.size(); ++i) {
            if (!m_routes[i].fallback || !m_routes[i].handler) continue;
            const Handler handler = m_routes[i].handler;
            handler(message);
        }
    }
    return hits;
}

std::size_t Dispatcher::dispatch(const Packet& packet) const {
    std::size_t hits = 0;
    packet.for_each_message([&](const Message& m, TimeTag) { hits += dispatch(m); });
    return hits;
}

} // namespace osc
