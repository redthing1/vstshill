#include "minimal.hpp"
#include <algorithm>

// vst3 sdk includes
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

using namespace Steinberg;

namespace vstk {
namespace host {

namespace {
// minimal vst3 host application implementation - provides context for plugins
class MinimalHostApplication : public Vst::IHostApplication {
public:
  MinimalHostApplication() = default;
  virtual ~MinimalHostApplication() = default;

  tresult PLUGIN_API getName(Vst::String128 name) override {
    return Vst::StringConvert::convert("vstshill minimal host", name) ? kResultTrue : kInternalError;
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

IMPLEMENT_FUNKNOWN_METHODS(MinimalHostApplication, Vst::IHostApplication, Vst::IHostApplication::iid)

// global host context for minimal host
FUnknown* get_minimal_host_context() {
  static MinimalHostApplication host_app;
  return &host_app;
}

} // namespace

MinimalHost::MinimalHost(const redlog::logger& logger) : _log(logger) {
  _log.trc("minimal host instance created");
}

void MinimalHost::inspect_plugin(const std::string& plugin_path) {
  _log.inf("loading vst3 plugin", redlog::field("path", plugin_path));

  std::string error_description;
  auto module = VST3::Hosting::Module::create(plugin_path, error_description);
  if (!module) {
    _log.error("failed to load module", redlog::field("path", plugin_path),
               redlog::field("error", error_description));
    return;
  }

  _log.dbg("module loaded successfully",
           redlog::field("module_path", module->getPath()),
           redlog::field("module_name", module->getName()));

  // get plugin factory
  auto factory = module->getFactory();
  auto factory_info = factory.info();

  _log.trc("factory information", 
           redlog::field("vendor", factory_info.vendor()),
           redlog::field("url", factory_info.url()),
           redlog::field("email", factory_info.email()),
           redlog::field("class_count", factory.classCount()));

  // enumerate and load audio effects
  bool found_audio_effect = false;
  for (auto& class_info : factory.classInfos()) {
    if (class_info.category() == kVstAudioEffectClass) {
      found_audio_effect = true;

      _log.inf("found audio effect plugin",
               redlog::field("name", class_info.name()),
               redlog::field("vendor", class_info.vendor()),
               redlog::field("version", class_info.version()));

      _log.trc("plugin details",
               redlog::field("sdk_version", class_info.sdkVersion()),
               redlog::field("categories", class_info.subCategoriesString()),
               redlog::field("class_id", class_info.ID().toString()));

      // create plugin component
      _log.dbg("creating component");

      auto component = factory.createInstance<Vst::IComponent>(class_info.ID());
      if (!component) {
        _log.error("failed to create component");
        continue;
      }

      _log.dbg("component created successfully");

      // initialize component
      tresult result = component->initialize(get_minimal_host_context());
      if (result != kResultOk) {
        _log.error("failed to initialize component", redlog::field("result", result));
        continue;
      }

      _log.dbg("component initialized successfully");

      // get bus information
      int32 num_audio_inputs = component->getBusCount(Vst::kAudio, Vst::kInput);
      int32 num_audio_outputs = component->getBusCount(Vst::kAudio, Vst::kOutput);
      int32 num_event_inputs = component->getBusCount(Vst::kEvent, Vst::kInput);
      int32 num_event_outputs = component->getBusCount(Vst::kEvent, Vst::kOutput);

      _log.trc("component bus configuration",
               redlog::field("audio_inputs", num_audio_inputs),
               redlog::field("audio_outputs", num_audio_outputs),
               redlog::field("event_inputs", num_event_inputs),
               redlog::field("event_outputs", num_event_outputs));

      // get bus details
      for (int32 i = 0; i < num_audio_inputs; ++i) {
        Vst::BusInfo bus_info;
        if (component->getBusInfo(Vst::kAudio, Vst::kInput, i, bus_info) == kResultOk) {
          std::string bus_name = Vst::StringConvert::convert(bus_info.name);
          _log.dbg("input bus details", 
                   redlog::field("bus_index", i),
                   redlog::field("bus_name", bus_name),
                   redlog::field("channel_count", bus_info.channelCount));
        }
      }

      for (int32 i = 0; i < num_audio_outputs; ++i) {
        Vst::BusInfo bus_info;
        if (component->getBusInfo(Vst::kAudio, Vst::kOutput, i, bus_info) == kResultOk) {
          std::string bus_name = Vst::StringConvert::convert(bus_info.name);
          _log.dbg("output bus details", 
                   redlog::field("bus_index", i),
                   redlog::field("bus_name", bus_name),
                   redlog::field("channel_count", bus_info.channelCount));
        }
      }

      // try to get controller
      TUID controller_cid;
      if (component->getControllerClassId(controller_cid) == kResultOk) {
        _log.dbg("creating edit controller");

        auto controller = factory.createInstance<Vst::IEditController>(controller_cid);
        if (controller) {
          _log.dbg("edit controller created successfully");

          if (controller->initialize(get_minimal_host_context()) == kResultOk) {
            _log.dbg("edit controller initialized successfully");

            // get parameter count
            int32 param_count = controller->getParameterCount();
            _log.trc("controller parameters", redlog::field("parameter_count", param_count));

            if (param_count > 0) {
              _log.trc("enumerating parameters (first 10)");
              for (int32 p = 0; p < std::min(param_count, 10); ++p) {
                Vst::ParameterInfo param_info;
                if (controller->getParameterInfo(p, param_info) == kResultOk) {
                  std::string param_title = Vst::StringConvert::convert(param_info.title);
                  _log.trc("parameter details", 
                           redlog::field("index", p),
                           redlog::field("title", param_title),
                           redlog::field("id", param_info.id));
                }
              }
              if (param_count > 10) {
                _log.trc("additional parameters available", redlog::field("remaining", param_count - 10));
              }
            }

            controller->terminate();
          } else {
            _log.error("failed to initialize edit controller");
          }
        } else {
          _log.error("failed to create edit controller");
        }
      }

      // plugin loaded successfully
      _log.inf("plugin loaded successfully",
               redlog::field("name", class_info.name()),
               redlog::field("vendor", class_info.vendor()),
               redlog::field("version", class_info.version()));

      // clean up
      _log.inf("terminating component");
      component->terminate();
      break; // only process first audio effect
    }
  }

  if (!found_audio_effect) {
    _log.inf("no audio effect plugins found in this module");

    _log.trc("available classes in module");
    for (auto& class_info : factory.classInfos()) {
      _log.trc("found class", 
               redlog::field("name", class_info.name()),
               redlog::field("category", class_info.category()));
    }
  }
}

} // namespace host
} // namespace vstk