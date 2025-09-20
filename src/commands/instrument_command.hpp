#pragma once

#include "../ext/args.hpp"
#include "command.hpp"

#include <optional>
#include <string>
#include <string_view>

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
  enum class TracerKind { Coverage, Transfer, Script };

  static constexpr std::string_view kTracerCoverage = "w1cov";
  static constexpr std::string_view kTracerTransfer = "w1xfer";
  static constexpr std::string_view kTracerScript = "w1script";

  struct InvocationContext {
    std::string plugin_path;
    bool pause_after_load;
    std::string module_filter;
    int verbosity;
  };

  args::Subparser& parser_;
  args::Positional<std::string> plugin_path_;
  args::Flag pause_flag_;

  // tracer selection
  args::ValueFlag<std::string> tracer_type_;

  // coverage options
  args::ValueFlag<std::string> coverage_out_;
  args::Flag coverage_inst_;

  // transfer options
  args::ValueFlag<std::string> transfers_out_;
  args::Flag no_registers_;
  args::Flag no_stack_info_;
  args::Flag analyze_apis_;

  // script options
  args::ValueFlag<std::string> script_path_;
  args::ValueFlagList<std::string> script_config_;

  // module filtering
  args::ValueFlag<std::string> module_filter_;
  args::Flag target_only_;

  int verbosity_level() const;
  std::optional<std::string> resolve_module_filter();
  InvocationContext make_context(const std::string& plugin_path,
                                 const std::string& module_filter);
  std::optional<TracerKind> parse_tracer_kind();
  bool validate_options(TracerKind kind);
  static constexpr std::string_view tracer_name(TracerKind kind) {
    switch (kind) {
    case TracerKind::Coverage:
      return kTracerCoverage;
    case TracerKind::Transfer:
      return kTracerTransfer;
    case TracerKind::Script:
      return kTracerScript;
    }
    return "unknown";
  }

  template <typename Config>
  void apply_common_config(Config& config, const InvocationContext& ctx) const;

  template <typename Session, typename Config>
  int run_tracer(const InvocationContext& ctx, Config&& config);

  // execution methods
  int execute_coverage(const InvocationContext& ctx);
  int execute_transfer(const InvocationContext& ctx);
  int execute_script(const InvocationContext& ctx);
};

} // namespace vstk
