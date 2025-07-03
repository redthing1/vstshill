#include "scan_command.hpp"
#include "../util/vst_discovery.hpp"
#include <redlog.hpp>

extern redlog::logger log_main;
extern void apply_verbosity();

namespace vstk {

ScanCommand::ScanCommand(args::Subparser& parser)
    : parser_(parser),
      search_paths_(parser, "paths", "additional search paths", {'p', "path"}),
      detailed_(parser, "detailed", "show detailed plugin information",
                {'d', "detailed"}) {}

int ScanCommand::execute() {
  apply_verbosity();

  parser_.Parse();

  std::vector<std::string> paths;
  if (search_paths_) {
    paths = args::get(search_paths_);
  }

  if (detailed_) {
    auto plugins = vstk::util::discover_vst3_plugins(paths);
    log_main.inf("discovered plugins", redlog::field("count", plugins.size()));

    for (const auto& plugin : plugins) {
      log_main.inf("plugin found", redlog::field("name", plugin.name),
                   redlog::field("path", plugin.path),
                   redlog::field("valid", plugin.is_valid_bundle),
                   redlog::field("size_bytes", plugin.file_size));
    }
  } else {
    auto plugin_paths = vstk::util::find_vst3_plugins(paths);
    log_main.inf("found plugins", redlog::field("count", plugin_paths.size()));

    for (const auto& path : plugin_paths) {
      log_main.inf("plugin", redlog::field("path", path));
    }
  }

  return 0;
}

} // namespace vstk