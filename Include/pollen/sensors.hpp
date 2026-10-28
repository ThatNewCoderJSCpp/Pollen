#ifndef POLLEN_SENSORS_HPP
#define POLLEN_SENSORS_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace pollen {

struct SensorSnapshot {
    std::optional<double> cpu_percent;
    std::optional<double> cpu_mhz;
    std::optional<double> cpu_temp;
    std::string cpu_name;
    unsigned cpu_threads = 0;

    std::optional<double> ram_total;
    std::optional<double> ram_used;
    std::optional<double> ram_app;
    std::optional<double> ram_vrchat;

    std::string gpu_name;
    std::string gpu_source;
    std::optional<double> gpu_percent;
    std::optional<double> gpu_temp;
    std::optional<double> gpu_mhz;

    std::optional<double> vram_total;
    std::optional<double> vram_used;
    std::optional<double> vram_app;
    std::optional<double> vram_vrchat;

    std::optional<double> uptime;
    bool vrchat_running = false;
    std::string notes;
    std::string cpu_temp_help;
    std::string gpu_temp_help;
};

class Sampler {
public:
    explicit Sampler(std::filesystem::path root = "/");
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    void sample(SensorSnapshot& out);

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

class Sensors {
public:
    Sensors();
    ~Sensors();

    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    void start(double interval_seconds = 1.0);
    void stop();
    SensorSnapshot snapshot() const;
    SensorSnapshot sample_now();

private:
    void run();

    Sampler m_sampler;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    double m_interval = 1.0;
    SensorSnapshot m_snapshot;
};

namespace linux_sensors {

struct CpuTimes {
    std::uint64_t idle = 0;
    std::uint64_t total = 0;
};

std::optional<CpuTimes> read_cpu_times(const std::filesystem::path& root);
std::string read_cpu_name(const std::filesystem::path& root);
std::optional<double> read_cpu_mhz(const std::filesystem::path& root);
std::optional<double> read_cpu_temp(const std::filesystem::path& root);
std::optional<double> read_process_rss(const std::filesystem::path& root, long pid);
long find_process(const std::filesystem::path& root, const std::string& comm);
std::string pci_name(const std::filesystem::path& root, unsigned vendor, unsigned device);
std::optional<double> fdinfo_vram(const std::filesystem::path& root, long pid, const std::string& pci_slot);

struct DrmGpu {
    std::filesystem::path device;
    std::string slot;
    unsigned vendor = 0;
    unsigned device_id = 0;
    std::optional<double> vram_total;
    std::optional<double> vram_used;
    std::optional<double> busy;
    std::optional<double> temp;
    std::optional<double> mhz;
};

std::optional<DrmGpu> read_drm_gpu(const std::filesystem::path& root);

} // namespace linux_sensors

} // namespace pollen

#endif // POLLEN_SENSORS_HPP
