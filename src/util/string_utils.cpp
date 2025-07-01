#include "string_utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace vstk {

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

} // namespace vstk