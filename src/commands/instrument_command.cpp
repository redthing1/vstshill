#include "instrument_command.hpp"
#include "../util/vst_discovery.hpp"
#include "../instrumentation/tracer_host.hpp"
#include <iostream>
#include <redlog.hpp>

extern redlog::logger log_main;
extern void apply_verbosity();
extern args::CounterFlag verbosity_flag;

namespace vstk {

InstrumentCommand::InstrumentCommand(args::Subparser& parser)
    : parser_(parser), plugin_path_(parser, "plugin_path", "vst3 plugin path"),
      pause_flag_(parser, "pause", "pause after load", {"pause"}),

      // tracer selection
      tracer_type_(parser, "tracer", "tracer type (w1cov|w1xfer|w1script)",
                   {"tracer"}, args::Options::Required),

      // coverage options
      coverage_out_(parser, "coverage-out", "coverage output file",
                    {"coverage-out"}),

      // transfer options
      transfers_out_(parser, "transfers-out", "transfers output file",
                     {"transfers-out"}),
      no_registers_(parser, "no-registers", "disable register logging",
                    {"no-registers"}),
      no_stack_info_(parser, "no-stack", "disable stack logging", {"no-stack"}),
      analyze_apis_(parser, "analyze-apis", "enable api analysis",
                    {"analyze-apis"}),

      // script options
      script_path_(parser, "script", "lua script path", {"script"}),
      script_config_(parser, "script-config", "script configuration key=value",
                     {"script-config"}) {}

int InstrumentCommand::execute() {
  apply_verbosity();
  parser_.Parse();

  if (!plugin_path_) {
    log_main.err("plugin path required");
    return 1;
  }

  auto resolved_path = vstk::util::resolve_plugin_path(args::get(plugin_path_));
  if (resolved_path.empty()) {
    return 1;
  }

  // get tracer type
  std::string tracer_type = args::get(tracer_type_);

  if (tracer_type == "w1cov") {
    return execute_coverage(resolved_path);
  } else if (tracer_type == "w1xfer") {
    return execute_transfer(resolved_path);
  } else if (tracer_type == "w1script") {
    return execute_script(resolved_path);
  } else {
    log_main.err("unknown tracer type", redlog::field("type", tracer_type));
    return 1;
  }
}

int InstrumentCommand::execute_coverage(const std::string& plugin_path) {
  instrumentation::TracerHost host(log_main);

  w1cov::coverage_config config;
  if (coverage_out_) {
    config.output_file = args::get(coverage_out_);
  }

  host.inspect<w1cov::session>(plugin_path, config, pause_flag_);
  return 0;
}

int InstrumentCommand::execute_transfer(const std::string& plugin_path) {
  instrumentation::TracerHost host(log_main);

  w1xfer::transfer_config config;
  if (transfers_out_) {
    config.output_file = args::get(transfers_out_);
  }
  config.log_registers = !no_registers_;
  config.log_stack_info = !no_stack_info_;
  config.analyze_apis = analyze_apis_;
  config.verbose = args::get(verbosity_flag);

  host.inspect<w1xfer::session>(plugin_path, config, pause_flag_);
  return 0;
}

int InstrumentCommand::execute_script(const std::string& plugin_path) {
#ifdef WITNESS_SCRIPT_ENABLED
  if (!script_path_) {
    log_main.err("--script required for script tracer");
    return 1;
  }

  instrumentation::TracerHost host(log_main);

  w1::tracers::script::config config;
  config.script_path = args::get(script_path_);
  if (script_config_) {
    for (const auto& kv : args::get(script_config_)) {
      auto pos = kv.find('=');
      if (pos != std::string::npos) {
        config.script_config[kv.substr(0, pos)] = kv.substr(pos + 1);
      }
    }
  }

  host.inspect<w1::tracers::script::session>(plugin_path, config, pause_flag_);
  return 0;
#else
  log_main.err("script tracer not available (lua support disabled)");
  return 1;
#endif
}

} // namespace vstk