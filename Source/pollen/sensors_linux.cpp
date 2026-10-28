#include "pollen/sensors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace pollen {
namespace linux_sensors {

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    return s.substr(a);
}

std::optional<double> read_number(const fs::path& p) {
    const std::string t = trim(read_text(p));
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return std::nullopt;
    return v;
}

unsigned read_hex(const fs::path& p) {
    const std::string t = trim(read_text(p));
    return static_cast<unsigned>(std::strtoul(t.c_str(), nullptr, 16));
}

fs::path under(const fs::path& root, const std::string& path) {
    return root / fs::path(path).relative_path();
}

std::vector<fs::path> list_dir(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<double> hwmon_temp(const fs::path& hwmon, const std::vector<std::string>& labels) {
    for (const std::string& want : labels) {
        for (int i = 1; i <= 32; ++i) {
            const std::string label = trim(read_text(hwmon / ("temp" + std::to_string(i) + "_label")));
            if (label == want) {
                if (const auto v = read_number(hwmon / ("temp" + std::to_string(i) + "_input"))) return *v / 1000.0;
            }
        }
    }
    if (const auto v = read_number(hwmon / "temp1_input")) return *v / 1000.0;
    return std::nullopt;
}

double unit_bytes(const std::string& unit) {
    if (unit == "KiB" || unit == "kB" || unit == "KB") return 1024.0;
    if (unit == "MiB" || unit == "MB") return 1024.0 * 1024.0;
    if (unit == "GiB" || unit == "GB") return 1024.0 * 1024.0 * 1024.0;
    return 1.0;
}

} // namespace

std::optional<CpuTimes> read_cpu_times(const fs::path& root) {
    std::istringstream in(read_text(under(root, "/proc/stat")));
    std::string cpu;
    if (!(in >> cpu) || cpu != "cpu") return std::nullopt;
    std::uint64_t v[10] = {};
    for (int i = 0; i < 10; ++i) {
        if (!(in >> v[i])) break;
    }
    CpuTimes t;
    t.idle = v[3] + v[4];
    for (int i = 0; i < 8; ++i) t.total += v[i];
    if (t.total == 0) return std::nullopt;
    return t;
}

std::string read_cpu_name(const fs::path& root) {
    std::istringstream in(read_text(under(root, "/proc/cpuinfo")));
    std::string line, fallback;
    while (std::getline(in, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (key == "model name" && !value.empty()) return value;
        if ((key == "Model" || key == "Hardware") && fallback.empty()) fallback = value;
    }
    return fallback;
}

std::optional<double> read_cpu_mhz(const fs::path& root) {
    double sum = 0.0;
    int n = 0;
    for (const fs::path& cpu : list_dir(under(root, "/sys/devices/system/cpu"))) {
        const std::string name = cpu.filename().string();
        if (name.size() < 4 || name.compare(0, 3, "cpu") != 0 || !std::isdigit(static_cast<unsigned char>(name[3]))) continue;
        if (const auto khz = read_number(cpu / "cpufreq" / "scaling_cur_freq")) {
            sum += *khz / 1000.0;
            ++n;
        }
    }
    if (n > 0) return sum / n;
    std::istringstream in(read_text(under(root, "/proc/cpuinfo")));
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || trim(line.substr(0, colon)) != "cpu MHz") continue;
        sum += std::strtod(line.c_str() + colon + 1, nullptr);
        ++n;
    }
    if (n > 0) return sum / n;
    return std::nullopt;
}

std::optional<double> read_cpu_temp(const fs::path& root) {
    const std::vector<std::pair<std::string, std::vector<std::string>>> chips = {
        { "k10temp", { "Tctl", "Tdie" } },
        { "zenpower", { "Tdie", "Tctl" } },
        { "coretemp", { "Package id 0" } },
        { "cpu_thermal", {} },
        { "soc_thermal", {} },
        { "acpitz", {} },
    };
    const std::vector<fs::path> hwmons = list_dir(under(root, "/sys/class/hwmon"));
    for (const auto& chip : chips) {
        for (const fs::path& h : hwmons) {
            if (trim(read_text(h / "name")) != chip.first) continue;
            if (const auto t = hwmon_temp(h, chip.second)) return t;
        }
    }
    for (const fs::path& zone : list_dir(under(root, "/sys/class/thermal"))) {
        const std::string type = trim(read_text(zone / "type"));
        if (type == "x86_pkg_temp" || type == "cpu-thermal" || type == "cpu_thermal") {
            if (const auto v = read_number(zone / "temp")) return *v / 1000.0;
        }
    }
    return std::nullopt;
}

std::optional<double> read_process_rss(const fs::path& root, long pid) {
    if (pid <= 0) return std::nullopt;
    std::istringstream in(read_text(under(root, "/proc/" + std::to_string(pid) + "/status")));
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 6, "VmRSS:") != 0) continue;
        std::istringstream v(line.substr(6));
        double amount = 0.0;
        std::string unit;
        v >> amount >> unit;
        return amount * unit_bytes(unit);
    }
    return std::nullopt;
}

long find_process(const fs::path& root, const std::string& comm) {
    for (const fs::path& p : list_dir(under(root, "/proc"))) {
        const std::string name = p.filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) continue;
        if (trim(read_text(p / "comm")) == comm) return std::atol(name.c_str());
    }
    return 0;
}

std::string pci_name(const fs::path& root, unsigned vendor, unsigned device) {
    for (const char* file : { "/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids", "/usr/local/share/pci.ids" }) {
        std::ifstream in(under(root, file));
        if (!in) continue;
        std::string line, vendor_name;
        bool in_vendor = false;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line[0] != '\t') {
                if (line.size() < 4) continue;
                if (in_vendor) break;
                const unsigned v = static_cast<unsigned>(std::strtoul(line.substr(0, 4).c_str(), nullptr, 16));
                if (v == vendor) {
                    in_vendor = true;
                    vendor_name = trim(line.substr(4));
                }
                continue;
            }
            if (!in_vendor || line.size() < 6 || line[1] == '\t') continue;
            const unsigned d = static_cast<unsigned>(std::strtoul(line.substr(1, 4).c_str(), nullptr, 16));
            if (d != device) continue;
            std::string name = trim(line.substr(5));
            const std::size_t open = name.find('[');
            const std::size_t close = name.rfind(']');
            if (open != std::string::npos && close != std::string::npos && close > open) name = name.substr(open + 1, close - open - 1);
            std::string brand;
            if (vendor == 0x1002) brand = "AMD ";
            else if (vendor == 0x10de) brand = "NVIDIA ";
            else if (vendor == 0x8086) brand = "Intel ";
            if (!brand.empty() && name.compare(0, brand.size(), brand) != 0) name = brand + name;
            return name;
        }
    }
    return std::string();
}

std::optional<double> fdinfo_vram(const fs::path& root, long pid, const std::string& pci_slot) {
    if (pid <= 0) return std::nullopt;
    const fs::path dir = under(root, "/proc/" + std::to_string(pid) + "/fdinfo");
    std::set<std::string> clients;
    double total = 0.0;
    bool any = false;
    for (const fs::path& f : list_dir(dir)) {
        std::istringstream in(read_text(f));
        std::string line, client, pdev;
        std::map<std::string, double> values;
        while (std::getline(in, line)) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = trim(line.substr(0, colon));
            const std::string rest = trim(line.substr(colon + 1));
            if (key == "drm-client-id") client = rest;
            else if (key == "drm-pdev") pdev = rest;
            else if (key == "drm-memory-vram" || key == "drm-resident-vram" || key == "drm-total-vram" || key == "drm-resident-local0" || key == "drm-total-local0") {
                std::istringstream v(rest);
                double amount = 0.0;
                std::string unit;
                v >> amount >> unit;
                values[key] = amount * unit_bytes(unit);
            }
        }
        if (client.empty() || values.empty()) continue;
        if (!pci_slot.empty() && !pdev.empty() && pdev != pci_slot) continue;
        if (!clients.insert(pdev + "/" + client).second) continue;
        for (const char* key : { "drm-memory-vram", "drm-resident-vram", "drm-resident-local0", "drm-total-vram", "drm-total-local0" }) {
            const auto it = values.find(key);
            if (it == values.end()) continue;
            total += it->second;
            any = true;
            break;
        }
    }
    if (!any) return std::nullopt;
    return total;
}

std::optional<DrmGpu> read_drm_gpu(const fs::path& root) {
    std::optional<DrmGpu> best;
    for (const fs::path& card : list_dir(under(root, "/sys/class/drm"))) {
        const std::string name = card.filename().string();
        if (name.compare(0, 4, "card") != 0 || name.find('-') != std::string::npos) continue;
        const fs::path dev = card / "device";
        DrmGpu g;
        g.device = dev;
        g.vendor = read_hex(dev / "vendor");
        g.device_id = read_hex(dev / "device");
        if (g.vendor == 0) continue;
        std::error_code ec;
        const fs::path real = fs::canonical(dev, ec);
        g.slot = ec ? std::string() : real.filename().string();
        if (g.slot.empty() || g.slot.find(':') == std::string::npos) {
            const std::string uevent = read_text(dev / "uevent");
            const std::size_t at = uevent.find("PCI_SLOT_NAME=");
            if (at != std::string::npos) g.slot = trim(uevent.substr(at + 14, uevent.find('\n', at) - at - 14));
        }
        g.vram_total = read_number(dev / "mem_info_vram_total");
        g.vram_used = read_number(dev / "mem_info_vram_used");
        g.busy = read_number(dev / "gpu_busy_percent");
        for (const fs::path& h : list_dir(dev / "hwmon")) {
            if (!g.temp) g.temp = hwmon_temp(h, { "edge", "junction" });
            if (!g.mhz) {
                if (const auto hz = read_number(h / "freq1_input")) g.mhz = *hz / 1e6;
            }
        }
        if (!g.mhz) {
            if (const auto mhz = read_number(card / "gt_act_freq_mhz")) g.mhz = *mhz;
        }
        const double size = g.vram_total.value_or(0.0);
        if (!best || size > best->vram_total.value_or(0.0)) best = g;
    }
    return best;
}

} // namespace linux_sensors
} // namespace pollen
