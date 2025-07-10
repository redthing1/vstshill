#include "command.hpp"
#include "gui_command.hpp"
#include "inspect_command.hpp"
#include "parameters_command.hpp"
#include "process_command.hpp"
#include "scan_command.hpp"
#ifdef VSTSHILL_WITNESS
#include "instrument_command.hpp"
#endif

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

#ifdef VSTSHILL_WITNESS
void cmd_instrument(args::Subparser& parser) {
  InstrumentCommand command(parser);
  command.execute();
}
#else
void cmd_instrument(args::Subparser& parser) {
  // stub for when witness is not enabled
  (void)parser;
}
#endif

} // namespace vstk