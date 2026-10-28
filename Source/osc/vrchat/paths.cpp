#include "osc/vrchat/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

namespace osc {
namespace vrchat {

namespace fs = std::filesystem;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

void add_unique(std::vector<fs::path>& list, const fs::path& p) {
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return;
    fs::path canon = fs::weakly_canonical(p, ec);
    if (ec) canon = p;
    for (const fs::path& q : list) {
        if (q == canon) return;
    }
    list.push_back(canon);
}

std::string unescape_vdf(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out += s[++i];
        } else {
            out += s[i];
        }
    }
    return out;
}

std::vector<fs::path> library_paths_in(const fs::path& vdf) {
    std::vector<fs::path> out;
    std::ifstream in(vdf);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> quoted;
        std::string cur;
        bool inside = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (inside && c == '\\' && i + 1 < line.size()) {
                cur += c;
                cur += line[++i];
            } else if (c == '"') {
                if (inside) quoted.push_back(unescape_vdf(cur));
                cur.clear();
                inside = !inside;
            } else if (inside) {
                cur += c;
            }
        }
        if (quoted.size() >= 2 && quoted[0] == "path") out.emplace_back(quoted[1]);
    }
    return out;
}

fs::path osc_under_library(const fs::path& library) {
    return library / "steamapps" / "compatdata" / steam_app_id / "pfx" / "drive_c" / "users" / "steamuser" / "AppData" / "LocalLow" / "VRChat" / "VRChat" / "OSC";
}

} // namespace

std::vector<fs::path> steam_libraries() {
    std::vector<fs::path> roots;
#if defined(_WIN32)
    const std::string pf86 = env("ProgramFiles(x86)");
    if (!pf86.empty()) add_unique(roots, fs::path(pf86) / "Steam");
    const std::string pf = env("ProgramFiles");
    if (!pf.empty()) add_unique(roots, fs::path(pf) / "Steam");
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    if (!home.empty()) add_unique(roots, fs::path(home) / "Library" / "Application Support" / "Steam");
#else
    const std::string home = env("HOME");
    const std::string data = env("XDG_DATA_HOME");
    if (!data.empty()) add_unique(roots, fs::path(data) / "Steam");
    if (!home.empty()) {
        add_unique(roots, fs::path(home) / ".steam" / "steam");
        add_unique(roots, fs::path(home) / ".steam" / "root");
        add_unique(roots, fs::path(home) / ".local" / "share" / "Steam");
        add_unique(roots, fs::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
        add_unique(roots, fs::path(home) / "snap" / "steam" / "common" / ".local" / "share" / "Steam");
    }
#endif
    std::vector<fs::path> libraries;
    for (const fs::path& root : roots) {
        add_unique(libraries, root);
        for (const fs::path& lib : library_paths_in(root / "steamapps" / "libraryfolders.vdf")) add_unique(libraries, lib);
        for (const fs::path& lib : library_paths_in(root / "config" / "libraryfolders.vdf")) add_unique(libraries, lib);
    }
    return libraries;
}

std::vector<fs::path> osc_folders(const std::vector<fs::path>& extra) {
    std::vector<fs::path> out;
    for (const fs::path& p : extra) add_unique(out, p);
    const std::string over = env("VRCHAT_OSC_DIR");
    if (!over.empty()) add_unique(out, fs::path(over));
#if defined(_WIN32)
    const std::string profile = env("USERPROFILE");
    if (!profile.empty()) add_unique(out, fs::path(profile) / "AppData" / "LocalLow" / "VRChat" / "VRChat" / "OSC");
    const std::string local = env("LOCALAPPDATA");
    if (!local.empty()) add_unique(out, fs::path(local).parent_path() / "LocalLow" / "VRChat" / "VRChat" / "OSC");
#endif
    for (const fs::path& lib : steam_libraries()) add_unique(out, osc_under_library(lib));
    return out;
}

std::vector<fs::path> avatar_config_files(const std::vector<fs::path>& extra) {
    std::vector<fs::path> out;
    for (const fs::path& folder : osc_folders(extra)) {
        std::error_code ec;
        for (fs::directory_iterator user(folder, ec), end; !ec && user != end; user.increment(ec)) {
            const fs::path avatars = user->path() / "Avatars";
            std::error_code ec2;
            if (!fs::is_directory(avatars, ec2)) continue;
            for (fs::directory_iterator f(avatars, ec2), fend; !ec2 && f != fend; f.increment(ec2)) {
                if (f->path().extension() == ".json") out.push_back(f->path());
            }
        }
    }
    return out;
}

std::optional<fs::path> find_avatar_config(std::string_view avatar_id, const std::vector<fs::path>& extra) {
    if (avatar_id.empty()) return std::nullopt;
    const std::string wanted = std::string(avatar_id) + ".json";
    std::optional<fs::path> best;
    fs::file_time_type best_time{};
    for (const fs::path& p : avatar_config_files(extra)) {
        if (p.filename().string() != wanted) continue;
        std::error_code ec;
        const fs::file_time_type t = fs::last_write_time(p, ec);
        if (!best || (!ec && t > best_time)) {
            best = p;
            best_time = t;
        }
    }
    return best;
}

} // namespace vrchat
} // namespace osc
