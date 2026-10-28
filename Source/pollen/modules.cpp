#include "pollen/modules.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>

namespace pollen {

namespace {

OptionDef toggle(const std::string& id, const std::string& label, bool on, const std::string& hint = std::string()) {
    OptionDef o;
    o.id = id;
    o.label = label;
    o.kind = OptionKind::Bool;
    o.fallback = on ? "1" : "0";
    o.hint = hint;
    return o;
}

OptionDef choice(const std::string& id, const std::string& label, const std::string& fallback, std::vector<std::pair<std::string, std::string>> choices, const std::string& hint = std::string()) {
    OptionDef o;
    o.id = id;
    o.label = label;
    o.kind = OptionKind::Choice;
    o.fallback = fallback;
    o.choices = std::move(choices);
    o.hint = hint;
    return o;
}

OptionDef text(const std::string& id, const std::string& label, const std::string& fallback, const std::string& hint = std::string()) {
    OptionDef o;
    o.id = id;
    o.label = label;
    o.kind = OptionKind::Text;
    o.fallback = fallback;
    o.hint = hint;
    return o;
}

OptionDef number(const std::string& id, const std::string& label, OptionKind kind, const std::string& fallback, double min, double max, const std::string& hint = std::string()) {
    OptionDef o;
    o.id = id;
    o.label = label;
    o.kind = kind;
    o.fallback = fallback;
    o.min = min;
    o.max = max;
    o.hint = hint;
    return o;
}

OptionDef when(OptionDef o, const std::string& condition) {
    o.when = condition;
    return o;
}

std::vector<ModuleDef> build_defs() {
    std::vector<ModuleDef> d;
    const std::vector<std::pair<std::string, std::string>> clock_units = { { "auto", "Automatic" }, { "GHz", "GHz" }, { "MHz", "MHz" } };

    {
        ModuleDef m;
        m.id = "units";
        m.title = "Units";
        m.icon = "\xe2\x9a\x99";
        m.description = "How sizes, percentages and temperatures are written, for every module";
        m.options = {
            choice("bytes_unit", "Sizes in", "auto", { { "auto", "Automatic" }, { "B", "B" }, { "KB", "KB" }, { "MB", "MB" }, { "GB", "GB" }, { "TB", "TB" } }),
            choice("bytes_decimals", "Decimals for sizes", "1", { { "0", "0" }, { "1", "1" }, { "2", "2" } }),
            toggle("bytes_label", "Write the unit after sizes", true, "Turn off to write it yourself, like {ram_used}/{ram_total} GB"),
            choice("percent_decimals", "Decimals for percentages", "0", { { "0", "0" }, { "1", "1" }, { "2", "2" } }),
            choice("temp_unit", "Temperatures in", "C", { { "C", "\xc2\xb0" "C" }, { "F", "\xc2\xb0" "F" }, { "K", "K" } }),
            toggle("temp_label", "Write the unit after temperatures", true),
            text("unknown", "Show this when a reading isn't available", "", "Empty leaves the value blank, and a line whose values are all blank is left out"),
        };
        m.example = "{ram_used} \xc2\xb7 {cpu_temp}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "cpu";
        m.title = "CPU";
        m.icon = "\xe2\x96\xa3";
        m.description = "Processor use, clock speed, name and temperature";
        m.options = {
            toggle("clock", "Clock speed", true),
            when(choice("clock_unit", "Clock speed in", "auto", clock_units), "clock=1"),
            toggle("name", "Name", true),
            when(text("custom_name", "Custom name", "", "Leave empty to use the detected name"), "name=1"),
            toggle("temp", "Temperature", true),
            toggle("threads", "Thread count", false),
        };
        m.placeholders = {
            { "cpu", "how busy the CPU is", "" },
            { "cpu_clock", "current clock speed", "clock" },
            { "cpu_name", "the CPU's name", "name" },
            { "cpu_temp", "CPU temperature", "temp" },
            { "cpu_threads", "number of threads", "threads" },
        };
        m.example = "CPU {cpu} \xc2\xb7 {cpu_clock} \xc2\xb7 {cpu_temp}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "gpu";
        m.title = "GPU";
        m.icon = "\xe2\x96\xa6";
        m.description = "Graphics card use, name, temperature, clock and memory";
        m.options = {
            toggle("name", "Name", true),
            when(text("custom_name", "Custom name", "", "Leave empty to use the detected name"), "name=1"),
            toggle("temp", "Temperature", true),
            toggle("clock", "Clock speed", false),
            when(choice("clock_unit", "Clock speed in", "auto", clock_units), "clock=1"),
            toggle("memory", "Memory used and total", true),
            toggle("free", "Memory free", false),
            toggle("app", "Memory Pollen uses", false),
            toggle("vrchat", "Memory VRChat uses", false),
            toggle("percent", "Show memory amounts as percentages", false, "Used, free, Pollen and VRChat become a share of the total"),
        };
        m.placeholders = {
            { "gpu", "how busy the GPU is", "" },
            { "gpu_name", "the GPU's name", "name" },
            { "gpu_temp", "GPU temperature", "temp" },
            { "gpu_clock", "current clock speed", "clock" },
            { "gpu_used", "video memory the whole computer uses", "memory" },
            { "gpu_total", "video memory on the card", "memory" },
            { "gpu_free", "video memory left", "free" },
            { "gpu_app", "video memory Pollen uses", "app" },
            { "gpu_vrchat", "video memory VRChat uses", "vrchat" },
        };
        m.example = "GPU {gpu} \xc2\xb7 {gpu_temp}\n{gpu_used} / {gpu_total}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "ram";
        m.title = "RAM";
        m.icon = "\xe2\x96\xa4";
        m.description = "Memory used by the computer, by Pollen and by VRChat";
        m.options = {
            toggle("free", "Memory free", false),
            toggle("app", "Memory Pollen uses", false),
            toggle("vrchat", "Memory VRChat uses", false),
            toggle("percent", "Show amounts as percentages", false, "Used, free, Pollen and VRChat become a share of the total"),
        };
        m.placeholders = {
            { "ram", "share of memory in use", "" },
            { "ram_used", "memory the whole computer uses", "" },
            { "ram_total", "memory installed", "" },
            { "ram_free", "memory left", "free" },
            { "ram_app", "memory Pollen uses", "app" },
            { "ram_vrchat", "memory VRChat uses", "vrchat" },
        };
        m.example = "RAM {ram_used} / {ram_total}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "vram";
        m.title = "VRAM";
        m.icon = "\xe2\x96\xa5";
        m.description = "Video memory used by the computer, by Pollen and by VRChat";
        m.options = {
            toggle("free", "Memory free", false),
            toggle("app", "Memory Pollen uses", false),
            toggle("vrchat", "Memory VRChat uses", false),
            toggle("percent", "Show amounts as percentages", false, "Used, free, Pollen and VRChat become a share of the total"),
        };
        m.placeholders = {
            { "vram", "share of video memory in use", "" },
            { "vram_used", "video memory the whole computer uses", "" },
            { "vram_total", "video memory on the card", "" },
            { "vram_free", "video memory left", "free" },
            { "vram_app", "video memory Pollen uses", "app" },
            { "vram_vrchat", "video memory VRChat uses", "vrchat" },
        };
        m.example = "VRAM {vram_used} / {vram_total}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "uptime";
        m.title = "Uptime";
        m.icon = "\xe2\x8f\xb1";
        m.description = "How long the computer has been on, and how long VRChat has been connected";
        m.options = {
            choice("style", "Style", "short", { { "short", "1d 2h 3m" }, { "long", "1 day, 2 hours, 3 minutes" }, { "clock", "26:03" } }),
            toggle("days", "Count days", true, "Off counts hours past 24 instead"),
            toggle("seconds", "Seconds", false),
            toggle("connected", "Time connected to VRChat", true),
            toggle("app", "Time Pollen has been open", false),
        };
        m.placeholders = {
            { "uptime", "how long the computer has been on", "" },
            { "uptime_days", "days of that", "days" },
            { "uptime_hours", "hours of that", "" },
            { "uptime_minutes", "minutes of that", "" },
            { "uptime_seconds", "seconds of that", "seconds" },
            { "connected", "how long VRChat has been talking to Pollen", "connected" },
            { "app_uptime", "how long Pollen has been open", "app" },
        };
        m.example = "Up {uptime}\nIn VRChat for {connected}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "time";
        m.title = "Time";
        m.icon = "\xf0\x9f\x95\x92";
        m.description = "The time here, and in UTC, GMT and TAI";
        m.options = {
            toggle("hour24", "24-hour clock", false),
            toggle("seconds", "Seconds", false),
            when(toggle("leading_zero", "Leading zero on the hour", false), "hour24=0"),
            toggle("utc", "UTC", false),
            toggle("gmt", "GMT", false),
            toggle("tai", "TAI (atomic time)", false),
            when(number("tai_offset", "TAI is ahead of UTC by (seconds)", OptionKind::Integer, "37", 0, 100, "37 since 2017; change it if a leap second is ever added"), "tai=1"),
            toggle("zone", "Time zone name", false),
        };
        m.placeholders = {
            { "time", "the time here", "" },
            { "hour", "the hour", "" },
            { "minute", "the minute", "" },
            { "second", "the second", "seconds" },
            { "ampm", "AM or PM", "!hour24" },
            { "zone", "the time zone's short name", "zone" },
            { "utc", "the time in UTC", "utc" },
            { "gmt", "the time in GMT", "gmt" },
            { "tai", "the time in TAI", "tai" },
        };
        m.example = "{time}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "date";
        m.title = "Date";
        m.icon = "\xf0\x9f\x93\x85";
        m.description = "Today's date, the day and the month";
        m.options = {
            choice("format", "Date as", "long", { { "long", "October 28, 2026" }, { "long_eu", "28 October 2026" }, { "iso", "2026-10-28" }, { "us", "10/28/2026" }, { "eu", "28/10/2026" } }),
            choice("month", "Months", "long", { { "long", "October" }, { "short", "Oct" }, { "number", "10" } }),
            choice("weekday", "Weekdays", "long", { { "long", "Wednesday" }, { "short", "Wed" } }),
            toggle("day_of_year", "Day of the year", false),
        };
        m.placeholders = {
            { "date", "today's date", "" },
            { "weekday", "the day of the week", "" },
            { "day", "the day of the month", "" },
            { "month", "the month", "" },
            { "year", "the year", "" },
            { "day_of_year", "the day of the year", "day_of_year" },
        };
        m.example = "{weekday}, {date}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "media";
        m.title = "Now playing";
        m.icon = "\xe2\x99\xaa";
        m.description = "The song playing on this computer";
        m.options = {
            toggle("paused", "Keep showing the song while it's paused", true),
            toggle("player", "Player name", false),
            toggle("progress", "Position and length", false),
        };
        m.placeholders = {
            { "song", "artist - title", "" },
            { "artist", "the artist", "" },
            { "title", "the title", "" },
            { "media", "a play or pause sign and the song", "" },
            { "player", "the app playing it", "player" },
            { "position", "how far into the song", "progress" },
            { "length", "how long the song is", "progress" },
        };
        m.example = "\xe2\x99\xaa {song}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "weather";
        m.title = "Weather";
        m.icon = "\xe2\x9b\x85";
        m.description = "The weather for a city or a latitude and longitude, from Open-Meteo";
        m.options = {
            choice("mode", "Find the place by", "city", { { "city", "City" }, { "coordinates", "Latitude and longitude" } }),
            when(text("city", "City", "", "Add a state or country after a comma if the name is common, like Springfield, Illinois"), "mode=city"),
            when(number("latitude", "Latitude", OptionKind::Float, "0", -90, 90), "mode=coordinates"),
            when(number("longitude", "Longitude", OptionKind::Float, "0", -180, 180), "mode=coordinates"),
            choice("wind_unit", "Wind in", "kmh", { { "kmh", "km/h" }, { "mph", "mph" }, { "ms", "m/s" }, { "kn", "knots" } }),
            choice("refresh", "Check every", "15", { { "10", "10 minutes" }, { "15", "15 minutes" }, { "30", "30 minutes" }, { "60", "hour" } }),
            toggle("feels", "Feels like", false),
            toggle("humidity", "Humidity", false),
            toggle("wind", "Wind", false),
            toggle("place", "Place name", false),
        };
        m.placeholders = {
            { "weather_temp", "the temperature", "" },
            { "weather_condition", "clear, rain, snow and so on", "" },
            { "weather_icon", "a weather emoji", "" },
            { "weather_feels", "what the temperature feels like", "feels" },
            { "weather_humidity", "relative humidity", "humidity" },
            { "weather_wind", "wind speed", "wind" },
            { "weather_place", "the place it's for", "place" },
        };
        m.example = "{weather_icon} {weather_temp} {weather_condition}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "avatar";
        m.title = "Avatar";
        m.icon = "\xe2\x98\xba";
        m.description = "The avatar you're wearing";
        m.options = {
            toggle("id", "Avatar ID", false),
        };
        m.placeholders = {
            { "avatar", "the avatar's name (or ID when there's no name)", "" },
            { "params", "how many parameters it has", "" },
            { "avatar_id", "the avatar's ID", "id" },
        };
        m.example = "Wearing {avatar}";
        d.push_back(m);
    }
    return d;
}

const char* const month_long[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
const char* const month_short[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
const char* const weekday_long[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char* const weekday_short[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

std::tm local_tm(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::tm utc_tm(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &t);
#else
    gmtime_r(&t, &out);
#endif
    return out;
}

std::string two(int v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", v);
    return buf;
}

std::string fixed(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

int to_int(const std::string& s, int fallback) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    return end == s.c_str() ? fallback : static_cast<int>(v);
}

double to_double(const std::string& s, double fallback) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? fallback : v;
}

std::string mm_ss(double seconds) {
    if (seconds < 0) seconds = 0;
    const long long s = static_cast<long long>(seconds);
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buf, sizeof(buf), "%lld:%02lld", s / 60, s % 60);
    return buf;
}

} // namespace

const std::vector<ModuleDef>& module_defs() {
    static const std::vector<ModuleDef> defs = build_defs();
    return defs;
}

const ModuleDef* find_module(const std::string& id) {
    for (const ModuleDef& m : module_defs()) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

const OptionDef* find_option(const ModuleDef& module, const std::string& option) {
    for (const OptionDef& o : module.options) {
        if (o.id == option) return &o;
    }
    return nullptr;
}

const ModuleDef* module_of_placeholder(const std::string& name) {
    for (const ModuleDef& m : module_defs()) {
        for (const PlaceholderDef& p : m.placeholders) {
            if (p.name == name) return &m;
        }
    }
    return nullptr;
}

std::string option_value(const ModuleOptions& options, const std::string& module, const std::string& option) {
    const auto m = options.find(module);
    if (m != options.end()) {
        const auto o = m->second.find(option);
        if (o != m->second.end()) return o->second;
    }
    if (const ModuleDef* def = find_module(module)) {
        if (const OptionDef* o = find_option(*def, option)) return o->fallback;
    }
    return std::string();
}

bool option_on(const ModuleOptions& options, const std::string& module, const std::string& option) {
    return option_value(options, module, option) == "1";
}

bool option_visible(const ModuleOptions& options, const ModuleDef& module, const OptionDef& option) {
    if (option.when.empty()) return true;
    const std::size_t eq = option.when.find('=');
    if (eq == std::string::npos) return option_on(options, module.id, option.when);
    return option_value(options, module.id, option.when.substr(0, eq)) == option.when.substr(eq + 1);
}

bool placeholder_enabled(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder) {
    if (placeholder.needs.empty()) return true;
    if (placeholder.needs[0] == '!') return !option_on(options, module.id, placeholder.needs.substr(1));
    return option_on(options, module.id, placeholder.needs);
}

std::string requirement_text(const ModuleDef& module, const PlaceholderDef& placeholder) {
    if (placeholder.needs.empty()) return std::string();
    const bool negate = placeholder.needs[0] == '!';
    const OptionDef* o = find_option(module, negate ? placeholder.needs.substr(1) : placeholder.needs);
    if (!o) return std::string();
    return negate ? "Turn off \"" + o->label + "\"" : "Turn on \"" + o->label + "\"";
}

bool set_option(ModuleOptions& options, const std::string& module, const std::string& option, const std::string& value, std::string* error) {
    const ModuleDef* m = find_module(module);
    if (!m) {
        if (error) *error = "no module called " + module;
        return false;
    }
    const OptionDef* o = find_option(*m, option);
    if (!o) {
        if (error) *error = m->title + " has no option called " + option;
        return false;
    }
    std::string v = value;
    switch (o->kind) {
    case OptionKind::Bool: v = (value == "1" || value == "true") ? "1" : "0"; break;
    case OptionKind::Choice: {
        bool known = false;
        for (const auto& c : o->choices) known = known || c.first == value;
        if (!known) {
            if (error) *error = "\"" + value + "\" isn't one of the choices for " + o->label;
            return false;
        }
        break;
    }
    case OptionKind::Integer:
    case OptionKind::Float: {
        char* end = nullptr;
        double d = std::strtod(value.c_str(), &end);
        if (end == value.c_str()) {
            if (error) *error = o->label + " has to be a number";
            return false;
        }
        if (o->max > o->min) d = d < o->min ? o->min : (d > o->max ? o->max : d);
        v = o->kind == OptionKind::Integer ? std::to_string(static_cast<long long>(std::llround(d))) : fixed(d, 6);
        while (o->kind == OptionKind::Float && v.find('.') != std::string::npos && (v.back() == '0' || v.back() == '.')) {
            const bool dot = v.back() == '.';
            v.pop_back();
            if (dot) break;
        }
        break;
    }
    case OptionKind::Text: break;
    }
    options[module][option] = v;
    return true;
}

std::vector<std::string> enabled_placeholders(const ModuleOptions& options) {
    std::vector<std::string> out;
    for (const ModuleDef& m : module_defs()) {
        for (const PlaceholderDef& p : m.placeholders) {
            if (placeholder_enabled(options, m, p)) out.push_back(p.name);
        }
    }
    return out;
}

Units units_of(const ModuleOptions& options) {
    Units u;
    u.bytes_unit = option_value(options, "units", "bytes_unit");
    u.bytes_decimals = to_int(option_value(options, "units", "bytes_decimals"), 1);
    u.bytes_label = option_on(options, "units", "bytes_label");
    u.percent_decimals = to_int(option_value(options, "units", "percent_decimals"), 0);
    u.temp_unit = option_value(options, "units", "temp_unit");
    u.temp_label = option_on(options, "units", "temp_label");
    u.unknown = option_value(options, "units", "unknown");
    return u;
}

std::string format_bytes(double bytes, const Units& units) {
    static const char* names[] = { "B", "KB", "MB", "GB", "TB" };
    int unit = 0;
    if (units.bytes_unit == "auto") {
        double v = bytes;
        while (v >= 1024.0 && unit < 4) {
            v /= 1024.0;
            ++unit;
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            if (units.bytes_unit == names[i]) unit = i;
        }
    }
    const double v = bytes / std::pow(1024.0, unit);
    const std::string n = fixed(v, unit == 0 ? 0 : units.bytes_decimals);
    return units.bytes_label ? n + " " + names[unit] : n;
}

std::string format_percent(double percent, const Units& units) {
    return fixed(percent, units.percent_decimals) + "%";
}

std::string format_temp(double celsius, const Units& units) {
    if (units.temp_unit == "F") return fixed(celsius * 9.0 / 5.0 + 32.0, 0) + (units.temp_label ? "\xc2\xb0" "F" : "");
    if (units.temp_unit == "K") return fixed(celsius + 273.15, 0) + (units.temp_label ? " K" : "");
    return fixed(celsius, 0) + (units.temp_label ? "\xc2\xb0" "C" : "");
}

std::string format_clock(double mhz, const std::string& unit) {
    if (unit == "MHz" || (unit == "auto" && mhz < 1000.0)) return fixed(mhz, 0) + " MHz";
    return fixed(mhz / 1000.0, 2) + " GHz";
}

std::string format_span(double seconds, bool days, bool seconds_shown, const std::string& style) {
    if (seconds < 0) seconds = 0;
    long long s = static_cast<long long>(seconds);
    long long d = 0;
    if (days) {
        d = s / 86400;
        s %= 86400;
    }
    const long long h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    if (style == "clock") {
        std::string out;
        if (d > 0) out = std::to_string(d) + "d ";
        out += std::to_string(h) + ":" + two(static_cast<int>(m));
        if (seconds_shown) out += ":" + two(static_cast<int>(sec));
        return out;
    }
    const bool long_style = style == "long";
    std::vector<std::string> parts;
    auto add = [&](long long v, const char* shortname, const char* name) {
        if (long_style) parts.push_back(std::to_string(v) + " " + name + (v == 1 ? "" : "s"));
        else parts.push_back(std::to_string(v) + shortname);
    };
    if (d > 0) add(d, "d", "day");
    if (d > 0 || h > 0) add(h, "h", "hour");
    add(m, "m", "minute");
    if (seconds_shown) add(sec, "s", "second");
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += long_style ? ", " : " ";
        out += parts[i];
    }
    return out;
}

std::string format_clock_time(const std::tm& tm, bool hour24, bool seconds, bool leading_zero) {
    int h = tm.tm_hour;
    std::string out;
    if (hour24) {
        out = two(h);
    } else {
        h %= 12;
        if (h == 0) h = 12;
        out = leading_zero ? two(h) : std::to_string(h);
    }
    out += ":" + two(tm.tm_min);
    if (seconds) out += ":" + two(tm.tm_sec);
    if (!hour24) out += tm.tm_hour < 12 ? " AM" : " PM";
    return out;
}

WeatherConfig weather_config(const ModuleOptions& options) {
    WeatherConfig c;
    c.coordinates = option_value(options, "weather", "mode") == "coordinates";
    c.city = option_value(options, "weather", "city");
    c.latitude = to_double(option_value(options, "weather", "latitude"), 0.0);
    c.longitude = to_double(option_value(options, "weather", "longitude"), 0.0);
    c.refresh_minutes = to_double(option_value(options, "weather", "refresh"), 15.0);
    return c;
}

void module_values(const ModuleDef& module, const ModuleOptions& options, const ModuleInputs& in, StatusValues& out) {
    const Units u = units_of(options);
    const std::string& id = module.id;
    StatusValues v;
    auto known = [&](const std::optional<double>& x, const std::function<std::string(double)>& f) { return x ? f(*x) : u.unknown; };
    auto amount = [&](const std::optional<double>& x, const std::optional<double>& total, bool percent) {
        if (!x) return u.unknown;
        if (percent) return total && *total > 0.0 ? format_percent(100.0 * *x / *total, u) : u.unknown;
        return format_bytes(*x, u);
    };
    const SensorSnapshot& s = in.sensors;

    if (id == "cpu") {
        v["cpu"] = known(s.cpu_percent, [&](double p) { return format_percent(p, u); });
        v["cpu_clock"] = known(s.cpu_mhz, [&](double mhz) { return format_clock(mhz, option_value(options, id, "clock_unit")); });
        const std::string custom = option_value(options, id, "custom_name");
        v["cpu_name"] = !custom.empty() ? custom : (s.cpu_name.empty() ? u.unknown : s.cpu_name);
        v["cpu_temp"] = known(s.cpu_temp, [&](double t) { return format_temp(t, u); });
        v["cpu_threads"] = s.cpu_threads ? std::to_string(s.cpu_threads) : u.unknown;
    } else if (id == "gpu") {
        const bool pct = option_on(options, id, "percent");
        std::optional<double> free;
        if (s.vram_total && s.vram_used) free = *s.vram_total - *s.vram_used;
        v["gpu"] = known(s.gpu_percent, [&](double p) { return format_percent(p, u); });
        const std::string custom = option_value(options, id, "custom_name");
        v["gpu_name"] = !custom.empty() ? custom : (s.gpu_name.empty() ? u.unknown : s.gpu_name);
        v["gpu_temp"] = known(s.gpu_temp, [&](double t) { return format_temp(t, u); });
        v["gpu_clock"] = known(s.gpu_mhz, [&](double mhz) { return format_clock(mhz, option_value(options, id, "clock_unit")); });
        v["gpu_used"] = amount(s.vram_used, s.vram_total, pct);
        v["gpu_total"] = amount(s.vram_total, s.vram_total, false);
        v["gpu_free"] = amount(free, s.vram_total, pct);
        v["gpu_app"] = amount(s.vram_app, s.vram_total, pct);
        v["gpu_vrchat"] = s.vrchat_running ? amount(s.vram_vrchat, s.vram_total, pct) : u.unknown;
    } else if (id == "ram" || id == "vram") {
        const bool ram = id == "ram";
        const bool pct = option_on(options, id, "percent");
        const std::optional<double>& total = ram ? s.ram_total : s.vram_total;
        const std::optional<double>& used = ram ? s.ram_used : s.vram_used;
        std::optional<double> free;
        if (total && used) free = *total - *used;
        v[id] = total && used && *total > 0.0 ? format_percent(100.0 * *used / *total, u) : u.unknown;
        v[id + "_used"] = amount(used, total, pct);
        v[id + "_total"] = amount(total, total, false);
        v[id + "_free"] = amount(free, total, pct);
        v[id + "_app"] = amount(ram ? s.ram_app : s.vram_app, total, pct);
        v[id + "_vrchat"] = s.vrchat_running ? amount(ram ? s.ram_vrchat : s.vram_vrchat, total, pct) : u.unknown;
    } else if (id == "uptime") {
        const bool days = option_on(options, id, "days");
        const bool secs = option_on(options, id, "seconds");
        const std::string style = option_value(options, id, "style");
        if (s.uptime) {
            const double up = *s.uptime;
            long long total = static_cast<long long>(up);
            v["uptime"] = format_span(up, days, secs, style);
            v["uptime_days"] = std::to_string(total / 86400);
            v["uptime_hours"] = std::to_string(days ? (total / 3600) % 24 : total / 3600);
            v["uptime_minutes"] = std::to_string((total / 60) % 60);
            v["uptime_seconds"] = std::to_string(total % 60);
        } else {
            v["uptime"] = v["uptime_days"] = v["uptime_hours"] = v["uptime_minutes"] = v["uptime_seconds"] = u.unknown;
        }
        v["connected"] = in.connected_seconds >= 0.0 ? format_span(in.connected_seconds, days, secs, style) : u.unknown;
        v["app_uptime"] = format_span(in.app_seconds, days, secs, style);
    } else if (id == "time") {
        const std::time_t t = std::chrono::system_clock::to_time_t(in.now);
        const bool h24 = option_on(options, id, "hour24");
        const bool secs = option_on(options, id, "seconds");
        const bool zero = option_on(options, id, "leading_zero");
        const std::tm local = local_tm(t);
        v["time"] = format_clock_time(local, h24, secs, zero);
        int hour = local.tm_hour;
        if (!h24) {
            hour %= 12;
            if (hour == 0) hour = 12;
        }
        v["hour"] = h24 || zero ? two(hour) : std::to_string(hour);
        v["minute"] = two(local.tm_min);
        v["second"] = two(local.tm_sec);
        v["ampm"] = local.tm_hour < 12 ? "AM" : "PM";
        char zone[64] = {};
        std::strftime(zone, sizeof(zone), "%Z", &local);
        v["zone"] = zone;
        const std::tm utc = utc_tm(t);
        v["utc"] = format_clock_time(utc, h24, secs, zero);
        v["gmt"] = v["utc"];
        const std::tm tai = utc_tm(t + to_int(option_value(options, id, "tai_offset"), 37));
        v["tai"] = format_clock_time(tai, h24, secs, zero);
    } else if (id == "date") {
        const std::tm d = local_tm(std::chrono::system_clock::to_time_t(in.now));
        const std::string month_style = option_value(options, id, "month");
        const std::string format = option_value(options, id, "format");
        const std::string long_month = month_style == "short" ? month_short[d.tm_mon] : month_long[d.tm_mon];
        const std::string year = std::to_string(d.tm_year + 1900);
        v["day"] = std::to_string(d.tm_mday);
        v["month"] = month_style == "number" ? std::to_string(d.tm_mon + 1) : long_month;
        v["year"] = year;
        v["weekday"] = option_value(options, id, "weekday") == "short" ? weekday_short[d.tm_wday] : weekday_long[d.tm_wday];
        v["day_of_year"] = std::to_string(d.tm_yday + 1);
        if (format == "iso") v["date"] = year + "-" + two(d.tm_mon + 1) + "-" + two(d.tm_mday);
        else if (format == "us") v["date"] = std::to_string(d.tm_mon + 1) + "/" + std::to_string(d.tm_mday) + "/" + year;
        else if (format == "eu") v["date"] = std::to_string(d.tm_mday) + "/" + std::to_string(d.tm_mon + 1) + "/" + year;
        else if (format == "long_eu") v["date"] = std::to_string(d.tm_mday) + " " + long_month + " " + year;
        else v["date"] = long_month + " " + std::to_string(d.tm_mday) + ", " + year;
    } else if (id == "media") {
        const NowPlaying& np = in.media;
        const bool show = np.has_song() && (np.playing || (np.paused && option_on(options, id, "paused")));
        v["song"] = show ? np.song() : "";
        v["artist"] = show ? np.artist : "";
        v["title"] = show ? np.title : "";
        v["player"] = show ? np.player : "";
        v["media"] = show ? std::string(np.playing ? "\xe2\x96\xb6 " : "\xe2\x8f\xb8 ") + np.song() : "";
        v["position"] = show && np.length > 0 ? mm_ss(np.position) : "";
        v["length"] = show && np.length > 0 ? mm_ss(np.length) : "";
    } else if (id == "weather") {
        const WeatherSnapshot& w = in.weather;
        if (w.ok) {
            const std::string wind_unit = option_value(options, id, "wind_unit");
            double wind = w.wind_kmh;
            std::string label = "km/h";
            if (wind_unit == "mph") {
                wind /= 1.609344;
                label = "mph";
            } else if (wind_unit == "ms") {
                wind /= 3.6;
                label = "m/s";
            } else if (wind_unit == "kn") {
                wind /= 1.852;
                label = "kn";
            }
            v["weather_temp"] = format_temp(w.temp_c, u);
            v["weather_feels"] = format_temp(w.feels_c, u);
            v["weather_condition"] = weather_condition(w.code);
            v["weather_icon"] = weather_icon(w.code, w.is_day);
            v["weather_humidity"] = fixed(w.humidity, 0) + "%";
            v["weather_wind"] = fixed(wind, 0) + " " + label;
            v["weather_place"] = w.place;
        } else {
            for (const PlaceholderDef& p : module.placeholders) v[p.name] = u.unknown;
        }
    } else if (id == "avatar") {
        v["avatar"] = !in.avatar_name.empty() ? in.avatar_name : (in.avatar_id.empty() ? u.unknown : in.avatar_id);
        v["avatar_id"] = in.avatar_id.empty() ? u.unknown : in.avatar_id;
        v["params"] = std::to_string(in.params);
    }

    for (const PlaceholderDef& p : module.placeholders) {
        if (!placeholder_enabled(options, module, p)) continue;
        const auto it = v.find(p.name);
        out[p.name] = it == v.end() ? u.unknown : it->second;
    }
}

StatusValues all_module_values(const ModuleOptions& options, const ModuleInputs& inputs) {
    StatusValues out;
    for (const ModuleDef& m : module_defs()) module_values(m, options, inputs, out);
    return out;
}

} // namespace pollen
