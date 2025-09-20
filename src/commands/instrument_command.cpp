#include "instrument_command.hpp"

#include "../host/constants.hpp"
#include "../instrumentation/tracer_host.hpp"
#include "../util/string_utils.hpp"
#include "../util/vst_discovery.hpp"

#include <iostream>
#include <redlog.hpp>
#include <type_traits>
#include <vector>

extern redlog::logger log_main;
extern void apply_verbosity();
extern args::CounterFlag verbosity_flag;

namespace vstk {

InstrumentCommand::InstrumentCommand(args::Subparser& parser)
    : parser_(parser), plugin_path_(parser, "plugin_path", "vst3 plugin path"),
      pause_flag_(parser, "pause", "pause after load", {"pause"}),

      tracer_type_(parser, "tracer", "tracer type (w1cov|w1xfer|w1script)",
                   {"tracer"}, args::Options::Required),

      coverage_out_(parser, "coverage-out", "coverage output file",
                    {"coverage-out"}),
      coverage_inst_(parser, "coverage-inst",
                     "enable instruction-level coverage tracing",
                     {"coverage-inst"}),

      transfers_out_(parser, "transfers-out", "transfers output file",
                     {"transfers-out"}),
      no_registers_(parser, "no-registers", "disable register logging",
                    {"no-registers"}),
      no_stack_info_(parser, "no-stack", "disable stack logging", {"no-stack"}),
      analyze_apis_(parser, "analyze-apis", "enable api analysis",
                    {"analyze-apis"}),

      script_path_(parser, "script", "lua script path", {"script"}),
      script_config_(parser, "script-config", "script configuration key=value",
                     {"script-config"}),

      module_filter_(
          parser, "module-filter",
          "filter modules to instrument (substring match, or '$' for "
          "target module only)",
          {'f', "module-filter"}),
      target_only_(parser, "target-only",
                   "restrict instrumentation to the plugin module only",
                   {"target-only"}) {}

int InstrumentCommand::execute() {
  apply_verbosity();
  parser_.Parse();

  if (!plugin_path_) {
    log_main.err("plugin path required");
    std::cerr << parser_;
    return 1;
  }

  auto resolved_path = vstk::util::resolve_plugin_path(args::get(plugin_path_));
  if (resolved_path.empty()) {
    return 1;
  }

  auto tracer = parse_tracer_kind();
  if (!tracer) {
    return 1;
  }

  if (!validate_options(*tracer)) {
    return 1;
  }

  auto filter_value = resolve_module_filter();
  if (!filter_value.has_value()) {
    return 1;
  }

  auto context = make_context(resolved_path, *filter_value);

  switch (*tracer) {
  case TracerKind::Coverage:
    return execute_coverage(context);
  case TracerKind::Transfer:
    return execute_transfer(context);
  case TracerKind::Script:
    return execute_script(context);
  }

  log_main.err("unsupported tracer selection");
  return 1;
}

int InstrumentCommand::verbosity_level() const {
  return args::get(verbosity_flag);
}

std::optional<std::string> InstrumentCommand::resolve_module_filter() {
  if (target_only_ && module_filter_) {
    log_main.err("--target-only cannot be combined with --module-filter");
    return std::nullopt;
  }

  if (target_only_) {
    return std::string{"$"};
  }

  if (module_filter_) {
    return vstk::util::trim(module_filter_.Get());
  }

  return std::string{};
}

InstrumentCommand::InvocationContext
InstrumentCommand::make_context(const std::string& plugin_path,
                                const std::string& module_filter) {
  InvocationContext ctx{plugin_path, static_cast<bool>(pause_flag_),
                        module_filter, verbosity_level()};
  return ctx;
}

std::optional<InstrumentCommand::TracerKind>
InstrumentCommand::parse_tracer_kind() {
  const std::string raw_type = args::get(tracer_type_);

  if (raw_type == kTracerCoverage) {
    return TracerKind::Coverage;
  }

  if (raw_type == kTracerTransfer) {
    return TracerKind::Transfer;
  }

#ifdef WITNESS_SCRIPT_ENABLED
  if (raw_type == kTracerScript) {
    return TracerKind::Script;
  }
#else
  if (raw_type == kTracerScript) {
    log_main.err("script tracer not available (lua support disabled)");
    return std::nullopt;
  }
#endif

  log_main.err("unknown tracer type", redlog::field("type", raw_type));
  return std::nullopt;
}

bool InstrumentCommand::validate_options(TracerKind kind) {
  std::vector<std::string> invalid;

  auto track_invalid = [&](bool used, const char* flag, TracerKind allowed) {
    if (used && kind != allowed) {
      invalid.emplace_back(flag);
    }
  };

  track_invalid(static_cast<bool>(coverage_out_), "--coverage-out",
                TracerKind::Coverage);
  track_invalid(static_cast<bool>(coverage_inst_), "--coverage-inst",
                TracerKind::Coverage);

  track_invalid(static_cast<bool>(transfers_out_), "--transfers-out",
                TracerKind::Transfer);
  track_invalid(static_cast<bool>(no_registers_), "--no-registers",
                TracerKind::Transfer);
  track_invalid(static_cast<bool>(no_stack_info_), "--no-stack",
                TracerKind::Transfer);
  track_invalid(static_cast<bool>(analyze_apis_), "--analyze-apis",
                TracerKind::Transfer);

  track_invalid(static_cast<bool>(script_path_), "--script",
                TracerKind::Script);
  track_invalid(static_cast<bool>(script_config_), "--script-config",
                TracerKind::Script);

  if (!invalid.empty()) {
    log_main.err("options not valid for selected tracer",
                 redlog::field("tracer", std::string(tracer_name(kind))),
                 redlog::field("flags", util::join_strings(invalid)));
    return false;
  }

#ifdef WITNESS_SCRIPT_ENABLED
  if (kind == TracerKind::Script && !script_path_) {
    log_main.err("--script required when tracer is w1script");
    return false;
  }
#endif

  return true;
}

template <typename Config>
void InstrumentCommand::apply_common_config(
    Config& config, const InvocationContext& ctx) const {
  config.verbose_instrumentation =
      ctx.verbosity >= constants::VERBOSITY_LEVEL_DEBUG;

#ifdef WITNESS_SCRIPT_ENABLED
  if constexpr (std::is_same_v<Config, w1::tracers::script::config>) {
    config.verbose = ctx.verbosity >= constants::VERBOSITY_LEVEL_TRACE;
  } else
#endif
  {
    config.verbose = ctx.verbosity;
  }
}

template <typename Session, typename Config>
int InstrumentCommand::run_tracer(const InvocationContext& ctx,
                                  Config&& config) {
  instrumentation::TracerHost host(log_main);
  host.inspect<Session>(ctx.plugin_path, config, ctx.pause_after_load,
                        ctx.module_filter);
  return 0;
}

int InstrumentCommand::execute_coverage(const InvocationContext& ctx) {
  w1cov::coverage_config config;
  if (coverage_out_) {
    config.output_file = args::get(coverage_out_);
  }
  config.inst_trace = static_cast<bool>(coverage_inst_);
  apply_common_config(config, ctx);
  return run_tracer<w1cov::session>(ctx, std::move(config));
}

int InstrumentCommand::execute_transfer(const InvocationContext& ctx) {
  w1xfer::transfer_config config;
  if (transfers_out_) {
    config.output_file = args::get(transfers_out_);
  }
  config.log_registers = !static_cast<bool>(no_registers_);
  config.log_stack_info = !static_cast<bool>(no_stack_info_);
  config.analyze_apis = static_cast<bool>(analyze_apis_);
  apply_common_config(config, ctx);
  return run_tracer<w1xfer::session>(ctx, std::move(config));
}

int InstrumentCommand::execute_script(const InvocationContext& ctx) {
#ifdef WITNESS_SCRIPT_ENABLED
  w1::tracers::script::config config;
  config.script_path = args::get(script_path_);
  apply_common_config(config, ctx);

  if (script_config_) {
    for (const auto& entry : args::get(script_config_)) {
      auto pos = entry.find('=');
      if (pos == std::string::npos) {
        log_main.warn("invalid script config entry (expected key=value)",
                      redlog::field("entry", entry));
        continue;
      }

      auto key = util::trim(entry.substr(0, pos));
      auto value = util::trim(entry.substr(pos + 1));

      if (key.empty()) {
        log_main.warn("ignoring empty script config key",
                      redlog::field("entry", entry));
        continue;
      }

      config.script_config[key] = value;
    }
  }

  return run_tracer<w1::tracers::script::session>(ctx, std::move(config));
#else
  (void)ctx;
  log_main.err("script tracer not available (lua support disabled)");
  return 1;
#endif
}

} // namespace vstk
