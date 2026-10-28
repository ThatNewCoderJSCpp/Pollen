#ifndef OSC_VRCHAT_VRCHAT_HPP
#define OSC_VRCHAT_VRCHAT_HPP

#include "../dispatcher.hpp"
#include "../message.hpp"
#include "../socket.hpp"
#include "avatar.hpp"
#include "paths.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace osc {
namespace vrchat {

constexpr std::uint16_t default_send_port = 9000;
constexpr std::uint16_t default_receive_port = 9001;
constexpr std::size_t chatbox_max_chars = 144;
constexpr std::size_t chatbox_max_lines = 9;

constexpr const char* parameter_prefix = "/avatar/parameters/";
constexpr const char* avatar_change_address = "/avatar/change";
constexpr const char* chatbox_input_address = "/chatbox/input";
constexpr const char* chatbox_typing_address = "/chatbox/typing";
constexpr const char* input_prefix = "/input/";

std::string parameter_address(std::string_view name);
bool is_parameter_address(std::string_view address) noexcept;
std::string parameter_name(std::string_view address);

std::string chatbox_text(std::string_view text);
std::size_t utf8_length(std::string_view text) noexcept;

Message parameter(std::string_view name, Value value);
Message chatbox(std::string_view text, bool send_now = true, bool notify = false);
Message typing(bool on);
Message input_axis(std::string_view name, float value);
Message input_button(std::string_view name, bool down);

struct ConnectionSettings {
    std::string host = "127.0.0.1";
    std::uint16_t send_port = default_send_port;
    std::uint16_t receive_port = default_receive_port;
    std::string listen_host = "127.0.0.1";
    std::vector<std::filesystem::path> extra_osc_folders;
};

class Connection {
public:
    Connection();
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool start(const ConnectionSettings& settings, std::string* error = nullptr);
    void stop();
    bool sending() const noexcept { return m_sending; }
    bool listening() const noexcept;
    const ConnectionSettings& settings() const noexcept { return m_settings; }

    bool send(const Message& message);
    bool send(const Bundle& bundle);
    bool set_parameter(std::string_view name, Value value);
    bool chatbox(std::string_view text, bool send_now = true, bool notify = false);
    bool typing(bool on);
    bool input_axis(std::string_view name, float value);
    bool input_button(std::string_view name, bool down);

    std::size_t poll(std::size_t max = 4096);
    bool reload_avatar_config();

    bool hearing_vrchat(std::chrono::milliseconds window = std::chrono::seconds(5)) const;

    Dispatcher& dispatcher() noexcept { return m_dispatcher; }
    AvatarState& avatar() noexcept { return m_avatar; }
    const AvatarState& avatar() const noexcept { return m_avatar; }
    Sender& sender() noexcept { return m_sender; }
    Receiver& receiver() noexcept { return m_receiver; }
    const Sender& sender() const noexcept { return m_sender; }
    const Receiver& receiver() const noexcept { return m_receiver; }

    std::function<void(const Received&)> on_receive;
    std::function<void(const Message&)> on_send;

private:
    ConnectionSettings m_settings;
    Sender m_sender;
    Receiver m_receiver;
    Dispatcher m_dispatcher;
    AvatarState m_avatar;
    bool m_sending = false;
};

} // namespace vrchat
} // namespace osc

#endif // OSC_VRCHAT_VRCHAT_HPP
