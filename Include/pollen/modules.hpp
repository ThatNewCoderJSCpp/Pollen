#ifndef POLLEN_MODULES_HPP
#define POLLEN_MODULES_HPP

#include "media.hpp"
#include "sensors.hpp"
#include "status.hpp"
#include "weather.hpp"

#include <chrono>
#include <cstddef>
#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pollen {

enum class OptionKind { Bool, Choice, Text, Integer, Float };

struct OptionDef {
    std::string id;
    std::string label;
    OptionKind kind = OptionKind::Bool;
    std::string fallback;
    std::string hint;
    std::vector<std::pair<std::string, std::string>> choices;
    double min = 0.0;
    double max = 0.0;
    std::string when;
};

struct PlaceholderDef {
    std::string name;
    std::string description;
    std::string needs;
};

struct ModuleDef {
    std::string id;
    std::string title;
    std::string icon;
    std::string description;
    std::vector<OptionDef> options;
    std::vector<PlaceholderDef> placeholders;
    std::string example;
};

using ModuleOptions = std::map<std::string, std::map<std::string, std::string>>;

const std::vector<ModuleDef>& module_defs();
const ModuleDef* find_module(const std::string& id);
const OptionDef* find_option(const ModuleDef& module, const std::string& option);
const ModuleDef* module_of_placeholder(const std::string& name);

std::string option_value(const ModuleOptions& options, const std::string& module, const std::string& option);
bool option_on(const ModuleOptions& options, const std::string& module, const std::string& option);
bool option_visible(const ModuleOptions& options, const ModuleDef& module, const OptionDef& option);
bool placeholder_enabled(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder);
std::string requirement_text(const ModuleDef& module, const PlaceholderDef& placeholder);
bool set_option(ModuleOptions& options, const std::string& module, const std::string& option, const std::string& value, std::string* error = nullptr);

std::vector<std::string> enabled_placeholders(const ModuleOptions& options);

struct ModuleInputs {
    SensorSnapshot sensors;
    NowPlaying media;
    WeatherSnapshot weather;
    std::string avatar_name;
    std::string avatar_id;
    std::size_t params = 0;
    double connected_seconds = -1.0;
    double app_seconds = 0.0;
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
};

void module_values(const ModuleDef& module, const ModuleOptions& options, const ModuleInputs& inputs, StatusValues& out);
StatusValues all_module_values(const ModuleOptions& options, const ModuleInputs& inputs);

struct Units {
    std::string bytes_unit = "auto";
    int bytes_decimals = 1;
    bool bytes_label = true;
    int percent_decimals = 0;
    std::string temp_unit = "C";
    bool temp_label = true;
    std::string unknown;
};

Units units_of(const ModuleOptions& options);
std::string format_bytes(double bytes, const Units& units);
std::string format_percent(double percent, const Units& units);
std::string format_temp(double celsius, const Units& units);
std::string format_clock(double mhz, const std::string& unit);
std::string format_span(double seconds, bool days, bool seconds_shown, const std::string& style);
std::string format_clock_time(const std::tm& tm, bool hour24, bool seconds, bool leading_zero);

WeatherConfig weather_config(const ModuleOptions& options);

} // namespace pollen

#endif // POLLEN_MODULES_HPP
