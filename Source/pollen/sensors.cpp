#include "pollen/sensors.hpp"

namespace pollen {

Sensors::Sensors() = default;

Sensors::~Sensors() {
    stop();
}

void Sensors::start(double interval_seconds) {
    if (m_running.exchange(true)) return;
    m_interval = interval_seconds < 0.25 ? 0.25 : interval_seconds;
    m_thread = std::thread([this] { run(); });
}

void Sensors::stop() {
    if (!m_running.exchange(false)) return;
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

SensorSnapshot Sensors::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

SensorSnapshot Sensors::sample_now() {
    SensorSnapshot s;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        s = m_snapshot;
    }
    m_sampler.sample(s);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot = s;
    return s;
}

void Sensors::run() {
    while (m_running.load()) {
        sample_now();
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::duration<double>(m_interval), [this] { return !m_running.load(); });
    }
}

} // namespace pollen
