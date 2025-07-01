#pragma once

#include "../ext/args.hpp"
#include "command.hpp"

namespace vstk {

class ScanCommand : public Command {
public:
  explicit ScanCommand(args::Subparser& parser);
  ~ScanCommand() = default;

  int execute() override;
  const char* name() const override { return "scan"; }
  const char* description() const override {
    return "scan for available vst3 plugins";
  }

private:
  args::Subparser& parser_;
  args::ValueFlagList<std::string> search_paths_;
  args::Flag detailed_;
};

} // namespace vstk