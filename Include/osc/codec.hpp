#ifndef OSC_CODEC_HPP
#define OSC_CODEC_HPP

#include "message.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osc {

constexpr std::size_t max_udp_payload = 65507;

std::vector<std::uint8_t> encode(const Message& message);
std::vector<std::uint8_t> encode(const Bundle& bundle);
std::vector<std::uint8_t> encode(const Packet& packet);

std::size_t encoded_size(const Message& message);

struct DecodeResult {
    std::optional<Packet> packet;
    std::string error;
    std::size_t offset = 0;

    bool ok() const noexcept { return packet.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
};

DecodeResult try_decode(const void* data, std::size_t size);

std::optional<Packet> decode(const void* data, std::size_t size, std::string* error = nullptr);
std::optional<Packet> decode(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
Packet decode_or_throw(const void* data, std::size_t size);

std::optional<Message> decode_message(const void* data, std::size_t size, std::string* error = nullptr);
std::optional<Message> decode_message(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);

bool looks_like_bundle(const void* data, std::size_t size) noexcept;

std::string hex_dump(const std::vector<std::uint8_t>& bytes, std::size_t max_bytes = 256);

} // namespace osc

#endif // OSC_CODEC_HPP
