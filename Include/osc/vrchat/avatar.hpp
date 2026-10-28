#ifndef OSC_VRCHAT_AVATAR_HPP
#define OSC_VRCHAT_AVATAR_HPP

#include "../message.hpp"
#include "../value.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osc {
namespace vrchat {

enum class ParamType { Unknown, Bool, Int, Float };

const char* to_string(ParamType type) noexcept;
ParamType param_type_from(std::string_view name) noexcept;
ParamType param_type_of(const Value& value) noexcept;
std::optional<Value> make_param_value(ParamType type, std::string_view text);
Value coerce_param_value(ParamType type, const Value& value);

struct ParameterInfo {
    std::string name;
    ParamType type = ParamType::Unknown;
    std::string input_address;
    std::string output_address;

    bool writable() const noexcept { return !input_address.empty(); }
    bool readable() const noexcept { return !output_address.empty(); }
};

struct AvatarConfig {
    std::string id;
    std::string name;
    std::vector<ParameterInfo> parameters;
    std::filesystem::path path;

    const ParameterInfo* find(std::string_view parameter) const noexcept;
};

std::optional<AvatarConfig> parse_avatar_config(std::string_view json, std::string* error = nullptr);
std::optional<AvatarConfig> load_avatar_config(const std::filesystem::path& path, std::string* error = nullptr);

class AvatarState {
public:
    struct Live {
        Value value;
        ParamType type = ParamType::Unknown;
        std::chrono::steady_clock::time_point updated;
        std::uint64_t changes = 0;
    };

    bool handle(const Message& message);

    const std::string& avatar_id() const noexcept { return m_avatar_id; }
    void set_avatar_id(std::string id);

    const std::optional<AvatarConfig>& config() const noexcept { return m_config; }
    void set_config(std::optional<AvatarConfig> config);

    const std::map<std::string, Live>& live() const noexcept { return m_live; }
    std::optional<Value> value(std::string_view parameter) const;
    void remember_sent(std::string_view parameter, const Value& value);

    std::uint64_t revision() const noexcept { return m_revision; }
    std::uint64_t value_revision() const noexcept { return m_value_revision; }

    void clear();

    std::function<void(const std::string&)> on_avatar_change;

private:
    void store(std::string name, const Value& value);

    std::string m_avatar_id;
    std::optional<AvatarConfig> m_config;
    std::map<std::string, Live> m_live;
    std::uint64_t m_revision = 0;
    std::uint64_t m_value_revision = 0;
};

} // namespace vrchat
} // namespace osc

#endif // OSC_VRCHAT_AVATAR_HPP
