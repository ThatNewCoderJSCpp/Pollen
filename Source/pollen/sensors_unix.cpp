#include "pollen/sensors.hpp"
#include "nvml.hpp"

#if !defined(_WIN32)

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <ctime>
#endif

namespace pollen {

namespace fs = std::filesystem;

struct Sampler::Impl {
    fs::path root;
    Nvml nvml;
    bool first = true;
    std::uint64_t last_idle = 0;
    std::uint64_t last_total = 0;
    bool have_last = false;
    std::string cpu_name;
    long vrchat_pid = 0;
    std::chrono::steady_clock::time_point next_vrchat_scan;
    std::optional<linux_sensors::DrmGpu> drm_static;
    std::string drm_name;

    void cpu_percent(SensorSnapshot& s, std::uint64_t idle, std::uint64_t total) {
        if (have_last && total > last_total) {
            const double dt = static_cast<double>(total - last_total);
            const double di = static_cast<double>(idle >= last_idle ? idle - last_idle : 0);
            double pct = 100.0 * (1.0 - di / dt);
            if (pct < 0.0) pct = 0.0;
            if (pct > 100.0) pct = 100.0;
            s.cpu_percent = pct;
        }
        last_idle = idle;
        last_total = total;
        have_last = true;
    }

#if defined(__APPLE__)
    void sample(SensorSnapshot& s) {
        host_cpu_load_info_data_t info;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&info), &count) == KERN_SUCCESS) {
            std::uint64_t total = 0;
            for (int i = 0; i < CPU_STATE_MAX; ++i) total += info.cpu_ticks[i];
            cpu_percent(s, info.cpu_ticks[CPU_STATE_IDLE], total);
        }
        if (cpu_name.empty()) {
            char buf[256] = {};
            std::size_t len = sizeof(buf);
            if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) cpu_name = buf;
        }
        s.cpu_name = cpu_name;
        std::uint64_t hz = 0;
        std::size_t len = sizeof(hz);
        if (sysctlbyname("hw.cpufrequency", &hz, &len, nullptr, 0) == 0 && hz > 0) s.cpu_mhz = static_cast<double>(hz) / 1e6;
        std::uint64_t memsize = 0;
        len = sizeof(memsize);
        if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) {
            vm_statistics64_data_t vm;
            mach_msg_type_number_t vcount = HOST_VM_INFO64_COUNT;
            if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &vcount) == KERN_SUCCESS) {
                const std::uint64_t page = static_cast<std::uint64_t>(vm_kernel_page_size);
                const std::uint64_t used = (static_cast<std::uint64_t>(vm.active_count) + vm.wire_count + vm.compressor_page_count) * page;
                s.ram_total = static_cast<double>(memsize);
                s.ram_used = static_cast<double>(used < memsize ? used : memsize);
            }
        }
        mach_task_basic_info_data_t task;
        mach_msg_type_number_t tcount = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&task), &tcount) == KERN_SUCCESS) s.ram_app = static_cast<double>(task.resident_size);
        struct timeval boot;
        len = sizeof(boot);
        int mib[2] = { CTL_KERN, KERN_BOOTTIME };
        if (sysctl(mib, 2, &boot, &len, nullptr, 0) == 0) s.uptime = static_cast<double>(std::time(nullptr) - boot.tv_sec);
        s.notes = "temperatures and GPU readings aren't available on macOS yet";
    }
#else
    void sample(SensorSnapshot& s) {
        if (const auto t = linux_sensors::read_cpu_times(root)) cpu_percent(s, t->idle, t->total);
        if (cpu_name.empty()) cpu_name = linux_sensors::read_cpu_name(root);
        s.cpu_name = cpu_name;
        s.cpu_mhz = linux_sensors::read_cpu_mhz(root);
        s.cpu_temp = linux_sensors::read_cpu_temp(root);

        std::ifstream meminfo(root / "proc" / "meminfo");
        std::string key, unit;
        double value = 0.0, total_kb = 0.0, avail_kb = -1.0;
        while (meminfo >> key >> value) {
            std::getline(meminfo, unit);
            if (key == "MemTotal:") total_kb = value;
            else if (key == "MemAvailable:") avail_kb = value;
        }
        if (total_kb > 0.0 && avail_kb >= 0.0) {
            s.ram_total = total_kb * 1024.0;
            s.ram_used = (total_kb - (avail_kb < total_kb ? avail_kb : total_kb)) * 1024.0;
        }
        const long self = root == "/" ? static_cast<long>(getpid()) : 1;
        s.ram_app = linux_sensors::read_process_rss(root, self);

        const auto now = std::chrono::steady_clock::now();
        if (vrchat_pid > 0) {
            std::error_code ec;
            if (!fs::exists(root / "proc" / std::to_string(vrchat_pid), ec)) vrchat_pid = 0;
        }
        if (vrchat_pid == 0 && now >= next_vrchat_scan) {
            vrchat_pid = linux_sensors::find_process(root, "VRChat.exe");
            next_vrchat_scan = now + std::chrono::seconds(5);
        }
        s.vrchat_running = vrchat_pid > 0;
        if (s.vrchat_running) s.ram_vrchat = linux_sensors::read_process_rss(root, vrchat_pid);

        std::ifstream up(root / "proc" / "uptime");
        double secs = 0.0;
        if (up >> secs) s.uptime = secs;

        if (root == "/" && nvml.available()) {
            if (const auto n = nvml.sample()) {
                s.gpu_source = "NVIDIA (NVML)";
                s.gpu_name = n->name;
                s.gpu_percent = n->percent;
                s.gpu_temp = n->temp;
                s.gpu_mhz = n->mhz;
                s.vram_total = n->total;
                s.vram_used = n->used;
                for (const auto& p : n->processes) {
                    if (p.first == self) s.vram_app = p.second;
                    if (vrchat_pid > 0 && p.first == vrchat_pid) s.vram_vrchat = p.second;
                }
                if (!s.vram_app && s.vram_total) s.vram_app = 0.0;
                return;
            }
        }
        if (const auto g = linux_sensors::read_drm_gpu(root)) {
            if (drm_name.empty() || !drm_static || drm_static->device_id != g->device_id) {
                drm_name = linux_sensors::pci_name(root, g->vendor, g->device_id);
                if (drm_name.empty()) {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "GPU %04x:%04x", g->vendor, g->device_id);
                    drm_name = buf;
                }
                drm_static = g;
            }
            s.gpu_source = g->vendor == 0x1002 ? "AMD (amdgpu)" : (g->vendor == 0x8086 ? "Intel" : "DRM");
            s.gpu_name = drm_name;
            s.gpu_percent = g->busy;
            s.gpu_temp = g->temp;
            s.gpu_mhz = g->mhz;
            s.vram_total = g->vram_total;
            s.vram_used = g->vram_used;
            s.vram_app = linux_sensors::fdinfo_vram(root, self, g->slot);
            if (vrchat_pid > 0) s.vram_vrchat = linux_sensors::fdinfo_vram(root, vrchat_pid, g->slot);
        }
    }
#endif
};

Sampler::Sampler(fs::path root) : m(new Impl) {
    m->root = std::move(root);
}

Sampler::~Sampler() = default;

void Sampler::sample(SensorSnapshot& out) {
    SensorSnapshot s;
    s.cpu_threads = std::thread::hardware_concurrency();
    m->sample(s);
    if (!s.cpu_percent && out.cpu_percent) s.cpu_percent = out.cpu_percent;
    out = std::move(s);
}

} // namespace pollen

#endif // _WIN32
