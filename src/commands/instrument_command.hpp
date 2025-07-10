#pragma once

#include "../ext/args.hpp"
#include "command.hpp"

namespace vstk {

class InstrumentCommand : public Command {
public:
  explicit InstrumentCommand(args::Subparser& parser);
  ~InstrumentCommand() = default;

  int execute() override;
  const char* name() const override { return "instrument"; }
  const char* description() const override {
    return "instrument vst3 plugin with dynamic analysis tools";
  }

private:
  args::Subparser& parser_;
  args::Positional<std::string> plugin_path_;
  args::Flag pause_flag_;

  // instrumentation options
  args::Flag coverage_flag_;
  args::ValueFlag<std::string> export_path_;

  // future: script, trace, etc.

  // implementation methods
  int execute_with_coverage(const std::string& plugin_path);
};

} // namespace vstk