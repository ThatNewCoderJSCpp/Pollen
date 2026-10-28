#ifndef OSC_TYPES_HPP
#define OSC_TYPES_HPP

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace osc {

enum class Type : char {
    Int32 = 'i',
    Float32 = 'f',
    String = 's',
    Blob = 'b',
    Int64 = 'h',
    Double = 'd',
    TimeTag = 't',
    Symbol = 'S',
    Char = 'c',
    Color = 'r',
    Midi = 'm',
    True = 'T',
    False = 'F',
    Nil = 'N',
    Impulse = 'I',
    Array = '['
};

const char* type_name(Type type) noexcept;

using Blob = std::vector<std::uint8_t>;

struct Nil {
    bool operator==(const Nil&) const noexcept { return true; }
    bool operator!=(const Nil&) const noexcept { return false; }
};

struct Impulse {
    bool operator==(const Impulse&) const noexcept { return true; }
    bool operator!=(const Impulse&) const noexcept { return false; }
};

struct Symbol {
    std::string text;

    Symbol() = default;
    explicit Symbol(std::string s) : text(std::move(s)) {}

    bool operator==(const Symbol& o) const noexcept { return text == o.text; }
    bool operator!=(const Symbol& o) const noexcept { return text != o.text; }
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    std::uint32_t packed() const noexcept { return (std::uint32_t(r) << 24) | (std::uint32_t(g) << 16) | (std::uint32_t(b) << 8) | std::uint32_t(a); }
    static Color from_packed(std::uint32_t v) noexcept { return Color{ std::uint8_t(v >> 24), std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v) }; }

    bool operator==(const Color& o) const noexcept { return packed() == o.packed(); }
    bool operator!=(const Color& o) const noexcept { return packed() != o.packed(); }
};

struct Midi {
    std::uint8_t port = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    std::uint32_t packed() const noexcept { return (std::uint32_t(port) << 24) | (std::uint32_t(status) << 16) | (std::uint32_t(data1) << 8) | std::uint32_t(data2); }
    static Midi from_packed(std::uint32_t v) noexcept { return Midi{ std::uint8_t(v >> 24), std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v) }; }

    bool operator==(const Midi& o) const noexcept { return packed() == o.packed(); }
    bool operator!=(const Midi& o) const noexcept { return packed() != o.packed(); }
};

class TimeTag {
public:
    constexpr TimeTag() noexcept = default;
    constexpr explicit TimeTag(std::uint64_t raw) noexcept : m_raw(raw) {}
    constexpr TimeTag(std::uint32_t seconds, std::uint32_t fraction) noexcept : m_raw((std::uint64_t(seconds) << 32) | fraction) {}

    static constexpr TimeTag immediately() noexcept { return TimeTag(1); }
    static TimeTag now();
    static TimeTag from_system(std::chrono::system_clock::time_point t);

    constexpr std::uint64_t raw() const noexcept { return m_raw; }
    constexpr std::uint32_t seconds() const noexcept { return std::uint32_t(m_raw >> 32); }
    constexpr std::uint32_t fraction() const noexcept { return std::uint32_t(m_raw); }
    constexpr bool is_immediate() const noexcept { return m_raw == 1; }

    std::chrono::system_clock::time_point to_system() const;
    TimeTag operator+(std::chrono::nanoseconds d) const;

    constexpr bool operator==(const TimeTag& o) const noexcept { return m_raw == o.m_raw; }
    constexpr bool operator!=(const TimeTag& o) const noexcept { return m_raw != o.m_raw; }
    constexpr bool operator<(const TimeTag& o) const noexcept { return m_raw < o.m_raw; }

private:
    std::uint64_t m_raw = 1;
};

class TypeError : public std::runtime_error {
public:
    TypeError(Type wanted, Type got);

    Type wanted() const noexcept { return m_wanted; }
    Type got() const noexcept { return m_got; }

private:
    Type m_wanted;
    Type m_got;
};

class DecodeError : public std::runtime_error {
public:
    DecodeError(const std::string& what, std::size_t offset);

    std::size_t offset() const noexcept { return m_offset; }

private:
    std::size_t m_offset;
};

} // namespace osc

#endif // OSC_TYPES_HPP
