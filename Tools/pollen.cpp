#include <pollen/session.hpp>

#include <grove/grove.hpp>
#include <grove/runtime/runtime.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef POLLEN_VERSION_STRING
#define POLLEN_VERSION_STRING "0.1.0"
#endif

namespace fs = std::filesystem;
namespace vrc = osc::vrchat;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string clean(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

std::string escape_lines(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c != '\r') out += c;
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> fields_of(const std::vector<std::string>& args) {
    if (args.size() == 1) return split(args[0], '\t');
    return args;
}

std::string arg(const std::vector<std::string>& args, std::size_t i) {
    return i < args.size() ? args[i] : std::string();
}

std::string number(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string find_ui() {
    std::vector<fs::path> candidates;
    const std::string over = env("POLLEN_UI_DIR");
    if (!over.empty()) candidates.push_back(over);
    const fs::path exe = fs::path(grove::executable_path()).parent_path();
    candidates.push_back(exe / "ui");
    candidates.push_back(exe.parent_path() / "share" / "pollen" / "ui");
#ifdef POLLEN_SOURCE_UI_DIR
    candidates.push_back(POLLEN_SOURCE_UI_DIR);
#endif
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c / "pollen.grove", ec)) return (c / "pollen.grove").string();
    }
    return std::string();
}

class App {
public:
    explicit App(grove::Project& project) : m_doc(project.document) {
        register_natives();
    }

    void install() {
        m_doc.events().set_interval(33.0, [this] { m_session.tick(); });
    }

private:
    void native(const std::string& name, std::function<std::string(const std::vector<std::string>&)> fn) {
        grove::register_native(m_doc, "pollen." + name, std::move(fn));
    }

    std::string state() {
        const osc::Stats in = m_session.received_stats();
        const osc::Stats out = m_session.sent_stats();
        const vrc::Connection& c = m_session.connection();
        const vrc::ConnectionSettings& cs = c.settings();
        std::vector<std::string> f;
        f.push_back(m_session.connected() ? "1" : "0");
        f.push_back(m_session.hearing_vrchat() ? "1" : "0");
        f.push_back(m_session.connected() ? c.sender().target().to_string() : std::string());
        f.push_back(c.listening() ? osc::Endpoint(cs.listen_host, c.receiver().port()).to_string() : std::string());
        f.push_back(std::to_string(in.messages));
        f.push_back(std::to_string(out.messages));
        f.push_back(std::to_string(in.errors + out.errors));
        f.push_back(in.any ? number(in.seconds_since_last(), 1) : std::string("-1"));
        f.push_back(clean(m_session.avatar_name()));
        f.push_back(clean(m_session.avatar_id()));
        f.push_back(std::to_string(m_session.parameter_revision()));
        f.push_back(std::to_string(m_session.value_revision()));
        f.push_back(clean(m_session.last_error()));
        f.push_back(m_session.settings().status_enabled ? "1" : "0");
        f.push_back(std::to_string(m_session.connection().avatar().live().size()));
        const auto& config = m_session.connection().avatar().config();
        f.push_back(config ? std::to_string(config->parameters.size()) : std::string("0"));
        f.push_back(clean(m_session.oscquery_status()));
        f.push_back(m_session.found_vrchat() ? "1" : "0");
        std::string line;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (i) line += '\t';
            line += f[i];
        }
        return line;
    }

    std::string system() {
        const pollen::SystemSnapshot& s = m_session.system();
        const pollen::NowPlaying np = m_session.now_playing();
        std::vector<std::string> f;
        f.push_back(s.cpu_known ? number(s.cpu_percent, 0) + "%" : std::string("-"));
        f.push_back(s.memory_known ? number(s.memory_percent(), 0) + "%" : std::string("-"));
        f.push_back(s.memory_known ? pollen::format_bytes(s.memory_used) + " of " + pollen::format_bytes(s.memory_total) : std::string());
        f.push_back(pollen::format_duration(s.uptime_seconds));
        f.push_back(clean(np.has_song() ? np.song() : std::string()));
        f.push_back(np.playing ? "playing" : (np.paused ? "paused" : "stopped"));
        f.push_back(clean(np.player));
        f.push_back(clean(np.reason));
        const pollen::SensorSnapshot sn = m_session.sensors();
        const pollen::Units units = pollen::units_of(m_session.settings().modules);
        f.push_back(sn.gpu_percent ? number(*sn.gpu_percent, 0) + "%" : std::string("-"));
        f.push_back(sn.vram_used && sn.vram_total ? pollen::format_bytes(*sn.vram_used, units) + " of " + pollen::format_bytes(*sn.vram_total, units) : std::string());
        f.push_back(clean(sn.gpu_name));
        f.push_back(clean(sn.cpu_name));
        f.push_back(s.memory_known ? pollen::format_bytes(static_cast<double>(s.memory_used), units) + " of " + pollen::format_bytes(static_cast<double>(s.memory_total), units) : std::string());
        std::string warning = m_session.module_warning("cpu");
        const std::string gpu_warning = m_session.module_warning("gpu");
        if (!gpu_warning.empty()) warning += (warning.empty() ? "" : " ") + gpu_warning;
        f.push_back(clean(warning));
        std::string line;
        for (std::size_t i = 0; i < f.size(); ++i) {
            if (i) line += '\t';
            line += f[i];
        }
        return line;
    }

    std::string settings_text() {
        const pollen::Settings& s = m_session.settings();
        std::string out;
        out += "host\t" + clean(s.host) + "\n";
        out += "send_port\t" + std::to_string(s.send_port) + "\n";
        out += "receive_port\t" + std::to_string(s.receive_port) + "\n";
        out += "listen_host\t" + clean(s.listen_host) + "\n";
        out += "osc_folder\t" + clean(s.osc_folder) + "\n";
        out += "auto_connect\t" + std::string(s.auto_connect ? "1" : "0") + "\n";
        out += "oscquery\t" + std::string(s.oscquery ? "1" : "0") + "\n";
        out += "status_enabled\t" + std::string(s.status_enabled ? "1" : "0") + "\n";
        out += "status_interval\t" + number(s.status_interval, 1) + "\n";
        out += "status_notify\t" + std::string(s.status_notify ? "1" : "0") + "\n";
        out += "log_parameters\t" + std::string(s.log_parameters ? "1" : "0") + "\n";
        out += "status_template\t" + escape_lines(s.status_template) + "\n";
        out += "settings_path\t" + clean(m_session.settings_path().string()) + "\n";
        return out;
    }

    std::string set_setting(const std::string& key, const std::string& value) {
        pollen::Settings& s = m_session.settings();
        auto as_port = [](const std::string& v, int& out) {
            char* end = nullptr;
            const long p = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str() || *end || p < 0 || p > 65535) return false;
            out = static_cast<int>(p);
            return true;
        };
        const bool on = value == "1" || value == "true";
        if (key == "host") s.host = value;
        else if (key == "listen_host") s.listen_host = value;
        else if (key == "osc_folder") s.osc_folder = value;
        else if (key == "send_port") {
            if (!as_port(value, s.send_port)) return "error\tports go from 1 to 65535";
        } else if (key == "receive_port") {
            if (!as_port(value, s.receive_port)) return "error\tports go from 0 (don't listen) to 65535";
        } else if (key == "auto_connect") s.auto_connect = on;
        else if (key == "oscquery") s.oscquery = on;
        else if (key == "status_enabled") m_session.set_status_enabled(on);
        else if (key == "status_notify") s.status_notify = on;
        else if (key == "log_parameters") s.log_parameters = on;
        else if (key == "status_interval") {
            const double v = std::strtod(value.c_str(), nullptr);
            if (!(v >= 2.0) || !std::isfinite(v)) return "error\tVRChat ignores chatbox messages sent faster than every 2 seconds";
            s.status_interval = v;
        } else if (key == "status_template") m_session.set_status_template(value);
        else return "error\tno setting called " + key;
        std::string why;
        if (!m_session.save_settings(&why)) return "error\t" + clean(why);
        return "ok";
    }

    void register_natives() {
        native("version", [](const std::vector<std::string>&) { return std::string(POLLEN_VERSION_STRING); });
        native("state", [this](const std::vector<std::string>&) { return state(); });
        native("system", [this](const std::vector<std::string>&) { return system(); });
        native("connect", [this](const std::vector<std::string>&) {
            std::string why;
            if (!m_session.connect(&why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("disconnect", [this](const std::vector<std::string>&) {
            m_session.disconnect();
            return std::string("ok");
        });
        native("settings", [this](const std::vector<std::string>&) { return settings_text(); });
        native("set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string value = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) value += "\t" + f[i];
            return set_setting(arg(f, 0), value);
        });
        native("params", [this](const std::vector<std::string>& args) {
            const std::string filter = arg(args, 0);
            std::string out;
            for (const pollen::ParamRow& r : m_session.parameters()) {
                if (!filter.empty()) {
                    std::string a = r.name, b = filter;
                    for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (a.find(b) == std::string::npos) continue;
                }
                out += clean(r.name) + "\t" + vrc::to_string(r.type) + "\t" + clean(r.value) + "\t" + (r.writable ? "1" : "0") + "\t" + (r.known ? "1" : "0") + "\t" + (r.live ? "1" : "0") + "\n";
            }
            return out;
        });
        native("values", [this](const std::vector<std::string>&) {
            std::string out;
            for (const auto& kv : m_session.connection().avatar().live()) {
                const osc::Value& v = kv.second.value;
                std::string shown;
                if (v.is_real()) shown = number(v.to_double(), 3);
                else shown = v.to_string();
                out += clean(kv.first) + "\t" + clean(shown) + "\n";
            }
            return out;
        });
        native("set_param", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string why;
            if (!m_session.set_parameter(arg(f, 0), arg(f, 1), arg(f, 2), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("chatbox", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string text = arg(f, 0);
            for (char& c : text) {
                if (c == '\r') c = ' ';
            }
            if (!m_session.chatbox(text, arg(f, 1) == "1")) return std::string("error\tnot connected");
            return std::string("ok");
        });
        native("chat_preview", [](const std::vector<std::string>& args) {
            std::string text = arg(args, 0);
            for (std::size_t i = 1; i < args.size(); ++i) text += "\t" + args[i];
            return vrc::chatbox_text(text);
        });
        native("chat_drop_last", [](const std::vector<std::string>& args) {
            std::string text = arg(args, 0);
            while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u) text.pop_back();
            if (!text.empty()) text.pop_back();
            return text;
        });
        native("chat_length", [](const std::vector<std::string>& args) {
            return std::to_string(vrc::utf8_length(arg(args, 0)));
        });
        native("typing", [this](const std::vector<std::string>& args) {
            m_session.typing(arg(args, 0) == "1");
            return std::string("ok");
        });
        native("status_preview", [this](const std::vector<std::string>&) { return m_session.status_preview(); });
        native("status_send", [this](const std::vector<std::string>&) {
            if (!m_session.send_status_now()) return std::string("error\tnothing to send (is it connected, and is the template empty?)");
            return std::string("ok");
        });
        native("placeholders", [this](const std::vector<std::string>&) {
            std::string out;
            const pollen::ModuleOptions& opts = m_session.settings().modules;
            for (const pollen::ModuleDef& m : pollen::module_defs()) {
                for (const pollen::PlaceholderDef& p : m.placeholders) {
                    if (pollen::placeholder_enabled(opts, m, p)) out += "{" + p.name + "}\t" + clean(m.title + ": " + p.description) + "\n";
                }
            }
            return out;
        });
        native("modules", [this](const std::vector<std::string>&) {
            std::string out;
            const pollen::ModuleOptions& opts = m_session.settings().modules;
            for (const pollen::ModuleDef& m : pollen::module_defs()) {
                std::size_t on = 0;
                for (const pollen::PlaceholderDef& p : m.placeholders) on += pollen::placeholder_enabled(opts, m, p) ? 1 : 0;
                std::string sample = m_session.module_preview(m.id, m_session.module_template(m.id));
                if (sample.empty()) sample = "(nothing to show right now)";
                out += m.id + "\t" + m.icon + "\t" + clean(m.title) + "\t" + clean(m.description) + "\t" + escape_lines(sample) + "\t" + std::to_string(on) + "\t" + std::to_string(m.placeholders.size()) + "\n";
            }
            return out;
        });
        native("module_options", [this](const std::vector<std::string>& args) {
            const pollen::ModuleDef* m = pollen::find_module(arg(args, 0));
            if (!m) return std::string();
            std::string out;
            for (const pollen::OptionDef& o : m->options) {
                if (!pollen::option_visible(m_session.settings().modules, *m, o)) continue;
                const char* kind = "bool";
                switch (o.kind) {
                case pollen::OptionKind::Bool: kind = "bool"; break;
                case pollen::OptionKind::Choice: kind = "choice"; break;
                case pollen::OptionKind::Text: kind = "text"; break;
                case pollen::OptionKind::Integer: kind = "integer"; break;
                case pollen::OptionKind::Float: kind = "float"; break;
                }
                std::string choices;
                for (const auto& c : o.choices) choices += (choices.empty() ? "" : "|") + c.first + "=" + c.second;
                out += std::string(kind) + "\t" + o.id + "\t" + clean(o.label) + "\t" + clean(pollen::option_value(m_session.settings().modules, m->id, o.id)) + "\t" + clean(o.hint) + "\t" + clean(choices) + "\t" + number(o.min, 6) + "\t" + number(o.max, 6) + "\n";
            }
            return out;
        });
        native("module_set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string why;
            if (!m_session.set_module_option(arg(f, 0), arg(f, 1), arg(f, 2), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("module_placeholders", [this](const std::vector<std::string>& args) {
            const pollen::ModuleDef* m = pollen::find_module(arg(args, 0));
            if (!m) return std::string();
            std::string out;
            for (const pollen::PlaceholderDef& p : m->placeholders) {
                const bool on = pollen::placeholder_enabled(m_session.settings().modules, *m, p);
                out += p.name + "\t" + clean(p.description) + "\t" + (on ? "1" : "0") + "\t" + clean(pollen::requirement_text(*m, p)) + "\n";
            }
            return out;
        });
        native("module_title", [](const std::vector<std::string>& args) {
            const pollen::ModuleDef* m = pollen::find_module(arg(args, 0));
            return m ? m->icon + "\t" + m->title + "\t" + m->description : std::string();
        });
        native("module_template", [this](const std::vector<std::string>& args) {
            return escape_lines(m_session.module_template(arg(args, 0)));
        });
        native("module_template_set", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string text = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) text += "\t" + f[i];
            std::string why;
            if (!m_session.set_module_template(arg(f, 0), text, &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("module_preview", [this](const std::vector<std::string>& args) {
            const auto f = fields_of(args);
            std::string text = arg(f, 1);
            for (std::size_t i = 2; i < f.size(); ++i) text += "\t" + f[i];
            return m_session.module_preview(arg(f, 0), text);
        });
        native("module_status", [this](const std::vector<std::string>& args) {
            return m_session.module_status(arg(args, 0));
        });
        native("module_warning", [this](const std::vector<std::string>& args) {
            return m_session.module_warning(arg(args, 0));
        });
        native("status_append", [this](const std::vector<std::string>& args) {
            std::string text = arg(args, 0);
            for (std::size_t i = 1; i < args.size(); ++i) text += "\t" + args[i];
            std::string tpl = m_session.settings().status_template;
            while (!tpl.empty() && tpl.back() == '\n') tpl.pop_back();
            tpl += tpl.empty() ? text : "\n" + text;
            m_session.set_status_template(tpl);
            std::string why;
            if (!m_session.save_settings(&why)) return "error\t" + clean(why);
            return escape_lines(tpl);
        });
        native("log", [this](const std::vector<std::string>& args) {
            const std::uint64_t since = std::strtoull(arg(args, 0).c_str(), nullptr, 10);
            std::string out;
            for (const pollen::LogLine& l : m_session.log_since(since, 300)) {
                out += std::to_string(l.seq) + "\t" + std::string(1, l.kind) + "\t" + number(l.seconds, 2) + "\t" + clean(l.text) + "\n";
            }
            return out;
        });
        native("log_clear", [this](const std::vector<std::string>&) {
            m_session.clear_log();
            return std::string("ok");
        });
        native("send", [this](const std::vector<std::string>& args) {
            std::string why;
            if (!m_session.send_text(arg(args, 0), &why)) return "error\t" + clean(why);
            return std::string("ok");
        });
        native("folders", [this](const std::vector<std::string>&) {
            std::string out;
            for (const fs::path& p : m_session.osc_folders()) out += clean(p.string()) + "\n";
            return out;
        });
        native("reload_avatar", [this](const std::vector<std::string>&) {
            return std::string(m_session.reload_avatar() ? "ok" : "error\tno OSC file for this avatar yet; turn OSC on in VRChat's action menu and switch avatars once");
        });
    }

    grove::Document& m_doc;
    pollen::Session m_session;
};

int bundle_ui(const std::string& output, bool portable) {
    const std::string ui = find_ui();
    if (ui.empty()) {
        std::cerr << "pollen: can't find the UI (pollen.grove) to bundle; set POLLEN_UI_DIR to the folder that holds it\n";
        return 1;
    }
    grove::AppBuildOptions options;
    options.output = output;
    options.stub = grove::executable_path();
    options.tools = grove::default_script_tools(ui);
    options.portable = portable;
    grove::Diagnostics diagnostics;
    const grove::AppBuildResult result = grove::build_app(ui, options, diagnostics, &std::cout);
    if (!result.ok) {
        diagnostics.print(std::cerr);
        std::cerr << "pollen: couldn't bundle the UI into " << output << "\n";
        return 1;
    }
    std::cout << "pollen: wrote " << result.output << " (" << result.files << " files, " << result.modules << " compiled scripts)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version") {
            std::cout << "pollen " << POLLEN_VERSION_STRING << "\n";
            return 0;
        }
        if (a == "--help") {
            std::cout << "usage: pollen [--check] [--bundle OUTPUT [--portable]]\n\n"
                         "  --check          load the window and its script without opening it, then exit\n"
                         "  --bundle OUTPUT  write a copy of pollen with the window and its compiled script built in\n"
                         "  --portable       with --bundle, link the script with the static C++ runtime (when the compiler has one)\n\n"
                         "Set POLLEN_UI_DIR to use a different UI folder, POLLEN_CONFIG_DIR to keep settings somewhere else,\n"
                         "and VRCHAT_OSC_DIR if Pollen can't find VRChat's OSC folder by itself.\n";
            return 0;
        }
        if (a == "--bundle") {
            if (i + 1 >= argc) {
                std::cerr << "pollen: --bundle needs an output path\n";
                return 2;
            }
            bool portable = false;
            for (int j = 1; j < argc; ++j) portable = portable || std::string(argv[j]) == "--portable";
            return bundle_ui(argv[i + 1], portable);
        }
        if (a == "--check") check = true;
    }

    grove::LoadedApp loaded;
    grove::Project from_files;
    grove::Project* project = nullptr;
    grove::runtime::RunOptions options;
    std::optional<grove::Bundle> bundle;
    if (env("POLLEN_UI_DIR").empty()) bundle = grove::read_embedded_bundle(grove::executable_path());
    if (bundle) {
        std::string error;
        if (!grove::load_app(*bundle, loaded, error)) {
            std::cerr << "pollen: " << error << "\n";
            return 1;
        }
        project = &loaded.project;
        options.window_id = loaded.window_id;
        options.prebuilt = &loaded.scripts;
        if (check) {
            std::cout << "pollen " << POLLEN_VERSION_STRING << ": built-in window ok, " << loaded.scripts.runnable_count() << " compiled script(s)\n";
            return loaded.scripts.ok() ? 0 : 1;
        }
    } else {
        const std::string ui = find_ui();
        if (ui.empty()) {
            std::cerr << "pollen: can't find the UI (pollen.grove); set POLLEN_UI_DIR to the folder that holds it\n";
            return 1;
        }
        from_files = grove::load_project(ui);
        if (!from_files.diagnostics.ok()) {
            from_files.diagnostics.print(std::cerr);
            return 1;
        }
        if (check) {
            std::cout << "pollen " << POLLEN_VERSION_STRING << ": window ok (" << ui << ")\n";
            return 0;
        }
        project = &from_files;
        options.tools = grove::default_script_tools(ui);
        options.tools.cache_dir = (pollen::Settings::config_dir() / "script-cache").string();
    }
    auto app = std::make_unique<App>(*project);
    app->install();
    return grove::runtime::run(*project, options);
}
