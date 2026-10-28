#ifndef OSC_VRCHAT_PATHS_HPP
#define OSC_VRCHAT_PATHS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osc {
namespace vrchat {

constexpr const char* steam_app_id = "438100";

std::vector<std::filesystem::path> steam_libraries();
std::vector<std::filesystem::path> osc_folders(const std::vector<std::filesystem::path>& extra = {});
std::vector<std::filesystem::path> avatar_config_files(const std::vector<std::filesystem::path>& extra = {});
std::optional<std::filesystem::path> find_avatar_config(std::string_view avatar_id, const std::vector<std::filesystem::path>& extra = {});

} // namespace vrchat
} // namespace osc

#endif // OSC_VRCHAT_PATHS_HPP
