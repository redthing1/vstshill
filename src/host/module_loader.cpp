#include "module_loader.hpp"
#include <redlog.hpp>
#include <filesystem>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <sys/utsname.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace vstk {
namespace host {

// static member initialization
LoadingCallback VstModule::_instrumentationCallback = nullptr;

// logger for module loading
static auto _log = redlog::get_logger("vstk::module_loader");

// helper function to call instrumentation callback (friend of VstModule)
static void call_instrumentation(const LoadingContext& context) {
  if (VstModule::getInstrumentationCallback()) {
    VstModule::getInstrumentationCallback()(context);
  }
}

// function pointer types for entry/exit hooks
#if defined(__APPLE__)
typedef bool (*BundleEntryFunc)(CFBundleRef);
typedef bool (*BundleExitFunc)();
#elif defined(__linux__)
typedef bool (*ModuleEntryFunc)(void*);
typedef bool (*ModuleExitFunc)();
#elif defined(_WIN32)
typedef bool(PLUGIN_API* InitModuleFunc)();
typedef bool(PLUGIN_API* ExitModuleFunc)();
#endif
typedef Steinberg::IPluginFactory*(PLUGIN_API* GetFactoryProc)();

// VstModule implementation

VstModule::VstModule(void* libraryHandle, Steinberg::IPluginFactory* factory,
                     const std::string& bundlePath)
    : _libraryHandle(libraryHandle), _factory(factory),
      _bundlePath(bundlePath) {
  _log.dbg("vst module instance created", redlog::field("path", bundlePath));
}

VstModule::~VstModule() {
  _log.dbg("unloading vst module", redlog::field("path", _bundlePath));

  if (_libraryHandle) {
#if defined(__APPLE__)
    _log.trc("calling bundleExit and unloading bundle");
    if (auto bundleExit =
            reinterpret_cast<BundleExitFunc>(CFBundleGetFunctionPointerForName(
                reinterpret_cast<CFBundleRef>(_libraryHandle),
                CFSTR("bundleExit")))) {
      bundleExit();
    }
    CFBundleUnloadExecutable(reinterpret_cast<CFBundleRef>(_libraryHandle));
    CFRelease(reinterpret_cast<CFBundleRef>(_libraryHandle));
#elif defined(__linux__)
    _log.trc("calling ModuleExit and closing library");
    if (auto moduleExit = reinterpret_cast<ModuleExitFunc>(
            dlsym(_libraryHandle, "ModuleExit"))) {
      moduleExit();
    }
    dlclose(_libraryHandle);
#elif defined(_WIN32)
    _log.trc("calling ExitDll and freeing library");
    if (auto exitDll = reinterpret_cast<ExitModuleFunc>(GetProcAddress(
            reinterpret_cast<HMODULE>(_libraryHandle), "ExitDll"))) {
      exitDll();
    }
    FreeLibrary(reinterpret_cast<HMODULE>(_libraryHandle));
#endif
    _log.dbg("vst module unloaded successfully");
  }
}

Steinberg::IPluginFactory* VstModule::getFactory() const { return _factory; }

void VstModule::setInstrumentationCallback(LoadingCallback callback) {
  _instrumentationCallback = callback;
  _log.inf("instrumentation callback ",
           redlog::field("enabled", callback != nullptr));
}

LoadingCallback VstModule::getInstrumentationCallback() {
  return _instrumentationCallback;
}

// loads only the library without any vst initialization
void* VstModule::loadLibraryOnly(const std::string& bundlePath,
                                 std::string& errorDescription) {
  namespace fs = std::filesystem;
  fs::path path(bundlePath);

  _log.inf("loading library only", redlog::field("path", bundlePath));

#if defined(__APPLE__)
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, (const UInt8*)bundlePath.c_str(),
      bundlePath.length(), true);
  if (!url) {
    errorDescription = "could not create cfurl from path";
    _log.error("cfurl creation failed", redlog::field("path", bundlePath));
    return nullptr;
  }

  CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
  CFRelease(url);

  if (!bundle) {
    errorDescription = "could not create cfbundle";
    _log.error("cfbundle creation failed");
    return nullptr;
  }

  if (!CFBundleLoadExecutable(bundle)) {
    errorDescription = "cfbundle load executable failed";
    _log.error("bundle executable loading failed");
    CFRelease(bundle);
    return nullptr;
  }

  _log.dbg("library loaded successfully");
  return bundle;

#elif defined(__linux__)
  struct utsname unameData;
  if (uname(&unameData) != 0) {
    errorDescription = "could not get machine name via uname()";
    _log.error("uname() failed");
    return nullptr;
  }

  fs::path libraryPath = path / "Contents" /
                         (std::string(unameData.machine) + "-linux") /
                         path.stem();
  libraryPath.replace_extension(".so");

  if (!fs::exists(libraryPath)) {
    errorDescription =
        "shared library not found at expected path: " + libraryPath.string();
    _log.error("shared library not found",
               redlog::field("path", libraryPath.string()));
    return nullptr;
  }

  void* handle = dlopen(libraryPath.c_str(), RTLD_LAZY);
  if (!handle) {
    errorDescription = "dlopen failed: " + std::string(dlerror());
    _log.error("dlopen failed", redlog::field("path", libraryPath.string()),
               redlog::field("error", dlerror()));
    return nullptr;
  }

  _log.dbg("library loaded successfully");
  return handle;

#elif defined(_WIN32)
  fs::path libraryPath = path / "Contents" / "x86_64-win" / path.filename();

  if (!fs::exists(libraryPath)) {
    libraryPath = path;
    if (!fs::exists(libraryPath)) {
      errorDescription =
          "could not find VST3 at bundle path or as single file: " +
          libraryPath.string();
      _log.error("vst3 file not found");
      return nullptr;
    }
  }

  HMODULE handle = LoadLibraryW(libraryPath.wstring().c_str());
  if (!handle) {
    DWORD error = GetLastError();
    errorDescription = "LoadLibraryW failed for " + libraryPath.string() +
                       " (error: " + std::to_string(error) + ")";
    _log.error("LoadLibraryW failed",
               redlog::field("path", libraryPath.string()),
               redlog::field("error_code", error));
    return nullptr;
  }

  _log.dbg("library loaded successfully");
  return handle;

#else
  errorDescription = "platform not supported";
  _log.error("unsupported platform for vst3 loading");
  return nullptr;
#endif
}

// unloads a library handle
void VstModule::unloadLibrary(void* libraryHandle) {
  if (!libraryHandle) {
    return;
  }

  _log.dbg("unloading library");

#if defined(__APPLE__)
  CFBundleUnloadExecutable(reinterpret_cast<CFBundleRef>(libraryHandle));
  CFRelease(reinterpret_cast<CFBundleRef>(libraryHandle));
#elif defined(__linux__)
  dlclose(libraryHandle);
#elif defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(libraryHandle));
#endif
}

// gets a function pointer from the loaded library
void* VstModule::getFunctionPointer(void* libraryHandle,
                                    const std::string& functionName) {
  if (!libraryHandle || functionName.empty()) {
    _log.err("invalid library handle or function name");
    return nullptr;
  }

  _log.trc("getting function pointer", redlog::field("function", functionName));

#if defined(__APPLE__)
  CFBundleRef bundle = reinterpret_cast<CFBundleRef>(libraryHandle);
  CFStringRef cfFunctionName = CFStringCreateWithCString(
      kCFAllocatorDefault, functionName.c_str(), kCFStringEncodingUTF8);
  if (!cfFunctionName) {
    _log.err("failed to create CFString for function name");
    return nullptr;
  }

  void* funcPtr = reinterpret_cast<void*>(
      CFBundleGetFunctionPointerForName(bundle, cfFunctionName));
  CFRelease(cfFunctionName);

  if (!funcPtr) {
    _log.dbg("function not found", redlog::field("name", functionName));
  }
  return funcPtr;

#elif defined(__linux__)
  void* funcPtr = dlsym(libraryHandle, functionName.c_str());
  if (!funcPtr) {
    _log.dbg("function not found", redlog::field("name", functionName),
             redlog::field("error", dlerror()));
  }
  return funcPtr;

#elif defined(_WIN32)
  void* funcPtr = reinterpret_cast<void*>(GetProcAddress(
      reinterpret_cast<HMODULE>(libraryHandle), functionName.c_str()));
  if (!funcPtr) {
    _log.dbg("function not found", redlog::field("name", functionName),
             redlog::field("error", GetLastError()));
  }
  return funcPtr;

#else
  _log.err("platform not supported for function pointer resolution");
  return nullptr;
#endif
}

// completes vst initialization from a pre-loaded library
std::unique_ptr<VstModule>
VstModule::initializeFromLibrary(void* libraryHandle,
                                 const std::string& bundlePath,
                                 std::string& errorDescription) {
  if (!libraryHandle) {
    errorDescription = "null library handle";
    return nullptr;
  }

  _log.inf("initializing vst from loaded library",
           redlog::field("path", bundlePath));

  LoadingContext context;
  context.bundle_path = bundlePath;
  context.library_handle = libraryHandle;

  auto signal_failure =
      [&](const std::string& message) -> std::unique_ptr<VstModule> {
    context.error_description = message;
    context.stage = LoadingStage::LOAD_FAILED;
    call_instrumentation(context);
    return nullptr;
  };

#if defined(__APPLE__)
  CFBundleRef bundle = reinterpret_cast<CFBundleRef>(libraryHandle);

  // resolve symbols
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  context.symbol_name = "bundleEntry";
  call_instrumentation(context);

  auto bundleEntry = reinterpret_cast<BundleEntryFunc>(
      CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));

  context.symbol_address = reinterpret_cast<void*>(bundleEntry);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  context.symbol_name = "GetPluginFactory";
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  call_instrumentation(context);

  auto getFactory = reinterpret_cast<GetFactoryProc>(
      CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));

  context.symbol_address = reinterpret_cast<void*>(getFactory);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  if (!bundleEntry || !getFactory) {
    errorDescription = "could not find bundleEntry or GetPluginFactory";
    _log.error("symbol resolution failed");
    return signal_failure(errorDescription);
  }

  // call bundleEntry
  context.stage = LoadingStage::PRE_INIT_DLL;
  context.symbol_name = "bundleEntry";
  context.symbol_address = reinterpret_cast<void*>(bundleEntry);
  call_instrumentation(context);

  if (!bundleEntry(bundle)) {
    errorDescription = "bundleEntry() failed";
    _log.error("bundleEntry call failed");
    return signal_failure(errorDescription);
  }

  context.stage = LoadingStage::POST_INIT_DLL;
  call_instrumentation(context);

  // get factory
  context.stage = LoadingStage::PRE_FACTORY_CALL;
  context.symbol_name = "GetPluginFactory";
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  call_instrumentation(context);

  Steinberg::IPluginFactory* factory = getFactory();
  if (!factory) {
    errorDescription = "GetPluginFactory() returned null";
    _log.error("GetPluginFactory returned null");
    return signal_failure(errorDescription);
  }

  context.stage = LoadingStage::POST_FACTORY_CALL;
  call_instrumentation(context);

#elif defined(__linux__)
  // similar implementation for linux
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  context.symbol_name = "ModuleEntry";
  call_instrumentation(context);

  auto moduleEntry =
      reinterpret_cast<ModuleEntryFunc>(dlsym(libraryHandle, "ModuleEntry"));

  context.symbol_address = reinterpret_cast<void*>(moduleEntry);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  context.symbol_name = "GetPluginFactory";
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  call_instrumentation(context);

  auto getFactory = reinterpret_cast<GetFactoryProc>(
      dlsym(libraryHandle, "GetPluginFactory"));

  context.symbol_address = reinterpret_cast<void*>(getFactory);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  if (!moduleEntry || !getFactory) {
    errorDescription = "could not find ModuleEntry or GetPluginFactory";
    _log.error("symbol resolution failed");
    return signal_failure(errorDescription);
  }

  context.stage = LoadingStage::PRE_INIT_DLL;
  context.symbol_name = "ModuleEntry";
  context.symbol_address = reinterpret_cast<void*>(moduleEntry);
  call_instrumentation(context);

  if (!moduleEntry(libraryHandle)) {
    errorDescription = "ModuleEntry() failed";
    _log.error("ModuleEntry call failed");
    return signal_failure(errorDescription);
  }

  context.stage = LoadingStage::POST_INIT_DLL;
  call_instrumentation(context);

  context.stage = LoadingStage::PRE_FACTORY_CALL;
  context.symbol_name = "GetPluginFactory";
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  call_instrumentation(context);

  Steinberg::IPluginFactory* factory = getFactory();
  if (!factory) {
    errorDescription = "GetPluginFactory() returned null";
    _log.error("GetPluginFactory returned null");
    return signal_failure(errorDescription);
  }

  context.stage = LoadingStage::POST_FACTORY_CALL;
  call_instrumentation(context);

#elif defined(_WIN32)
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  context.symbol_name = "InitDll";
  call_instrumentation(context);

  auto initDll = reinterpret_cast<InitModuleFunc>(
      GetProcAddress(reinterpret_cast<HMODULE>(libraryHandle), "InitDll"));

  context.symbol_address = reinterpret_cast<void*>(initDll);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  context.symbol_name = "GetPluginFactory";
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  call_instrumentation(context);

  auto getFactory = reinterpret_cast<GetFactoryProc>(GetProcAddress(
      reinterpret_cast<HMODULE>(libraryHandle), "GetPluginFactory"));

  context.symbol_address = reinterpret_cast<void*>(getFactory);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  if (!getFactory) {
    errorDescription = "GetProcAddress could not find GetPluginFactory";
    _log.error("GetPluginFactory symbol not found");
    return signal_failure(errorDescription);
  }

  if (initDll) {
    context.stage = LoadingStage::PRE_INIT_DLL;
    context.symbol_name = "InitDll";
    context.symbol_address = reinterpret_cast<void*>(initDll);
    call_instrumentation(context);

    if (!initDll()) {
      errorDescription = "InitDll() failed";
      _log.error("InitDll call failed");
      return signal_failure(errorDescription);
    }

    context.stage = LoadingStage::POST_INIT_DLL;
    call_instrumentation(context);
  }

  context.stage = LoadingStage::PRE_FACTORY_CALL;
  context.symbol_name = "GetPluginFactory";
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  call_instrumentation(context);

  Steinberg::IPluginFactory* factory = getFactory();
  if (!factory) {
    errorDescription = "GetPluginFactory() returned null";
    _log.error("GetPluginFactory returned null");
    return signal_failure(errorDescription);
  }

  context.stage = LoadingStage::POST_FACTORY_CALL;
  call_instrumentation(context);

#else
  errorDescription = "platform not supported";
  _log.error("platform not supported for vst3 loading");
  return signal_failure(errorDescription);
#endif

  _log.inf("vst3 module initialized successfully",
           redlog::field("path", bundlePath),
           redlog::field("factory", factory != nullptr));

  context.error_description.clear();
  context.stage = LoadingStage::LOAD_COMPLETE;
  call_instrumentation(context);

  return std::unique_ptr<VstModule>(
      new VstModule(libraryHandle, factory, bundlePath));
}

// static load method with platform-specific logic
std::unique_ptr<VstModule> VstModule::load(const std::string& bundlePath,
                                           std::string& errorDescription) {
  _log.inf("loading vst3 module", redlog::field("path", bundlePath));

  // instrumentation: pre-module load
  LoadingContext context;
  context.bundle_path = bundlePath;
  context.stage = LoadingStage::PRE_MODULE_LOAD;
  call_instrumentation(context);

  // load library
  void* libraryHandle = loadLibraryOnly(bundlePath, errorDescription);
  if (!libraryHandle) {
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  context.library_handle = libraryHandle;
  context.stage = LoadingStage::POST_MODULE_LOAD;
  call_instrumentation(context);

  // initialize vst from loaded library
  auto module =
      initializeFromLibrary(libraryHandle, bundlePath, errorDescription);
  if (!module) {
    unloadLibrary(libraryHandle);
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  return module;
}

} // namespace host
} // namespace vstk
