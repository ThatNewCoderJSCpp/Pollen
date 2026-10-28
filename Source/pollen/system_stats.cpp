#include "pollen/system_stats.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <ctime>
#endif

namespace pollen {

namespace {

#if defined(_WIN32)
std::uint64_t file_time(const FILETIME& t) {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
#endif

} // namespace

SystemStats::SystemStats() {
    update();
}

bool SystemStats::read_cpu(std::uint64_t& idle, std::uint64_t& total) {
#if defined(_WIN32)
    FILETIME idle_t, kernel_t, user_t;
    if (!GetSystemTimes(&idle_t, &kernel_t, &user_t)) return false;
    idle = file_time(idle_t);
    total = file_time(kernel_t) + file_time(user_t);
    return true;
#elif defined(__APPLE__)
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&info), &count) != KERN_SUCCESS) return false;
    idle = info.cpu_ticks[CPU_STATE_IDLE];
    total = 0;
    for (int i = 0; i < CPU_STATE_MAX; ++i) total += info.cpu_ticks[i];
    return true;
#else
    std::ifstream in("/proc/stat");
    std::string cpu;
    if (!(in >> cpu) || cpu != "cpu") return false;
    std::uint64_t v[10] = {};
    for (int i = 0; i < 10; ++i) {
        if (!(in >> v[i])) break;
    }
    idle = v[3] + v[4];
    total = 0;
    for (int i = 0; i < 8; ++i) total += v[i];
    return total > 0;
#endif
}

const SystemSnapshot& SystemStats::update() {
    std::uint64_t idle = 0, total = 0;
    if (read_cpu(idle, total)) {
        if (m_have_last && total > m_last_total) {
            const double dt = static_cast<double>(total - m_last_total);
            const double di = static_cast<double>(idle >= m_last_idle ? idle - m_last_idle : 0);
            double pct = 100.0 * (1.0 - di / dt);
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;
            m_snapshot.cpu_percent = pct;
            m_snapshot.cpu_known = true;
        }
        m_last_idle = idle;
        m_last_total = total;
        m_have_last = true;
    }

#if defined(_WIN32)
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        m_snapshot.memory_total = mem.ullTotalPhys;
        m_snapshot.memory_used = mem.ullTotalPhys - mem.ullAvailPhys;
        m_snapshot.memory_known = true;
    }
    m_snapshot.uptime_seconds = static_cast<double>(GetTickCount64()) / 1000.0;
#elif defined(__APPLE__)
    std::uint64_t memsize = 0;
    std::size_t len = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) {
        vm_statistics64_data_t vm;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
            const std::uint64_t page = static_cast<std::uint64_t>(vm_kernel_page_size);
            const std::uint64_t used = (static_cast<std::uint64_t>(vm.active_count) + vm.wire_count + vm.compressor_page_count) * page;
            m_snapshot.memory_total = memsize;
            m_snapshot.memory_used = used < memsize ? used : memsize;
            m_snapshot.memory_known = true;
        }
    }
    struct timeval boot;
    len = sizeof(boot);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boot, &len, nullptr, 0) == 0) m_snapshot.uptime_seconds = static_cast<double>(std::time(nullptr) - boot.tv_sec);
#else
    std::ifstream mem("/proc/meminfo");
    std::string key, unit;
    std::uint64_t value = 0, total_kb = 0, avail_kb = 0;
    while (mem >> key >> value) {
        std::getline(mem, unit);
        if (key == "MemTotal:") total_kb = value;
        else if (key == "MemAvailable:") avail_kb = value;
    }
    if (total_kb) {
        m_snapshot.memory_total = total_kb * 1024ull;
        m_snapshot.memory_used = (total_kb - (avail_kb < total_kb ? avail_kb : total_kb)) * 1024ull;
        m_snapshot.memory_known = true;
    }
    std::ifstream up("/proc/uptime");
    double secs = 0.0;
    if (up >> secs) m_snapshot.uptime_seconds = secs;
#endif
    return m_snapshot;
}

std::string format_bytes(std::uint64_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof(buf), "%llu %s", static_cast<unsigned long long>(bytes), units[u]);
    else std::snprintf(buf, sizeof(buf), v < 10.0 ? "%.1f %s" : "%.0f %s", v, units[u]);
    return buf;
}

std::string format_duration(double seconds) {
    if (seconds < 0) seconds = 0;
    const long long s = static_cast<long long>(seconds);
    const long long d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60, sec = s % 60;
    char buf[48];
    if (d > 0) std::snprintf(buf, sizeof(buf), "%lldd %lldh %lldm", d, h, m);
    else if (h > 0) std::snprintf(buf, sizeof(buf), "%lldh %lldm", h, m);
    else if (m > 0) std::snprintf(buf, sizeof(buf), "%lldm %llds", m, sec);
    else std::snprintf(buf, sizeof(buf), "%llds", sec);
    return buf;
}

} // namespace pollen
