#include "parameters_command.hpp"
#include "../host/parameter.hpp"
#include "../host/vstk.hpp"
#include "../util/vst_discovery.hpp"
#include <iostream>
#include <redlog/redlog.hpp>

extern redlog::logger log_main;
extern void apply_verbosity();

namespace vstk {

ParametersCommand::ParametersCommand(args::Subparser& parser)
    : parser_(parser),
      plugin_path_(parser, "plugin_path",
                   "path or name of vst3 plugin to analyze parameters") {}

int ParametersCommand::execute() {
  apply_verbosity();

  parser_.Parse();

  if (!plugin_path_) {
    log_main.err("plugin path or name required for parameters command");
    std::cerr << parser_;
    return 1;
  }

  auto resolved_path = vstk::util::resolve_plugin_path(args::get(plugin_path_));
  if (resolved_path.empty()) {
    return 1;
  }

  try {
    // create a plugin instance
    vstk::Plugin plugin(log_main);

    // load the plugin
    auto result = plugin.load(resolved_path);
    if (!result) {
      log_main.error("failed to load plugin",
                     redlog::field("error", result.error()));
      return 1;
    }

    log_main.info("plugin loaded successfully",
                  redlog::field("name", plugin.name()));

    // test parameter discovery
    const auto& params = plugin.parameters().parameters();
    log_main.info("parameter discovery",
                  redlog::field("parameter_count", params.size()));

    if (params.empty()) {
      log_main.info("no parameters found in plugin");
      return 0;
    }

    // show all parameters
    for (size_t i = 0; i < params.size(); ++i) {
      const auto& param = params[i];

      log_main.info(
          "parameter details", redlog::field("index", i),
          redlog::field("name", param.name), redlog::field("id", param.id),
          redlog::field("discrete", param.is_discrete),
          redlog::field("text_conversion", param.supports_text_conversion),
          redlog::field("default_value", param.default_normalized_value));

      // get current values
      auto current_norm =
          plugin.parameters().get_parameter_normalized(param.name);
      auto current_text = plugin.parameters().get_parameter_text(param.name);

      if (current_norm) {
        log_main.debug(
            "parameter values", redlog::field("parameter", param.name),
            redlog::field("normalized", *current_norm),
            redlog::field("text", current_text.value_or("(no text)")));
      }

      // if discrete, show available values
      if (param.is_discrete && !param.value_strings.empty()) {
        std::string values_list;
        for (size_t j = 0; j < std::min(param.value_strings.size(), size_t(5));
             ++j) {
          if (j > 0) {
            values_list += ", ";
          }
          values_list += param.value_strings[j];
        }
        if (param.value_strings.size() > 5) {
          values_list += " ...";
        }

        log_main.debug("discrete values",
                       redlog::field("parameter", param.name),
                       redlog::field("values", values_list));
      }
    }

  } catch (const std::exception& e) {
    log_main.error("exception during parameter analysis",
                   redlog::field("what", e.what()));
    return 1;
  }

  return 0;
}

} // namespace vstk