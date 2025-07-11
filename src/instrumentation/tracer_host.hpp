#pragma once
#include <redlog.hpp>
#include <memory>
#include <variant>
#include <tracers/w1cov/session.hpp>
#include <tracers/w1xfer/session.hpp>
#ifdef WITNESS_SCRIPT_ENABLED
#include <tracers/w1script/session.hpp>
#endif

namespace vstk::instrumentation {

// unified tracer host that handles all tracer types
class TracerHost {
public:
  explicit TracerHost(const redlog::logger& logger) : _log(logger) {}

  // single entry point for all tracers
  template <typename TSession>
  void inspect(const std::string& plugin_path,
               const typename TSession::config_type& config,
               bool pause_after_load = false,
               const std::string& module_filter = "") {

    _log.inf("starting instrumented inspection",
             redlog::field("plugin", plugin_path),
             redlog::field("tracer", tracer_name<TSession>()));

    // execute instrumented inspection (session will be initialized inside)
    execute_inspection<TSession>(plugin_path, config, pause_after_load,
                                 module_filter);
  }

private:
  redlog::logger _log;

  // inspection logic with proper tracer initialization timing
  template <typename TSession>
  void execute_inspection(const std::string& plugin_path,
                          const typename TSession::config_type& config,
                          bool pause_after_load,
                          const std::string& module_filter = "");

  // tracer-specific finalization via overloading
  void finalize(w1cov::session& session, const w1cov::coverage_config& config);
  void finalize(w1xfer::session& session,
                const w1xfer::transfer_config& config);
#ifdef WITNESS_SCRIPT_ENABLED
  void finalize(w1::tracers::script::session& session,
                const w1::tracers::script::config& config);
#endif

  // helper to get tracer name
  template <typename TSession> static constexpr const char* tracer_name() {
    if constexpr (std::is_same_v<TSession, w1cov::session>) {
      return "w1cov";
    } else if constexpr (std::is_same_v<TSession, w1xfer::session>) {
      return "w1xfer";
    }
#ifdef WITNESS_SCRIPT_ENABLED
    else if constexpr (std::is_same_v<TSession, w1::tracers::script::session>) {
      return "w1script";
    }
#endif
    else {
      return "unknown";
    }
  }
};

} // namespace vstk::instrumentation