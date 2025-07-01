#pragma once

#include "../ext/args.hpp"
#include "../host/vstk.hpp"
#include <memory>

namespace vstk {

// abstract base class for all commands
class Command {
public:
  virtual ~Command() = default;

  // execute the command with parsed arguments
  virtual int execute() = 0;

  // get command name for help/error messages
  virtual const char* name() const = 0;

  // get command description
  virtual const char* description() const = 0;

protected:
  Command() = default;
};

// command execution functions for args library
void cmd_scan(args::Subparser& parser);
void cmd_inspect(args::Subparser& parser);
void cmd_parameters(args::Subparser& parser);
void cmd_gui(args::Subparser& parser);
void cmd_process(args::Subparser& parser);

} // namespace vstk