#ifndef POLLEN_MODULES_HPP
#define POLLEN_MODULES_HPP

#include "media.hpp"
#include "sensors.hpp"
#include "status.hpp"
#include "vrchat_api.hpp"
#include "vrchat_log.hpp"
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
    std::string units;
    bool percent = false;
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
const PlaceholderDef* find_placeholder(const ModuleDef& module, const std::string& name);
std::vector<OptionDef> placeholder_settings(const ModuleDef& module, const PlaceholderDef& placeholder);
std::string placeholder_default(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder, const std::string& setting);
std::string placeholder_value(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder, const std::string& setting);
std::vector<OptionDef> visible_placeholder_settings(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder);
std::string placeholder_key(const std::string& placeholder, const std::string& setting);
std::string choice_label(const OptionDef& option, const std::string& value);
std::string placeholder_raw(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder, const std::string& setting);
bool apply_modifiers(ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder, const std::string& modifiers, std::string* error = nullptr);
std::vector<std::string> placeholder_examples(const ModuleDef& module, const PlaceholderDef& placeholder);
std::vector<std::pair<std::string, std::string>> modified_placeholders(const std::string& text);

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
    WorldSnapshot world;
    AccountSnapshot account;
    std::size_t friends_total = 0;
    std::size_t friends_online = 0;
    std::size_t friends_web = 0;
    std::vector<std::string> friends_here;
    std::string last_joined;
    std::string last_left;
    std::string friend_news;
};

void module_values(const ModuleDef& module, const ModuleOptions& options, const ModuleInputs& inputs, StatusValues& out);
StatusValues all_module_values(const ModuleOptions& options, const ModuleInputs& inputs);
void add_modified_values(const std::string& text, const ModuleOptions& options, const ModuleInputs& inputs, StatusValues& values, std::vector<std::string>* problems = nullptr);

struct Units {
    std::string bytes_unit = "auto";
    int bytes_decimals = 1;
    bool bytes_label = true;
    int percent_decimals = 0;
    std::string temp_unit = "C";
    bool temp_label = true;
    std::string unknown;
    std::string temp_also = "none";
    int temp_decimals = 0;
    std::string both_style = "paren";
};

Units units_of(const ModuleOptions& options);
std::string format_bytes(double bytes, const Units& units);
std::string format_bytes_in(double bytes, const std::string& unit, int decimals, bool label);
std::string format_percent(double percent, const Units& units);
std::string format_temp(double celsius, const Units& units);
std::string format_temp_in(double celsius, const std::string& unit, int decimals, bool label);
std::string format_speed(double kmh, const std::string& unit, int decimals);
std::string format_both(const std::string& first, const std::string& second, const std::string& style);
std::string format_clock(double mhz, const std::string& unit);
std::string format_span(double seconds, bool days, bool seconds_shown, const std::string& style);
std::string format_clock_time(const std::tm& tm, bool hour24, bool seconds, bool leading_zero);
long long utc_offset_seconds(std::time_t t);
std::string format_offset(long long seconds, const std::string& precision, const std::string& style, const std::string& name);

WeatherConfig weather_config(const ModuleOptions& options);

} // namespace pollen

#endif // POLLEN_MODULES_HPP
