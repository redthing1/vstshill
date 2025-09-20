#include "instrument_command.hpp"
#include "../util/vst_discovery.hpp"
#include "../util/string_utils.hpp"
#include "../instrumentation/tracer_host.hpp"
#include "../host/constants.hpp"
#include <iostream>
#include <redlog.hpp>
#include <functional>
#include <unordered_map>

extern redlog::logger log_main;
extern void apply_verbosity();
extern args::CounterFlag verbosity_flag;

namespace vstk {

namespace {

template <typename TConfig>
void set_instrumentation_verbosity(TConfig& config, int verbosity) {
  config.verbose_instrumentation =
      verbosity >= vstk::constants::VERBOSITY_LEVEL_DEBUG;
}

} // namespace

InstrumentCommand::InstrumentCommand(args::Subparser& parser)
    : parser_(parser), plugin_path_(parser, "plugin_path", "vst3 plugin path"),
      pause_flag_(parser, "pause", "pause after load", {"pause"}),

      // tracer selection
      tracer_type_(parser, "tracer", "tracer type (w1cov|w1xfer|w1script)",
                   {"tracer"}, args::Options::Required),

      // coverage options
      coverage_out_(parser, "coverage-out", "coverage output file",
                    {"coverage-out"}),
      coverage_inst_(parser, "coverage-inst",
                     "enable instruction-level coverage tracing",
                     {"coverage-inst"}),

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
                     {"script-config"}),

      // module filtering
      module_filter_(parser, "module-filter",
                     "filter modules to instrument (substring match, or '$' "
                     "for target module only)",
                     {'f', "module-filter"}) {}

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

  const std::string tracer_type = args::get(tracer_type_);

  const std::unordered_map<std::string, std::function<int(const std::string&)>>
      dispatch{{"w1cov", [this](const std::string& path) {
                   return execute_coverage(path);
                 }},
                {"w1xfer", [this](const std::string& path) {
                   return execute_transfer(path);
                 }},
                {"w1script", [this](const std::string& path) {
                   return execute_script(path);
                 }}};

  auto it = dispatch.find(tracer_type);
  if (it == dispatch.end()) {
    log_main.err("unknown tracer type", redlog::field("type", tracer_type));
    return 1;
  }

  return it->second(resolved_path);
}

int InstrumentCommand::execute_coverage(const std::string& plugin_path) {
  instrumentation::TracerHost host(log_main);

  w1cov::coverage_config config;
  if (coverage_out_) {
    config.output_file = args::get(coverage_out_);
  }
  config.inst_trace = coverage_inst_;
  config.verbose = verbosity_level();
  set_instrumentation_verbosity(config, verbosity_level());

  const std::string filter = module_filter_value();
  host.inspect<w1cov::session>(plugin_path, config, pause_flag_, filter);
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
  config.verbose = verbosity_level();
  set_instrumentation_verbosity(config, verbosity_level());

  const std::string filter = module_filter_value();
  host.inspect<w1xfer::session>(plugin_path, config, pause_flag_, filter);
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
  config.verbose = verbosity_level() >= vstk::constants::VERBOSITY_LEVEL_TRACE;
  set_instrumentation_verbosity(config, verbosity_level());
  if (script_config_) {
    for (const auto& kv : args::get(script_config_)) {
      auto pos = kv.find('=');
      if (pos != std::string::npos) {
        auto key = vstk::trim(kv.substr(0, pos));
        auto value = vstk::trim(kv.substr(pos + 1));
        if (!key.empty()) {
          config.script_config[key] = value;
        } else {
          log_main.warn("ignoring empty script config key",
                        redlog::field("entry", kv));
        }
      } else {
        log_main.warn("invalid script config entry (expected key=value)",
                      redlog::field("entry", kv));
      }
    }
  }

  const std::string filter = module_filter_value();
  host.inspect<w1::tracers::script::session>(plugin_path, config, pause_flag_,
                                             filter);
  return 0;
#else
  log_main.err("script tracer not available (lua support disabled)");
  return 1;
#endif
}

int InstrumentCommand::verbosity_level() const {
  return args::get(verbosity_flag);
}

std::string InstrumentCommand::module_filter_value() {
  if (module_filter_) {
    return module_filter_.Get();
  }
  return {};
}

} // namespace vstk
