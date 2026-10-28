#ifndef OSC_MESSAGE_HPP
#define OSC_MESSAGE_HPP

#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace osc {

class Message {
public:
    Message() = default;
    explicit Message(std::string address) : m_address(std::move(address)) {}

    template <typename First, typename... Rest>
    Message(std::string address, First&& first, Rest&&... rest) : m_address(std::move(address)) {
        m_args.reserve(1 + sizeof...(Rest));
        m_args.emplace_back(std::forward<First>(first));
        (m_args.emplace_back(std::forward<Rest>(rest)), ...);
    }

    const std::string& address() const noexcept { return m_address; }
    void set_address(std::string address) { m_address = std::move(address); }

    std::size_t size() const noexcept { return m_args.size(); }
    bool empty() const noexcept { return m_args.empty(); }
    const std::vector<Value>& args() const noexcept { return m_args; }
    std::vector<Value>& args() noexcept { return m_args; }
    std::vector<Value>::const_iterator begin() const noexcept { return m_args.begin(); }
    std::vector<Value>::const_iterator end() const noexcept { return m_args.end(); }

    const Value& operator[](std::size_t i) const { return at(i); }
    const Value& at(std::size_t i) const;

    Message& add(Value v) {
        m_args.push_back(std::move(v));
        return *this;
    }

    template <typename T>
    Message& operator<<(T&& v) {
        m_args.emplace_back(std::forward<T>(v));
        return *this;
    }

    void clear() noexcept { m_args.clear(); }

    template <typename T>
    T arg(std::size_t i) const { return at(i).as<T>(); }

    template <typename T>
    std::optional<T> get(std::size_t i) const noexcept {
        if (i >= m_args.size()) return std::nullopt;
        return m_args[i].get<T>();
    }

    template <typename T>
    T to(std::size_t i, T fallback = T()) const noexcept {
        if (i >= m_args.size()) return fallback;
        return m_args[i].to<T>(fallback);
    }

    template <typename... Ts>
    bool has_types() const noexcept {
        if (sizeof...(Ts) != m_args.size()) return false;
        std::size_t i = 0;
        return (m_args[i++].is<Ts>() && ...);
    }

    template <typename... Ts>
    std::tuple<Ts...> unpack() const {
        if (sizeof...(Ts) > m_args.size()) throw std::out_of_range("osc::Message::unpack: " + m_address + " has " + std::to_string(m_args.size()) + " argument(s)");
        return unpack_at<Ts...>(std::index_sequence_for<Ts...>{});
    }

    std::string type_tags() const;
    bool matches(std::string_view pattern) const;
    std::string to_string() const;

    std::vector<std::uint8_t> encode() const;
    void encode_into(std::vector<std::uint8_t>& out) const;

    bool operator==(const Message& o) const noexcept { return m_address == o.m_address && m_args == o.m_args; }
    bool operator!=(const Message& o) const noexcept { return !(*this == o); }

private:
    template <typename... Ts, std::size_t... I>
    std::tuple<Ts...> unpack_at(std::index_sequence<I...>) const {
        return std::tuple<Ts...>(m_args[I].template as<Ts>()...);
    }

    std::string m_address;
    std::vector<Value> m_args;
};

class Packet;

class Bundle {
public:
    Bundle() = default;
    explicit Bundle(TimeTag time) : m_time(time) {}

    TimeTag time() const noexcept { return m_time; }
    void set_time(TimeTag t) noexcept { m_time = t; }

    std::size_t size() const noexcept;
    bool empty() const noexcept;
    const std::vector<Packet>& elements() const noexcept { return m_elements; }
    std::vector<Packet>& elements() noexcept { return m_elements; }

    Bundle& add(Message m);
    Bundle& add(Bundle b);
    Bundle& operator<<(Message m) { return add(std::move(m)); }
    Bundle& operator<<(Bundle b) { return add(std::move(b)); }

    std::size_t message_count() const noexcept;
    void for_each_message(const std::function<void(const Message&, TimeTag)>& fn) const;

    std::vector<std::uint8_t> encode() const;
    void encode_into(std::vector<std::uint8_t>& out) const;

    bool operator==(const Bundle& o) const noexcept;
    bool operator!=(const Bundle& o) const noexcept { return !(*this == o); }

private:
    TimeTag m_time = TimeTag::immediately();
    std::vector<Packet> m_elements;
};

class Packet {
public:
    Packet() = default;
    Packet(Message m) : m_bundle_kind(false), m_message(std::move(m)) {}
    Packet(Bundle b) : m_bundle_kind(true), m_bundle(std::move(b)) {}

    bool is_message() const noexcept { return !m_bundle_kind; }
    bool is_bundle() const noexcept { return m_bundle_kind; }

    const Message& message() const;
    const Bundle& bundle() const;
    Message& message();
    Bundle& bundle();

    std::size_t message_count() const noexcept { return m_bundle_kind ? m_bundle.message_count() : 1; }
    void for_each_message(const std::function<void(const Message&, TimeTag)>& fn) const;

    std::vector<std::uint8_t> encode() const;
    void encode_into(std::vector<std::uint8_t>& out) const;

    bool operator==(const Packet& o) const noexcept;
    bool operator!=(const Packet& o) const noexcept { return !(*this == o); }

private:
    bool m_bundle_kind = false;
    Message m_message;
    Bundle m_bundle;
};

inline std::size_t Bundle::size() const noexcept { return m_elements.size(); }
inline bool Bundle::empty() const noexcept { return m_elements.empty(); }

} // namespace osc

#endif // OSC_MESSAGE_HPP
