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

using namespace Steinberg;

// Global verbosity level
static int verbosity = 0;

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

// Scans a VST3 plugin bundle and displays its information
void scan_vst3_plugin(const std::string& pluginPath) {
  if (verbosity >= 1) {
    std::cout << "Scanning: " << pluginPath << std::endl;
  }

  // Load the VST3 module
  std::string errorDescription;
  auto module = VST3::Hosting::Module::create(pluginPath, errorDescription);
  if (!module) {
    std::cout << "Failed to load: " << pluginPath << std::endl;
    if (!errorDescription.empty()) {
      std::cout << "Error: " << errorDescription << std::endl;
    }
    return;
  }

  // Get plugin factory and basic info
  auto factory = module->getFactory();
  auto factoryInfo = factory.info();

  if (verbosity >= 1) {
    std::cout << "Module: " << module->getName() << std::endl;
    std::cout << "Vendor: " << factoryInfo.vendor() << std::endl;
  }

  // Enumerate audio effect plugins in this module
  for (auto& classInfo : factory.classInfos()) {
    if (classInfo.category() == kVstAudioEffectClass) {
      std::cout << "Found VST3 plugin: " << classInfo.name() << std::endl;
      if (verbosity >= 1) {
        std::cout << "  Vendor: " << classInfo.vendor() << std::endl;
        std::cout << "  Version: " << classInfo.version() << std::endl;
        if (verbosity >= 2) {
          std::cout << "  SDK Version: " << classInfo.sdkVersion() << std::endl;
        }
      }
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
  std::cout << "Example plugin paths:" << std::endl;
#ifdef __APPLE__
  std::cout << "  /Library/Audio/Plug-Ins/VST3/SomePlugin.vst3" << std::endl;
  std::cout << "  ~/Library/Audio/Plug-Ins/VST3/SomePlugin.vst3" << std::endl;
#elif _WIN32
  std::cout << "  C:\\Program Files\\Common Files\\VST3\\SomePlugin.vst3"
            << std::endl;
#else
  std::cout << "  ~/.vst3/SomePlugin.vst3" << std::endl;
  std::cout << "  /usr/lib/vst3/SomePlugin.vst3" << std::endl;
#endif
}

int main(int argc, char* argv[]) {
  args::ArgumentParser parser("VST Shill - Minimal VST3 Host",
                              "Scans and loads VST3 plugins");
  args::CounterFlag verbose(parser, "verbose", "Increase verbosity level",
                            {'v', "verbose"});
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

  verbosity = args::get(verbose);

  if (verbosity >= 1) {
    std::cout << "VST Shill - Minimal VST3 Host" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "Verbosity level: " << verbosity << std::endl;
  }

  if (plugin_path) {
    // Scan specific plugin provided as argument
    scan_vst3_plugin(args::get(plugin_path));
  } else {
    // Show usage and scan common directories
    std::cout << "Usage: " << argv[0] << " [-v] <plugin_path>" << std::endl;
    show_example_paths();

    if (verbosity >= 1) {
      std::cout << std::endl << "Common VST3 directories:" << std::endl;
      for (const auto& path : get_vst3_search_paths()) {
        if (!path.empty()) {
          std::cout << "  " << path << std::endl;
        }
      }
    }
  }

  if (verbosity >= 1) {
    std::cout << std::endl << "Host initialized successfully!" << std::endl;
  }
  return 0;
}
