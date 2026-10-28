#include "nvml.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdint>

namespace pollen {

namespace {

typedef int Result;
typedef struct DeviceOpaque* Device;

struct Utilization {
    unsigned int gpu;
    unsigned int memory;
};

struct Memory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

struct ProcessInfo {
    unsigned int pid;
    unsigned long long used;
};

constexpr Result ok = 0;
constexpr unsigned long long not_available = 0xFFFFFFFFFFFFFFFFull;

void* open_library() {
#if defined(_WIN32)
    HMODULE h = LoadLibraryW(L"nvml.dll");
    if (!h) h = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    return reinterpret_cast<void*>(h);
#elif defined(__APPLE__)
    return nullptr;
#else
    void* h = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_LOCAL);
    return h;
#endif
}

void* symbol(void* lib, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

} // namespace

struct Nvml::Impl {
    bool tried = false;
    bool ready = false;
    void* lib = nullptr;
    Device device = nullptr;
    Result (*init)() = nullptr;
    Result (*shutdown)() = nullptr;
    Result (*count)(unsigned int*) = nullptr;
    Result (*handle)(unsigned int, Device*) = nullptr;
    Result (*name)(Device, char*, unsigned int) = nullptr;
    Result (*utilization)(Device, Utilization*) = nullptr;
    Result (*memory)(Device, Memory*) = nullptr;
    Result (*temperature)(Device, int, unsigned int*) = nullptr;
    Result (*clock)(Device, int, unsigned int*) = nullptr;
    Result (*graphics)(Device, unsigned int*, ProcessInfo*) = nullptr;
    Result (*compute)(Device, unsigned int*, ProcessInfo*) = nullptr;

    template <typename F>
    void bind(F& fn, const char* n) {
        fn = reinterpret_cast<F>(symbol(lib, n));
    }

    bool start() {
        if (tried) return ready;
        tried = true;
        lib = open_library();
        if (!lib) return false;
        bind(init, "nvmlInit_v2");
        bind(shutdown, "nvmlShutdown");
        bind(count, "nvmlDeviceGetCount_v2");
        bind(handle, "nvmlDeviceGetHandleByIndex_v2");
        bind(name, "nvmlDeviceGetName");
        bind(utilization, "nvmlDeviceGetUtilizationRates");
        bind(memory, "nvmlDeviceGetMemoryInfo");
        bind(temperature, "nvmlDeviceGetTemperature");
        bind(clock, "nvmlDeviceGetClockInfo");
        bind(graphics, "nvmlDeviceGetGraphicsRunningProcesses");
        bind(compute, "nvmlDeviceGetComputeRunningProcesses");
        if (!init || !count || !handle || init() != ok) return false;
        unsigned int n = 0;
        if (count(&n) != ok || n == 0) return false;
        unsigned long long best = 0;
        for (unsigned int i = 0; i < n; ++i) {
            Device d = nullptr;
            if (handle(i, &d) != ok) continue;
            Memory mem{};
            const unsigned long long size = memory && memory(d, &mem) == ok ? mem.total : 0;
            if (!device || size > best) {
                device = d;
                best = size;
            }
        }
        ready = device != nullptr;
        return ready;
    }

    void processes(Result (*fn)(Device, unsigned int*, ProcessInfo*), NvmlSample& s) {
        if (!fn) return;
        ProcessInfo infos[128];
        unsigned int n = 128;
        if (fn(device, &n, infos) != ok) return;
        for (unsigned int i = 0; i < n && i < 128; ++i) {
            if (infos[i].used == not_available) continue;
            bool merged = false;
            for (auto& p : s.processes) {
                if (p.first == static_cast<long>(infos[i].pid)) {
                    p.second = static_cast<double>(infos[i].used) > p.second ? static_cast<double>(infos[i].used) : p.second;
                    merged = true;
                }
            }
            if (!merged) s.processes.emplace_back(static_cast<long>(infos[i].pid), static_cast<double>(infos[i].used));
        }
    }
};

Nvml::Nvml() : m(new Impl) {}

Nvml::~Nvml() {
    if (m->ready && m->shutdown) m->shutdown();
}

bool Nvml::available() {
    return m->start();
}

std::optional<NvmlSample> Nvml::sample() {
    if (!m->start()) return std::nullopt;
    NvmlSample s;
    char buf[96] = {};
    if (m->name && m->name(m->device, buf, sizeof(buf)) == ok) s.name = buf;
    Utilization u{};
    if (m->utilization && m->utilization(m->device, &u) == ok) s.percent = u.gpu;
    unsigned int t = 0;
    if (m->temperature && m->temperature(m->device, 0, &t) == ok) s.temp = t;
    unsigned int mhz = 0;
    if (m->clock && m->clock(m->device, 0, &mhz) == ok) s.mhz = mhz;
    Memory mem{};
    if (m->memory && m->memory(m->device, &mem) == ok) {
        s.total = static_cast<double>(mem.total);
        s.used = static_cast<double>(mem.used);
    }
    m->processes(m->graphics, s);
    m->processes(m->compute, s);
    return s;
}

} // namespace pollen
