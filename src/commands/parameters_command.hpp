#pragma once

#include "../ext/args.hpp"
#include "command.hpp"

namespace vstk {

class ParametersCommand : public Command {
public:
  explicit ParametersCommand(args::Subparser& parser);
  ~ParametersCommand() = default;

  int execute() override;
  const char* name() const override { return "parameters"; }
  const char* description() const override { return "list plugin parameters"; }

private:
  args::Subparser& parser_;
  args::Positional<std::string> plugin_path_;
};

} // namespace vstk