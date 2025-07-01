// cross-platform vst3 host application

#include <iostream>
#include <string>
#include <vector>

#include <redlog/redlog.hpp>

#include "ext/args.hpp"
#include "host/minimal.hpp"
#include "host/vstk.hpp"
#include "util/vst_discovery.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
auto log_main = redlog::get_logger("vstshill");
}

void open_plugin_gui(const std::string& plugin_path) {
  auto log = log_main.with_name("gui");
  log.inf("opening plugin editor", redlog::field("path", plugin_path));

  vstk::Plugin plugin(log);
  auto load_result = plugin.load(plugin_path);
  if (!load_result) {
    log.error("failed to load plugin",
              redlog::field("error", load_result.error()));
    return;
  }

  log.inf("plugin loaded successfully", redlog::field("name", plugin.name()));

  if (!plugin.has_editor()) {
    log.warn("plugin does not have an editor interface (headless plugin)");
    return;
  }

  auto window_result = plugin.create_editor_window();
  if (!window_result) {
    log.error("failed to create editor window",
              redlog::field("error", window_result.error()));
    return;
  }

  auto window = std::move(window_result.value());
  log.inf("editor window opened successfully");

  log.inf("entering gui event loop (close window to exit)");
  while (window->is_open()) {
    vstk::GuiWindow::process_events();
#ifdef _WIN32
    Sleep(16);
#else
    usleep(16000);
#endif
  }

  log.inf("gui session ended");
}

void process_audio_file(const std::string& input_file,
                        const std::string& output_file,
                        const std::string& plugin_path) {
  auto log = log_main.with_name("processor");
  log.inf("processing audio file", redlog::field("input", input_file),
          redlog::field("output", output_file),
          redlog::field("plugin", plugin_path));

  vstk::Plugin plugin(log);
  vstk::PluginConfig config;
  config.with_process_mode(vstk::ProcessMode::Offline)
      .with_sample_rate(44100)
      .with_block_size(512);

  auto load_result = plugin.load(plugin_path, config);
  if (!load_result) {
    log.error("failed to load plugin",
              redlog::field("error", load_result.error()));
    return;
  }

  log.inf("plugin loaded for audio processing",
          redlog::field("name", plugin.name()));
  log.warn(
      "audio file processing not yet implemented - placeholder functionality");
  log.inf("would process:", redlog::field("input_file", input_file),
          redlog::field("output_file", output_file),
          redlog::field("plugin_name", plugin.name()));
}

void cmd_inspect(args::Subparser& parser) {
  args::Positional<std::string> plugin_path(parser, "plugin_path",
                                            "path to vst3 plugin to inspect");
  parser.Parse();

  if (!plugin_path) {
    log_main.error("plugin path required for inspect command");
    std::cerr << parser;
    return;
  }

  vstk::host::MinimalHost host(log_main);
  host.inspect_plugin(args::get(plugin_path));
}

void cmd_gui(args::Subparser& parser) {
  args::Positional<std::string> plugin_path(
      parser, "plugin_path", "path to vst3 plugin to open in gui");
  parser.Parse();

  if (!plugin_path) {
    log_main.error("plugin path required for gui command");
    std::cerr << parser;
    return;
  }

  open_plugin_gui(args::get(plugin_path));
}

void cmd_process(args::Subparser& parser) {
  args::ValueFlag<std::string> input_file(parser, "input", "input audio file",
                                          {'i', "input"});
  args::ValueFlag<std::string> output_file(
      parser, "output", "output audio file", {'o', "output"});
  args::Positional<std::string> plugin_path(
      parser, "plugin_path", "path to vst3 plugin to use for processing");
  parser.Parse();

  if (!plugin_path || !input_file || !output_file) {
    log_main.error("plugin path, input file, and output file required for "
                   "process command");
    std::cerr << parser;
    return;
  }

  process_audio_file(args::get(input_file), args::get(output_file),
                     args::get(plugin_path));
}

void cmd_scan(args::Subparser& parser) {
  args::ValueFlagList<std::string> search_paths(
      parser, "paths", "additional search paths", {'p', "path"});
  args::Flag detailed(parser, "detailed", "show detailed plugin information",
                      {'d', "detailed"});
  parser.Parse();

  std::vector<std::string> paths;
  if (search_paths) {
    paths = args::get(search_paths);
  }

  if (detailed) {
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
}

int main(int argc, char* argv[]) {
  redlog::set_level(redlog::level::info);

  args::Group arguments("arguments");
  args::HelpFlag help_flag(arguments, "help", "help", {'h', "help"});
  args::CounterFlag verbosity_flag(arguments, "verbosity", "verbosity level",
                                   {'v'});

  args::ArgumentParser parser("vstshill - cross-platform vst3 host",
                              "analyze, host, and process vst3 plugins");
  parser.helpParams.showTerminator = false;
  parser.SetArgumentSeparations(false, false, true, true);
  parser.LongSeparator(" ");

  args::GlobalOptions globals(parser, arguments);
  args::Group commands(parser, "commands");

  args::Command inspect_cmd(commands, "inspect",
                            "inspect and analyze a vst3 plugin", &cmd_inspect);
  args::Command gui_cmd(commands, "gui", "open plugin editor gui window",
                        &cmd_gui);
  args::Command process_cmd(commands, "process",
                            "process audio files through plugin", &cmd_process);
  args::Command scan_cmd(commands, "scan",
                         "scan for vst3 plugins in standard directories",
                         &cmd_scan);

  try {
    parser.ParseCLI(argc, argv);
  } catch (args::Help) {
    std::cout << parser;
    return 0;
  } catch (args::ParseError& e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  // apply verbosity
  int verbosity = args::get(verbosity_flag);
  redlog::set_level(redlog::level::info);
  if (verbosity == 1) {
    redlog::set_level(redlog::level::verbose);
  } else if (verbosity == 2) {
    redlog::set_level(redlog::level::trace);
  } else if (verbosity >= 3) {
    redlog::set_level(redlog::level::debug);
  }

  return 0;
}