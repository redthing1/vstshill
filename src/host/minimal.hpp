#pragma once

#include <redlog.hpp>
#include <string>

namespace vstk {
namespace host {

// minimal vst3 host for cli inspection - no gui functionality
class MinimalHost {
public:
  explicit MinimalHost(
      const redlog::logger& logger = redlog::get_logger("vstk::minimal"));
  ~MinimalHost() = default;

  // non-copyable, non-movable
  MinimalHost(const MinimalHost&) = delete;
  MinimalHost& operator=(const MinimalHost&) = delete;
  MinimalHost(MinimalHost&&) = delete;
  MinimalHost& operator=(MinimalHost&&) = delete;

  // loads and inspects a vst3 plugin, displaying detailed information
  void inspect_plugin(const std::string& plugin_path,
                      bool pause_after_load = false);

private:
  redlog::logger _log;
};

} // namespace host
} // namespace vstk