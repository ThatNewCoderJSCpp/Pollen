#include "pollen/status.hpp"
#include "pollen/modules.hpp"

namespace pollen {

namespace {

bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string trim_line(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

const char* const separators[] = { "\xc2\xb7", "\xe2\x80\xa2", "\xe2\x80\x94", "\xe2\x80\x93", "|", "/", "-", ",", ":" };

bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

std::string tidy_line(std::string line) {
    bool changed = true;
    while (changed) {
        changed = false;
        std::string collapsed;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
            collapsed += line[i];
        }
        if (collapsed != line) {
            line = collapsed;
            changed = true;
        }
        line = trim_line(line);
        for (const char* sep : separators) {
            const std::string s(sep);
            if (line == s) {
                line.clear();
                changed = true;
                break;
            }
            if (ends_with(line, " " + s)) {
                line.erase(line.size() - s.size() - 1);
                changed = true;
            }
            if (line.compare(0, s.size() + 1, s + " ") == 0) {
                line.erase(0, s.size() + 1);
                changed = true;
            }
            const std::string doubled = s + " " + s;
            const std::size_t at = line.find(" " + doubled + " ");
            if (at != std::string::npos) {
                line.replace(at, doubled.size() + 2, " " + s + " ");
                changed = true;
            }
        }
    }
    return line;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

bool block_start(const std::string& raw, std::size_t& every) {
    const std::string line = trim_line(raw);
    if (line == "[rotate]") {
        every = 1;
        return true;
    }
    const std::string head = "[rotate every ";
    if (line.size() > head.size() + 1 && line.compare(0, head.size(), head) == 0 && line.back() == ']') {
        const std::string n = trim_line(line.substr(head.size(), line.size() - head.size() - 1));
        if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos || n.size() > 6) return false;
        every = static_cast<std::size_t>(std::stoul(n));
        if (every == 0) every = 1;
        return true;
    }
    return false;
}

std::string pick_choice(const std::vector<std::string>& choices, std::size_t turn, std::size_t every, const StatusValues& values) {
    if (choices.empty()) return std::string();
    const std::size_t n = choices.size();
    const std::size_t first = (turn / (every ? every : 1)) % n;
    for (std::size_t k = 0; k < n; ++k) {
        const std::string& c = choices[(first + k) % n];
        if (!render_status(c, values).empty()) return c;
    }
    return choices[first];
}

std::string expand_inline(const std::string& line, std::size_t turn, const StatusValues& values) {
    std::string out;
    std::size_t i = 0;
    while (i < line.size()) {
        const std::size_t open = line.find("[[", i);
        if (open == std::string::npos) break;
        const std::size_t close = line.find("]]", open + 2);
        if (close == std::string::npos) break;
        out += line.substr(i, open - i);
        const std::string body = line.substr(open + 2, close - open - 2);
        std::vector<std::string> choices;
        std::size_t s = 0;
        while (true) {
            const std::size_t bar = body.find("||", s);
            choices.push_back(trim_line(body.substr(s, bar == std::string::npos ? std::string::npos : bar - s)));
            if (bar == std::string::npos) break;
            s = bar + 2;
        }
        out += pick_choice(choices, turn, 1, values);
        i = close + 2;
    }
    out += line.substr(i);
    return out;
}

} // namespace

std::string expand_rotations(const std::string& page, std::size_t turn, const StatusValues& values) {
    const std::vector<std::string> lines = split_lines(page);
    std::vector<std::string> out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::size_t every = 1;
        if (!block_start(lines[i], every)) {
            out.push_back(expand_inline(lines[i], turn, values));
            continue;
        }
        std::vector<std::string> body;
        std::size_t j = i + 1;
        while (j < lines.size() && trim_line(lines[j]) != "[/rotate]") body.push_back(lines[j++]);
        i = j;
        bool separated = false;
        for (const std::string& b : body) separated = separated || trim_line(b) == "--";
        std::vector<std::string> choices;
        if (separated) {
            std::string cur;
            bool any = false;
            for (const std::string& b : body) {
                if (trim_line(b) == "--") {
                    if (any) choices.push_back(cur);
                    cur.clear();
                    any = false;
                } else {
                    cur += (any ? "\n" : "") + b;
                    any = true;
                }
            }
            if (any) choices.push_back(cur);
        } else {
            for (const std::string& b : body) {
                if (!trim_line(b).empty()) choices.push_back(b);
            }
        }
        std::vector<std::string> expanded;
        for (const std::string& c : choices) expanded.push_back(expand_inline(c, turn, values));
        const std::string chosen = pick_choice(expanded, turn, every, values);
        if (!chosen.empty()) out.push_back(chosen);
    }
    std::string text;
    for (std::size_t i = 0; i < out.size(); ++i) text += (i ? "\n" : "") + out[i];
    return text;
}

std::string render_status_turn(const std::string& page, std::size_t turn, const StatusValues& values) {
    return render_status(expand_rotations(page, turn, values), values);
}

std::string strip_rotation_markers(const std::string& text) {
    return expand_rotations(text, 0, StatusValues());
}

std::vector<std::string> split_status_pages(const std::string& text) {
    std::vector<std::string> pages;
    std::string cur;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        if (trim_line(line) == "---") {
            if (!trim_line(cur).empty()) pages.push_back(cur);
            cur.clear();
        } else {
            if (!cur.empty()) cur += '\n';
            cur += line;
        }
        start = end + 1;
    }
    if (!trim_line(cur).empty()) pages.push_back(cur);
    return pages;
}

std::string render_status(const std::string& page, const StatusValues& values, std::vector<std::string>* unknown) {
    std::string out;
    std::size_t start = 0;
    bool first = true;
    while (start <= page.size()) {
        std::size_t end = page.find('\n', start);
        if (end == std::string::npos) end = page.size();
        const std::string line = page.substr(start, end - start);
        start = end + 1;

        std::string rendered;
        int placeholders = 0, filled = 0, blanks = 0;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (c == '{' && i + 1 < line.size() && line[i + 1] == '{') {
                rendered += '{';
                ++i;
                continue;
            }
            if (c == '}' && i + 1 < line.size() && line[i + 1] == '}') {
                rendered += '}';
                ++i;
                continue;
            }
            if (c == '{') {
                std::size_t j = i + 1;
                while (j < line.size() && name_char(line[j])) ++j;
                if (j < line.size() && line[j] == ':' && j > i + 1) {
                    const std::size_t close = line.find('}', j);
                    const std::size_t inner = line.find('{', j);
                    if (close != std::string::npos && (inner == std::string::npos || inner > close)) j = close;
                }
                if (j < line.size() && line[j] == '}' && j > i + 1) {
                    const std::string name = line.substr(i + 1, j - i - 1);
                    ++placeholders;
                    const auto it = values.find(name);
                    if (it == values.end()) {
                        if (unknown) unknown->push_back(name);
                        rendered += line.substr(i, j - i + 1);
                        ++filled;
                    } else if (!it->second.empty()) {
                        rendered += it->second;
                        ++filled;
                    } else {
                        ++blanks;
                    }
                    i = j;
                    continue;
                }
            }
            rendered += c;
        }
        if (placeholders > 0 && filled == 0) continue;
        if (!first) out += '\n';
        out += blanks > 0 ? tidy_line(rendered) : trim_line(rendered);
        first = false;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::vector<std::string> status_placeholders() {
    std::vector<std::string> out;
    for (const ModuleDef& m : module_defs()) {
        for (const PlaceholderDef& p : m.placeholders) out.push_back(p.name);
    }
    return out;
}

void StatusRotation::set_template(const std::string& text) {
    std::vector<std::string> pages = split_status_pages(text);
    if (pages != m_pages) {
        m_pages = std::move(pages);
        m_turns.resize(m_pages.size(), 0);
        if (m_index >= m_pages.size()) m_index = 0;
    }
}

bool StatusRotation::due(std::chrono::steady_clock::time_point now, double interval_seconds) const {
    if (m_pages.empty()) return false;
    if (!m_sent) return true;
    return std::chrono::duration<double>(now - m_last).count() >= interval_seconds;
}

std::string StatusRotation::next(const StatusValues& values, std::chrono::steady_clock::time_point now) {
    if (m_pages.empty()) return std::string();
    if (m_index >= m_pages.size()) m_index = 0;
    m_turns.resize(m_pages.size(), 0);
    const std::string text = render_status_turn(m_pages[m_index], m_turns[m_index], values);
    ++m_turns[m_index];
    m_index = (m_index + 1) % m_pages.size();
    m_last = now;
    m_sent = true;
    return text;
}

std::string StatusRotation::peek(const StatusValues& values) const {
    if (m_pages.empty()) return std::string();
    const std::size_t i = m_index < m_pages.size() ? m_index : 0;
    return render_status_turn(m_pages[i], i < m_turns.size() ? m_turns[i] : 0, values);
}

std::string StatusRotation::showing(const StatusValues& values, std::chrono::steady_clock::time_point now, double interval_seconds, bool sending) const {
    if (m_pages.empty()) return std::string();
    const std::size_t n = m_pages.size();
    if (sending && m_sent) {
        const std::size_t last = (m_index + n - 1) % n;
        const std::size_t turn = last < m_turns.size() && m_turns[last] > 0 ? m_turns[last] - 1 : 0;
        return render_status_turn(m_pages[last], turn, values);
    }
    if (interval_seconds < 0.5) interval_seconds = 0.5;
    const double t = std::chrono::duration<double>(now.time_since_epoch()).count();
    const std::size_t step = static_cast<std::size_t>(t / interval_seconds);
    return render_status_turn(m_pages[step % n], step / n, values);
}

void StatusRotation::reset() noexcept {
    m_index = 0;
    for (std::size_t& t : m_turns) t = 0;
    m_sent = false;
}

} // namespace pollen
