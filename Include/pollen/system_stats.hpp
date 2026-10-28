#ifndef POLLEN_SYSTEM_STATS_HPP
#define POLLEN_SYSTEM_STATS_HPP

#include <cstdint>
#include <string>

namespace pollen {

struct SystemSnapshot {
    bool cpu_known = false;
    double cpu_percent = 0.0;
    bool memory_known = false;
    std::uint64_t memory_used = 0;
    std::uint64_t memory_total = 0;
    double uptime_seconds = 0.0;

    double memory_percent() const noexcept { return memory_total ? 100.0 * static_cast<double>(memory_used) / static_cast<double>(memory_total) : 0.0; }
};

class SystemStats {
public:
    SystemStats();

    const SystemSnapshot& update();
    const SystemSnapshot& snapshot() const noexcept { return m_snapshot; }

private:
    bool read_cpu(std::uint64_t& idle, std::uint64_t& total);

    SystemSnapshot m_snapshot;
    std::uint64_t m_last_idle = 0;
    std::uint64_t m_last_total = 0;
    bool m_have_last = false;
};

std::string format_bytes(std::uint64_t bytes);
std::string format_duration(double seconds);

} // namespace pollen

#endif // POLLEN_SYSTEM_STATS_HPP
