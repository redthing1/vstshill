#include "vst_operations.hpp"
#include "../util/string_utils.hpp"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <redlog.hpp>

using namespace Steinberg;

namespace vstk::instrumentation {

namespace {

// minimal vst3 host application implementation
class MinimalHostApplication : public Vst::IHostApplication {
public:
  MinimalHostApplication() = default;
  virtual ~MinimalHostApplication() = default;

  tresult PLUGIN_API getName(Vst::String128 name) override {
    return Vst::StringConvert::convert("vstshill tracer host", name)
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

  tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IHostApplication)
    QUERY_INTERFACE(_iid, obj, IHostApplication::iid, IHostApplication)
    *obj = nullptr;
    return kNoInterface;
  }

  uint32 PLUGIN_API addRef() override {
    // static object, don't track references
    return 1;
  }

  uint32 PLUGIN_API release() override {
    // static object, don't delete
    return 1;
  }
};

// global host context
FUnknown* get_host_context() {
  static MinimalHostApplication host_app;
  return &host_app;
}

} // namespace

// initializes vst module from library handle
extern "C" uint64_t vst_init_module(uint64_t library_handle,
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

// performs full vst inspection
extern "C" uint64_t vst_inspect_plugin(uint64_t context_ptr) {
  auto* ctx = reinterpret_cast<VstContext*>(context_ptr);
  auto log = redlog::get_logger("vstk::vst_operations");

  log.dbg("starting vst inspection");

  if (!ctx->module) {
    log.err("null module pointer in context");
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

      log.trc("plugin details",
              redlog::field("sdk_version", class_info.sdkVersion()),
              redlog::field("categories", class_info.subCategoriesString()),
              redlog::field("class_id", class_info.ID().toString()));

      // create component
      log.dbg("creating component");
      auto component = factory.createInstance<Vst::IComponent>(class_info.ID());
      if (!component) {
        log.err("failed to create component");
        continue;
      }

      // initialize component
      tresult result = component->initialize(get_host_context());
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
          log.dbg("edit controller created successfully");
          if (controller->initialize(get_host_context()) == kResultOk) {
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
      log.inf("terminating component");
      component->terminate();
      break; // only process first audio effect
    }
  }

  if (!found_audio_effect) {
    log.inf("no audio effect plugins found in this module");
  }

  return 0;
}

} // namespace vstk::instrumentation