#ifndef OSC_PATTERN_HPP
#define OSC_PATTERN_HPP

#include <string>
#include <string_view>

namespace osc {

bool matches(std::string_view pattern, std::string_view address);

bool is_pattern(std::string_view address) noexcept;

bool is_valid_address(std::string_view address) noexcept;

bool is_valid_pattern(std::string_view pattern) noexcept;

} // namespace osc

#endif // OSC_PATTERN_HPP
