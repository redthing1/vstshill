#pragma once

#include <map>
#include <string>

namespace vstk {

// automation keyframes: sample timestamp -> parameter value
using AutomationKeyframes = std::map<size_t, float>;

// parameter automation: parameter name -> keyframes
using ParameterAutomation = std::map<std::string, AutomationKeyframes>;

// automation processing utilities
class Automation {
public:
  // parse automation definition from json string
  static ParameterAutomation
  parse_automation_definition(const std::string& json_str, double sample_rate,
                              size_t input_length_in_samples);

  // get parameter values at specific sample index with interpolation
  static std::map<std::string, float>
  get_parameter_values(const ParameterAutomation& automation,
                       size_t sample_index);

private:
  // convert time string to samples (supports samples, seconds, percentage)
  static size_t parse_keyframe_time(const std::string& time_str,
                                    double sample_rate,
                                    size_t input_length_in_samples);

  // parse json primitive to parameter value (number or text)
  static std::pair<float, bool>
  get_parameter_value_from_json_primitive(const std::string& primitive_str);
};

} // namespace vstk