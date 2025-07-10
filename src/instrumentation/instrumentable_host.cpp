#include "instrumentable_host.hpp"
#include "../host/module_loader.hpp"
#include "../util/string_utils.hpp"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

using namespace Steinberg;

namespace vstk {
namespace instrumentation {

namespace {
// minimal vst3 host application implementation - provides context for plugins
class MinimalHostApplication : public Vst::IHostApplication {
public:
  MinimalHostApplication() = default;
  virtual ~MinimalHostApplication() = default;

  tresult PLUGIN_API getName(Vst::String128 name) override {
    return Vst::StringConvert::convert("vstshill instrumentation host", name)
               ? kResultTrue
               : kInternalError;
  }

  tresult PLUGIN_API createInstance(TUID cid, TUID _iid, void** obj) override {
    if (FUnknownPrivate::iidEqual(cid, Vst::IMessage::iid) &&
        FUnknownPrivate::iidEqual(_iid, Vst::IMessage::iid)) {
      *obj = new Vst::HostMessage;
      return kResultTrue;
    }

    if (FUnknownPrivate::iidEqual(cid, Vst::IAttributeList::iid) &&
        FUnknownPrivate::iidEqual(_iid, Vst::IAttributeList::iid)) {
      if (auto al = Vst::HostAttributeList::make()) {
        *obj = al.take();
        return kResultTrue;
      }
      return kOutOfMemory;
    }

    *obj = nullptr;
    return kResultFalse;
  }

  DECLARE_FUNKNOWN_METHODS
};

IMPLEMENT_FUNKNOWN_METHODS(MinimalHostApplication, Vst::IHostApplication,
                           Vst::IHostApplication::iid)

// global host context for instrumentation host
FUnknown* get_instrumentation_host_context() {
  static MinimalHostApplication host_app;
  return &host_app;
}

} // namespace

InstrumentableHost::InstrumentableHost(const redlog::logger& logger)
    : _log(logger) {
  _log.trc("instrumentable host instance created");
}

// helper struct for passing inspection context
struct InspectionContext {
  InstrumentableHost* host;
  host::VstModule* module;
};

// c-style function that performs the actual vst inspection under
// instrumentation
extern "C" uint64_t perform_instrumented_inspection(uint64_t context_ptr) {
  auto* ctx = reinterpret_cast<InspectionContext*>(context_ptr);
  auto& log = ctx->host->_log;

  log.dbg("starting instrumented vst inspection");

  if (!ctx->module) {
    log.err("null module pointer in inspection context");
    return 1;
  }

  // get plugin factory
  VST3::Hosting::PluginFactory factory(ctx->module->getFactory());
  auto factory_info = factory.info();

  log.trc("factory information", redlog::field("vendor", factory_info.vendor()),
          redlog::field("url", factory_info.url()),
          redlog::field("email", factory_info.email()),
          redlog::field("class_count", factory.classCount()));

  // enumerate and inspect audio effects
  bool found_audio_effect = false;
  for (auto& class_info : factory.classInfos()) {
    if (class_info.category() == kVstAudioEffectClass) {
      found_audio_effect = true;

      log.inf("found audio effect plugin",
              redlog::field("name", class_info.name()),
              redlog::field("vendor", class_info.vendor()),
              redlog::field("version", class_info.version()));

      // create component - under instrumentation
      log.dbg("creating component");
      auto component = factory.createInstance<Vst::IComponent>(class_info.ID());
      if (!component) {
        log.err("failed to create component");
        continue;
      }

      // initialize component - under instrumentation
      tresult result =
          component->initialize(get_instrumentation_host_context());
      if (result != kResultOk) {
        log.err("failed to initialize component",
                redlog::field("result", result));
        continue;
      }

      log.dbg("component initialized successfully");

      // query bus information
      int32 num_audio_inputs = component->getBusCount(Vst::kAudio, Vst::kInput);
      int32 num_audio_outputs =
          component->getBusCount(Vst::kAudio, Vst::kOutput);
      int32 num_event_inputs = component->getBusCount(Vst::kEvent, Vst::kInput);
      int32 num_event_outputs =
          component->getBusCount(Vst::kEvent, Vst::kOutput);

      log.trc("component bus configuration",
              redlog::field("audio_inputs", num_audio_inputs),
              redlog::field("audio_outputs", num_audio_outputs),
              redlog::field("event_inputs", num_event_inputs),
              redlog::field("event_outputs", num_event_outputs));

      // get bus details
      for (int32 i = 0; i < num_audio_inputs; ++i) {
        Vst::BusInfo bus_info;
        if (component->getBusInfo(Vst::kAudio, Vst::kInput, i, bus_info) ==
            kResultOk) {
          std::string bus_name = Vst::StringConvert::convert(bus_info.name);
          log.dbg("input bus details", redlog::field("bus_index", i),
                  redlog::field("bus_name", bus_name),
                  redlog::field("channel_count", bus_info.channelCount));
        }
      }

      for (int32 i = 0; i < num_audio_outputs; ++i) {
        Vst::BusInfo bus_info;
        if (component->getBusInfo(Vst::kAudio, Vst::kOutput, i, bus_info) ==
            kResultOk) {
          std::string bus_name = Vst::StringConvert::convert(bus_info.name);
          log.dbg("output bus details", redlog::field("bus_index", i),
                  redlog::field("bus_name", bus_name),
                  redlog::field("channel_count", bus_info.channelCount));
        }
      }

      // try to get controller
      TUID controller_cid;
      if (component->getControllerClassId(controller_cid) == kResultOk) {
        log.dbg("creating edit controller");
        auto controller =
            factory.createInstance<Vst::IEditController>(controller_cid);
        if (controller) {
          if (controller->initialize(get_instrumentation_host_context()) ==
              kResultOk) {
            log.dbg("edit controller initialized successfully");

            // get parameter count
            int32 param_count = controller->getParameterCount();
            log.trc("controller parameters",
                    redlog::field("parameter_count", param_count));

            if (param_count > 0) {
              log.trc("enumerating parameters (first 10)");
              for (int32 p = 0; p < std::min(param_count, 10); ++p) {
                Vst::ParameterInfo param_info;
                if (controller->getParameterInfo(p, param_info) == kResultOk) {
                  std::string param_title =
                      Vst::StringConvert::convert(param_info.title);
                  log.trc("parameter details", redlog::field("index", p),
                          redlog::field("title", param_title),
                          redlog::field("id", param_info.id));
                }
              }
              if (param_count > 10) {
                log.trc("additional parameters available",
                        redlog::field("remaining", param_count - 10));
              }
            }

            controller->terminate();
          } else {
            log.err("failed to initialize edit controller");
          }
        } else {
          log.err("failed to create edit controller");
        }
      }

      log.inf("plugin inspected successfully");

      // clean up
      component->terminate();
      break; // only process first audio effect
    }
  }

  if (!found_audio_effect) {
    log.inf("no audio effect plugins found in this module");
  }

  return 0;
}

// helper to set library handle in context for trace_function
extern "C" uint64_t perform_instrumented_vst_init(uint64_t library_handle,
                                                  uint64_t plugin_path_ptr) {
  void* handle = reinterpret_cast<void*>(library_handle);
  const std::string* path =
      reinterpret_cast<const std::string*>(plugin_path_ptr);

  std::string error_description;
  auto module =
      host::VstModule::initializeFromLibrary(handle, *path, error_description);

  if (!module) {
    return 0; // failure
  }

  // return the module pointer as success indicator
  // note: we leak the module here, but it's ok for instrumentation purposes
  return reinterpret_cast<uint64_t>(module.release());
}

void InstrumentableHost::inspect_with_coverage(const std::string& plugin_path,
                                               bool pause_after_load,
                                               const std::string& export_path) {
  _log.inf("starting coverage instrumentation for plugin",
           redlog::field("path", plugin_path));

  // create coverage session
  w1cov::session coverage;
  if (!coverage.initialize()) {
    _log.err("failed to initialize coverage session");
    return;
  }

  // step 1: load the library WITHOUT instrumentation
  _log.dbg("loading plugin library (outside instrumentation)");

  std::string error_description;
  void* library_handle =
      host::VstModule::loadLibraryOnly(plugin_path, error_description);
  if (!library_handle) {
    _log.err("failed to load library",
             redlog::field("error", error_description));
    return;
  }

  _log.dbg("library loaded successfully");

  // pause immediately after load if requested (before any plugin code runs)
  if (pause_after_load) {
    _log.inf("pausing after library load (before plugin initialization)");
    wait_for_input("library loaded. press enter to continue with plugin "
                   "initialization...");
  }

  // step 2: get a function pointer from the module to add it to instrumentation
  void* func_ptr =
      host::VstModule::getFunctionPointer(library_handle, "GetPluginFactory");
  if (!func_ptr) {
    _log.err("failed to get function pointer from module");
    host::VstModule::unloadLibrary(library_handle);
    return;
  }

  // add the module to instrumentation range using the function pointer
  if (!coverage.add_instrumented_module(func_ptr)) {
    _log.err("failed to add module to instrumentation range");
    return;
  }

  _log.inf("module added to instrumentation range");

  // step 3: initialize vst module under instrumentation
  uint64_t module_ptr;
  bool success = coverage.trace_function(
      reinterpret_cast<void*>(&perform_instrumented_vst_init),
      {reinterpret_cast<uint64_t>(library_handle),
       reinterpret_cast<uint64_t>(&plugin_path)},
      &module_ptr);

  if (!success || !module_ptr) {
    _log.err("failed to initialize vst module under instrumentation");
    host::VstModule::unloadLibrary(library_handle);
    return;
  }

  // wrap the module pointer
  std::unique_ptr<host::VstModule> module(
      reinterpret_cast<host::VstModule*>(module_ptr));

  // step 4: perform inspection under instrumentation
  InspectionContext ctx{this, module.get()};
  uint64_t result;

  success = coverage.trace_function(
      reinterpret_cast<void*>(&perform_instrumented_inspection),
      {reinterpret_cast<uint64_t>(&ctx)}, &result);

  if (!success || result != 0) {
    _log.err("instrumented inspection failed");
  }

  // print statistics
  _log.inf("coverage statistics:");
  coverage.print_statistics();

  // export if requested
  if (!export_path.empty()) {
    if (!coverage.export_coverage(export_path)) {
      _log.err("failed to export coverage data");
    } else {
      _log.inf("exported coverage data", redlog::field("path", export_path));
    }
  }

  _log.dbg("cleanup complete");
}

} // namespace instrumentation
} // namespace vstk