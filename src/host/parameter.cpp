#include "parameter.hpp"
#include "vstk.hpp"
#include <algorithm>
#include <public.sdk/source/vst/utility/stringconvert.h>
#include <redlog/redlog.hpp>
#include <sstream>

namespace vstk {

ParameterManager::ParameterManager(Plugin& plugin) : _plugin(plugin) {}

bool ParameterManager::discover_parameters() {
  if (!_plugin.is_loaded() || !_plugin._edit_controller) {
    return false;
  }

  auto* controller = _plugin._edit_controller.get();
  int32_t param_count = controller->getParameterCount();

  _parameters.clear();
  _parameters.reserve(param_count);

  for (int32_t i = 0; i < param_count; ++i) {
    Steinberg::Vst::ParameterInfo vst_param_info;
    if (controller->getParameterInfo(i, vst_param_info) ==
        Steinberg::kResultOk) {
      auto param_info = create_parameter_info(vst_param_info.id);
      _parameters.push_back(std::move(param_info));
    }
  }

  build_lookup_maps();
  return true;
}

std::optional<ParameterInfo>
ParameterManager::find_by_name(const std::string& name) const {
  auto it = _name_to_index.find(name);
  if (it != _name_to_index.end()) {
    return _parameters[it->second];
  }
  return std::nullopt;
}

std::optional<ParameterInfo>
ParameterManager::find_by_id(Steinberg::Vst::ParamID id) const {
  auto it = _id_to_index.find(id);
  if (it != _id_to_index.end()) {
    return _parameters[it->second];
  }
  return std::nullopt;
}

bool ParameterManager::set_parameter(const std::string& name,
                                     const ParameterValue& value) {
  auto param_info = find_by_name(name);
  if (!param_info) {
    return false;
  }
  return set_parameter(param_info->id, value);
}

bool ParameterManager::set_parameter(Steinberg::Vst::ParamID id,
                                     const ParameterValue& value) {
  if (!_plugin.is_loaded() || !_plugin._edit_controller) {
    return false;
  }

  auto* controller = _plugin._edit_controller.get();
  double normalized_value = value.normalized_value;

  // if text-based value, try to convert to normalized
  if (value.is_text_based) {
    Steinberg::Vst::String128 text;
    Steinberg::Vst::StringConvert::convert(value.text_value, text);

    Steinberg::Vst::ParamValue norm_val;
    if (controller->getParamValueByString(id, text, norm_val) ==
        Steinberg::kResultOk) {
      normalized_value = norm_val;
    } else {
      // text conversion failed, use provided normalized value or default to 0.0
      normalized_value = value.normalized_value;
    }
  }

  // clamp to valid range
  normalized_value = std::clamp(normalized_value, 0.0, 1.0);

  // set the parameter value
  return controller->setParamNormalized(id, normalized_value) ==
         Steinberg::kResultOk;
}

std::optional<double>
ParameterManager::get_parameter_normalized(const std::string& name) const {
  auto param_info = find_by_name(name);
  if (!param_info) {
    return std::nullopt;
  }
  return get_parameter_normalized(param_info->id);
}

std::optional<double>
ParameterManager::get_parameter_normalized(Steinberg::Vst::ParamID id) const {
  if (!_plugin.is_loaded() || !_plugin._edit_controller) {
    return std::nullopt;
  }

  auto* controller = _plugin._edit_controller.get();
  return controller->getParamNormalized(id);
}

std::optional<std::string>
ParameterManager::get_parameter_text(const std::string& name) const {
  auto param_info = find_by_name(name);
  if (!param_info) {
    return std::nullopt;
  }
  return get_parameter_text(param_info->id);
}

std::optional<std::string>
ParameterManager::get_parameter_text(Steinberg::Vst::ParamID id) const {
  if (!_plugin.is_loaded() || !_plugin._edit_controller) {
    return std::nullopt;
  }

  auto* controller = _plugin._edit_controller.get();
  double normalized_value = controller->getParamNormalized(id);

  Steinberg::Vst::String128 text;
  if (controller->getParamStringByValue(id, normalized_value, text) ==
      Steinberg::kResultOk) {
    return Steinberg::Vst::StringConvert::convert(text);
  }

  return std::nullopt;
}

std::optional<double>
ParameterManager::text_to_normalized_value(const std::string& param_name,
                                           const std::string& text) const {
  auto param_info = find_by_name(param_name);
  if (!param_info || !_plugin._edit_controller) {
    return std::nullopt;
  }

  auto* controller = _plugin._edit_controller.get();
  Steinberg::Vst::String128 vst_text;
  Steinberg::Vst::StringConvert::convert(text, vst_text);

  Steinberg::Vst::ParamValue normalized_value;
  if (controller->getParamValueByString(
          param_info->id, vst_text, normalized_value) == Steinberg::kResultOk) {
    return normalized_value;
  }

  return std::nullopt;
}

std::optional<std::string>
ParameterManager::normalized_value_to_text(const std::string& param_name,
                                           double normalized_value) const {
  auto param_info = find_by_name(param_name);
  if (!param_info || !_plugin._edit_controller) {
    return std::nullopt;
  }

  auto* controller = _plugin._edit_controller.get();
  Steinberg::Vst::String128 text;
  if (controller->getParamStringByValue(param_info->id, normalized_value,
                                        text) == Steinberg::kResultOk) {
    return Steinberg::Vst::StringConvert::convert(text);
  }

  return std::nullopt;
}

bool ParameterManager::validate_text_conversion(
    const ParameterInfo& param_info) const {
  if (!_plugin._edit_controller) {
    return false;
  }

  auto* controller = _plugin._edit_controller.get();

  // test conversion symmetry for several values
  int num_values_to_try =
      std::min(20, param_info.step_count > 0 ? param_info.step_count : 20);

  for (int i = 0; i < num_values_to_try; ++i) {
    double normalized_value =
        static_cast<double>(i) / static_cast<double>(num_values_to_try - 1);

    // get text representation
    Steinberg::Vst::String128 text;
    if (controller->getParamStringByValue(param_info.id, normalized_value,
                                          text) != Steinberg::kResultOk) {
      continue;
    }

    // convert back to normalized value
    Steinberg::Vst::ParamValue converted_value;
    if (controller->getParamValueByString(
            param_info.id, text, converted_value) != Steinberg::kResultOk) {
      return false; // conversion failed
    }

    // get text representation of converted value
    Steinberg::Vst::String128 text2;
    if (controller->getParamStringByValue(param_info.id, converted_value,
                                          text2) != Steinberg::kResultOk) {
      return false;
    }

    // check if text representations match (conversion is symmetric)
    std::string text_str1 = Steinberg::Vst::StringConvert::convert(text);
    std::string text_str2 = Steinberg::Vst::StringConvert::convert(text2);
    if (text_str1 != text_str2) {
      return false;
    }
  }

  return true;
}

void ParameterManager::build_lookup_maps() {
  _name_to_index.clear();
  _id_to_index.clear();

  for (size_t i = 0; i < _parameters.size(); ++i) {
    const auto& param = _parameters[i];
    _name_to_index[param.name] = i;
    _id_to_index[param.id] = i;
  }
}

ParameterInfo
ParameterManager::create_parameter_info(Steinberg::Vst::ParamID id) const {
  if (!_plugin._edit_controller) {
    return {};
  }

  auto* controller = _plugin._edit_controller.get();

  // find parameter index by id
  Steinberg::Vst::ParameterInfo vst_info;
  int32_t param_count = controller->getParameterCount();
  bool found = false;
  for (int32_t i = 0; i < param_count; ++i) {
    if (controller->getParameterInfo(i, vst_info) == Steinberg::kResultOk &&
        vst_info.id == id) {
      found = true;
      break;
    }
  }
  if (!found) {
    return {};
  }

  // create parameter info
  ParameterInfo param_info;
  param_info.name = Steinberg::Vst::StringConvert::convert(vst_info.title);
  param_info.short_title =
      Steinberg::Vst::StringConvert::convert(vst_info.shortTitle);
  param_info.units = Steinberg::Vst::StringConvert::convert(vst_info.units);
  param_info.id = vst_info.id;
  param_info.default_normalized_value = vst_info.defaultNormalizedValue;
  param_info.step_count = vst_info.stepCount;
  param_info.flags = vst_info.flags;
  param_info.is_discrete = (vst_info.stepCount > 0);

  // skip text conversion validation during initial loading
  param_info.supports_text_conversion = false;

  // extract discrete values if applicable
  if (param_info.is_discrete) {
    extract_discrete_values(param_info);
  }

  return param_info;
}

bool ParameterManager::extract_discrete_values(
    ParameterInfo& param_info) const {
  if (!_plugin._edit_controller || param_info.step_count <= 0) {
    return false;
  }

  auto* controller = _plugin._edit_controller.get();
  param_info.value_strings.clear();
  param_info.value_strings.reserve(param_info.step_count + 1);

  for (int32_t i = 0; i <= param_info.step_count; ++i) {
    double normalized_value =
        static_cast<double>(i) / static_cast<double>(param_info.step_count);

    Steinberg::Vst::String128 text;
    if (controller->getParamStringByValue(param_info.id, normalized_value,
                                          text) == Steinberg::kResultOk) {
      param_info.value_strings.emplace_back(
          Steinberg::Vst::StringConvert::convert(text));
    } else {
      // fallback to numeric representation
      std::ostringstream oss;
      oss << normalized_value;
      param_info.value_strings.push_back(oss.str());
    }
  }

  return true;
}

} // namespace vstk