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

// restrict instrumentation to the module that owns the target function
template <typename TSession>
static void restrict_to_target_module(TSession& session,
                                      void* target_function_addr,
                                      redlog::logger& log) {
  if (!target_function_addr) {
    log.warn("cannot restrict instrumentation - null function address");
    return;
  }

  auto memory_maps = QBDI::getCurrentProcessMaps(true);
  if (memory_maps.empty()) {
    log.warn("no process memory maps available for filtering");
    return;
  }

  QBDI::rword target_addr = reinterpret_cast<QBDI::rword>(target_function_addr);
  std::string target_module_name;

  for (const auto& map : memory_maps) {
    if (map.range.contains(target_addr)) {
      target_module_name = map.name;
      log.dbg("target function located",
              redlog::field("module", target_module_name),
              redlog::field("function_addr", "0x%lx", target_addr));
      break;
    }
  }

  if (target_module_name.empty()) {
    log.warn("unable to determine target module for instrumentation",
             redlog::field("function_addr", "0x%lx", target_addr));
    return;
  }

  session.get_vm()->removeAllInstrumentedRanges();

  size_t instrumented_modules = 0;

  for (const auto& map : memory_maps) {
    if (map.name.empty()) {
      continue;
    }

    const bool is_target = map.name == target_module_name;
    const bool keep = is_target || is_critical_module(map.name);
    if (!keep) {
      continue;
    }

    if (session.get_vm()->addInstrumentedModuleFromAddr(map.range.start())) {
      instrumented_modules++;
      log.dbg("instrumented module", redlog::field("name", map.name),
              redlog::field("range_start", "0x%lx", map.range.start()),
              redlog::field("range_end", "0x%lx", map.range.end()),
              redlog::field("reason", is_target ? "target_module" : "critical"));
    } else {
      log.warn("failed to instrument module", redlog::field("name", map.name));
    }
  }

  log.inf("applied target-only instrumentation",
          redlog::field("target_module", target_module_name),
          redlog::field("instrumented_modules", instrumented_modules));
}

template <typename TSession>
void TracerHost::execute_inspection(
    const std::string& plugin_path,
    const typename TSession::config_type& config, bool pause_after_load,
    const std::string& module_filter) {
  auto session_config = config;

  auto trimmed_filter = vstk::util::trim(module_filter);
  bool target_module_only = trimmed_filter == "$";

  if (!trimmed_filter.empty() && !target_module_only) {
    session_config.module_filter.clear();
    session_config.module_filter.push_back(trimmed_filter);
    _log.inf("configured module filter",
             redlog::field("pattern", trimmed_filter));
  }

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
    vstk::util::wait_for_input("press enter to continue...");
  }

  // step 2: initialize tracer session after VST is loaded
  _log.dbg("initializing tracer session");
  TSession session(session_config);

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

  if (target_module_only) {
    restrict_to_target_module(session, func_ptr, _log);
  }

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

  if (result != 0) {
    _log.err("inspection reported failure",
             redlog::field("result", result));
    return;
  }

  // handle tracer-specific finalization
  finalize(session, session_config);
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
