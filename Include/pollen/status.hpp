#ifndef POLLEN_STATUS_HPP
#define POLLEN_STATUS_HPP

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace pollen {

using StatusValues = std::map<std::string, std::string>;

std::vector<std::string> split_status_pages(const std::string& text);

std::string render_status(const std::string& page, const StatusValues& values, std::vector<std::string>* unknown = nullptr);

std::vector<std::string> status_placeholders();

std::string expand_rotations(const std::string& page, std::size_t turn, const StatusValues& values);
std::string render_status_turn(const std::string& page, std::size_t turn, const StatusValues& values);
std::string strip_rotation_markers(const std::string& text);

class StatusRotation {
public:
    void set_template(const std::string& text);
    const std::vector<std::string>& pages() const noexcept { return m_pages; }

    bool due(std::chrono::steady_clock::time_point now, double interval_seconds) const;
    std::string next(const StatusValues& values, std::chrono::steady_clock::time_point now);
    std::string peek(const StatusValues& values) const;
    std::string showing(const StatusValues& values, std::chrono::steady_clock::time_point now, double interval_seconds, bool sending) const;
    bool sent() const noexcept { return m_sent; }
    void reset() noexcept;
    std::size_t index() const noexcept { return m_index; }

private:
    std::vector<std::string> m_pages;
    std::vector<std::size_t> m_turns;
    std::size_t m_index = 0;
    bool m_sent = false;
    std::chrono::steady_clock::time_point m_last;
};

} // namespace pollen

#endif // POLLEN_STATUS_HPP
