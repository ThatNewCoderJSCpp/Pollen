#include "osc/codec.hpp"

#include <cstdio>
#include <cstring>

namespace osc {

namespace {

constexpr std::size_t max_depth = 32;
const char bundle_tag[8] = { '#', 'b', 'u', 'n', 'd', 'l', 'e', '\0' };

std::size_t padded(std::size_t n) noexcept {
    return (n + 3u) & ~std::size_t(3u);
}

class Writer {
public:
    explicit Writer(std::vector<std::uint8_t>& out) : m_out(out) {}

    void u32(std::uint32_t v) {
        m_out.push_back(std::uint8_t(v >> 24));
        m_out.push_back(std::uint8_t(v >> 16));
        m_out.push_back(std::uint8_t(v >> 8));
        m_out.push_back(std::uint8_t(v));
    }

    void u64(std::uint64_t v) {
        u32(std::uint32_t(v >> 32));
        u32(std::uint32_t(v));
    }

    void f32(float f) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        u32(bits);
    }

    void f64(double d) {
        std::uint64_t bits;
        std::memcpy(&bits, &d, sizeof(bits));
        u64(bits);
    }

    void str(const std::string& s) {
        m_out.insert(m_out.end(), s.begin(), s.end());
        const std::size_t total = padded(s.size() + 1);
        m_out.insert(m_out.end(), total - s.size(), std::uint8_t(0));
    }

    void blob(const Blob& b) {
        u32(static_cast<std::uint32_t>(b.size()));
        m_out.insert(m_out.end(), b.begin(), b.end());
        m_out.insert(m_out.end(), padded(b.size()) - b.size(), std::uint8_t(0));
    }

    void raw(const void* data, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        m_out.insert(m_out.end(), p, p + n);
    }

    std::size_t size() const noexcept { return m_out.size(); }

    void patch_u32(std::size_t at, std::uint32_t v) {
        m_out[at] = std::uint8_t(v >> 24);
        m_out[at + 1] = std::uint8_t(v >> 16);
        m_out[at + 2] = std::uint8_t(v >> 8);
        m_out[at + 3] = std::uint8_t(v);
    }

private:
    std::vector<std::uint8_t>& m_out;
};

void append_tags(const std::vector<Value>& args, std::string& tags) {
    for (const Value& v : args) {
        if (v.is_array()) {
            tags += '[';
            append_tags(v.as_array(), tags);
            tags += ']';
        } else {
            tags += v.tag();
        }
    }
}

void write_args(Writer& w, const std::vector<Value>& args) {
    for (const Value& v : args) {
        switch (v.type()) {
        case Type::Int32: w.u32(static_cast<std::uint32_t>(v.as_int32())); break;
        case Type::Float32: w.f32(v.as_float()); break;
        case Type::String: w.str(v.as_string()); break;
        case Type::Symbol: w.str(v.as_text()); break;
        case Type::Blob: w.blob(v.as_blob()); break;
        case Type::Int64: w.u64(static_cast<std::uint64_t>(v.as_int64())); break;
        case Type::Double: w.f64(v.as_double()); break;
        case Type::TimeTag: w.u64(v.as_time().raw()); break;
        case Type::Char: w.u32(static_cast<unsigned char>(v.as_char())); break;
        case Type::Color: w.u32(v.as_color().packed()); break;
        case Type::Midi: w.u32(v.as_midi().packed()); break;
        case Type::Array: write_args(w, v.as_array()); break;
        case Type::True:
        case Type::False:
        case Type::Nil:
        case Type::Impulse: break;
        }
    }
}

void write_message(Writer& w, const Message& m) {
    if (m.address().empty() || m.address()[0] != '/') throw std::invalid_argument("osc: an address has to start with '/', not \"" + m.address() + "\"");
    w.str(m.address());
    std::string tags = ",";
    append_tags(m.args(), tags);
    w.str(tags);
    write_args(w, m.args());
}

void write_packet(Writer& w, const Packet& p, std::size_t depth);

void write_bundle(Writer& w, const Bundle& b, std::size_t depth) {
    if (depth > max_depth) throw std::invalid_argument("osc: bundles are nested too deeply");
    w.raw(bundle_tag, sizeof(bundle_tag));
    w.u64(b.time().raw());
    for (const Packet& e : b.elements()) {
        const std::size_t at = w.size();
        w.u32(0);
        write_packet(w, e, depth + 1);
        w.patch_u32(at, static_cast<std::uint32_t>(w.size() - at - 4));
    }
}

void write_packet(Writer& w, const Packet& p, std::size_t depth) {
    if (p.is_bundle()) write_bundle(w, p.bundle(), depth);
    else write_message(w, p.message());
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size, std::size_t base) : m_data(data), m_size(size), m_base(base) {}

    std::size_t offset() const noexcept { return m_base + m_pos; }
    std::size_t left() const noexcept { return m_size - m_pos; }
    bool done() const noexcept { return m_pos >= m_size; }

    [[noreturn]] void fail(const std::string& what) const { throw DecodeError(what, offset()); }

    void need(std::size_t n, const char* what) const {
        if (left() < n) fail(std::string("the packet ends in the middle of ") + what);
    }

    std::uint32_t u32(const char* what = "a 32-bit value") {
        need(4, what);
        const std::uint8_t* p = m_data + m_pos;
        m_pos += 4;
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
    }

    std::uint64_t u64(const char* what = "a 64-bit value") {
        const std::uint64_t hi = u32(what);
        const std::uint64_t lo = u32(what);
        return (hi << 32) | lo;
    }

    float f32() {
        const std::uint32_t bits = u32("a float");
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    double f64() {
        const std::uint64_t bits = u64("a double");
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }

    std::string str(const char* what) {
        const std::uint8_t* start = m_data + m_pos;
        const void* nul = std::memchr(start, 0, left());
        if (!nul) fail(std::string(what) + " has no terminating zero byte");
        const std::size_t len = static_cast<std::size_t>(static_cast<const std::uint8_t*>(nul) - start);
        const std::size_t total = padded(len + 1);
        if (total > left()) fail(std::string(what) + " isn't padded to 4 bytes");
        std::string s(reinterpret_cast<const char*>(start), len);
        m_pos += total;
        return s;
    }

    Blob blob() {
        const std::uint32_t n = u32("a blob's size");
        if (n > left()) fail("a blob says it has " + std::to_string(n) + " bytes but only " + std::to_string(left()) + " are left");
        const std::size_t total = padded(n);
        if (total > left()) fail("a blob isn't padded to 4 bytes");
        Blob b(m_data + m_pos, m_data + m_pos + n);
        m_pos += total;
        return b;
    }

    Reader sub(std::size_t n) {
        need(n, "a bundle element");
        Reader r(m_data + m_pos, n, offset());
        m_pos += n;
        return r;
    }

    const std::uint8_t* here() const noexcept { return m_data + m_pos; }

private:
    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_base;
    std::size_t m_pos = 0;
};

std::vector<Value> read_args(Reader& r, const std::string& tags, std::size_t& i, std::size_t depth) {
    std::vector<Value> out;
    while (i < tags.size()) {
        const char t = tags[i++];
        switch (t) {
        case 'i': out.emplace_back(static_cast<std::int32_t>(r.u32("an int32"))); break;
        case 'f': out.emplace_back(r.f32()); break;
        case 's': out.emplace_back(r.str("a string")); break;
        case 'S': out.emplace_back(Symbol(r.str("a symbol"))); break;
        case 'b': out.emplace_back(r.blob()); break;
        case 'h': out.push_back(Value::int64(static_cast<std::int64_t>(r.u64("an int64")))); break;
        case 'd': out.emplace_back(r.f64()); break;
        case 't': out.emplace_back(TimeTag(r.u64("a time tag"))); break;
        case 'c': out.emplace_back(static_cast<char>(r.u32("a char") & 0xffu)); break;
        case 'r': out.emplace_back(Color::from_packed(r.u32("a color"))); break;
        case 'm': out.emplace_back(Midi::from_packed(r.u32("a MIDI message"))); break;
        case 'T': out.emplace_back(true); break;
        case 'F': out.emplace_back(false); break;
        case 'N': out.emplace_back(Nil{}); break;
        case 'I': out.emplace_back(Impulse{}); break;
        case '[': {
            if (depth >= max_depth) r.fail("arrays are nested too deeply");
            Array items = read_args(r, tags, i, depth + 1);
            if (i == 0 || tags[i - 1] != ']') r.fail("an array in the type tags is never closed with ']'");
            out.emplace_back(std::move(items));
            break;
        }
        case ']':
            if (depth == 0) r.fail("the type tags close an array that was never opened");
            return out;
        default: r.fail(std::string("unknown type tag '") + t + "'");
        }
    }
    if (depth > 0) r.fail("an array in the type tags is never closed with ']'");
    return out;
}

Message read_message(Reader& r) {
    std::string address = r.str("the address");
    if (address.empty() || address[0] != '/') r.fail("the address has to start with '/'");
    Message m(std::move(address));
    if (r.done()) return m;
    std::string tags = r.str("the type tags");
    if (tags.empty() || tags[0] != ',') r.fail("the type tags have to start with ','");
    std::size_t i = 1;
    m.args() = read_args(r, tags, i, 0);
    return m;
}

Packet read_packet(Reader& r, std::size_t depth);

Bundle read_bundle(Reader& r, std::size_t depth) {
    if (depth > max_depth) r.fail("bundles are nested too deeply");
    r.need(8, "the bundle header");
    if (std::memcmp(r.here(), bundle_tag, sizeof(bundle_tag)) != 0) r.fail("not a bundle");
    r.sub(8);
    Bundle b(TimeTag(r.u64("the bundle's time tag")));
    while (!r.done()) {
        const std::uint32_t n = r.u32("a bundle element's size");
        if (n % 4 != 0) r.fail("a bundle element's size isn't a multiple of 4");
        Reader sub = r.sub(n);
        b.elements().push_back(read_packet(sub, depth + 1));
    }
    return b;
}

Packet read_packet(Reader& r, std::size_t depth) {
    if (r.left() >= 8 && std::memcmp(r.here(), bundle_tag, sizeof(bundle_tag)) == 0) return Packet(read_bundle(r, depth));
    if (r.left() == 0) r.fail("the packet is empty");
    if (r.left() % 4 != 0) r.fail("the packet's size isn't a multiple of 4");
    return Packet(read_message(r));
}

} // namespace

std::vector<std::uint8_t> encode(const Message& message) {
    std::vector<std::uint8_t> out;
    out.reserve(64);
    Writer w(out);
    write_message(w, message);
    return out;
}

std::vector<std::uint8_t> encode(const Bundle& bundle) {
    std::vector<std::uint8_t> out;
    Writer w(out);
    write_bundle(w, bundle, 0);
    return out;
}

std::vector<std::uint8_t> encode(const Packet& packet) {
    std::vector<std::uint8_t> out;
    Writer w(out);
    write_packet(w, packet, 0);
    return out;
}

std::size_t encoded_size(const Message& message) {
    return encode(message).size();
}

DecodeResult try_decode(const void* data, std::size_t size) {
    DecodeResult result;
    try {
        Reader r(static_cast<const std::uint8_t*>(data), data ? size : 0, 0);
        result.packet = read_packet(r, 0);
    } catch (const DecodeError& e) {
        result.error = e.what();
        result.offset = e.offset();
    }
    return result;
}

std::optional<Packet> decode(const void* data, std::size_t size, std::string* error) {
    DecodeResult r = try_decode(data, size);
    if (!r.ok() && error) *error = r.error;
    return std::move(r.packet);
}

std::optional<Packet> decode(const std::vector<std::uint8_t>& bytes, std::string* error) {
    return decode(bytes.data(), bytes.size(), error);
}

Packet decode_or_throw(const void* data, std::size_t size) {
    Reader r(static_cast<const std::uint8_t*>(data), data ? size : 0, 0);
    return read_packet(r, 0);
}

std::optional<Message> decode_message(const void* data, std::size_t size, std::string* error) {
    std::optional<Packet> p = decode(data, size, error);
    if (!p) return std::nullopt;
    if (!p->is_message()) {
        if (error) *error = "osc: the packet is a bundle, not a message";
        return std::nullopt;
    }
    return std::move(p->message());
}

std::optional<Message> decode_message(const std::vector<std::uint8_t>& bytes, std::string* error) {
    return decode_message(bytes.data(), bytes.size(), error);
}

bool looks_like_bundle(const void* data, std::size_t size) noexcept {
    return data && size >= 16 && std::memcmp(data, bundle_tag, sizeof(bundle_tag)) == 0;
}

std::string hex_dump(const std::vector<std::uint8_t>& bytes, std::size_t max_bytes) {
    std::string out;
    const std::size_t n = bytes.size() < max_bytes ? bytes.size() : max_bytes;
    for (std::size_t row = 0; row < n; row += 16) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04zx  ", row);
        out += buf;
        std::string text;
        for (std::size_t i = row; i < row + 16; ++i) {
            if (i < n) {
                std::snprintf(buf, sizeof(buf), "%02x ", bytes[i]);
                out += buf;
                const char c = static_cast<char>(bytes[i]);
                text += (bytes[i] >= 0x20 && bytes[i] < 0x7f) ? c : '.';
            } else {
                out += "   ";
            }
        }
        out += " " + text + "\n";
    }
    if (n < bytes.size()) out += "... " + std::to_string(bytes.size() - n) + " more bytes\n";
    return out;
}

std::vector<std::uint8_t> Message::encode() const {
    return osc::encode(*this);
}

void Message::encode_into(std::vector<std::uint8_t>& out) const {
    Writer w(out);
    write_message(w, *this);
}

std::vector<std::uint8_t> Bundle::encode() const {
    return osc::encode(*this);
}

void Bundle::encode_into(std::vector<std::uint8_t>& out) const {
    Writer w(out);
    write_bundle(w, *this, 0);
}

std::vector<std::uint8_t> Packet::encode() const {
    return osc::encode(*this);
}

void Packet::encode_into(std::vector<std::uint8_t>& out) const {
    Writer w(out);
    write_packet(w, *this, 0);
}

} // namespace osc
