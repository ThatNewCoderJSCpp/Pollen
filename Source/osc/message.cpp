#include "osc/message.hpp"
#include "osc/pattern.hpp"

namespace osc {

namespace {

void tags_of(const std::vector<Value>& args, std::string& out) {
    for (const Value& v : args) {
        if (v.is_array()) {
            out += '[';
            tags_of(v.as_array(), out);
            out += ']';
        } else {
            out += v.tag();
        }
    }
}

std::string shown(const Value& v) {
    if (v.is_string()) {
        std::string out = "\"";
        for (char c : v.as_string()) {
            if (c == '"' || c == '\\') out += '\\';
            if (c == '\n') {
                out += "\\n";
                continue;
            }
            out += c;
        }
        return out + "\"";
    }
    return v.to_string();
}

} // namespace

const Value& Message::at(std::size_t i) const {
    if (i >= m_args.size()) throw std::out_of_range("osc::Message: " + m_address + " has no argument " + std::to_string(i) + " (it has " + std::to_string(m_args.size()) + ")");
    return m_args[i];
}

std::string Message::type_tags() const {
    std::string out = ",";
    tags_of(m_args, out);
    return out;
}

bool Message::matches(std::string_view pattern) const {
    return osc::matches(pattern, m_address);
}

std::string Message::to_string() const {
    std::string out = m_address;
    if (m_args.empty()) return out;
    out += ' ';
    out += type_tags();
    for (const Value& v : m_args) {
        out += ' ';
        out += shown(v);
    }
    return out;
}

Bundle& Bundle::add(Message m) {
    m_elements.emplace_back(std::move(m));
    return *this;
}

Bundle& Bundle::add(Bundle b) {
    m_elements.emplace_back(std::move(b));
    return *this;
}

std::size_t Bundle::message_count() const noexcept {
    std::size_t n = 0;
    for (const Packet& p : m_elements) n += p.message_count();
    return n;
}

void Bundle::for_each_message(const std::function<void(const Message&, TimeTag)>& fn) const {
    for (const Packet& p : m_elements) {
        if (p.is_bundle()) p.bundle().for_each_message(fn);
        else fn(p.message(), m_time);
    }
}

bool Bundle::operator==(const Bundle& o) const noexcept {
    return m_time == o.m_time && m_elements == o.m_elements;
}

const Message& Packet::message() const {
    if (m_bundle_kind) throw std::logic_error("osc::Packet: this packet is a bundle, not a message");
    return m_message;
}

const Bundle& Packet::bundle() const {
    if (!m_bundle_kind) throw std::logic_error("osc::Packet: this packet is a message, not a bundle");
    return m_bundle;
}

Message& Packet::message() {
    if (m_bundle_kind) throw std::logic_error("osc::Packet: this packet is a bundle, not a message");
    return m_message;
}

Bundle& Packet::bundle() {
    if (!m_bundle_kind) throw std::logic_error("osc::Packet: this packet is a message, not a bundle");
    return m_bundle;
}

void Packet::for_each_message(const std::function<void(const Message&, TimeTag)>& fn) const {
    if (m_bundle_kind) m_bundle.for_each_message(fn);
    else fn(m_message, TimeTag::immediately());
}

bool Packet::operator==(const Packet& o) const noexcept {
    if (m_bundle_kind != o.m_bundle_kind) return false;
    return m_bundle_kind ? m_bundle == o.m_bundle : m_message == o.m_message;
}

} // namespace osc
