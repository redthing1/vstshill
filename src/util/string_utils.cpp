#include "string_utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace vstk::util {

size_t seconds_to_samples(double sec, double sample_rate) {
  return static_cast<size_t>(sec * sample_rate);
}

#define PARSE_STRICT(fun_name)                                                 \
  size_t end_ptr;                                                              \
  auto num = (fun_name)(str, &end_ptr);                                        \
  if (end_ptr != str.size()) {                                                 \
    throw std::invalid_argument("invalid number: '" + str + "'");              \
  }                                                                            \
  return num

float parse_float_strict(const std::string& str) { PARSE_STRICT(std::stof); }

unsigned long parse_ulong_strict(const std::string& str) {
  PARSE_STRICT(std::stoul);
}

std::string trim(const std::string& str) {
  auto start = str.begin();
  while (start != str.end() && std::isspace(*start)) {
    start++;
  }

  auto end = str.end();
  do {
    end--;
  } while (std::distance(start, end) > 0 && std::isspace(*end));

  return std::string(start, end + 1);
}

bool ends_with_char(const std::string& str, char ch) {
  return !str.empty() && str.back() == ch;
}

void wait_for_input(const std::string& message) {
  std::cout << message << std::flush;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

std::string join_strings(const std::vector<std::string>& values,
                         std::string_view separator) {
  if (values.empty()) {
    return {};
  }

  std::ostringstream stream;
  for (size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      stream << separator;
    }
    stream << values[index];
  }

  return stream.str();
}

} // namespace vstk::util
