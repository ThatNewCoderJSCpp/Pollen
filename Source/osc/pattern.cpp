#include "osc/pattern.hpp"

#include <utility>

namespace osc {

namespace {

bool match_from(std::string_view p, std::string_view a, int budget);

bool class_matches(std::string_view cls, char c) {
    bool negate = false;
    std::size_t i = 0;
    if (!cls.empty() && cls[0] == '!') {
        negate = true;
        i = 1;
    }
    bool hit = false;
    while (i < cls.size()) {
        if (i + 2 < cls.size() && cls[i + 1] == '-') {
            char lo = cls[i], hi = cls[i + 2];
            if (lo > hi) std::swap(lo, hi);
            if (c >= lo && c <= hi) hit = true;
            i += 3;
        } else {
            if (cls[i] == c) hit = true;
            ++i;
        }
    }
    return negate ? !hit : hit;
}

bool match_from(std::string_view p, std::string_view a, int budget) {
    if (--budget < 0) return false;
    while (!p.empty()) {
        const char c = p[0];
        if (c == '/' && p.size() > 1 && p[1] == '/') {
            if (a.empty() || a[0] != '/') return false;
            const std::string_view rest = p.substr(1);
            for (std::size_t k = 0; k < a.size(); ++k) {
                if (a[k] == '/' && match_from(rest, a.substr(k), budget)) return true;
            }
            return false;
        }
        if (c == '*') {
            while (!p.empty() && p[0] == '*') p.remove_prefix(1);
            for (std::size_t k = 0;; ++k) {
                if (match_from(p, a.substr(k), budget)) return true;
                if (k >= a.size() || a[k] == '/') return false;
            }
        }
        if (a.empty()) return false;
        if (c == '?') {
            if (a[0] == '/') return false;
        } else if (c == '[') {
            const std::size_t close = p.find(']', 1);
            if (close == std::string_view::npos) return false;
            if (a[0] == '/' || !class_matches(p.substr(1, close - 1), a[0])) return false;
            p.remove_prefix(close + 1);
            a.remove_prefix(1);
            continue;
        } else if (c == '{') {
            const std::size_t close = p.find('}', 1);
            if (close == std::string_view::npos) return false;
            const std::string_view options = p.substr(1, close - 1);
            const std::string_view rest = p.substr(close + 1);
            std::size_t start = 0;
            while (start <= options.size()) {
                std::size_t comma = options.find(',', start);
                if (comma == std::string_view::npos) comma = options.size();
                const std::string_view option = options.substr(start, comma - start);
                if (a.substr(0, option.size()) == option && option.find('/') == std::string_view::npos && match_from(rest, a.substr(option.size()), budget)) return true;
                start = comma + 1;
            }
            return false;
        } else if (c != a[0]) {
            return false;
        }
        p.remove_prefix(1);
        a.remove_prefix(1);
    }
    return a.empty();
}

} // namespace

bool matches(std::string_view pattern, std::string_view address) {
    if (pattern == address) return true;
    return match_from(pattern, address, 100000);
}

bool is_pattern(std::string_view address) noexcept {
    if (address.find("//") != std::string_view::npos) return true;
    return address.find_first_of("*?[]{}") != std::string_view::npos;
}

bool is_valid_address(std::string_view address) noexcept {
    if (address.empty() || address[0] != '/') return false;
    for (char c : address) {
        if (c == ' ' || c == '#' || c == ',' || c == '*' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}' || static_cast<unsigned char>(c) < 0x20) return false;
    }
    return true;
}

bool is_valid_pattern(std::string_view pattern) noexcept {
    if (pattern.empty() || pattern[0] != '/') return false;
    int square = 0, curly = 0;
    for (char c : pattern) {
        if (c == ' ' || c == '#' || static_cast<unsigned char>(c) < 0x20) return false;
        if (c == '[') {
            if (square || curly) return false;
            ++square;
        } else if (c == ']') {
            if (!square) return false;
            --square;
        } else if (c == '{') {
            if (square || curly) return false;
            ++curly;
        } else if (c == '}') {
            if (!curly) return false;
            --curly;
        } else if (c == ',' && !curly) {
            return false;
        }
    }
    return square == 0 && curly == 0;
}

} // namespace osc
