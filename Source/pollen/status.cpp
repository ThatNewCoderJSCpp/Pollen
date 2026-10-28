#include "pollen/status.hpp"

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

} // namespace

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
        int placeholders = 0, filled = 0;
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
                    }
                    i = j;
                    continue;
                }
            }
            rendered += c;
        }
        if (placeholders > 0 && filled == 0) continue;
        if (!first) out += '\n';
        out += trim_line(rendered);
        first = false;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

std::vector<std::string> status_placeholders() {
    return { "time", "time24", "date", "weekday", "cpu", "ram", "ram_used", "ram_total", "uptime", "song", "artist", "title", "player", "media", "avatar", "params" };
}

void StatusRotation::set_template(const std::string& text) {
    std::vector<std::string> pages = split_status_pages(text);
    if (pages != m_pages) {
        m_pages = std::move(pages);
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
    const std::string text = render_status(m_pages[m_index], values);
    m_index = (m_index + 1) % m_pages.size();
    m_last = now;
    m_sent = true;
    return text;
}

std::string StatusRotation::peek(const StatusValues& values) const {
    if (m_pages.empty()) return std::string();
    return render_status(m_pages[m_index < m_pages.size() ? m_index : 0], values);
}

void StatusRotation::reset() noexcept {
    m_index = 0;
    m_sent = false;
}

} // namespace pollen
