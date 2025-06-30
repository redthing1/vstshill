// minimal cross-platform vst3 host application

// standard library
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// third-party libraries
#include <SDL2/SDL.h>
#include <redlog/redlog.hpp>

// local includes
#include "ext/args.hpp"
#include "vstk.hpp"

// vst3 sdk (minimal includes for host application)
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

using namespace Steinberg;

namespace {
auto log_main = redlog::get_logger("vstshill");
}

namespace {
// vst3 host application implementation - provides context for plugins
class VstHostApplication : public Vst::IHostApplication {
public:
  VstHostApplication() = default;
  virtual ~VstHostApplication() = default;

  tresult PLUGIN_API getName(Vst::String128 name) override {
    return Vst::StringConvert::convert("vstshill host", name) ? kResultTrue
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

IMPLEMENT_FUNKNOWN_METHODS(VstHostApplication, Vst::IHostApplication,
                           Vst::IHostApplication::iid)
} // namespace

// global host context required by vst3 sdk
namespace Steinberg {
FUnknown* gStandardPluginContext = new VstHostApplication();
}

// loads and initializes a vst3 plugin, displaying detailed information
void load_vst3_plugin(const std::string& plugin_path) {
  auto log = log_main.with_name("loader");
  log.inf("loading vst3 plugin", redlog::field("path", plugin_path));

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

// returns platform-specific vst3 directory paths
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

// displays platform-specific example paths
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

// opens plugin editor gui using vstk
void open_plugin_gui(const std::string& plugin_path) {
  auto log = log_main.with_name("gui");
  log.inf("opening plugin editor", redlog::field("path", plugin_path));

  // create plugin instance using vstk
  vstk::Plugin plugin(log);

  // load plugin with default configuration
  auto load_result = plugin.load(plugin_path);
  if (!load_result) {
    log.error("failed to load plugin",
              redlog::field("error", load_result.error()));
    return;
  }

  log.inf("plugin loaded successfully", redlog::field("name", plugin.name()));

  // check if plugin has an editor
  if (!plugin.has_editor()) {
    log.warn("plugin does not have an editor interface (headless plugin)");
    return;
  }

  // create editor window
  auto window_result = plugin.create_editor_window();
  if (!window_result) {
    log.error("failed to create editor window",
              redlog::field("error", window_result.error()));
    return;
  }

  auto window = std::move(window_result.value());
  log.inf("editor window opened successfully");

  // event loop for gui
  log.inf("entering gui event loop (close window to exit)");
  bool running = true;
  while (running && window->is_open()) {
    vstk::GuiWindow::process_events();

    // small delay to prevent high cpu usage
    SDL_Delay(16); // ~60 fps
  }

  log.inf("gui session ended");
}

// processes audio file through plugin using vstk
void process_audio_file(const std::string& input_file,
                        const std::string& output_file,
                        const std::string& plugin_path) {
  auto log = log_main.with_name("processor");
  log.inf("processing audio file", redlog::field("input", input_file),
          redlog::field("output", output_file),
          redlog::field("plugin", plugin_path));

  // create plugin instance
  vstk::Plugin plugin(log);

  // configure for offline processing
  vstk::PluginConfig config;
  config.with_process_mode(vstk::ProcessMode::Offline)
      .with_sample_rate(44100)
      .with_block_size(512);

  // load plugin
  auto load_result = plugin.load(plugin_path, config);
  if (!load_result) {
    log.error("failed to load plugin",
              redlog::field("error", load_result.error()));
    return;
  }

  log.inf("plugin loaded for audio processing",
          redlog::field("name", plugin.name()));

  // todo: implement actual audio file i/o and processing
  // this would require an audio library like libsndfile or similar
  log.warn(
      "audio file processing not yet implemented - placeholder functionality");
  log.inf("would process:", redlog::field("input_file", input_file),
          redlog::field("output_file", output_file),
          redlog::field("plugin_name", plugin.name()));
}

int main(int argc, char* argv[]) {
  // set default log level to info
  redlog::set_level(redlog::level::info);

  args::ArgumentParser parser("vstshill - minimal vst3 host",
                              "scans, loads, and hosts vst3 plugins");
  args::CounterFlag trace(
      parser, "trace",
      "increase verbosity level (-v=trace, -vv=debug, -vvv=trace)",
      {'v', "trace"});
  args::Flag gui_mode(parser, "gui", "open plugin editor gui window", {"gui"});
  args::ValueFlag<std::string> input_file(
      parser, "input", "input audio file for processing mode", {"input", 'i'});
  args::ValueFlag<std::string> output_file(
      parser, "output", "output audio file for processing mode",
      {"output", 'o'});
  args::Positional<std::string> plugin_path(parser, "plugin_path",
                                            "path to vst3 plugin to scan/load");

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

  // set log level based on verbosity flags
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

  log_main.inf("vstshill: vst3 host");
  log_main.trc("verbosity level set",
               redlog::field("level", redlog::level_name(redlog::get_level())),
               redlog::field("flag_count", verbosity));

  if (plugin_path) {
    std::string path = args::get(plugin_path);

    // determine mode based on flags
    if (gui_mode) {
      // gui mode - open plugin editor
      log_main.inf("entering GUI mode");
      open_plugin_gui(path);
    } else if (input_file && output_file) {
      // processing mode - process audio file
      log_main.inf("entering audio processing mode");
      process_audio_file(args::get(input_file), args::get(output_file), path);
    } else {
      // default inspection mode - analyze plugin
      log_main.inf("entering inspection mode");
      load_vst3_plugin(path);
    }
  } else {
    // show usage and scan common directories
    log_main.inf("usage examples:");
    log_main.inf("  " + std::string(argv[0]) +
                 " <plugin_path>                    # inspect plugin");
    log_main.inf("  " + std::string(argv[0]) +
                 " --gui <plugin_path>              # open editor GUI");
    log_main.inf("  " + std::string(argv[0]) +
                 " -i input.wav -o output.wav <plugin_path>  # process audio");

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
