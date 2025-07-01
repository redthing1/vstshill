#include "command.hpp"
#include "gui_command.hpp"
#include "inspect_command.hpp"
#include "parameters_command.hpp"
#include "process_command.hpp"
#include "scan_command.hpp"

namespace vstk {

void cmd_scan(args::Subparser& parser) {
  ScanCommand command(parser);
  command.execute();
}

void cmd_inspect(args::Subparser& parser) {
  InspectCommand command(parser);
  command.execute();
}

void cmd_parameters(args::Subparser& parser) {
  ParametersCommand command(parser);
  command.execute();
}

void cmd_gui(args::Subparser& parser) {
  GuiCommand command(parser);
  command.execute();
}

void cmd_process(args::Subparser& parser) {
  ProcessCommand command(parser);
  command.execute();
}

} // namespace vstk