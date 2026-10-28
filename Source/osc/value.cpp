#include "osc/value.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace osc {

namespace {

constexpr std::uint64_t ntp_unix_offset = 2208988800ull;

std::string format_real(double v, int digits) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", digits, v);
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

} // namespace

const char* type_name(Type type) noexcept {
    switch (type) {
    case Type::Int32: return "int32";
    case Type::Float32: return "float32";
    case Type::String: return "string";
    case Type::Blob: return "blob";
    case Type::Int64: return "int64";
    case Type::Double: return "double";
    case Type::TimeTag: return "timetag";
    case Type::Symbol: return "symbol";
    case Type::Char: return "char";
    case Type::Color: return "color";
    case Type::Midi: return "midi";
    case Type::True: return "true";
    case Type::False: return "false";
    case Type::Nil: return "nil";
    case Type::Impulse: return "impulse";
    case Type::Array: return "array";
    }
    return "unknown";
}

TypeError::TypeError(Type wanted, Type got)
    : std::runtime_error(std::string("osc: wanted a ") + type_name(wanted) + " but the value is a " + type_name(got)), m_wanted(wanted), m_got(got) {}

DecodeError::DecodeError(const std::string& what, std::size_t offset)
    : std::runtime_error("osc: " + what + " (at byte " + std::to_string(offset) + ")"), m_offset(offset) {}

TimeTag TimeTag::now() {
    return from_system(std::chrono::system_clock::now());
}

TimeTag TimeTag::from_system(std::chrono::system_clock::time_point t) {
    const auto since = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
    const std::int64_t secs = since / 1000000000ll;
    const std::int64_t nanos = since % 1000000000ll;
    const std::uint64_t frac = (static_cast<std::uint64_t>(nanos) << 32) / 1000000000ull;
    return TimeTag(static_cast<std::uint32_t>(static_cast<std::uint64_t>(secs) + ntp_unix_offset), static_cast<std::uint32_t>(frac));
}

std::chrono::system_clock::time_point TimeTag::to_system() const {
    if (is_immediate()) return std::chrono::system_clock::now();
    const std::int64_t secs = static_cast<std::int64_t>(seconds()) - static_cast<std::int64_t>(ntp_unix_offset);
    const std::int64_t nanos = static_cast<std::int64_t>((static_cast<std::uint64_t>(fraction()) * 1000000000ull) >> 32);
    return std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(secs) + std::chrono::nanoseconds(nanos)));
}

TimeTag TimeTag::operator+(std::chrono::nanoseconds d) const {
    if (is_immediate()) return from_system(std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(d));
    return from_system(to_system() + std::chrono::duration_cast<std::chrono::system_clock::duration>(d));
}

Value Value::int64(std::int64_t v) noexcept {
    Value out;
    out.m_type = Type::Int64;
    out.m_int = v;
    return out;
}

Value Value::blob(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    return Value(Blob(p, p + size));
}

void Value::require(Type t) const {
    if (m_type != t) throw TypeError(t, m_type);
}

std::int32_t Value::as_int32() const {
    require(Type::Int32);
    return static_cast<std::int32_t>(m_int);
}

std::int64_t Value::as_int64() const {
    if (m_type == Type::Int32) return m_int;
    require(Type::Int64);
    return m_int;
}

float Value::as_float() const {
    require(Type::Float32);
    return static_cast<float>(m_real);
}

double Value::as_double() const {
    if (m_type == Type::Float32) return m_real;
    require(Type::Double);
    return m_real;
}

bool Value::as_bool() const {
    if (m_type == Type::True) return true;
    if (m_type == Type::False) return false;
    throw TypeError(Type::True, m_type);
}

char Value::as_char() const {
    require(Type::Char);
    return static_cast<char>(m_int);
}

const std::string& Value::as_string() const {
    require(Type::String);
    return m_text;
}

const std::string& Value::as_text() const {
    if (m_type == Type::Symbol) return m_text;
    require(Type::String);
    return m_text;
}

Symbol Value::as_symbol() const {
    require(Type::Symbol);
    return Symbol(m_text);
}

const Blob& Value::as_blob() const {
    require(Type::Blob);
    return m_blob;
}

TimeTag Value::as_time() const {
    require(Type::TimeTag);
    return TimeTag(static_cast<std::uint64_t>(m_int));
}

Color Value::as_color() const {
    require(Type::Color);
    return Color::from_packed(static_cast<std::uint32_t>(m_int));
}

Midi Value::as_midi() const {
    require(Type::Midi);
    return Midi::from_packed(static_cast<std::uint32_t>(m_int));
}

const Array& Value::as_array() const {
    require(Type::Array);
    return m_items;
}

std::optional<double> Value::number() const noexcept {
    switch (m_type) {
    case Type::Int32:
    case Type::Int64: return static_cast<double>(m_int);
    case Type::Float32:
    case Type::Double: return m_real;
    case Type::True: return 1.0;
    case Type::False: return 0.0;
    case Type::Char: return static_cast<double>(m_int);
    default: return std::nullopt;
    }
}

double Value::to_double(double fallback) const noexcept {
    if (m_type == Type::String || m_type == Type::Symbol) {
        char* end = nullptr;
        const double v = std::strtod(m_text.c_str(), &end);
        return end != m_text.c_str() ? v : fallback;
    }
    const auto n = number();
    return n ? *n : fallback;
}

std::int64_t Value::to_int64(std::int64_t fallback) const noexcept {
    if (m_type == Type::Int32 || m_type == Type::Int64 || m_type == Type::Char) return m_int;
    const double d = to_double(std::numeric_limits<double>::quiet_NaN());
    if (std::isnan(d)) return fallback;
    if (d >= 9.2e18) return std::numeric_limits<std::int64_t>::max();
    if (d <= -9.2e18) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(std::llround(d));
}

bool Value::to_bool(bool fallback) const noexcept {
    if (m_type == Type::True) return true;
    if (m_type == Type::False) return false;
    if (m_type == Type::String || m_type == Type::Symbol) {
        if (m_text == "true" || m_text == "True" || m_text == "TRUE" || m_text == "on" || m_text == "yes" || m_text == "1") return true;
        if (m_text == "false" || m_text == "False" || m_text == "FALSE" || m_text == "off" || m_text == "no" || m_text == "0") return false;
        return fallback;
    }
    const auto n = number();
    if (!n) return fallback;
    return *n != 0.0;
}

std::string Value::to_string() const {
    switch (m_type) {
    case Type::Int32:
    case Type::Int64: return std::to_string(m_int);
    case Type::Float32: return format_real(static_cast<float>(m_real), 7);
    case Type::Double: return format_real(m_real, 15);
    case Type::String: return m_text;
    case Type::Symbol: return "'" + m_text;
    case Type::Blob: return "<blob " + std::to_string(m_blob.size()) + " bytes>";
    case Type::TimeTag: {
        const TimeTag t(static_cast<std::uint64_t>(m_int));
        if (t.is_immediate()) return "<immediately>";
        return "<time " + std::to_string(t.seconds()) + "." + std::to_string(t.fraction()) + ">";
    }
    case Type::Char: return std::string(1, static_cast<char>(m_int));
    case Type::Color: {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "#%08x", static_cast<unsigned>(m_int));
        return buf;
    }
    case Type::Midi: {
        const Midi m = Midi::from_packed(static_cast<std::uint32_t>(m_int));
        char buf[48];
        std::snprintf(buf, sizeof(buf), "<midi %u %02x %u %u>", unsigned(m.port), unsigned(m.status), unsigned(m.data1), unsigned(m.data2));
        return buf;
    }
    case Type::True: return "true";
    case Type::False: return "false";
    case Type::Nil: return "nil";
    case Type::Impulse: return "impulse";
    case Type::Array: {
        std::string out = "[";
        for (std::size_t i = 0; i < m_items.size(); ++i) {
            if (i) out += ", ";
            out += m_items[i].is_string() ? quote(m_items[i].m_text) : m_items[i].to_string();
        }
        return out + "]";
    }
    }
    return std::string();
}

const Value& Value::operator[](std::size_t i) const {
    require(Type::Array);
    if (i >= m_items.size()) throw std::out_of_range("osc::Value: array index " + std::to_string(i) + " is past its " + std::to_string(m_items.size()) + " item(s)");
    return m_items[i];
}

bool Value::operator==(const Value& o) const noexcept {
    if (m_type != o.m_type) return false;
    switch (m_type) {
    case Type::Int32:
    case Type::Int64:
    case Type::Char:
    case Type::Color:
    case Type::Midi:
    case Type::TimeTag: return m_int == o.m_int;
    case Type::Float32:
    case Type::Double: return m_real == o.m_real || (std::isnan(m_real) && std::isnan(o.m_real));
    case Type::String:
    case Type::Symbol: return m_text == o.m_text;
    case Type::Blob: return m_blob == o.m_blob;
    case Type::Array: return m_items == o.m_items;
    case Type::True:
    case Type::False:
    case Type::Nil:
    case Type::Impulse: return true;
    }
    return false;
}

} // namespace osc
