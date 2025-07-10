#include "instrument_command.hpp"
#include "../util/vst_discovery.hpp"
#include "../instrumentation/instrumentable_host.hpp"
#include <iostream>
#include <redlog.hpp>

extern redlog::logger log_main;
extern void apply_verbosity();

namespace vstk {

InstrumentCommand::InstrumentCommand(args::Subparser& parser)
    : parser_(parser),
      plugin_path_(parser, "plugin_path",
                   "path or name of vst3 plugin to instrument"),
      pause_flag_(parser, "pause", "pause after plugin load for debugging",
                  {"pause"}),
      coverage_flag_(parser, "coverage", "enable coverage tracing",
                     {"coverage"}),
      export_path_(parser, "export", "export coverage data to file",
                   {"export"}) {}

int InstrumentCommand::execute() {
  apply_verbosity();
  parser_.Parse();

  if (!plugin_path_) {
    log_main.err("plugin path or name required for instrument command");
    std::cerr << parser_;
    return 1;
  }

  auto resolved_path = vstk::util::resolve_plugin_path(args::get(plugin_path_));
  if (resolved_path.empty()) {
    return 1;
  }

  if (coverage_flag_) {
    return execute_with_coverage(resolved_path);
  }

  // future: other instrumentation types
  log_main.err("must specify instrumentation type (e.g. --coverage)");
  return 1;
}

int InstrumentCommand::execute_with_coverage(const std::string& plugin_path) {
  // use instrumentable host for proper coverage workflow
  instrumentation::InstrumentableHost host(log_main);

  std::string export_file;
  if (export_path_) {
    export_file = args::get(export_path_);
  }

  host.inspect_with_coverage(plugin_path, pause_flag_, export_file);

  return 0;
}

} // namespace vstk