#pragma once

#include <redlog.hpp>
#include <string>
#include <memory>
#include <tracers/w1cov/session.hpp>

namespace vstk {
namespace instrumentation {

// host specifically designed for instrumentation workflows
// loads modules outside of qbdi, then instruments the vst operations
class InstrumentableHost {
public:
  explicit InstrumentableHost(
      const redlog::logger& logger = redlog::get_logger("vstk::instrument"));
  ~InstrumentableHost() = default;

  // non-copyable, non-movable
  InstrumentableHost(const InstrumentableHost&) = delete;
  InstrumentableHost& operator=(const InstrumentableHost&) = delete;
  InstrumentableHost(InstrumentableHost&&) = delete;
  InstrumentableHost& operator=(InstrumentableHost&&) = delete;

  // inspect plugin with coverage instrumentation
  void inspect_with_coverage(const std::string& plugin_path,
                             bool pause_after_load = false,
                             const std::string& export_path = "");

  // future: other instrumentation types
  // void inspect_with_script(const std::string& plugin_path, const std::string&
  // script_path); void inspect_with_trace(const std::string& plugin_path);

public:
  redlog::logger _log;
};

} // namespace instrumentation
} // namespace vstk