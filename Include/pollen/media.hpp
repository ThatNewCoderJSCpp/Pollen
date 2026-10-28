#ifndef POLLEN_MEDIA_HPP
#define POLLEN_MEDIA_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace pollen {

struct NowPlaying {
    bool available = false;
    std::string reason;
    bool playing = false;
    bool paused = false;
    std::string player;
    std::string artist;
    std::string title;
    double position = 0.0;
    double length = 0.0;

    bool has_song() const noexcept { return !title.empty(); }
    std::string song() const;
};

NowPlaying parse_playerctl(const std::string& line);

class Media {
public:
    Media();
    ~Media();

    Media(const Media&) = delete;
    Media& operator=(const Media&) = delete;

    void start(double interval_seconds = 2.0);
    void stop();
    bool running() const noexcept { return m_running.load(); }

    NowPlaying now_playing() const;
    NowPlaying refresh();

private:
    void run();

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    double m_interval = 2.0;
    NowPlaying m_now;
};

} // namespace pollen

#endif // POLLEN_MEDIA_HPP
