#include "tracer_host.hpp"
#include "vst_operations.hpp"
#include "../host/module_loader.hpp"
#include "../util/string_utils.hpp"

namespace vstk::instrumentation {

template <typename TSession>
void TracerHost::execute_inspection(TSession& session,
                                    const std::string& plugin_path,
                                    bool pause_after_load) {
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

  // step 2: add module to instrumentation
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

  // step 3: initialize vst under instrumentation
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

  // step 4: inspect vst under instrumentation
  std::unique_ptr<host::VstModule> module(
      reinterpret_cast<host::VstModule*>(module_ptr));
  VstContext ctx{this, module.get(), &plugin_path};

  uint64_t result;
  if (!session.trace_function(reinterpret_cast<void*>(&vst_inspect_plugin),
                              {reinterpret_cast<uint64_t>(&ctx)}, &result) ||
      result != 0) {
    _log.err("vst inspection failed");
  }
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
template void TracerHost::execute_inspection<w1cov::session>(w1cov::session&,
                                                             const std::string&,
                                                             bool);
template void
TracerHost::execute_inspection<w1xfer::session>(w1xfer::session&,
                                                const std::string&, bool);
#ifdef WITNESS_SCRIPT_ENABLED
template void TracerHost::execute_inspection<w1::tracers::script::session>(
    w1::tracers::script::session&, const std::string&, bool);
#endif

template void TracerHost::inspect<w1cov::session>(const std::string&,
                                                  const w1cov::coverage_config&,
                                                  bool);
template void
TracerHost::inspect<w1xfer::session>(const std::string&,
                                     const w1xfer::transfer_config&, bool);
#ifdef WITNESS_SCRIPT_ENABLED
template void TracerHost::inspect<w1::tracers::script::session>(
    const std::string&, const w1::tracers::script::config&, bool);
#endif

} // namespace vstk::instrumentation