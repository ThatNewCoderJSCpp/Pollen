#include "osc/vrchat/vrchat.hpp"

#include <cstring>

namespace osc {
namespace vrchat {

std::string parameter_address(std::string_view name) {
    std::string out = parameter_prefix;
    out.append(name.data(), name.size());
    return out;
}

bool is_parameter_address(std::string_view address) noexcept {
    const std::size_t n = std::strlen(parameter_prefix);
    return address.size() > n && address.compare(0, n, parameter_prefix) == 0;
}

std::string parameter_name(std::string_view address) {
    if (!is_parameter_address(address)) return std::string();
    return std::string(address.substr(std::strlen(parameter_prefix)));
}

std::size_t utf8_length(std::string_view text) noexcept {
    std::size_t n = 0;
    for (char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) ++n;
    }
    return n;
}

std::string chatbox_text(std::string_view text) {
    std::string out;
    std::size_t chars = 0;
    std::size_t lines = 1;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if (lead >= 0xF0) len = 4;
        else if (lead >= 0xE0) len = 3;
        else if (lead >= 0xC0) len = 2;
        if (i + len > text.size()) break;
        if (text[i] == '\r') {
            ++i;
            continue;
        }
        if (text[i] == '\n') {
            if (lines >= chatbox_max_lines) break;
            ++lines;
        }
        if (chars >= chatbox_max_chars) break;
        out.append(text.data() + i, len);
        ++chars;
        i += len;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

Message parameter(std::string_view name, Value value) {
    return Message(parameter_address(name), std::move(value));
}

Message chatbox(std::string_view text, bool send_now, bool notify) {
    return Message(chatbox_input_address, chatbox_text(text), send_now, notify);
}

Message typing(bool on) {
    return Message(chatbox_typing_address, on);
}

Message input_axis(std::string_view name, float value) {
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    return Message(std::string(input_prefix) + std::string(name), value);
}

Message input_button(std::string_view name, bool down) {
    return Message(std::string(input_prefix) + std::string(name), static_cast<std::int32_t>(down ? 1 : 0));
}

Connection::Connection() {
    m_dispatcher.on("/avatar/change", [this](const Message& m) {
        m_avatar.handle(m);
        reload_avatar_config();
    });
    m_dispatcher.on("/avatar/parameters/*", [this](const Message& m) { m_avatar.handle(m); });
}

Connection::~Connection() {
    stop();
}

bool Connection::start(const ConnectionSettings& settings, std::string* error) {
    stop();
    m_settings = settings;
    std::string why;
    if (!m_sender.set_target(Endpoint(settings.host, settings.send_port), &why)) {
        if (error) *error = why;
        return false;
    }
    m_sending = true;
    if (settings.receive_port != 0) {
        if (!m_receiver.listen(settings.receive_port, settings.listen_host, &why)) {
            if (error) *error = why;
            return false;
        }
    }
    return true;
}

void Connection::stop() {
    m_receiver.stop();
    m_sending = false;
}

bool Connection::listening() const noexcept {
    return m_receiver.listening();
}

bool Connection::send(const Message& message) {
    if (!m_sending) return false;
    const bool ok = m_sender.send(message);
    if (ok && on_send) on_send(message);
    return ok;
}

bool Connection::send(const Bundle& bundle) {
    if (!m_sending) return false;
    const bool ok = m_sender.send(bundle);
    if (ok && on_send) bundle.for_each_message([this](const Message& m, TimeTag) { on_send(m); });
    return ok;
}

bool Connection::set_parameter(std::string_view name, Value value) {
    if (const AvatarConfig* config = m_avatar.config() ? &*m_avatar.config() : nullptr) {
        if (const ParameterInfo* info = config->find(name)) {
            value = coerce_param_value(info->type, value);
            Message m(info->input_address.empty() ? parameter_address(name) : info->input_address, value);
            const bool ok = send(m);
            if (ok) m_avatar.remember_sent(name, value);
            return ok;
        }
    }
    const bool ok = send(parameter(name, value));
    if (ok) m_avatar.remember_sent(name, value);
    return ok;
}

bool Connection::chatbox(std::string_view text, bool send_now, bool notify) {
    return send(vrchat::chatbox(text, send_now, notify));
}

bool Connection::typing(bool on) {
    return send(vrchat::typing(on));
}

bool Connection::input_axis(std::string_view name, float value) {
    return send(vrchat::input_axis(name, value));
}

bool Connection::input_button(std::string_view name, bool down) {
    return send(vrchat::input_button(name, down));
}

std::size_t Connection::poll(std::size_t max) {
    std::vector<Received> batch = m_receiver.take(max);
    for (const Received& r : batch) {
        if (on_receive) on_receive(r);
        m_dispatcher.dispatch(r.packet);
    }
    return batch.size();
}

bool Connection::reload_avatar_config() {
    const std::string& id = m_avatar.avatar_id();
    if (id.empty()) return false;
    const std::optional<std::filesystem::path> path = find_avatar_config(id, m_settings.extra_osc_folders);
    if (!path) {
        m_avatar.set_config(std::nullopt);
        return false;
    }
    std::optional<AvatarConfig> config = load_avatar_config(*path);
    const bool ok = config.has_value();
    m_avatar.set_config(std::move(config));
    return ok;
}

bool Connection::hearing_vrchat(std::chrono::milliseconds window) const {
    const Stats s = m_receiver.stats();
    if (!s.any) return false;
    return std::chrono::steady_clock::now() - s.last <= window;
}

} // namespace vrchat
} // namespace osc
