#ifndef OSC_DISPATCHER_HPP
#define OSC_DISPATCHER_HPP

#include "message.hpp"
#include "pattern.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace osc {

class Dispatcher {
public:
    using Handler = std::function<void(const Message&)>;
    using Id = std::uint64_t;

    Id on(std::string pattern, Handler handler);

    template <typename... Ts, typename F, typename std::enable_if<(sizeof...(Ts) > 0), int>::type = 0>
    Id on(std::string pattern, F&& fn) {
        return on(std::move(pattern), Handler([f = std::forward<F>(fn)](const Message& m) {
            if (m.size() < sizeof...(Ts)) return;
            call_with<Ts...>(f, m, std::index_sequence_for<Ts...>{});
        }));
    }

    Id on_unmatched(Handler handler);

    bool off(Id id);
    void clear() noexcept;
    std::size_t size() const noexcept { return m_routes.size(); }

    std::size_t dispatch(const Message& message) const;
    std::size_t dispatch(const Packet& packet) const;

private:
    template <typename... Ts, typename F, std::size_t... I>
    static void call_with(F& f, const Message& m, std::index_sequence<I...>) {
        f(m.to<Ts>(I)...);
    }

    struct Route {
        Id id;
        std::string pattern;
        bool literal;
        bool fallback;
        Handler handler;
    };

    std::vector<Route> m_routes;
    Id m_next = 1;
};

} // namespace osc

#endif // OSC_DISPATCHER_HPP
