// cross-platform vst3 host application

#include <iostream>
#include <memory>

#include "ext/args.hpp"
#include <redlog/redlog.hpp>

#include "commands/command.hpp"
#include "host/constants.hpp"

// global logger and verbosity handling
redlog::logger log_main = redlog::get_logger("vstshill");

args::Group arguments("arguments");
args::HelpFlag help_flag(arguments, "help", "help", {'h', "help"});
args::CounterFlag verbosity_flag(arguments, "verbosity", "verbosity level",
                                 {'v'});

void apply_verbosity() {
  int verbosity = args::get(verbosity_flag);
  redlog::set_level(redlog::level::info);
  if (verbosity == vstk::constants::VERBOSITY_LEVEL_VERBOSE) {
    redlog::set_level(redlog::level::verbose);
  } else if (verbosity == vstk::constants::VERBOSITY_LEVEL_TRACE) {
    redlog::set_level(redlog::level::trace);
  } else if (verbosity >= vstk::constants::VERBOSITY_LEVEL_DEBUG) {
    redlog::set_level(redlog::level::debug);
  }
}

int main(int argc, char* argv[]) {
  args::ArgumentParser parser("vstshill - cross-platform vst3 host",
                              "analyze, host, and process vst3 plugins");
  parser.helpParams.showTerminator = false;
  parser.SetArgumentSeparations(false, false, true, true);
  parser.LongSeparator(" ");

  args::GlobalOptions globals(parser, arguments);
  args::Group commands(parser, "commands");

  // command parsers
  args::Command inspect_cmd(commands, "inspect",
                            "inspect and analyze a vst3 plugin",
                            &vstk::cmd_inspect);
  args::Command gui_cmd(commands, "gui", "open plugin editor gui window",
                        &vstk::cmd_gui);
  args::Command process_cmd(commands, "process",
                            "process audio files through plugin",
                            &vstk::cmd_process);
  args::Command scan_cmd(commands, "scan",
                         "scan for vst3 plugins in standard directories",
                         &vstk::cmd_scan);
  args::Command parameters_cmd(commands, "parameters",
                               "analyze and list plugin parameters",
                               &vstk::cmd_parameters);

  try {
    parser.ParseCLI(argc, argv);
  } catch (args::Help) {
    std::cout << parser;
  } catch (args::Error& e) {
    std::cerr << e.what() << std::endl << parser;
    return 1;
  }
  return 0;
}