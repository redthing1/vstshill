// Minimal cross-platform VST3 host application
#include <iostream>
#include <string>
#include <vector>

#include "ext/args.hpp"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include <redlog/redlog.hpp>

using namespace Steinberg;

// Global logger instance
auto log_main = redlog::get_logger("vstshill");

// VST3 host application implementation - provides context for plugins
class VstHostApplication : public Vst::IHostApplication {
public:
  VstHostApplication() = default;
  virtual ~VstHostApplication() = default;

  // Returns the host application name to plugins
  tresult PLUGIN_API getName(Vst::String128 name) override {
    return Vst::StringConvert::convert("VST Shill Host", name) ? kResultTrue
                                                               : kInternalError;
  }

  // Creates host-side objects that plugins may request
  tresult PLUGIN_API createInstance(TUID cid, TUID _iid, void** obj) override {
    // Create message objects for plugin communication
    if (FUnknownPrivate::iidEqual(cid, Vst::IMessage::iid) &&
        FUnknownPrivate::iidEqual(_iid, Vst::IMessage::iid)) {
      *obj = new Vst::HostMessage;
      return kResultTrue;
    }

    // Create attribute list objects for parameter storage
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

IMPLEMENT_FUNKNOWN_METHODS(VstHostApplication, Vst::IHostApplication,
                           Vst::IHostApplication::iid)

// Global host context required by VST3 SDK
namespace Steinberg {
FUnknown* gStandardPluginContext = new VstHostApplication();
}

// loads and initializes a vst3 plugin, displaying detailed information
void load_vst3_plugin(const std::string& plugin_path) {
  auto log = log_main.with_name("loader");
  log.inf("loading vst3 plugin", redlog::field("path", plugin_path));

  // load the vst3 module
  std::string error_description;
  auto module = VST3::Hosting::Module::create(plugin_path, error_description);
  if (!module) {
    log.error("failed to load module", redlog::field("path", plugin_path),
              redlog::field("error", error_description));
    return;
  }

  log.dbg("module loaded successfully",
          redlog::field("module_path", module->getPath()),
          redlog::field("module_name", module->getName()));

  // get plugin factory
  auto factory = module->getFactory();
  auto factory_info = factory.info();

  log.trc("factory information", redlog::field("vendor", factory_info.vendor()),
          redlog::field("url", factory_info.url()),
          redlog::field("email", factory_info.email()),
          redlog::field("class_count", factory.classCount()));

  // enumerate and load audio effects
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

      // create plugin component
      log.dbg("creating component");

      auto component = factory.createInstance<Vst::IComponent>(class_info.ID());
      if (!component) {
        log.error("failed to create component");
        continue;
      }

      log.dbg("component created successfully");

      // initialize component
      tresult result = component->initialize(gStandardPluginContext);
      if (result != kResultOk) {
        log.error("failed to initialize component",
                  redlog::field("result", result));
        continue;
      }

      log.dbg("component initialized successfully");

      // get component information
      // get bus information
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

          if (controller->initialize(gStandardPluginContext) == kResultOk) {
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
            log.error("failed to initialize edit controller");
          }
        } else {
          log.error("failed to create edit controller");
        }
      }

      // plugin loaded successfully
      log.inf("plugin loaded successfully",
              redlog::field("name", class_info.name()),
              redlog::field("vendor", class_info.vendor()),
              redlog::field("version", class_info.version()));

      // clean up
      log.inf("terminating component");
      component->terminate();
      break; // only process first audio effect
    }
  }

  if (!found_audio_effect) {
    log.inf("no audio effect plugins found in this module");

    log.trc("available classes in module");
    for (auto& class_info : factory.classInfos()) {
      log.trc("found class", redlog::field("name", class_info.name()),
              redlog::field("category", class_info.category()));
    }
  }
}

// Returns platform-specific VST3 directory paths
std::vector<std::string> get_vst3_search_paths() {
#ifdef __APPLE__
  return {"/Library/Audio/Plug-Ins/VST3",
          std::string(getenv("HOME") ? getenv("HOME") : "") +
              "/Library/Audio/Plug-Ins/VST3"};
#elif _WIN32
  return {"C:\\Program Files\\Common Files\\VST3"};
#else
  return {std::string(getenv("HOME") ? getenv("HOME") : "") + "/.vst3",
          "/usr/lib/vst3"};
#endif
}

// Displays platform-specific example paths
void show_example_paths() {
  log_main.inf("example plugin paths:");
#ifdef __APPLE__
  log_main.inf("  /Library/Audio/Plug-Ins/VST3/SomePlugin.vst3");
  log_main.inf("  ~/Library/Audio/Plug-Ins/VST3/SomePlugin.vst3");
#elif _WIN32
  log_main.inf("  C:\\Program Files\\Common Files\\VST3\\SomePlugin.vst3");
#else
  log_main.inf("  ~/.vst3/SomePlugin.vst3");
  log_main.inf("  /usr/lib/vst3/SomePlugin.vst3");
#endif
}

int main(int argc, char* argv[]) {
  // Set default log level to info
  redlog::set_level(redlog::level::info);

  args::ArgumentParser parser("VST Shill - Minimal VST3 Host",
                              "Scans and loads VST3 plugins");
  args::CounterFlag trace(
      parser, "trace",
      "Increase verbosity level (-v=trace, -vv=debug, -vvv=trace)",
      {'v', "trace"});
  args::Positional<std::string> plugin_path(parser, "plugin_path",
                                            "Path to VST3 plugin to scan");

  try {
    parser.ParseCLI(argc, argv);
  } catch (args::Help) {
    std::cout << parser;
    return 0;
  } catch (args::ParseError e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  // Set log level based on verbosity flags
  int verbosity = args::get(trace);
  switch (verbosity) {
  case 0:
    redlog::set_level(redlog::level::info);
    break;
  case 1:
    redlog::set_level(redlog::level::trace);
    break;
  case 2:
    redlog::set_level(redlog::level::debug);
    break;
  case 3:
  default:
    redlog::set_level(redlog::level::trace);
    break;
  }

  log_main.inf("vstshill: vst3 analyzer");
  log_main.trc("verbosity level set",
               redlog::field("level", redlog::level_name(redlog::get_level())),
               redlog::field("flag_count", verbosity));

  if (plugin_path) {
    // load specific plugin provided as argument
    load_vst3_plugin(args::get(plugin_path));
  } else {
    // Show usage and scan common directories
    log_main.inf("usage: " + std::string(argv[0]) + " [-v] <plugin_path>");
    show_example_paths();

    log_main.inf("common vst3 directories:");
    for (const auto& path : get_vst3_search_paths()) {
      if (!path.empty()) {
        log_main.trc("search path", redlog::field("path", path));
      }
    }
  }

  log_main.dbg("host initialized successfully");
  return 0;
}
