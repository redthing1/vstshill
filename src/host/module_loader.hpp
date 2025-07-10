#pragma once

#include "pluginterfaces/base/ipluginbase.h"
#include <memory>
#include <string>
#include <functional>
#include <filesystem>

namespace vstk {
namespace host {

// instrumentation hook types
enum class LoadingStage {
  PRE_MODULE_LOAD,     // before dynamic library loading
  POST_MODULE_LOAD,    // after library loaded, before symbol resolution
  PRE_SYMBOL_RESOLVE,  // before resolving specific symbols
  POST_SYMBOL_RESOLVE, // after symbol resolution, before calling entry
                       // functions
  PRE_INIT_DLL,        // before calling InitDll/bundleEntry/ModuleEntry
  POST_INIT_DLL,       // after successful initialization
  PRE_FACTORY_CALL,    // before calling GetPluginFactory
  POST_FACTORY_CALL,   // after factory creation
  LOAD_COMPLETE,       // module fully loaded and ready
  LOAD_FAILED          // loading failed at any stage
};

struct LoadingContext {
  std::string bundle_path;
  void* library_handle = nullptr;
  void* symbol_address = nullptr;
  std::string symbol_name;
  std::string error_description;
  LoadingStage stage;
};

using LoadingCallback = std::function<void(const LoadingContext& context)>;

// manages a loaded VST3 module (shared library)
// handles platform-specific loading of VST3 bundles, retrieves the
// IPluginFactory, and ensures the library is unloaded when the object is
// destroyed
class VstModule {
public:
  // loads a VST3 module from the given bundle path
  // returns a std::unique_ptr<VstModule> on success, nullptr on failure
  static std::unique_ptr<VstModule> load(const std::string& bundlePath,
                                         std::string& errorDescription);

  // loads only the library handle without initializing VST
  static void* loadLibraryOnly(const std::string& bundlePath,
                               std::string& errorDescription);

  // completes VST initialization on a pre-loaded library
  static std::unique_ptr<VstModule>
  initializeFromLibrary(void* libraryHandle, const std::string& bundlePath,
                        std::string& errorDescription);

  // unloads a library handle obtained from loadLibraryOnly
  static void unloadLibrary(void* libraryHandle);

  // gets a function pointer from the loaded library by name
  static void* getFunctionPointer(void* libraryHandle,
                                  const std::string& functionName);

  // destructor that unloads the native library
  ~VstModule();

  // gets the raw IPluginFactory pointer from the module
  Steinberg::IPluginFactory* getFactory() const;

  // gets the path of the loaded bundle
  const std::string& getPath() const { return _bundlePath; }

  // gets the name of the loaded bundle (filename without path)
  std::string getName() const {
    return std::filesystem::path(_bundlePath).stem().string();
  }

  // sets a global callback for instrumentation during loading
  static void setInstrumentationCallback(LoadingCallback callback);

  // gets the current instrumentation callback
  static LoadingCallback getInstrumentationCallback();

  // rule of five: prevent copying and moving to ensure unique ownership
  VstModule(const VstModule&) = delete;
  VstModule& operator=(const VstModule&) = delete;
  VstModule(VstModule&&) = delete;
  VstModule& operator=(VstModule&&) = delete;

private:
  // private constructor to be used by the load factory method
  VstModule(void* libraryHandle, Steinberg::IPluginFactory* factory,
            const std::string& bundlePath);

  void* _libraryHandle = nullptr;
  Steinberg::IPluginFactory* _factory = nullptr;
  std::string _bundlePath;

  // static instrumentation callback
  static LoadingCallback _instrumentationCallback;
};

} // namespace host
} // namespace vstk
