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

  // tracer selection
  args::ValueFlag<std::string> tracer_type_;

  // coverage options
  args::ValueFlag<std::string> coverage_out_;

  // transfer options
  args::ValueFlag<std::string> transfers_out_;
  args::Flag no_registers_;
  args::Flag no_stack_info_;
  args::Flag analyze_apis_;

  // script options
  args::ValueFlag<std::string> script_path_;
  args::ValueFlagList<std::string> script_config_;

  // execution methods
  int execute_coverage(const std::string& plugin_path);
  int execute_transfer(const std::string& plugin_path);
  int execute_script(const std::string& plugin_path);
};

} // namespace vstk