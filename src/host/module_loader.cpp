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
typedef bool (*ModuleEntryFunc)(void *);
typedef bool (*ModuleExitFunc)();
#elif defined(_WIN32)
typedef bool (PLUGIN_API *InitModuleFunc)();
typedef bool (PLUGIN_API *ExitModuleFunc)();
#endif
typedef Steinberg::IPluginFactory *(PLUGIN_API *GetFactoryProc)();

// VstModule implementation

VstModule::VstModule(void *libraryHandle,
                     Steinberg::IPluginFactory *factory,
                     const std::string &bundlePath)
    : _libraryHandle(libraryHandle), _factory(factory), _bundlePath(bundlePath) {
  _log.dbg("vst module instance created", redlog::field("path", bundlePath));
}

VstModule::~VstModule() {
  _log.dbg("unloading vst module", redlog::field("path", _bundlePath));
  
  if (_libraryHandle) {
#if defined(__APPLE__)
    _log.trc("calling bundleExit and unloading bundle");
    if (auto bundleExit = reinterpret_cast<BundleExitFunc>(CFBundleGetFunctionPointerForName(
            reinterpret_cast<CFBundleRef>(_libraryHandle), CFSTR("bundleExit")))) {
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
    if (auto exitDll = reinterpret_cast<ExitModuleFunc>(
            GetProcAddress(reinterpret_cast<HMODULE>(_libraryHandle), "ExitDll"))) {
      exitDll();
    }
    FreeLibrary(reinterpret_cast<HMODULE>(_libraryHandle));
#endif
    _log.dbg("vst module unloaded successfully");
  }
}

Steinberg::IPluginFactory *VstModule::getFactory() const { return _factory; }

void VstModule::setInstrumentationCallback(LoadingCallback callback) {
  _instrumentationCallback = callback;
  _log.inf("instrumentation callback ", 
           redlog::field("enabled", callback != nullptr));
}

LoadingCallback VstModule::getInstrumentationCallback() {
  return _instrumentationCallback;
}

// static load method with platform-specific logic

std::unique_ptr<VstModule>
VstModule::load(const std::string &bundlePath,
                std::string &errorDescription) {
  namespace fs = std::filesystem;
  fs::path path(bundlePath);
  
  _log.inf("loading vst3 module", redlog::field("path", bundlePath));
  
  // instrumentation: pre-module load
  LoadingContext context;
  context.bundle_path = bundlePath;
  context.stage = LoadingStage::PRE_MODULE_LOAD;
  call_instrumentation(context);

#if defined(__APPLE__)
  _log.trc("creating cfurl from path");
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, (const UInt8 *)bundlePath.c_str(),
      bundlePath.length(), true);
  if (!url) {
    errorDescription = "could not create cfurl from path";
    _log.error("cfurl creation failed", redlog::field("path", bundlePath));
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  _log.trc("creating cfbundle");
  CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
  CFRelease(url);

  if (!bundle) {
    errorDescription = "could not create cfbundle";
    _log.error("cfbundle creation failed");
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  _log.trc("loading bundle executable");
  if (!CFBundleLoadExecutable(bundle)) {
    errorDescription = "cfbundle load executable failed";
    _log.error("bundle executable loading failed");
    CFRelease(bundle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  context.library_handle = bundle;
  context.stage = LoadingStage::POST_MODULE_LOAD;
  call_instrumentation(context);

  _log.trc("resolving symbols");
  
  // instrumentation: pre-symbol resolve
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
    _log.error("symbol resolution failed", 
               redlog::field("bundleEntry", bundleEntry != nullptr),
               redlog::field("getFactory", getFactory != nullptr));
    CFBundleUnloadExecutable(bundle);
    CFRelease(bundle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  _log.trc("calling bundleEntry");
  
  // instrumentation: pre-init
  context.stage = LoadingStage::PRE_INIT_DLL;
  context.symbol_name = "bundleEntry";
  context.symbol_address = reinterpret_cast<void*>(bundleEntry);
  call_instrumentation(context);
  
  if (!bundleEntry(bundle)) {
    errorDescription = "bundleEntry() failed";
    _log.error("bundleEntry call failed");
    CFBundleUnloadExecutable(bundle);
    CFRelease(bundle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  // instrumentation: post-init
  context.stage = LoadingStage::POST_INIT_DLL;
  call_instrumentation(context);

  _log.trc("calling GetPluginFactory");
  
  // instrumentation: pre-factory call
  context.stage = LoadingStage::PRE_FACTORY_CALL;
  context.symbol_name = "GetPluginFactory";
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  call_instrumentation(context);
  
  Steinberg::IPluginFactory *factory = getFactory();
  if (!factory) {
    errorDescription = "GetPluginFactory() returned null";
    _log.error("GetPluginFactory returned null");
    CFBundleUnloadExecutable(bundle);
    CFRelease(bundle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  // instrumentation: post-factory call
  context.stage = LoadingStage::POST_FACTORY_CALL;
  call_instrumentation(context);

  _log.inf("vst3 module loaded successfully", 
           redlog::field("path", bundlePath),
           redlog::field("factory", factory != nullptr));
  
  // instrumentation: load complete
  context.stage = LoadingStage::LOAD_COMPLETE;
  call_instrumentation(context);
  
  return std::unique_ptr<VstModule>(new VstModule(reinterpret_cast<void *>(bundle), factory,
                                     bundlePath));

#elif defined(__linux__)
  _log.trc("determining target architecture");
  struct utsname unameData;
  if (uname(&unameData) != 0) {
    errorDescription = "could not get machine name via uname()";
    _log.error("uname() failed");
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  fs::path libraryPath = path / "Contents" / (std::string(unameData.machine) + "-linux") / path.stem();
  libraryPath.replace_extension(".so");
  
  _log.trc("resolved library path", 
           redlog::field("architecture", unameData.machine),
           redlog::field("library_path", libraryPath.string()));

  if (!fs::exists(libraryPath)) {
    errorDescription = "shared library not found at expected path: " + libraryPath.string();
    _log.error("shared library not found", redlog::field("path", libraryPath.string()));
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  _log.trc("loading shared library with dlopen");
  void *handle = dlopen(libraryPath.c_str(), RTLD_LAZY);
  if (!handle) {
    errorDescription = "dlopen failed: " + std::string(dlerror());
    _log.error("dlopen failed", 
               redlog::field("path", libraryPath.string()),
               redlog::field("error", dlerror()));
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  context.library_handle = handle;
  context.stage = LoadingStage::POST_MODULE_LOAD;
  call_instrumentation(context);

  _log.trc("resolving symbols");
  
  // instrumentation: pre-symbol resolve
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  context.symbol_name = "ModuleEntry";
  call_instrumentation(context);
  
  auto moduleEntry = reinterpret_cast<ModuleEntryFunc>(dlsym(handle, "ModuleEntry"));
  
  context.symbol_address = reinterpret_cast<void*>(moduleEntry);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);
  
  context.symbol_name = "GetPluginFactory";
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  call_instrumentation(context);
  
  auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(handle, "GetPluginFactory"));
  
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  if (!moduleEntry || !getFactory) {
    errorDescription = "could not find ModuleEntry or GetPluginFactory";
    _log.error("symbol resolution failed", 
               redlog::field("moduleEntry", moduleEntry != nullptr),
               redlog::field("getFactory", getFactory != nullptr));
    dlclose(handle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  _log.trc("calling ModuleEntry");
  
  // instrumentation: pre-init
  context.stage = LoadingStage::PRE_INIT_DLL;
  context.symbol_name = "ModuleEntry";
  context.symbol_address = reinterpret_cast<void*>(moduleEntry);
  call_instrumentation(context);
  
  if (!moduleEntry(handle)) {
    errorDescription = "ModuleEntry() failed";
    _log.error("ModuleEntry call failed");
    dlclose(handle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  // instrumentation: post-init
  context.stage = LoadingStage::POST_INIT_DLL;
  call_instrumentation(context);

  _log.trc("calling GetPluginFactory");
  
  // instrumentation: pre-factory call
  context.stage = LoadingStage::PRE_FACTORY_CALL;
  context.symbol_name = "GetPluginFactory";
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  call_instrumentation(context);
  
  Steinberg::IPluginFactory *factory = getFactory();
  if (!factory) {
    errorDescription = "GetPluginFactory() returned null";
    _log.error("GetPluginFactory returned null");
    dlclose(handle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  // instrumentation: post-factory call
  context.stage = LoadingStage::POST_FACTORY_CALL;
  call_instrumentation(context);

  _log.inf("vst3 module loaded successfully", 
           redlog::field("path", bundlePath),
           redlog::field("factory", factory != nullptr));
  
  // instrumentation: load complete
  context.stage = LoadingStage::LOAD_COMPLETE;
  call_instrumentation(context);
  
  return std::unique_ptr<VstModule>(new VstModule(handle, factory, bundlePath));

#elif defined(_WIN32)
  fs::path libraryPath = path / "Contents" / "x86_64-win" / path.filename();
  
  _log.trc("checking bundle structure", redlog::field("library_path", libraryPath.string()));
  
  if (!fs::exists(libraryPath)) {
    _log.trc("bundle library not found, trying legacy single-file format");
    errorDescription = "shared library not found at expected path: " + libraryPath.string();
    
    // fallback for legacy single-file VST3
    libraryPath = path;
    if (!fs::exists(libraryPath)) {
      errorDescription = "could not find VST3 at bundle path or as single file: " + libraryPath.string();
      _log.error("vst3 file not found", 
                 redlog::field("bundle_path", (path / "Contents" / "x86_64-win" / path.filename()).string()),
                 redlog::field("legacy_path", path.string()));
      
      context.stage = LoadingStage::LOAD_FAILED;
      context.error_description = errorDescription;
      call_instrumentation(context);
      return nullptr;
    }
    
    _log.trc("using legacy single-file format", redlog::field("path", libraryPath.string()));
  }

  _log.trc("loading library with LoadLibraryW", redlog::field("path", libraryPath.string()));
  HMODULE handle = LoadLibraryW(libraryPath.wstring().c_str());
  if (!handle) {
    DWORD error = GetLastError();
    errorDescription = "LoadLibraryW failed for " + libraryPath.string() + " (error: " + std::to_string(error) + ")";
    _log.error("LoadLibraryW failed", 
               redlog::field("path", libraryPath.string()),
               redlog::field("error_code", error));
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  context.library_handle = handle;
  context.stage = LoadingStage::POST_MODULE_LOAD;
  call_instrumentation(context);

  _log.trc("resolving symbols");
  
  // instrumentation: pre-symbol resolve (InitDll is optional)
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  context.symbol_name = "InitDll";
  call_instrumentation(context);
  
  auto initDll = reinterpret_cast<InitModuleFunc>(GetProcAddress(handle, "InitDll"));
  
  context.symbol_address = reinterpret_cast<void*>(initDll);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);
  
  if (!initDll) {
    _log.trc("InitDll not found (optional)");
  }
  
  context.symbol_name = "GetPluginFactory";
  context.stage = LoadingStage::PRE_SYMBOL_RESOLVE;
  call_instrumentation(context);
  
  auto getFactory = reinterpret_cast<GetFactoryProc>(GetProcAddress(handle, "GetPluginFactory"));
  
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  context.stage = LoadingStage::POST_SYMBOL_RESOLVE;
  call_instrumentation(context);

  if (!getFactory) {
    errorDescription = "GetProcAddress could not find GetPluginFactory";
    _log.error("GetPluginFactory symbol not found");
    FreeLibrary(handle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }

  if (initDll) {
    _log.trc("calling InitDll");
    
    // instrumentation: pre-init
    context.stage = LoadingStage::PRE_INIT_DLL;
    context.symbol_name = "InitDll";
    context.symbol_address = reinterpret_cast<void*>(initDll);
    call_instrumentation(context);
    
    if (!initDll()) {
      errorDescription = "InitDll() failed";
      _log.error("InitDll call failed");
      FreeLibrary(handle);
      
      context.stage = LoadingStage::LOAD_FAILED;
      context.error_description = errorDescription;
      call_instrumentation(context);
      return nullptr;
    }
    
    // instrumentation: post-init
    context.stage = LoadingStage::POST_INIT_DLL;
    call_instrumentation(context);
  } else {
    _log.trc("no InitDll function, skipping initialization");
  }

  _log.trc("calling GetPluginFactory");
  
  // instrumentation: pre-factory call
  context.stage = LoadingStage::PRE_FACTORY_CALL;
  context.symbol_name = "GetPluginFactory";
  context.symbol_address = reinterpret_cast<void*>(getFactory);
  call_instrumentation(context);
  
  Steinberg::IPluginFactory *factory = getFactory();
  if (!factory) {
    errorDescription = "GetPluginFactory() returned null";
    _log.error("GetPluginFactory returned null");
    FreeLibrary(handle);
    
    context.stage = LoadingStage::LOAD_FAILED;
    context.error_description = errorDescription;
    call_instrumentation(context);
    return nullptr;
  }
  
  // instrumentation: post-factory call
  context.stage = LoadingStage::POST_FACTORY_CALL;
  call_instrumentation(context);

  _log.inf("vst3 module loaded successfully", 
           redlog::field("path", bundlePath),
           redlog::field("factory", factory != nullptr),
           redlog::field("had_init_dll", initDll != nullptr));
  
  // instrumentation: load complete
  context.stage = LoadingStage::LOAD_COMPLETE;
  call_instrumentation(context);
  
  return std::unique_ptr<VstModule>(new VstModule(reinterpret_cast<void *>(handle), factory,
                                     bundlePath));
#else
  errorDescription = "platform not supported";
  _log.error("unsupported platform for vst3 loading");
  
  context.stage = LoadingStage::LOAD_FAILED;
  context.error_description = errorDescription;
  call_instrumentation(context);
  return nullptr;
#endif
}

} // namespace host
} // namespace vstk
