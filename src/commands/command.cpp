#include "command.hpp"
#include "gui_command.hpp"
#include "inspect_command.hpp"
#include "parameters_command.hpp"
#include "process_command.hpp"
#include "scan_command.hpp"
#include <cstdlib>

namespace vstk {

void cmd_scan(args::Subparser& parser) {
  ScanCommand command(parser);
  int result = command.execute();
  if (result != 0) {
    std::exit(result);
  }
}

void cmd_inspect(args::Subparser& parser) {
  InspectCommand command(parser);
  int result = command.execute();
  if (result != 0) {
    std::exit(result);
  }
}

void cmd_parameters(args::Subparser& parser) {
  ParametersCommand command(parser);
  int result = command.execute();
  if (result != 0) {
    std::exit(result);
  }
}

void cmd_gui(args::Subparser& parser) {
  GuiCommand command(parser);
  int result = command.execute();
  if (result != 0) {
    std::exit(result);
  }
}

void cmd_process(args::Subparser& parser) {
  ProcessCommand command(parser);
  int result = command.execute();
  if (result != 0) {
    std::exit(result);
  }
}

} // namespace vstk