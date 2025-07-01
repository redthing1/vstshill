#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// vst3 sdk includes
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>

namespace vstk {

// forward declaration
class Plugin;

// parameter information structure
struct ParameterInfo {
  std::string name;
  std::string short_title;
  std::string units;
  Steinberg::Vst::ParamID id;
  double default_normalized_value;
  int32_t step_count;
  int32_t flags;
  bool is_discrete;
  bool supports_text_conversion;
  std::vector<std::string> value_strings;

  // convenience methods
  bool is_continuous() const { return !is_discrete; }
  bool is_automatable() const {
    return !(flags & Steinberg::Vst::ParameterInfo::kIsReadOnly);
  }
  bool is_bypassed() const {
    return flags & Steinberg::Vst::ParameterInfo::kIsBypass;
  }
};

// parameter value with metadata
struct ParameterValue {
  double normalized_value;
  std::string text_value;
  bool is_text_based;

  ParameterValue(double norm_val)
      : normalized_value(norm_val), is_text_based(false) {}
  ParameterValue(const std::string& text_val, double norm_val = 0.0)
      : normalized_value(norm_val), text_value(text_val), is_text_based(true) {}
};

// parameter discovery and manipulation interface
class ParameterManager {
public:
  explicit ParameterManager(Plugin& plugin);

  // parameter discovery
  bool discover_parameters();
  const std::vector<ParameterInfo>& parameters() const { return _parameters; }
  std::optional<ParameterInfo> find_by_name(const std::string& name) const;
  std::optional<ParameterInfo> find_by_id(Steinberg::Vst::ParamID id) const;

  // parameter manipulation
  bool set_parameter(const std::string& name, const ParameterValue& value);
  bool set_parameter(Steinberg::Vst::ParamID id, const ParameterValue& value);
  std::optional<double> get_parameter_normalized(const std::string& name) const;
  std::optional<double>
  get_parameter_normalized(Steinberg::Vst::ParamID id) const;
  std::optional<std::string> get_parameter_text(const std::string& name) const;
  std::optional<std::string>
  get_parameter_text(Steinberg::Vst::ParamID id) const;

  // text-to-value conversion
  std::optional<double> text_to_normalized_value(const std::string& param_name,
                                                 const std::string& text) const;
  std::optional<std::string>
  normalized_value_to_text(const std::string& param_name,
                           double normalized_value) const;

  // validation utilities
  bool validate_text_conversion(const ParameterInfo& param_info) const;

private:
  Plugin& _plugin;
  std::vector<ParameterInfo> _parameters;
  std::map<std::string, size_t> _name_to_index;
  std::map<Steinberg::Vst::ParamID, size_t> _id_to_index;

  // helper methods
  void build_lookup_maps();
  ParameterInfo create_parameter_info(Steinberg::Vst::ParamID id) const;
  bool extract_discrete_values(ParameterInfo& param_info) const;
};

} // namespace vstk