#pragma once

#include "../ext/args.hpp"
#include "command.hpp"

namespace vstk {

class InspectCommand : public Command {
public:
  explicit InspectCommand(args::Subparser& parser);
  ~InspectCommand() = default;

  int execute() override;
  const char* name() const override { return "inspect"; }
  const char* description() const override {
    return "inspect vst3 plugin capabilities";
  }

private:
  args::Subparser& parser_;
  args::Positional<std::string> plugin_path_;
};

} // namespace vstk