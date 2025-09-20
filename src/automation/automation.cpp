#include "automation.hpp"
#include "../ext/json.hpp"
#include "../util/string_utils.hpp"
#include <algorithm>
#include <stdexcept>

namespace vstk {

ParameterAutomation
Automation::parse_automation_definition(const std::string& json_str,
                                        double sample_rate,
                                        size_t input_length_in_samples) {
  auto json = nlohmann::json::parse(json_str);

  // parse json into a map of parameter names to automation definitions
  auto def = json.get<std::map<std::string, nlohmann::json>>();

  // convert automation definition into parameter automation instance
  // by converting keyframe times from string format to samples,
  // and text values into normalized float values
  ParameterAutomation automation;

  for (const auto& [param_name, automation_definition] : def) {
    AutomationKeyframes keyframes;

    if (automation_definition.is_primitive()) {
      // the entry is a single value to use the entire time
      auto [value, is_text] =
          get_parameter_value_from_json_primitive(automation_definition.dump());
      keyframes[0] = value;

    } else {
      // the entry is an automation object
      auto automation_object =
          automation_definition.get<std::map<std::string, nlohmann::json>>();

      for (const auto& [time_str, val] : automation_object) {
        // convert keyframe time to samples
        auto time_samples =
            parse_keyframe_time(time_str, sample_rate, input_length_in_samples);

        if (keyframes.contains(time_samples)) {
          throw std::runtime_error(
              "duplicate keyframe time: " + std::to_string(time_samples) +
              " (obtained from input string " + time_str + ")");
        }

        // get the internal float representation of the value provided
        auto [value, is_text] =
            get_parameter_value_from_json_primitive(val.dump());
        keyframes[time_samples] = value;
      }
    }

    automation[param_name] = keyframes;
  }

  return automation;
}

std::map<std::string, float>
Automation::get_parameter_values(const ParameterAutomation& automation,
                                 size_t sample_index) {
  std::map<std::string, float> values;

  for (const auto& [param_name, keyframes] : automation) {
    // interpolate value for current sample index based on keyframes.
    float value;

    // find first keyframe with time that is greater than the sample time.
    // this works because std::map is sorted by key in ascending order
    auto next_keyframe = std::upper_bound(
        keyframes.begin(), keyframes.end(), sample_index,
        [](size_t sample_index, const std::pair<size_t, float>& keyframe) {
          return sample_index < keyframe.first;
        });

    if (next_keyframe == keyframes.begin()) {
      // use the value of the first keyframe
      value = next_keyframe->second;

    } else if (next_keyframe == keyframes.end()) {
      // use the value of the last keyframe
      value = std::prev(next_keyframe)->second;

    } else {
      // linearly interpolate between the two keyframes
      auto prev_keyframe = std::prev(next_keyframe);

      auto keyframe_distance = next_keyframe->first - prev_keyframe->first;
      auto relative_pos =
          static_cast<float>(sample_index - prev_keyframe->first) /
          static_cast<float>(keyframe_distance);

      value =
          std::lerp(prev_keyframe->second, next_keyframe->second, relative_pos);
    }

    values[param_name] = value;
  }

  return values;
}

size_t Automation::parse_keyframe_time(const std::string& time_str,
                                       double sample_rate,
                                       size_t input_length_in_samples) {
  // remove any excess whitespace
  std::string trimmed = util::trim(time_str);

  bool is_seconds = util::ends_with_char(trimmed, 's');
  bool is_percentage = util::ends_with_char(trimmed, '%');

  if (is_seconds || is_percentage) {
    // remove the suffix and any whitespace preceding it
    std::string number_str = trimmed.substr(0, trimmed.length() - 1);
    number_str = util::trim(number_str);

    // parse the floating-point number
    float time;
    try {
      time = util::parse_float_strict(number_str);
    } catch (const std::invalid_argument& ia) {
      throw std::runtime_error("invalid floating-point number '" + number_str +
                               "'");
    }

    if (is_seconds) {
      return util::seconds_to_samples(time, sample_rate);
    } else /* if (is_percentage) */ {
      return static_cast<size_t>(std::round(
          (time / 100.0) * static_cast<double>(input_length_in_samples)));
    }
  }

  // no known suffix was detected - parse as an integer sample value
  size_t time;
  try {
    time = util::parse_ulong_strict(trimmed);
  } catch (const std::invalid_argument& ia) {
    throw std::runtime_error("invalid sample index '" + trimmed + "'");
  }

  return time;
}

std::pair<float, bool> Automation::get_parameter_value_from_json_primitive(
    const std::string& primitive_str) {
  auto json_val = nlohmann::json::parse(primitive_str);

  if (json_val.is_number()) {
    float val = json_val.get<float>();
    if (val < 0.0f || val > 1.0f) {
      throw std::out_of_range(
          "normalized parameter value must be between 0 and 1, but is " +
          std::to_string(val));
    }
    return {val, false};
  }

  if (json_val.is_string()) {
    // return as text value - caller will need to handle text-to-value
    // conversion
    std::string text_val = json_val.get<std::string>();
    // for now, just return 0.5 as placeholder - proper text conversion will be
    // implemented when integrating with VST3 parameter interfaces
    return {0.5f, true};
  }

  throw std::invalid_argument(
      "invalid parameter value type. must be a number or string");
}

} // namespace vstk
