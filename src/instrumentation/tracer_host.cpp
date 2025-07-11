#include "tracer_host.hpp"
#include "vst_operations.hpp"
#include "../host/module_loader.hpp"
#include "../util/string_utils.hpp"
#include <QBDI/Memory.hpp>

namespace vstk::instrumentation {

// critical modules we should keep instrumented for stability
static bool is_critical_module(const std::string& module_name) {
  std::string vstshill_name = "vstshill";
#ifdef __APPLE__
  static const std::vector<std::string> critical_modules = {"libdyld",
                                                            vstshill_name};
#elif defined(__linux__)
  static const std::vector<std::string> critical_modules = {
      vstshill_name,
  };
#elif defined(_WIN32)
  static const std::vector<std::string> critical_modules = {
      vstshill_name,
  };
#else
  static const std::vector<std::string> critical_modules = {vstshill_name};
#endif

  for (const auto& critical : critical_modules) {
    if (module_name.find(critical) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// apply module filtering based on filter pattern
template <typename TSession>
static void
apply_module_filtering(TSession& session, const std::string& filter_pattern,
                       void* target_function_addr, redlog::logger& log) {
  if (filter_pattern.empty()) {
    log.dbg("no module filter specified, instrumenting all modules");
    return;
  }

  log.inf("applying module filter", redlog::field("pattern", filter_pattern));

  // get all current process memory maps
  auto memory_maps = QBDI::getCurrentProcessMaps(true);

  // remove all instrumented ranges first
  session.get_vm()->removeAllInstrumentedRanges();

  size_t total_modules = 0;
  size_t instrumented_modules = 0;

  // special handling for '$' pattern - instrument only the module containing
  // the target function
  bool main_module_only = (filter_pattern == "$");
  std::string target_module_name;

  if (main_module_only) {
    // find the module containing the target function address
    QBDI::rword addr = reinterpret_cast<QBDI::rword>(target_function_addr);
    for (const auto& map : memory_maps) {
      if (map.range.contains(addr)) {
        target_module_name = map.name;
        log.dbg("target function found in module",
                redlog::field("module", target_module_name),
                redlog::field("function_addr", "0x%lx", addr));
        break;
      }
    }
    if (target_module_name.empty()) {
      log.warn("could not find module containing target function",
               redlog::field("function_addr", "0x%lx", addr));
      return;
    }
  }

  // re-add only modules that match the filter or are critical
  for (const auto& map : memory_maps) {
    if (map.name.empty()) {
      continue;
    }

    total_modules++;

    bool matches_filter = false;
    bool is_critical = is_critical_module(map.name);

    if (main_module_only) {
      // for '$' pattern, match only the module containing the target function
      matches_filter = (map.name == target_module_name);
    } else {
      // normal substring matching
      matches_filter = map.name.find(filter_pattern) != std::string::npos;
    }

    if (matches_filter || is_critical) {
      if (session.get_vm()->addInstrumentedModuleFromAddr(map.range.start())) {
        instrumented_modules++;
        log.dbg("instrumented module", redlog::field("name", map.name),
                redlog::field("range_start", "0x%lx", map.range.start()),
                redlog::field("range_end", "0x%lx", map.range.end()),
                redlog::field("reason", matches_filter ? (main_module_only
                                                              ? "target_module"
                                                              : "filter_match")
                                                       : "critical"));
      } else {
        log.warn("failed to instrument module",
                 redlog::field("name", map.name));
      }
    }
  }

  log.inf("module filtering complete",
          redlog::field("total_modules", total_modules),
          redlog::field("instrumented_modules", instrumented_modules),
          redlog::field("filter_pattern", filter_pattern));
}

template <typename TSession>
void TracerHost::execute_inspection(
    const std::string& plugin_path,
    const typename TSession::config_type& config, bool pause_after_load,
    const std::string& module_filter) {
  // step 1: load library outside instrumentation
  _log.dbg("loading plugin library");
  std::string error_desc;
  void* library_handle =
      host::VstModule::loadLibraryOnly(plugin_path, error_desc);
  if (!library_handle) {
    _log.err("failed to load library", redlog::field("error", error_desc));
    return;
  }

  // optional pause point
  if (pause_after_load) {
    _log.inf("pausing after library load");
    wait_for_input("press enter to continue...");
  }

  // step 2: initialize tracer session after VST is loaded
  _log.dbg("initializing tracer session");
  TSession session(config);

  if (!session.initialize()) {
    _log.err("failed to initialize tracer session");
    host::VstModule::unloadLibrary(library_handle);
    return;
  }

  // step 3: add module to instrumentation
  void* func_ptr =
      host::VstModule::getFunctionPointer(library_handle, "GetPluginFactory");
  if (!func_ptr) {
    _log.err("failed to get function pointer");
    host::VstModule::unloadLibrary(library_handle);
    return;
  }

  if (!session.add_instrumented_module_from_addr(func_ptr)) {
    _log.err("failed to add module to instrumentation");
    host::VstModule::unloadLibrary(library_handle);
    return;
  }

  // apply module filtering if specified
  apply_module_filtering(session, module_filter, func_ptr, _log);

  // step 4: initialize vst under instrumentation
  uint64_t module_ptr;
  if (!session.trace_function(reinterpret_cast<void*>(&vst_init_module),
                              {reinterpret_cast<uint64_t>(library_handle),
                               reinterpret_cast<uint64_t>(&plugin_path)},
                              &module_ptr) ||
      !module_ptr) {
    _log.err("failed to initialize vst module");
    host::VstModule::unloadLibrary(library_handle);
    return;
  }

  // step 5: inspect vst under instrumentation
  std::unique_ptr<host::VstModule> module(
      reinterpret_cast<host::VstModule*>(module_ptr));
  VstContext ctx{this, module.get(), &plugin_path};

  uint64_t result;
  if (!session.trace_function(reinterpret_cast<void*>(&vst_inspect_plugin),
                              {reinterpret_cast<uint64_t>(&ctx)}, &result)) {
    _log.err("failed to inspect vst plugin");
    return;
  }

  // handle tracer-specific finalization
  finalize(session, config);
}

// finalization for coverage tracer
void TracerHost::finalize(w1cov::session& session,
                          const w1cov::coverage_config& config) {
  session.print_statistics();

  if (!config.output_file.empty()) {
    if (session.export_coverage(config.output_file)) {
      _log.inf("exported coverage", redlog::field("path", config.output_file));
    } else {
      _log.err("failed to export coverage");
    }
  }
}

// finalization for transfer tracer
void TracerHost::finalize(w1xfer::session& session,
                          const w1xfer::transfer_config& config) {
  const auto& stats = session.get_stats();
  _log.inf("transfer statistics", redlog::field("calls", stats.total_calls),
           redlog::field("returns", stats.total_returns),
           redlog::field("max_depth", stats.max_call_depth));
}

#ifdef WITNESS_SCRIPT_ENABLED
// finalization for script tracer
void TracerHost::finalize(w1::tracers::script::session& session,
                          const w1::tracers::script::config& config) {
  _log.inf("script execution completed");
}
#endif

// explicit instantiations
template void
TracerHost::execute_inspection<w1cov::session>(const std::string&,
                                               const w1cov::coverage_config&,
                                               bool, const std::string&);
template void
TracerHost::execute_inspection<w1xfer::session>(const std::string&,
                                                const w1xfer::transfer_config&,
                                                bool, const std::string&);
#ifdef WITNESS_SCRIPT_ENABLED
template void TracerHost::execute_inspection<w1::tracers::script::session>(
    const std::string&, const w1::tracers::script::config&, bool,
    const std::string&);
#endif

template void TracerHost::inspect<w1cov::session>(const std::string&,
                                                  const w1cov::coverage_config&,
                                                  bool, const std::string&);
template void
TracerHost::inspect<w1xfer::session>(const std::string&,
                                     const w1xfer::transfer_config&, bool,
                                     const std::string&);
#ifdef WITNESS_SCRIPT_ENABLED
template void TracerHost::inspect<w1::tracers::script::session>(
    const std::string&, const w1::tracers::script::config&, bool,
    const std::string&);
#endif

} // namespace vstk::instrumentation