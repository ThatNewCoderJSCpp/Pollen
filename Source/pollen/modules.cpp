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

PlaceholderDef ph(const std::string& name, const std::string& description, const std::string& needs, const std::string& units = std::string(), bool percent = false) {
    PlaceholderDef p;
    p.name = name;
    p.description = description;
    p.needs = needs;
    p.units = units;
    p.percent = percent;
    return p;
}

OptionDef when(OptionDef o, const std::string& condition) {
    o.when = condition;
    return o;
}

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices& byte_units() {
    static const Choices c = { { "auto", "Automatic" }, { "B", "B" }, { "KB", "KB" }, { "MB", "MB" }, { "GB", "GB" }, { "TB", "TB" } };
    return c;
}

const Choices& temp_units() {
    static const Choices c = { { "C", "\xc2\xb0" "C" }, { "F", "\xc2\xb0" "F" }, { "K", "K" } };
    return c;
}

const Choices& speed_units() {
    static const Choices c = { { "kmh", "km/h" }, { "mph", "mph" }, { "ms", "m/s" }, { "kn", "knots" }, { "fts", "ft/s" } };
    return c;
}

const Choices& clock_units() {
    static const Choices c = { { "auto", "Automatic" }, { "GHz", "GHz" }, { "MHz", "MHz" } };
    return c;
}

const Choices& offset_precisions() {
    static const Choices c = { { "auto", "What's needed" }, { "hours", "Hours (-5)" }, { "minutes", "Minutes (-5:00)" }, { "seconds", "Seconds (-5:00:00)" } };
    return c;
}

Choices with_none(const Choices& c) {
    Choices out = { { "none", "Nothing else" } };
    out.insert(out.end(), c.begin(), c.end());
    return out;
}

Choices with_default(const Choices& c) {
    Choices out = { { "default", "Module default" } };
    out.insert(out.end(), c.begin(), c.end());
    return out;
}

std::vector<ModuleDef> build_defs() {
    std::vector<ModuleDef> d;

    {
        ModuleDef m;
        m.id = "units";
        m.title = "Units";
        m.icon = "\xe2\x9a\x99";
        m.description = "The default way sizes, percentages and temperatures are written in every module. Each placeholder can pick its own unit, and whether it's a percentage, in its own module";
        m.options = {
            choice("bytes_unit", "Sizes in (default)", "auto", byte_units()),
            choice("bytes_decimals", "Decimals for sizes", "1", { { "0", "0" }, { "1", "1" }, { "2", "2" } }),
            toggle("bytes_label", "Write the unit after sizes", true, "Turn off to write it yourself, like {ram_used}/{ram_total} GB"),
            choice("percent_decimals", "Decimals for percentages", "0", { { "0", "0" }, { "1", "1" }, { "2", "2" } }),
            choice("temp_unit", "Temperatures in (default)", "C", temp_units()),
            toggle("temp_label", "Write the unit after temperatures", true),
            choice("temp_also", "Also show temperatures in (default)", "none", with_none(temp_units()), "Shows both, like 12\xc2\xb0" "C (54\xc2\xb0" "F)"),
            choice("temp_decimals", "Decimals for temperatures", "0", { { "0", "0" }, { "1", "1" }, { "2", "2" } }),
            choice("both_style", "Show two units as", "paren", { { "paren", "12\xc2\xb0" "C (54\xc2\xb0" "F)" }, { "slash", "12\xc2\xb0" "C / 54\xc2\xb0" "F" }, { "space", "12\xc2\xb0" "C 54\xc2\xb0" "F" } }, "Used when temperatures or wind have a second unit"),
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
            when(choice("clock_unit", "Clock speed in (default)", "auto", clock_units()), "clock=1"),
            toggle("name", "Name", true),
            when(text("custom_name", "Custom name", "", "Leave empty to use the detected name"), "name=1"),
            toggle("temp", "Temperature", true),
            toggle("threads", "Thread count", false),
        };
        m.placeholders = {
            ph("cpu", "CPU load: how busy the processor is right now, as a percentage", ""),
            ph("cpu_clock", "the processor's current clock speed", "clock", "clock"),
            ph("cpu_name", "the processor's name", "name"),
            ph("cpu_temp", "the processor's temperature", "temp", "temp"),
            ph("cpu_threads", "how many threads the processor has", "threads"),
        };
        m.example = "CPU {cpu} \xc2\xb7 {cpu_clock} \xc2\xb7 {cpu_temp}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "gpu";
        m.title = "GPU";
        m.icon = "\xe2\x96\xa6";
        m.description = "Graphics card load (how busy it is), name, temperature, clock, and its video memory (VRAM) in GB or as a percentage";
        m.options = {
            toggle("name", "Name", true),
            when(text("custom_name", "Custom name", "", "Leave empty to use the detected name"), "name=1"),
            toggle("temp", "Temperature", true),
            toggle("clock", "Clock speed", false),
            when(choice("clock_unit", "Clock speed in (default)", "auto", clock_units()), "clock=1"),
            toggle("memory", "Video memory used and total", true),
            toggle("free", "Video memory free", false),
            toggle("app", "Video memory Pollen uses", false),
            toggle("vrchat", "Video memory VRChat uses", false),
        };
        m.placeholders = {
            ph("gpu", "GPU load: how busy the graphics card is right now, as a percentage. This is not memory; use {gpu_used} for that", ""),
            ph("gpu_name", "the graphics card's name", "name"),
            ph("gpu_temp", "the graphics card's temperature", "temp", "temp"),
            ph("gpu_clock", "the graphics card's current clock speed", "clock", "clock"),
            { "gpu_used", "video memory (VRAM) in use by everything on the computer, like 3.2 GB", "memory", "bytes", true },
            ph("gpu_total", "video memory (VRAM) the card has, like 16.0 GB", "memory", "bytes"),
            { "gpu_free", "video memory (VRAM) still free, like 12.8 GB", "free", "bytes", true },
            { "gpu_app", "video memory (VRAM) Pollen uses", "app", "bytes", true },
            { "gpu_vrchat", "video memory (VRAM) VRChat uses", "vrchat", "bytes", true },
        };
        m.example = "GPU {gpu} \xc2\xb7 {gpu_temp}\nVRAM {gpu_used} / {gpu_total}";
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
        };
        m.placeholders = {
            ph("ram", "share of memory (RAM) in use, as a percentage. {ram_used} is the same thing in GB", ""),
            { "ram_used", "memory (RAM) in use by everything on the computer", "", "bytes", true },
            ph("ram_total", "memory (RAM) installed", "", "bytes"),
            { "ram_free", "memory (RAM) still free", "free", "bytes", true },
            { "ram_app", "memory (RAM) Pollen uses", "app", "bytes", true },
            { "ram_vrchat", "memory (RAM) VRChat uses", "vrchat", "bytes", true },
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
            toggle("free", "Video memory free", false),
            toggle("app", "Video memory Pollen uses", false),
            toggle("vrchat", "Video memory VRChat uses", false),
        };
        m.placeholders = {
            ph("vram", "share of video memory (VRAM) in use, as a percentage. {vram_used} is the same thing in GB", ""),
            { "vram_used", "video memory (VRAM) in use by everything on the computer. Same as {gpu_used}", "", "bytes", true },
            ph("vram_total", "video memory (VRAM) the card has", "", "bytes"),
            { "vram_free", "video memory (VRAM) still free", "free", "bytes", true },
            { "vram_app", "video memory (VRAM) Pollen uses", "app", "bytes", true },
            { "vram_vrchat", "video memory (VRAM) VRChat uses", "vrchat", "bytes", true },
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
            ph("uptime", "how long the computer has been on", ""),
            ph("uptime_days", "days of that", "days"),
            ph("uptime_hours", "hours of that", ""),
            ph("uptime_minutes", "minutes of that", ""),
            ph("uptime_seconds", "seconds of that", "seconds"),
            ph("connected", "how long VRChat has been talking to Pollen", "connected"),
            ph("app_uptime", "how long Pollen has been open", "app"),
        };
        m.example = "Up {uptime}\nIn VRChat for {connected}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "time";
        m.title = "Time";
        m.icon = "\xf0\x9f\x95\x92";
        m.description = "The time here, in UTC, GMT and TAI, and how far the time here is from each";
        m.options = {
            toggle("hour24", "24-hour clock", false),
            toggle("seconds", "Seconds", false),
            when(toggle("leading_zero", "Leading zero on the hour", false), "hour24=0"),
            toggle("utc", "UTC", false),
            toggle("gmt", "GMT", false),
            toggle("tai", "TAI (atomic time)", false),
            when(number("tai_offset", "TAI is ahead of UTC by (seconds)", OptionKind::Integer, "37", 0, 100, "37 since 2017; change it if a leap second is ever added"), "tai=1"),
            toggle("zone", "Time zone name", false),
            toggle("offset", "Offset from UTC, GMT and TAI", false, "How far the time here is ahead of or behind each, like UTC-5 or UTC+5:30"),
            when(choice("offset_precision", "Offsets show (default)", "auto", offset_precisions(), "\"What's needed\" writes -5, +5:30 or -5:00:37; \"Hours\" rounds to the nearest hour"), "offset=1"),
            when(choice("offset_style", "Write offsets as", "named", { { "named", "UTC-5" }, { "bare", "-5" }, { "padded", "-05 (two-digit hour)" }, { "named_padded", "UTC-05 (two-digit hour)" } }), "offset=1"),
        };
        m.placeholders = {
            ph("time", "the time here", ""),
            ph("hour", "the hour", ""),
            ph("minute", "the minute", ""),
            ph("second", "the second", "seconds"),
            ph("ampm", "AM or PM", "!hour24"),
            ph("zone", "the time zone's short name", "zone"),
            ph("utc", "the time in UTC", "utc"),
            ph("gmt", "the time in GMT", "gmt"),
            ph("tai", "the time in TAI", "tai"),
            ph("utc_offset", "how far the time here is from UTC, like UTC-5", "offset", "offset"),
            ph("gmt_offset", "how far the time here is from GMT, like GMT-5", "offset", "offset"),
            ph("tai_offset", "how far the time here is from TAI, which is ahead of UTC by the leap seconds, like TAI-5:00:37", "offset", "offset"),
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
            ph("date", "today's date", ""),
            ph("weekday", "the day of the week", ""),
            ph("day", "the day of the month", ""),
            ph("month", "the month", ""),
            ph("year", "the year", ""),
            ph("day_of_year", "the day of the year", "day_of_year"),
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
            ph("song", "artist - title", ""),
            ph("artist", "the artist", ""),
            ph("title", "the title", ""),
            ph("media", "a play or pause sign and the song", ""),
            ph("player", "the app playing it", "player"),
            ph("position", "how far into the song", "progress"),
            ph("length", "how long the song is", "progress"),
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
            choice("wind_unit", "Wind in (default)", "kmh", speed_units()),
            choice("refresh", "Check every", "15", { { "10", "10 minutes" }, { "15", "15 minutes" }, { "30", "30 minutes" }, { "60", "hour" } }),
            toggle("feels", "Feels like", false),
            toggle("humidity", "Humidity", false),
            toggle("wind", "Wind", false),
            when(choice("wind_also", "Also show wind in (default)", "none", with_none(speed_units()), "Shows both, like 18 km/h (11 mph)"), "wind=1"),
            when(choice("wind_decimals", "Decimals for wind", "0", { { "0", "0" }, { "1", "1" }, { "2", "2" } }), "wind=1"),
            toggle("place", "Place name", false),
        };
        m.placeholders = {
            ph("weather_temp", "the temperature outside", "", "temp"),
            ph("weather_condition", "clear, rain, snow and so on", ""),
            ph("weather_icon", "a weather emoji", ""),
            ph("weather_feels", "what the temperature feels like, counting wind and humidity", "feels", "temp"),
            ph("weather_humidity", "relative humidity, as a percentage", "humidity"),
            ph("weather_wind", "wind speed", "wind", "speed"),
            ph("weather_place", "the place it's for", "place"),
        };
        m.example = "{weather_icon} {weather_temp} {weather_condition}";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "world";
        m.title = "World";
        m.icon = "\xf0\x9f\x8c\x8d";
        m.description = "The world you're in, what kind of instance it is, and who's there, read from VRChat's log";
        m.options = {
            toggle("instance", "Instance number", false),
            toggle("region", "Region", false),
            toggle("time_here", "Time in this world", true),
            when(choice("time_style", "Write the time as", "short", { { "short", "1h 2m" }, { "long", "1 hour, 2 minutes" }, { "clock", "1:02" } }), "time_here=1"),
            toggle("last", "Who joined or left last", false),
            toggle("id", "World ID", false),
        };
        m.placeholders = {
            ph("world", "the name of the world you're in", ""),
            ph("world_type", "the kind of instance: Public, Friends+, Friends, Invite+, Invite, Group, Group+ or Group Public", ""),
            ph("players", "how many people are in your instance, counting you", ""),
            ph("world_instance", "the instance number", "instance"),
            ph("world_region", "the server region: US West, US East, Europe or Japan", "region"),
            ph("world_time", "how long you've been in this world", "time_here"),
            ph("last_joined", "the last person who joined your instance", "last"),
            ph("last_left", "the last person who left your instance", "last"),
            ph("world_id", "the world's ID (wrld_...)", "id"),
        };
        m.example = "\xf0\x9f\x8c\x8d {world} ({world_type}) \xc2\xb7 {players} here";
        d.push_back(m);
    }
    {
        ModuleDef m;
        m.id = "friends";
        m.title = "Friends";
        m.icon = "\xf0\x9f\x91\xa5";
        m.description = "Your status and your friends, from your VRChat account. Log in under Settings to use it";
        m.options = {
            toggle("status_text", "Your status text", false),
            toggle("counts", "How many friends are online", true),
            toggle("here", "Friends in your instance", false),
            when(number("here_max", "Names to show at most", OptionKind::Integer, "3", 1, 20), "here=1"),
            toggle("news", "Latest friend news", false, "Like \"Alex is online\" or \"Sam went to The Black Cat (Public)\""),
        };
        m.placeholders = {
            ph("status", "your status: Join Me, Online, Ask Me or Do Not Disturb", ""),
            ph("status_color", "your status's color: Blue, Green, Orange or Red", ""),
            ph("status_icon", "a colored dot for your status", ""),
            ph("status_text", "the text under your status", "status_text"),
            ph("friends_online", "how many friends are in VRChat right now", "counts"),
            ph("friends_web", "how many friends are only on the VRChat website", "counts"),
            ph("friends_total", "how many friends you have", "counts"),
            ph("friends_here", "how many of your friends are in your instance", "here"),
            ph("friends_here_names", "their names", "here"),
            ph("friend_news", "the latest thing a friend did", "news"),
        };
        m.example = "{status_icon} {status} \xc2\xb7 {friends_online} friends online";
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
            ph("avatar", "the avatar's name (or ID when there's no name)", ""),
            ph("params", "how many parameters it has", ""),
            ph("avatar_id", "the avatar's ID", "id"),
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

std::string signed_fixed(double v, int decimals) {
    std::string out = fixed(v, decimals);
    if (!out.empty() && out[0] == '-' && out.find_first_not_of("-0.") == std::string::npos) out.erase(0, 1);
    return out;
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

const PlaceholderDef* find_placeholder(const ModuleDef& module, const std::string& name) {
    for (const PlaceholderDef& p : module.placeholders) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::string placeholder_key(const std::string& placeholder, const std::string& setting) {
    return placeholder + "." + setting;
}

std::vector<OptionDef> placeholder_settings(const ModuleDef&, const PlaceholderDef& placeholder) {
    std::vector<OptionDef> out;
    if (placeholder.percent) out.push_back(toggle("percent", "As a percentage", false, "Show it as a share of the total instead of an amount"));
    if (placeholder.units == "bytes") {
        out.push_back(choice("unit", "Unit", "default", with_default(byte_units())));
    } else if (placeholder.units == "temp") {
        out.push_back(choice("unit", "Unit", "default", with_default(temp_units())));
        out.push_back(choice("also", "Also in", "default", with_default(with_none(temp_units()))));
    } else if (placeholder.units == "speed") {
        out.push_back(choice("unit", "Unit", "default", with_default(speed_units())));
        out.push_back(choice("also", "Also in", "default", with_default(with_none(speed_units()))));
    } else if (placeholder.units == "clock") {
        out.push_back(choice("unit", "Unit", "default", with_default(clock_units())));
    } else if (placeholder.units == "offset") {
        out.push_back(choice("precision", "Shows", "default", with_default(offset_precisions())));
    }
    return out;
}

std::string placeholder_default(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder, const std::string& setting) {
    if (setting == "percent") return "0";
    const std::string& kind = placeholder.units;
    if (kind == "bytes" && setting == "unit") return option_value(options, "units", "bytes_unit");
    if (kind == "temp" && setting == "unit") return option_value(options, "units", "temp_unit");
    if (kind == "temp" && setting == "also") return option_value(options, "units", "temp_also");
    if (kind == "speed" && setting == "unit") return option_value(options, module.id, "wind_unit");
    if (kind == "speed" && setting == "also") return option_value(options, module.id, "wind_also");
    if (kind == "clock" && setting == "unit") return option_value(options, module.id, "clock_unit");
    if (kind == "offset" && setting == "precision") return option_value(options, module.id, "offset_precision");
    return std::string();
}

std::string placeholder_value(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder, const std::string& setting) {
    const auto m = options.find(module.id);
    if (m != options.end()) {
        const auto o = m->second.find(placeholder_key(placeholder.name, setting));
        if (o != m->second.end() && !o->second.empty() && o->second != "default") return o->second;
    }
    return placeholder_default(options, module, placeholder, setting);
}

std::vector<OptionDef> visible_placeholder_settings(const ModuleOptions& options, const ModuleDef& module, const PlaceholderDef& placeholder) {
    std::vector<OptionDef> out;
    const bool percent = placeholder.percent && placeholder_value(options, module, placeholder, "percent") == "1";
    for (const OptionDef& o : placeholder_settings(module, placeholder)) {
        if (percent && o.id != "percent") continue;
        out.push_back(o);
    }
    return out;
}

std::string choice_label(const OptionDef& option, const std::string& value) {
    for (const auto& c : option.choices) {
        if (c.first == value) return c.second;
    }
    return value;
}

std::string option_value(const ModuleOptions& options, const std::string& module, const std::string& option) {
    const auto m = options.find(module);
    if (m != options.end()) {
        const auto o = m->second.find(option);
        if (o != m->second.end()) return o->second;
    }
    if (const ModuleDef* def = find_module(module)) {
        if (const OptionDef* o = find_option(*def, option)) return o->fallback;
        const std::size_t dot = option.find('.');
        if (dot != std::string::npos) {
            if (const PlaceholderDef* p = find_placeholder(*def, option.substr(0, dot))) {
                for (const OptionDef& o : placeholder_settings(*def, *p)) {
                    if (o.id == option.substr(dot + 1)) return o.fallback;
                }
            }
        }
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
    std::vector<OptionDef> settings;
    const std::size_t dot = option.find('.');
    if (!o && dot != std::string::npos) {
        const PlaceholderDef* p = find_placeholder(*m, option.substr(0, dot));
        if (p) settings = placeholder_settings(*m, *p);
        for (const OptionDef& s : settings) {
            if (s.id == option.substr(dot + 1)) o = &s;
        }
    }
    if (!o && option == "percent") {
        bool any = false;
        for (const PlaceholderDef& p : m->placeholders) {
            if (!p.percent) continue;
            any = set_option(options, module, placeholder_key(p.name, "percent"), value, error) || any;
        }
        if (any) return true;
    }
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
    u.temp_also = option_value(options, "units", "temp_also");
    u.temp_decimals = to_int(option_value(options, "units", "temp_decimals"), 0);
    u.both_style = option_value(options, "units", "both_style");
    return u;
}

std::string format_bytes_in(double bytes, const std::string& unit_name, int decimals, bool label) {
    static const char* names[] = { "B", "KB", "MB", "GB", "TB" };
    int unit = 0;
    if (unit_name == "auto" || unit_name.empty()) {
        double v = bytes;
        while (v >= 1024.0 && unit < 4) {
            v /= 1024.0;
            ++unit;
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            if (unit_name == names[i]) unit = i;
        }
    }
    const double v = bytes / std::pow(1024.0, unit);
    const std::string n = fixed(v, unit == 0 ? 0 : decimals);
    return label ? n + " " + names[unit] : n;
}

std::string format_bytes(double bytes, const Units& units) {
    return format_bytes_in(bytes, units.bytes_unit, units.bytes_decimals, units.bytes_label);
}

std::string format_percent(double percent, const Units& units) {
    return fixed(percent, units.percent_decimals) + "%";
}

std::string format_temp_in(double celsius, const std::string& unit, int decimals, bool label) {
    if (unit == "F") return signed_fixed(celsius * 9.0 / 5.0 + 32.0, decimals) + (label ? "\xc2\xb0" "F" : "");
    if (unit == "K") return signed_fixed(celsius + 273.15, decimals) + (label ? " K" : "");
    return signed_fixed(celsius, decimals) + (label ? "\xc2\xb0" "C" : "");
}

std::string format_temp(double celsius, const Units& units) {
    const std::string first = format_temp_in(celsius, units.temp_unit, units.temp_decimals, units.temp_label);
    if (units.temp_also.empty() || units.temp_also == "none" || units.temp_also == units.temp_unit) return first;
    return format_both(first, format_temp_in(celsius, units.temp_also, units.temp_decimals, units.temp_label), units.both_style);
}

std::string format_speed(double kmh, const std::string& unit, int decimals) {
    if (unit == "mph") return signed_fixed(kmh / 1.609344, decimals) + " mph";
    if (unit == "ms") return signed_fixed(kmh / 3.6, decimals) + " m/s";
    if (unit == "kn") return signed_fixed(kmh / 1.852, decimals) + " kn";
    if (unit == "fts") return signed_fixed(kmh / 1.09728, decimals) + " ft/s";
    return signed_fixed(kmh, decimals) + " km/h";
}

std::string format_both(const std::string& first, const std::string& second, const std::string& style) {
    if (style == "slash") return first + " / " + second;
    if (style == "space") return first + " " + second;
    return first + " (" + second + ")";
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

long long utc_offset_seconds(std::time_t t) {
    const std::tm l = local_tm(t);
    const std::tm g = utc_tm(t);
    long long days = 0;
    if (l.tm_year != g.tm_year) days = l.tm_year > g.tm_year ? 1 : -1;
    else days = l.tm_yday - g.tm_yday;
    return days * 86400 + (l.tm_hour - g.tm_hour) * 3600LL + (l.tm_min - g.tm_min) * 60LL + (l.tm_sec - g.tm_sec);
}

std::string format_offset(long long seconds, const std::string& precision, const std::string& style, const std::string& name) {
    const char sign = seconds < 0 ? '-' : '+';
    long long a = seconds < 0 ? -seconds : seconds;
    if (precision == "hours") a = (a + 1800) / 3600 * 3600;
    const long long h = a / 3600, m = (a / 60) % 60, s = a % 60;
    const bool padded = style == "padded" || style == "named_padded";
    const bool named = style == "named" || style == "named_padded";
    std::string out = named ? name : std::string();
    out += sign;
    out += padded ? two(static_cast<int>(h)) : std::to_string(h);
    const bool show_seconds = precision == "seconds" || (precision != "hours" && precision != "minutes" && s != 0);
    const bool show_minutes = show_seconds || precision == "minutes" || (precision != "hours" && m != 0);
    if (show_minutes) out += ":" + two(static_cast<int>(m));
    if (show_seconds) out += ":" + two(static_cast<int>(s));
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
    auto setting = [&](const std::string& name, const std::string& which) {
        const PlaceholderDef* p = find_placeholder(module, name);
        return p ? placeholder_value(options, module, *p, which) : std::string();
    };
    auto amount = [&](const std::string& name, const std::optional<double>& x, const std::optional<double>& total) {
        if (!x) return u.unknown;
        const PlaceholderDef* p = find_placeholder(module, name);
        if (p && p->percent && setting(name, "percent") == "1") return total && *total > 0.0 ? format_percent(100.0 * *x / *total, u) : u.unknown;
        return format_bytes_in(*x, setting(name, "unit"), u.bytes_decimals, u.bytes_label);
    };
    auto temp = [&](const std::string& name, const std::optional<double>& c) {
        if (!c) return u.unknown;
        Units t = u;
        t.temp_unit = setting(name, "unit");
        t.temp_also = setting(name, "also");
        return format_temp(*c, t);
    };
    auto clock = [&](const std::string& name, const std::optional<double>& mhz) { return mhz ? format_clock(*mhz, setting(name, "unit")) : u.unknown; };
    const SensorSnapshot& s = in.sensors;

    if (id == "cpu") {
        v["cpu"] = known(s.cpu_percent, [&](double p) { return format_percent(p, u); });
        v["cpu_clock"] = clock("cpu_clock", s.cpu_mhz);
        const std::string custom = option_value(options, id, "custom_name");
        v["cpu_name"] = !custom.empty() ? custom : (s.cpu_name.empty() ? u.unknown : s.cpu_name);
        v["cpu_temp"] = temp("cpu_temp", s.cpu_temp);
        v["cpu_threads"] = s.cpu_threads ? std::to_string(s.cpu_threads) : u.unknown;
    } else if (id == "gpu") {
        std::optional<double> free;
        if (s.vram_total && s.vram_used) free = *s.vram_total - *s.vram_used;
        v["gpu"] = known(s.gpu_percent, [&](double p) { return format_percent(p, u); });
        const std::string custom = option_value(options, id, "custom_name");
        v["gpu_name"] = !custom.empty() ? custom : (s.gpu_name.empty() ? u.unknown : s.gpu_name);
        v["gpu_temp"] = temp("gpu_temp", s.gpu_temp);
        v["gpu_clock"] = clock("gpu_clock", s.gpu_mhz);
        v["gpu_used"] = amount("gpu_used", s.vram_used, s.vram_total);
        v["gpu_total"] = amount("gpu_total", s.vram_total, s.vram_total);
        v["gpu_free"] = amount("gpu_free", free, s.vram_total);
        v["gpu_app"] = amount("gpu_app", s.vram_app, s.vram_total);
        v["gpu_vrchat"] = s.vrchat_running ? amount("gpu_vrchat", s.vram_vrchat, s.vram_total) : u.unknown;
    } else if (id == "ram" || id == "vram") {
        const bool ram = id == "ram";
        const std::optional<double>& total = ram ? s.ram_total : s.vram_total;
        const std::optional<double>& used = ram ? s.ram_used : s.vram_used;
        std::optional<double> free;
        if (total && used) free = *total - *used;
        v[id] = total && used && *total > 0.0 ? format_percent(100.0 * *used / *total, u) : u.unknown;
        v[id + "_used"] = amount(id + "_used", used, total);
        v[id + "_total"] = amount(id + "_total", total, total);
        v[id + "_free"] = amount(id + "_free", free, total);
        v[id + "_app"] = amount(id + "_app", ram ? s.ram_app : s.vram_app, total);
        v[id + "_vrchat"] = s.vrchat_running ? amount(id + "_vrchat", ram ? s.ram_vrchat : s.vram_vrchat, total) : u.unknown;
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
        const int tai_seconds = to_int(option_value(options, id, "tai_offset"), 37);
        const std::tm tai = utc_tm(t + tai_seconds);
        v["tai"] = format_clock_time(tai, h24, secs, zero);
        const long long offset = utc_offset_seconds(t);
        const std::string style = option_value(options, id, "offset_style");
        v["utc_offset"] = format_offset(offset, setting("utc_offset", "precision"), style, "UTC");
        v["gmt_offset"] = format_offset(offset, setting("gmt_offset", "precision"), style, "GMT");
        v["tai_offset"] = format_offset(offset - tai_seconds, setting("tai_offset", "precision"), style, "TAI");
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
            const int wind_decimals = to_int(option_value(options, id, "wind_decimals"), 0);
            const std::string wind_unit = setting("weather_wind", "unit");
            const std::string wind_also = setting("weather_wind", "also");
            std::string wind = format_speed(w.wind_kmh, wind_unit, wind_decimals);
            if (!wind_also.empty() && wind_also != "none" && wind_also != wind_unit) wind = format_both(wind, format_speed(w.wind_kmh, wind_also, wind_decimals), u.both_style);
            v["weather_temp"] = temp("weather_temp", w.temp_c);
            v["weather_feels"] = temp("weather_feels", w.feels_c);
            v["weather_condition"] = weather_condition(w.code);
            v["weather_icon"] = weather_icon(w.code, w.is_day);
            v["weather_humidity"] = fixed(w.humidity, 0) + "%";
            v["weather_wind"] = wind;
            v["weather_place"] = w.place;
        } else {
            for (const PlaceholderDef& p : module.placeholders) v[p.name] = u.unknown;
        }
    } else if (id == "world") {
        const WorldSnapshot& w = in.world;
        if (w.in_world) {
            v["world"] = w.world_name.empty() ? u.unknown : w.world_name;
            v["world_type"] = w.info.valid ? access_label(w.info.access) : u.unknown;
            v["players"] = std::to_string(w.players.size());
            v["world_instance"] = w.info.instance.empty() ? u.unknown : w.info.instance;
            v["world_region"] = w.info.valid ? region_label(w.info.region) : u.unknown;
            const double here = std::chrono::duration<double>(in.now - w.entered).count();
            v["world_time"] = here >= 0.0 ? format_span(here, true, false, option_value(options, id, "time_style")) : u.unknown;
            v["world_id"] = w.info.world_id.empty() ? u.unknown : w.info.world_id;
        } else {
            for (const char* name : { "world", "world_type", "players", "world_instance", "world_region", "world_time", "world_id" }) v[name] = u.unknown;
        }
        v["last_joined"] = in.last_joined.empty() ? u.unknown : in.last_joined;
        v["last_left"] = in.last_left.empty() ? u.unknown : in.last_left;
    } else if (id == "friends") {
        const bool on = in.account.state == LoginState::LoggedIn;
        const std::string& status = in.account.me.status;
        v["status"] = on && !status.empty() ? status_label(status) : u.unknown;
        v["status_color"] = on && !status.empty() ? status_color(status) : u.unknown;
        v["status_icon"] = on && !status.empty() ? status_icon(status) : u.unknown;
        v["status_text"] = on && !in.account.me.status_text.empty() ? in.account.me.status_text : u.unknown;
        v["friends_online"] = on ? std::to_string(in.friends_online) : u.unknown;
        v["friends_web"] = on ? std::to_string(in.friends_web) : u.unknown;
        v["friends_total"] = on ? std::to_string(in.friends_total) : u.unknown;
        v["friends_here"] = on ? std::to_string(in.friends_here.size()) : u.unknown;
        const int max = to_int(option_value(options, id, "here_max"), 3);
        std::string names;
        for (std::size_t i = 0; i < in.friends_here.size(); ++i) {
            if (static_cast<int>(i) >= max) {
                names += " +" + std::to_string(in.friends_here.size() - i);
                break;
            }
            names += (i ? ", " : "") + in.friends_here[i];
        }
        v["friends_here_names"] = names.empty() ? u.unknown : names;
        v["friend_news"] = in.friend_news.empty() ? u.unknown : in.friend_news;
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
