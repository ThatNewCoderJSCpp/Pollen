#include "pollen/sensors.hpp"
#include "nvml.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wbemidl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <map>
#include <thread>
#include <vector>

namespace pollen {

namespace fs = std::filesystem;

namespace {

std::string narrow(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::uint64_t file_time(const FILETIME& t) {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

long instance_pid(const std::wstring& name) {
    if (name.compare(0, 4, L"pid_") != 0) return -1;
    return std::wcstol(name.c_str() + 4, nullptr, 10);
}

struct Pdh {
    typedef PDH_STATUS(WINAPI* OpenFn)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
    typedef PDH_STATUS(WINAPI* AddFn)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
    typedef PDH_STATUS(WINAPI* CollectFn)(PDH_HQUERY);
    typedef PDH_STATUS(WINAPI* ArrayFn)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
    typedef PDH_STATUS(WINAPI* CloseFn)(PDH_HQUERY);

    HMODULE lib = nullptr;
    OpenFn open = nullptr;
    AddFn add = nullptr;
    CollectFn collect = nullptr;
    ArrayFn array = nullptr;
    CloseFn close = nullptr;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr;
    PDH_HCOUNTER adapter_memory = nullptr;
    PDH_HCOUNTER process_memory = nullptr;
    PDH_HCOUNTER cpu_performance = nullptr;
    std::chrono::steady_clock::time_point rebuilt;

    bool load() {
        if (lib) return open != nullptr;
        lib = LoadLibraryW(L"pdh.dll");
        if (!lib) return false;
        open = reinterpret_cast<OpenFn>(reinterpret_cast<void*>(GetProcAddress(lib, "PdhOpenQueryW")));
        add = reinterpret_cast<AddFn>(reinterpret_cast<void*>(GetProcAddress(lib, "PdhAddEnglishCounterW")));
        collect = reinterpret_cast<CollectFn>(reinterpret_cast<void*>(GetProcAddress(lib, "PdhCollectQueryData")));
        array = reinterpret_cast<ArrayFn>(reinterpret_cast<void*>(GetProcAddress(lib, "PdhGetFormattedCounterArrayW")));
        close = reinterpret_cast<CloseFn>(reinterpret_cast<void*>(GetProcAddress(lib, "PdhCloseQuery")));
        if (!open || !add || !collect || !array || !close) open = nullptr;
        return open != nullptr;
    }

    void reset() {
        if (query && close) close(query);
        query = nullptr;
        engine = adapter_memory = process_memory = cpu_performance = nullptr;
    }

    void rebuild() {
        reset();
        if (!load() || open(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            return;
        }
        if (add(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engine) != ERROR_SUCCESS) engine = nullptr;
        if (add(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &adapter_memory) != ERROR_SUCCESS) adapter_memory = nullptr;
        if (add(query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &process_memory) != ERROR_SUCCESS) process_memory = nullptr;
        if (add(query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &cpu_performance) != ERROR_SUCCESS) cpu_performance = nullptr;
        collect(query);
        rebuilt = std::chrono::steady_clock::now();
    }

    bool tick() {
        const auto now = std::chrono::steady_clock::now();
        if (!query || now - rebuilt > std::chrono::seconds(15)) rebuild();
        if (!query) return false;
        return collect(query) == ERROR_SUCCESS;
    }

    std::vector<std::pair<std::wstring, double>> values(PDH_HCOUNTER counter, bool large) {
        std::vector<std::pair<std::wstring, double>> out;
        if (!counter) return out;
        const DWORD format = (large ? PDH_FMT_LARGE : PDH_FMT_DOUBLE) | PDH_FMT_NOCAP100;
        DWORD size = 0, count = 0;
        PDH_STATUS st = array(counter, format, &size, &count, nullptr);
        if (st != static_cast<PDH_STATUS>(PDH_MORE_DATA) || size == 0) return out;
        std::vector<unsigned char> buffer(size);
        auto* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
        if (array(counter, format, &size, &count, items) != ERROR_SUCCESS) return out;
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].FmtValue.CStatus != ERROR_SUCCESS && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
            const double v = large ? static_cast<double>(items[i].FmtValue.largeValue) : items[i].FmtValue.doubleValue;
            out.emplace_back(lower(items[i].szName ? items[i].szName : L""), v);
        }
        return out;
    }
};

struct Adapter {
    bool found = false;
    std::string name;
    double total = 0.0;
    unsigned vendor = 0;
    std::wstring luid;
    IDXGIAdapter3* adapter3 = nullptr;
};

Adapter find_adapter() {
    Adapter out;
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return out;
    typedef HRESULT(WINAPI * CreateFn)(REFIID, void**);
    auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
    if (!create) return out;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) return out;
    IDXGIAdapter1* best = nullptr;
    DXGI_ADAPTER_DESC1 best_desc{};
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* a = nullptr;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        if (!a) continue;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!best || d.DedicatedVideoMemory > best_desc.DedicatedVideoMemory)) {
            if (best) best->Release();
            best = a;
            best_desc = d;
        } else {
            a->Release();
        }
    }
    factory->Release();
    if (!best) return out;
    out.found = true;
    out.name = narrow(best_desc.Description);
    out.total = static_cast<double>(best_desc.DedicatedVideoMemory);
    out.vendor = best_desc.VendorId;
    wchar_t buf[64];
    std::swprintf(buf, 64, L"luid_0x%08x_0x%08x", static_cast<unsigned>(best_desc.AdapterLuid.HighPart), static_cast<unsigned>(best_desc.AdapterLuid.LowPart));
    out.luid = buf;
    if (FAILED(best->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&out.adapter3)))) out.adapter3 = nullptr;
    best->Release();
    return out;
}

const CLSID clsid_wbem_locator = { 0x4590f811, 0x1d3a, 0x11d0, { 0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 } };

struct HardwareMonitor {
    bool com = false;
    IWbemServices* services = nullptr;
    std::chrono::steady_clock::time_point next_try;
    bool running = false;

    ~HardwareMonitor() {
        if (services) services->Release();
        if (com) CoUninitialize();
    }

    bool connect() {
        if (services) return true;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_try) return false;
        next_try = now + std::chrono::seconds(15);
        if (!com) com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        IWbemLocator* locator = nullptr;
        if (FAILED(CoCreateInstance(clsid_wbem_locator, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWbemLocator), reinterpret_cast<void**>(&locator))) || !locator) return false;
        BSTR ns = SysAllocString(L"ROOT\\LibreHardwareMonitor");
        const HRESULT hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
        SysFreeString(ns);
        locator->Release();
        if (FAILED(hr) || !services) {
            services = nullptr;
            return false;
        }
        CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }

    std::optional<double> cpu_temp() {
        running = false;
        if (!connect()) return std::nullopt;
        BSTR lang = SysAllocString(L"WQL");
        BSTR query = SysAllocString(L"SELECT Name, Identifier, Value FROM Sensor WHERE SensorType = 'Temperature'");
        IEnumWbemClassObject* rows = nullptr;
        const HRESULT hr = services->ExecQuery(lang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rows);
        SysFreeString(lang);
        SysFreeString(query);
        if (FAILED(hr) || !rows) {
            services->Release();
            services = nullptr;
            return std::nullopt;
        }
        running = true;
        std::optional<double> package, hottest;
        while (true) {
            IWbemClassObject* row = nullptr;
            ULONG got = 0;
            if (rows->Next(2000, 1, &row, &got) != WBEM_S_NO_ERROR || got == 0) break;
            VARIANT name, id, value;
            VariantInit(&name);
            VariantInit(&id);
            VariantInit(&value);
            row->Get(L"Name", 0, &name, nullptr, nullptr);
            row->Get(L"Identifier", 0, &id, nullptr, nullptr);
            row->Get(L"Value", 0, &value, nullptr, nullptr);
            const std::wstring n = name.vt == VT_BSTR && name.bstrVal ? name.bstrVal : L"";
            const std::wstring i = id.vt == VT_BSTR && id.bstrVal ? lower(id.bstrVal) : L"";
            double v = -1000.0;
            if (value.vt == VT_R4) v = value.fltVal;
            else if (value.vt == VT_R8) v = value.dblVal;
            if ((i.find(L"/amdcpu/") == 0 || i.find(L"/intelcpu/") == 0) && v > -100.0 && v < 200.0) {
                if (n == L"Core (Tctl/Tdie)" || n == L"CPU Package" || n == L"Core (Tctl)" || n == L"Package") {
                    if (!package) package = v;
                }
                if (!hottest || v > *hottest) hottest = v;
            }
            VariantClear(&name);
            VariantClear(&id);
            VariantClear(&value);
            row->Release();
        }
        rows->Release();
        return package ? package : hottest;
    }
};

std::string registry_string(const wchar_t* key, const wchar_t* value) {
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return std::string();
    std::string s = narrow(buf);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

DWORD registry_dword(const wchar_t* key, const wchar_t* value) {
    DWORD v = 0;
    DWORD size = sizeof(v);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return 0;
    return v;
}

long find_vrchat() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    long pid = 0;
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, L"VRChat.exe") == 0) {
                pid = static_cast<long>(e.th32ProcessID);
                break;
            }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return pid;
}

std::optional<double> process_ram(long pid) {
    HANDLE h = pid == static_cast<long>(GetCurrentProcessId()) ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return std::nullopt;
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    std::optional<double> out;
    if (K32GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) out = static_cast<double>(pmc.WorkingSetSize);
    if (h != GetCurrentProcess()) CloseHandle(h);
    return out;
}

bool process_alive(long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

} // namespace

struct Sampler::Impl {
    Nvml nvml;
    Pdh pdh;
    Adapter adapter;
    bool adapter_tried = false;
    std::uint64_t last_idle = 0;
    std::uint64_t last_total = 0;
    bool have_last = false;
    std::string cpu_name;
    double base_mhz = 0.0;
    long vrchat_pid = 0;
    std::chrono::steady_clock::time_point next_vrchat_scan;
    HardwareMonitor monitor;

    ~Impl() {
        pdh.reset();
        if (adapter.adapter3) adapter.adapter3->Release();
    }

    void sample(SensorSnapshot& s) {
        FILETIME idle_t, kernel_t, user_t;
        if (GetSystemTimes(&idle_t, &kernel_t, &user_t)) {
            const std::uint64_t idle = file_time(idle_t);
            const std::uint64_t total = file_time(kernel_t) + file_time(user_t);
            if (have_last && total > last_total) {
                double pct = 100.0 * (1.0 - static_cast<double>(idle - last_idle) / static_cast<double>(total - last_total));
                s.cpu_percent = pct < 0.0 ? 0.0 : (pct > 100.0 ? 100.0 : pct);
            }
            last_idle = idle;
            last_total = total;
            have_last = true;
        }
        if (cpu_name.empty()) {
            cpu_name = registry_string(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
            base_mhz = static_cast<double>(registry_dword(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz"));
        }
        s.cpu_name = cpu_name;

        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem)) {
            s.ram_total = static_cast<double>(mem.ullTotalPhys);
            s.ram_used = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys);
        }
        const long self = static_cast<long>(GetCurrentProcessId());
        s.ram_app = process_ram(self);
        s.uptime = static_cast<double>(GetTickCount64()) / 1000.0;

        const auto now = std::chrono::steady_clock::now();
        if (vrchat_pid > 0 && !process_alive(vrchat_pid)) vrchat_pid = 0;
        if (vrchat_pid == 0 && now >= next_vrchat_scan) {
            vrchat_pid = find_vrchat();
            next_vrchat_scan = now + std::chrono::seconds(5);
        }
        s.vrchat_running = vrchat_pid > 0;
        if (s.vrchat_running) s.ram_vrchat = process_ram(vrchat_pid);

        if (!adapter_tried) {
            adapter = find_adapter();
            adapter_tried = true;
        }
        const bool have_pdh = pdh.tick();
        if (have_pdh && pdh.cpu_performance && base_mhz > 0.0) {
            DWORD type = 0;
            PDH_FMT_COUNTERVALUE v{};
            typedef PDH_STATUS(WINAPI * ValueFn)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
            static ValueFn value_fn = reinterpret_cast<ValueFn>(reinterpret_cast<void*>(GetProcAddress(pdh.lib, "PdhGetFormattedCounterValue")));
            if (value_fn && value_fn(pdh.cpu_performance, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &type, &v) == ERROR_SUCCESS && v.doubleValue > 0.0) s.cpu_mhz = base_mhz * v.doubleValue / 100.0;
        }
        if (!s.cpu_mhz && base_mhz > 0.0) s.cpu_mhz = base_mhz;

        if (adapter.found) {
            s.gpu_name = adapter.name;
            s.gpu_source = "Windows (DXGI)";
            s.vram_total = adapter.total;
            if (adapter.adapter3) {
                DXGI_QUERY_VIDEO_MEMORY_INFO info{};
                if (SUCCEEDED(adapter.adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) s.vram_app = static_cast<double>(info.CurrentUsage);
            }
            if (have_pdh) {
                std::map<std::wstring, double> engines;
                for (const auto& kv : pdh.values(pdh.engine, false)) {
                    if (kv.first.find(adapter.luid) == std::wstring::npos) continue;
                    const std::size_t at = kv.first.find(L"engtype_");
                    engines[at == std::wstring::npos ? std::wstring() : kv.first.substr(at + 8)] += kv.second;
                }
                double busiest = 0.0;
                for (const auto& kv : engines) busiest = std::max(busiest, kv.second);
                if (!engines.empty()) s.gpu_percent = std::min(100.0, busiest);
                double used = 0.0;
                bool any = false;
                for (const auto& kv : pdh.values(pdh.adapter_memory, true)) {
                    if (kv.first.find(adapter.luid) == std::wstring::npos) continue;
                    used += kv.second;
                    any = true;
                }
                if (any) s.vram_used = used;
                double app = 0.0, vrc = 0.0;
                bool app_any = false, vrc_any = false;
                for (const auto& kv : pdh.values(pdh.process_memory, true)) {
                    if (kv.first.find(adapter.luid) == std::wstring::npos) continue;
                    const long pid = instance_pid(kv.first);
                    if (pid == self) {
                        app += kv.second;
                        app_any = true;
                    } else if (vrchat_pid > 0 && pid == vrchat_pid) {
                        vrc += kv.second;
                        vrc_any = true;
                    }
                }
                if (app_any) s.vram_app = app;
                if (vrc_any) s.vram_vrchat = vrc;
            }
        }
        if (nvml.available()) {
            if (const auto n = nvml.sample()) {
                if (s.gpu_name.empty()) s.gpu_name = n->name;
                s.gpu_source = "NVIDIA (NVML)";
                s.gpu_temp = n->temp;
                s.gpu_mhz = n->mhz;
                if (n->percent) s.gpu_percent = n->percent;
                if (n->total) s.vram_total = n->total;
                if (n->used) s.vram_used = n->used;
            }
        }
        s.cpu_temp = monitor.cpu_temp();
        if (!s.cpu_temp) {
            if (monitor.running) {
                s.cpu_temp_help = "Libre Hardware Monitor is running but isn't giving a CPU temperature. Close it and start it again with \"Run as administrator\", so its PawnIO driver can read the CPU's sensors.";
            } else {
                s.cpu_temp_help = "Windows doesn't let apps read the CPU temperature on their own. To show it, install Libre Hardware Monitor (and the PawnIO driver it asks for), start it with \"Run as administrator\", and leave it running in the background. Pollen reads the temperature from it.";
            }
        }
        if (!s.gpu_temp) {
            std::string brand = "your graphics card";
            if (adapter.vendor == 0x1002) brand = "AMD cards";
            else if (adapter.vendor == 0x8086) brand = "Intel graphics";
            if (adapter.found && adapter.vendor != 0x10de) s.gpu_temp_help = "GPU temperature on Windows only works on NVIDIA cards, so it isn't available for " + brand + ".";
            else s.gpu_temp_help = "GPU temperature needs the NVIDIA driver to be installed (Pollen reads it through nvml.dll, which comes with the driver).";
        }
        s.notes.clear();
    }
};

Sampler::Sampler(fs::path) : m(new Impl) {}

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
