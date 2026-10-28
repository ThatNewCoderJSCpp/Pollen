#ifndef POLLEN_SETTINGS_HPP
#define POLLEN_SETTINGS_HPP

#include <osc/vrchat/vrchat.hpp>

#include <filesystem>
#include <string>

namespace pollen {

struct Settings {
    std::string host = "127.0.0.1";
    int send_port = osc::vrchat::default_send_port;
    int receive_port = osc::vrchat::default_receive_port;
    std::string listen_host = "127.0.0.1";
    std::string osc_folder;
    bool auto_connect = true;
    bool oscquery = true;

    bool status_enabled = false;
    std::string status_template = "{time} \xc2\xb7 CPU {cpu} \xc2\xb7 RAM {ram}\n\xe2\x99\xaa {song}";
    double status_interval = 3.0;
    bool status_notify = false;

    bool log_parameters = false;

    osc::vrchat::ConnectionSettings connection() const;

    static std::filesystem::path config_dir();
    static std::filesystem::path default_path();

    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool save(const std::filesystem::path& path, std::string* error = nullptr) const;

    std::string to_json() const;
    bool from_json(const std::string& text, std::string* error = nullptr);
};

} // namespace pollen

#endif // POLLEN_SETTINGS_HPP
