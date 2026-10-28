#include "pollen/media.hpp"
#include "media_windows.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace pollen {

namespace {

std::vector<std::string> split_tabs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\t') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\n' && c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

#if !defined(_WIN32) && !defined(__APPLE__)
bool run_command(const char* command, std::string& output) {
    output.clear();
    FILE* pipe = popen(command, "r");
    if (!pipe) return false;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), pipe)) output += buf;
    const int status = pclose(pipe);
    return status == 0;
}
#endif

} // namespace

std::string NowPlaying::song() const {
    if (title.empty()) return std::string();
    if (artist.empty()) return title;
    return artist + " - " + title;
}

NowPlaying parse_playerctl(const std::string& line) {
    NowPlaying n;
    n.available = true;
    const std::vector<std::string> f = split_tabs(line);
    if (f.size() < 4) return n;
    n.playing = f[0] == "Playing";
    n.paused = f[0] == "Paused";
    n.player = f[1];
    n.artist = f[2];
    n.title = f[3];
    if (f.size() > 4) n.position = std::atof(f[4].c_str()) / 1e6;
    if (f.size() > 5) n.length = std::atof(f[5].c_str()) / 1e6;
    return n;
}

Media::Media() = default;

Media::~Media() {
    stop();
}

void Media::start(double interval_seconds) {
    if (m_running.exchange(true)) return;
    m_interval = interval_seconds < 0.5 ? 0.5 : interval_seconds;
    m_thread = std::thread([this] { run(); });
}

void Media::stop() {
    if (!m_running.exchange(false)) return;
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

NowPlaying Media::now_playing() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_now;
}

NowPlaying Media::refresh() {
    NowPlaying n;
#if defined(_WIN32)
    n = windows_now_playing();
#elif defined(__APPLE__)
    n.reason = "now playing isn't supported on macOS yet";
#else
    static const bool has_playerctl = [] {
        std::string ignored;
        return run_command("playerctl --version >/dev/null 2>&1", ignored);
    }();
    std::string out;
    if (!has_playerctl) {
        n.reason = "install playerctl to show what's playing";
    } else if (run_command("playerctl metadata --format '{{status}}\t{{playerName}}\t{{artist}}\t{{title}}\t{{position}}\t{{mpris:length}}' 2>/dev/null", out) && !out.empty()) {
        n = parse_playerctl(out);
    } else {
        n.available = true;
        n.reason = "nothing is playing";
    }
#endif
    std::lock_guard<std::mutex> lock(m_mutex);
    m_now = n;
    return n;
}

void Media::run() {
    while (m_running.load()) {
        refresh();
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::duration<double>(m_interval), [this] { return !m_running.load(); });
    }
}

} // namespace pollen
