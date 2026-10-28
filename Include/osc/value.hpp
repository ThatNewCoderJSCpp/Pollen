#ifndef OSC_VALUE_HPP
#define OSC_VALUE_HPP

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace osc {

class Value;
using Array = std::vector<Value>;

class Value {
public:
    Value() noexcept : m_type(Type::Nil) {}
    Value(Nil) noexcept : m_type(Type::Nil) {}
    Value(Impulse) noexcept : m_type(Type::Impulse) {}
    Value(bool b) noexcept : m_type(b ? Type::True : Type::False) {}
    Value(char c) noexcept : m_type(Type::Char), m_int(static_cast<unsigned char>(c)) {}
    Value(float f) noexcept : m_type(Type::Float32), m_real(f) {}
    Value(double d) noexcept : m_type(Type::Double), m_real(d) {}
    Value(const char* s) : m_type(Type::String), m_text(s ? s : "") {}
    Value(std::string s) noexcept : m_type(Type::String), m_text(std::move(s)) {}
    Value(std::string_view s) : m_type(Type::String), m_text(s) {}
    Value(Symbol s) noexcept : m_type(Type::Symbol), m_text(std::move(s.text)) {}
    Value(Blob b) noexcept : m_type(Type::Blob), m_blob(std::move(b)) {}
    Value(TimeTag t) noexcept : m_type(Type::TimeTag), m_int(static_cast<std::int64_t>(t.raw())) {}
    Value(Color c) noexcept : m_type(Type::Color), m_int(c.packed()) {}
    Value(Midi m) noexcept : m_type(Type::Midi), m_int(m.packed()) {}
    Value(Array items) : m_type(Type::Array), m_items(std::move(items)) {}
    Value(std::initializer_list<Value> items) : m_type(Type::Array), m_items(items) {}

    template <typename T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value && !std::is_same<T, char>::value, int>::type = 0>
    Value(T v) noexcept : m_type(fits_int32<T>() ? Type::Int32 : Type::Int64), m_int(static_cast<std::int64_t>(v)) {}

    static Value int32(std::int32_t v) noexcept { return Value(v); }
    static Value int64(std::int64_t v) noexcept;
    static Value float32(float v) noexcept { return Value(v); }
    static Value float64(double v) noexcept { return Value(v); }
    static Value string(std::string s) { return Value(std::move(s)); }
    static Value symbol(std::string s) { return Value(Symbol(std::move(s))); }
    static Value blob(const void* data, std::size_t size);
    static Value array(Array items) { return Value(std::move(items)); }

    Type type() const noexcept { return m_type; }
    char tag() const noexcept { return static_cast<char>(m_type); }
    const char* type_name() const noexcept { return osc::type_name(m_type); }

    bool is_int32() const noexcept { return m_type == Type::Int32; }
    bool is_int64() const noexcept { return m_type == Type::Int64; }
    bool is_float() const noexcept { return m_type == Type::Float32; }
    bool is_double() const noexcept { return m_type == Type::Double; }
    bool is_string() const noexcept { return m_type == Type::String; }
    bool is_symbol() const noexcept { return m_type == Type::Symbol; }
    bool is_text() const noexcept { return m_type == Type::String || m_type == Type::Symbol; }
    bool is_blob() const noexcept { return m_type == Type::Blob; }
    bool is_bool() const noexcept { return m_type == Type::True || m_type == Type::False; }
    bool is_char() const noexcept { return m_type == Type::Char; }
    bool is_color() const noexcept { return m_type == Type::Color; }
    bool is_midi() const noexcept { return m_type == Type::Midi; }
    bool is_time() const noexcept { return m_type == Type::TimeTag; }
    bool is_nil() const noexcept { return m_type == Type::Nil; }
    bool is_impulse() const noexcept { return m_type == Type::Impulse; }
    bool is_array() const noexcept { return m_type == Type::Array; }
    bool is_integer() const noexcept { return m_type == Type::Int32 || m_type == Type::Int64; }
    bool is_real() const noexcept { return m_type == Type::Float32 || m_type == Type::Double; }
    bool is_number() const noexcept { return is_integer() || is_real(); }

    std::int32_t as_int32() const;
    std::int64_t as_int64() const;
    float as_float() const;
    double as_double() const;
    bool as_bool() const;
    char as_char() const;
    const std::string& as_string() const;
    const std::string& as_text() const;
    Symbol as_symbol() const;
    const Blob& as_blob() const;
    TimeTag as_time() const;
    Color as_color() const;
    Midi as_midi() const;
    const Array& as_array() const;

    std::optional<double> number() const noexcept;
    double to_double(double fallback = 0.0) const noexcept;
    float to_float(float fallback = 0.0f) const noexcept { return static_cast<float>(to_double(fallback)); }
    std::int64_t to_int64(std::int64_t fallback = 0) const noexcept;
    std::int32_t to_int32(std::int32_t fallback = 0) const noexcept { return static_cast<std::int32_t>(to_int64(fallback)); }
    bool to_bool(bool fallback = false) const noexcept;
    std::string to_string() const;

    template <typename T>
    bool is() const noexcept;

    template <typename T>
    T as() const;

    template <typename T>
    std::optional<T> get() const noexcept {
        if (!is<T>()) return std::nullopt;
        return as<T>();
    }

    template <typename T>
    T to(T fallback = T()) const noexcept;

    std::size_t size() const noexcept { return m_type == Type::Array ? m_items.size() : 0; }
    const Value& operator[](std::size_t i) const;
    Array::const_iterator begin() const noexcept { return m_items.begin(); }
    Array::const_iterator end() const noexcept { return m_items.end(); }

    bool operator==(const Value& o) const noexcept;
    bool operator!=(const Value& o) const noexcept { return !(*this == o); }

private:
    template <typename T>
    static constexpr bool fits_int32() noexcept {
        return sizeof(T) < sizeof(std::int32_t) || (sizeof(T) == sizeof(std::int32_t) && std::is_signed<T>::value);
    }

    void require(Type t) const;

    Type m_type;
    std::int64_t m_int = 0;
    double m_real = 0.0;
    std::string m_text;
    Blob m_blob;
    Array m_items;
};

template <typename T>
bool Value::is() const noexcept {
    using U = typename std::decay<T>::type;
    if constexpr (std::is_same<U, bool>::value) return is_bool();
    else if constexpr (std::is_same<U, char>::value) return is_char();
    else if constexpr (std::is_integral<U>::value) return sizeof(U) <= sizeof(std::int32_t) ? is_int32() : is_integer();
    else if constexpr (std::is_same<U, float>::value) return is_float();
    else if constexpr (std::is_floating_point<U>::value) return is_real();
    else if constexpr (std::is_same<U, std::string>::value || std::is_same<U, std::string_view>::value) return is_string();
    else if constexpr (std::is_same<U, Symbol>::value) return is_symbol();
    else if constexpr (std::is_same<U, Blob>::value) return is_blob();
    else if constexpr (std::is_same<U, TimeTag>::value) return is_time();
    else if constexpr (std::is_same<U, Color>::value) return is_color();
    else if constexpr (std::is_same<U, Midi>::value) return is_midi();
    else if constexpr (std::is_same<U, Nil>::value) return is_nil();
    else if constexpr (std::is_same<U, Impulse>::value) return is_impulse();
    else if constexpr (std::is_same<U, Array>::value) return is_array();
    else if constexpr (std::is_same<U, Value>::value) return true;
    else {
        static_assert(sizeof(U) == 0, "osc::Value::is<T> doesn't know this type");
        return false;
    }
}

template <typename T>
T Value::as() const {
    using U = typename std::decay<T>::type;
    if constexpr (std::is_same<U, bool>::value) return as_bool();
    else if constexpr (std::is_same<U, char>::value) return as_char();
    else if constexpr (std::is_integral<U>::value) {
        if constexpr (sizeof(U) <= sizeof(std::int32_t)) return static_cast<U>(as_int32());
        else return static_cast<U>(as_int64());
    }
    else if constexpr (std::is_same<U, float>::value) return as_float();
    else if constexpr (std::is_floating_point<U>::value) return static_cast<U>(as_double());
    else if constexpr (std::is_same<U, std::string>::value) return as_string();
    else if constexpr (std::is_same<U, std::string_view>::value) return std::string_view(as_string());
    else if constexpr (std::is_same<U, Symbol>::value) return as_symbol();
    else if constexpr (std::is_same<U, Blob>::value) return as_blob();
    else if constexpr (std::is_same<U, TimeTag>::value) return as_time();
    else if constexpr (std::is_same<U, Color>::value) return as_color();
    else if constexpr (std::is_same<U, Midi>::value) return as_midi();
    else if constexpr (std::is_same<U, Nil>::value) { require(Type::Nil); return Nil{}; }
    else if constexpr (std::is_same<U, Impulse>::value) { require(Type::Impulse); return Impulse{}; }
    else if constexpr (std::is_same<U, Array>::value) return as_array();
    else if constexpr (std::is_same<U, Value>::value) return *this;
    else static_assert(sizeof(U) == 0, "osc::Value::as<T> doesn't know this type");
}

template <typename T>
T Value::to(T fallback) const noexcept {
    using U = typename std::decay<T>::type;
    if constexpr (std::is_same<U, bool>::value) return to_bool(fallback);
    else if constexpr (std::is_integral<U>::value) return static_cast<U>(to_int64(static_cast<std::int64_t>(fallback)));
    else if constexpr (std::is_floating_point<U>::value) return static_cast<U>(to_double(static_cast<double>(fallback)));
    else if constexpr (std::is_same<U, std::string>::value) {
        if (is_nil()) return fallback;
        return to_string();
    }
    else {
        if (is<U>()) return as<U>();
        return fallback;
    }
}

} // namespace osc

#endif // OSC_VALUE_HPP
