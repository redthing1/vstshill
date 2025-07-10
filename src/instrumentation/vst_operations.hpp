#pragma once
#include "../host/module_loader.hpp"
#include <cstdint>

namespace vstk::instrumentation {

// c-style functions for tracing (required by trace_function)
extern "C" {
// initializes vst module from library handle
uint64_t vst_init_module(uint64_t library_handle, uint64_t plugin_path_ptr);

// performs full vst inspection
uint64_t vst_inspect_plugin(uint64_t context_ptr);
}

// context for passing data to instrumented functions
struct VstContext {
  void* host; // tracer host instance
  host::VstModule* module;
  const std::string* plugin_path;
};

} // namespace vstk::instrumentation