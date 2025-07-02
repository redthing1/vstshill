#include "inspect_command.hpp"
#include "../host/minimal.hpp"
#include "../util/vst_discovery.hpp"
#include <iostream>
#include <redlog/redlog.hpp>

extern redlog::logger log_main;
extern void apply_verbosity();

namespace vstk {

InspectCommand::InspectCommand(args::Subparser& parser)
    : parser_(parser), plugin_path_(parser, "plugin_path",
                                    "path or name of vst3 plugin to inspect"),
      pause_flag_(parser, "pause", "pause after plugin load for debugging",
                  {"pause"}) {}

int InspectCommand::execute() {
  apply_verbosity();

  parser_.Parse();

  if (!plugin_path_) {
    log_main.err("plugin path or name required for inspect command");
    std::cerr << parser_;
    return 1;
  }

  auto resolved_path = vstk::util::resolve_plugin_path(args::get(plugin_path_));
  if (resolved_path.empty()) {
    return 1;
  }

  vstk::host::MinimalHost host(log_main);
  host.inspect_plugin(resolved_path, pause_flag_);

  return 0;
}

} // namespace vstk