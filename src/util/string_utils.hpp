#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vstk::util {

// convert seconds to samples
size_t seconds_to_samples(double sec, double sample_rate);

// parse string to float, strict validation
float parse_float_strict(const std::string& str);

// parse string to unsigned long, strict validation
unsigned long parse_ulong_strict(const std::string& str);

// trim whitespace from both ends
std::string trim(const std::string& str);

// join list of strings with separator (defaults to ", ")
std::string join_strings(const std::vector<std::string>& values,
                         std::string_view separator = ", ");

// check if string ends with character
bool ends_with_char(const std::string& str, char ch);

// wait for user input (debugging utility)
void wait_for_input(const std::string& message = "press enter to continue...");

} // namespace vstk::util
