// Minimal cross-platform VST3 host application
#include <iostream>
#include <string>
#include <vector>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace Steinberg;

// VST3 host application implementation - provides context for plugins
class VstHostApplication : public Vst::IHostApplication
{
public:
    VstHostApplication() = default;
    virtual ~VstHostApplication() = default;

    // Returns the host application name to plugins
    tresult PLUGIN_API getName(Vst::String128 name) override
    {
        return Vst::StringConvert::convert("VST Shill Host", name) ? kResultTrue : kInternalError;
    }

    // Creates host-side objects that plugins may request
    tresult PLUGIN_API createInstance(TUID cid, TUID _iid, void** obj) override
    {
        // Create message objects for plugin communication
        if (FUnknownPrivate::iidEqual(cid, Vst::IMessage::iid) &&
            FUnknownPrivate::iidEqual(_iid, Vst::IMessage::iid))
        {
            *obj = new Vst::HostMessage;
            return kResultTrue;
        }
        
        // Create attribute list objects for parameter storage
        if (FUnknownPrivate::iidEqual(cid, Vst::IAttributeList::iid) &&
            FUnknownPrivate::iidEqual(_iid, Vst::IAttributeList::iid))
        {
            if (auto al = Vst::HostAttributeList::make())
            {
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

IMPLEMENT_FUNKNOWN_METHODS(VstHostApplication, Vst::IHostApplication, Vst::IHostApplication::iid)

// Global host context required by VST3 SDK
namespace Steinberg {
    FUnknown* gStandardPluginContext = new VstHostApplication();
}

// Scans a VST3 plugin bundle and displays its information
void scanVst3Plugin(const std::string& pluginPath)
{
    std::cout << "Scanning: " << pluginPath << std::endl;
    
    // Load the VST3 module
    std::string errorDescription;
    auto module = VST3::Hosting::Module::create(pluginPath, errorDescription);
    if (!module)
    {
        std::cout << "Failed to load: " << pluginPath << std::endl;
        if (!errorDescription.empty())
            std::cout << "Error: " << errorDescription << std::endl;
        return;
    }

    // Get plugin factory and basic info
    auto factory = module->getFactory();
    auto factoryInfo = factory.info();
    
    std::cout << "Module: " << module->getName() << std::endl;
    std::cout << "Vendor: " << factoryInfo.vendor() << std::endl;

    // Enumerate audio effect plugins in this module
    for (auto& classInfo : factory.classInfos())
    {
        if (classInfo.category() == kVstAudioEffectClass)
        {
            std::cout << "Found VST3 plugin: " << classInfo.name() << std::endl;
            std::cout << "  Vendor: " << classInfo.vendor() << std::endl;
            std::cout << "  Version: " << classInfo.version() << std::endl;
            std::cout << "  SDK Version: " << classInfo.sdkVersion() << std::endl;
        }
    }
}

// Returns platform-specific VST3 directory paths
std::vector<std::string> getVst3SearchPaths()
{
#ifdef __APPLE__
    return {
        "/Library/Audio/Plug-Ins/VST3",
        std::string(getenv("HOME") ? getenv("HOME") : "") + "/Library/Audio/Plug-Ins/VST3"
    };
#elif _WIN32
    return { "C:\\Program Files\\Common Files\\VST3" };
#else
    return {
        std::string(getenv("HOME") ? getenv("HOME") : "") + "/.vst3",
        "/usr/lib/vst3"
    };
#endif
}

// Displays platform-specific example paths
void showExamplePaths()
{
    std::cout << "Example plugin paths:" << std::endl;
#ifdef __APPLE__
    std::cout << "  /Library/Audio/Plug-Ins/VST3/SomePlugin.vst3" << std::endl;
    std::cout << "  ~/Library/Audio/Plug-Ins/VST3/SomePlugin.vst3" << std::endl;
#elif _WIN32
    std::cout << "  C:\\Program Files\\Common Files\\VST3\\SomePlugin.vst3" << std::endl;
#else
    std::cout << "  ~/.vst3/SomePlugin.vst3" << std::endl;
    std::cout << "  /usr/lib/vst3/SomePlugin.vst3" << std::endl;
#endif
}

int main(int argc, char* argv[])
{
    std::cout << "VST Shill - Minimal VST3 Host" << std::endl;
    std::cout << "=============================" << std::endl;
    
    if (argc > 1)
    {
        // Scan specific plugin provided as argument
        scanVst3Plugin(argv[1]);
    }
    else
    {
        // Show usage and scan common directories
        std::cout << "Usage: " << argv[0] << " <plugin_path>" << std::endl;
        showExamplePaths();
        
        std::cout << std::endl << "Common VST3 directories:" << std::endl;
        for (const auto& path : getVst3SearchPaths())
        {
            if (!path.empty())
            {
                std::cout << "  " << path << std::endl;
            }
        }
    }

    std::cout << std::endl << "Host initialized successfully!" << std::endl;
    return 0;
}