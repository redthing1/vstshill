#pragma once

// standard library includes
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// third-party includes
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <redlog.hpp>

// vst3 sdk includes
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>
#include <public.sdk/source/vst/hosting/processdata.h>

namespace vstk {

// gui window size constraints
namespace gui {
constexpr int max_window_width = 900;
constexpr int max_window_height = 650;
constexpr int min_window_width = 400;
constexpr int min_window_height = 300;
} // namespace gui

// result type for error handling
template <typename T> class Result {
public:
  Result(T&& value) : _value(std::move(value)), _has_value(true) {}
  Result(const T& value) : _value(value), _has_value(true) {}
  Result(const std::string& error) : _error(error), _has_value(false) {}

  bool is_ok() const { return _has_value; }
  bool is_error() const { return !_has_value; }

  const T& value() const { return _value; }
  T& value() { return _value; }
  const std::string& error() const { return _error; }

  operator bool() const { return is_ok(); }

private:
  T _value{};
  std::string _error;
  bool _has_value;
};

// type aliases for better readability
using Sample32 = Steinberg::Vst::Sample32;
using Sample64 = Steinberg::Vst::Sample64;
using BusInfo = Steinberg::Vst::BusInfo;
using ProcessContext = Steinberg::Vst::ProcessContext;
using EventList = Steinberg::Vst::EventList;
using ParameterChanges = Steinberg::Vst::ParameterChanges;

// enum classes instead of raw vst constants
enum class MediaType {
  Audio = Steinberg::Vst::kAudio,
  Event = Steinberg::Vst::kEvent
};

enum class BusDirection {
  Input = Steinberg::Vst::kInput,
  Output = Steinberg::Vst::kOutput
};

enum class SymbolicSampleSize {
  Sample32 = Steinberg::Vst::kSample32,
  Sample64 = Steinberg::Vst::kSample64
};

enum class ProcessMode {
  Realtime = Steinberg::Vst::kRealtime,
  Offline = Steinberg::Vst::kOffline
};

// configuration structure for plugin initialization
struct PluginConfig {
  int sample_rate = 44100;
  int max_block_size = 512;
  SymbolicSampleSize sample_size = SymbolicSampleSize::Sample32;
  ProcessMode process_mode = ProcessMode::Realtime;

  // builder pattern methods for fluent configuration
  PluginConfig& with_sample_rate(int rate) {
    sample_rate = rate;
    return *this;
  }
  PluginConfig& with_block_size(int size) {
    max_block_size = size;
    return *this;
  }
  PluginConfig& with_sample_size(SymbolicSampleSize size) {
    sample_size = size;
    return *this;
  }
  PluginConfig& with_process_mode(ProcessMode mode) {
    process_mode = mode;
    return *this;
  }
};

// bus information structure
struct BusConfiguration {
  std::string name;
  int32_t channel_count;
  bool is_active;
  Steinberg::Vst::SpeakerArrangement speaker_arrangement;
};

// comprehensive plugin information structure
struct PluginInfo {
  std::string name;
  std::string vendor;
  std::string version;
  std::string category;
  std::string path;

  std::vector<BusConfiguration> audio_inputs;
  std::vector<BusConfiguration> audio_outputs;
  std::vector<BusConfiguration> event_inputs;
  std::vector<BusConfiguration> event_outputs;

  int32_t parameter_count = 0;
  bool has_editor = false;
};

// forward declarations
class Plugin;
class GuiWindow;
class ParameterManager;

// vst3 host context - singleton
class HostContext {
public:
  static HostContext& instance();

  Steinberg::FUnknown* get_context() const { return _context.get(); }

private:
  HostContext();
  ~HostContext() = default;

  std::unique_ptr<Steinberg::Vst::HostApplication> _context;
  redlog::logger _log;
};

// vst3 plugin wrapper with error handling
class Plugin {
  friend class GuiWindow;
  friend class ParameterManager;

public:
  explicit Plugin(
      const redlog::logger& logger = redlog::get_logger("vstk::plugin"));
  ~Plugin();

  // non-copyable, movable
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;
  Plugin(Plugin&&) = default;
  Plugin& operator=(Plugin&&) = default;

  // plugin lifecycle
  Result<bool> load(const std::string& plugin_path,
                    const PluginConfig& config = {});
  void unload();
  bool is_loaded() const { return _module != nullptr; }

  // plugin information
  const PluginInfo& info() const { return _info; }
  std::string name() const { return _info.name; }
  std::string vendor() const { return _info.vendor; }

  // bus management
  int32_t bus_count(MediaType type, BusDirection direction) const;
  std::optional<BusConfiguration>
  bus_info(MediaType type, BusDirection direction, int32_t index) const;
  Result<bool> set_bus_active(MediaType type, BusDirection direction,
                              int32_t index, bool active);

  // audio processing
  Result<bool> prepare_processing();
  Result<bool> refresh_audio_buffers();
  Result<bool> start_processing();
  void stop_processing();
  bool is_processing() const { return _is_processing; }

  Result<bool> process(int32_t num_samples);

  // audio buffer access
  Sample32* get_audio_buffer_32(BusDirection direction, int32_t bus_index,
                                int32_t channel_index) const;
  Sample64* get_audio_buffer_64(BusDirection direction, int32_t bus_index,
                                int32_t channel_index) const;

  // event handling
  EventList* get_event_list(BusDirection direction, int32_t bus_index) const;
  ParameterChanges* get_parameter_changes(BusDirection direction) const;

  // process context access
  ProcessContext* get_process_context() { return &_process_context; }
  const ProcessContext* get_process_context() const {
    return &_process_context;
  }

  // parameter management
  class ParameterManager& parameters();
  const class ParameterManager& parameters() const;

  // gui support
  Result<std::unique_ptr<GuiWindow>> create_editor_window();
  bool has_editor() const { return _edit_controller != nullptr; }

private:
  void reset_state();
  Result<bool> setup_buses();
  Result<bool> activate_default_buses();
  Result<bool> configure_processing();

  redlog::logger _log;
  PluginConfig _config;
  PluginInfo _info;

  // vst3 objects
  VST3::Hosting::Module::Ptr _module;
  Steinberg::IPtr<Steinberg::Vst::PlugProvider> _plugin_provider;
  Steinberg::IPtr<Steinberg::Vst::IComponent> _component;
  Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> _audio_processor;
  Steinberg::IPtr<Steinberg::Vst::IEditController> _edit_controller;

  // process data
  Steinberg::Vst::HostProcessData _process_data;
  Steinberg::Vst::ProcessSetup _process_setup;
  Steinberg::Vst::ProcessContext _process_context;

  // state
  bool _is_active = false;
  bool _is_processing = false;

  // bus data storage
  std::vector<Steinberg::Vst::SpeakerArrangement> _input_arrangements;
  std::vector<Steinberg::Vst::SpeakerArrangement> _output_arrangements;
  std::unique_ptr<Steinberg::Vst::EventList[]> _input_events;
  std::unique_ptr<Steinberg::Vst::EventList[]> _output_events;

  // parameter management
  std::unique_ptr<ParameterManager> _parameter_manager;
};

// iplugframe implementation for resize requests
class GuiPlugFrame : public Steinberg::IPlugFrame {
public:
  explicit GuiPlugFrame(class GuiWindow* window) : _window(window) {}

  // iplugframe interface
  Steinberg::tresult PLUGIN_API
  resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

  // funknown interface
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                               void** obj) override;
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }

private:
  class GuiWindow* _window;
};

// cross-platform gui window for vst3 editors
class GuiWindow {
public:
  GuiWindow(Plugin& plugin,
            const redlog::logger& logger = redlog::get_logger("vstk::gui"));
  ~GuiWindow();

  // non-copyable, non-movable
  GuiWindow(const GuiWindow&) = delete;
  GuiWindow& operator=(const GuiWindow&) = delete;
  GuiWindow(GuiWindow&&) = delete;
  GuiWindow& operator=(GuiWindow&&) = delete;

  // window lifecycle
  Result<bool> create();
  void destroy();
  bool is_open() const { return _window != nullptr; }

  // event processing
  static void process_events();

  // window properties
  std::pair<int, int> size() const;
  void set_title(const std::string& title);

  // called by guiplugframe when plugin requests resize
  Steinberg::tresult handle_plugin_resize(Steinberg::IPlugView* view,
                                          Steinberg::ViewRect* newSize);

private:
  Result<bool> attach_plugin_view();
  Result<bool> setup_content_scaling();
  static void handle_window_event(const SDL_Event& event);

  redlog::logger _log;
  Plugin& _plugin;

  SDL_Window* _window = nullptr;
  Steinberg::IPtr<Steinberg::IPlugView> _plugin_view;
  void* _native_view = nullptr;
  GuiPlugFrame _plug_frame;
  bool _resize_recursion_guard = false;

  // static event handling
  static std::vector<GuiWindow*> _active_windows;
};

// audio utility functions
namespace util {
// audio format conversion helpers
void interleave_audio(const std::vector<Sample32*>& channels,
                      Sample32* interleaved, int32_t num_samples);
void deinterleave_audio(const Sample32* interleaved,
                        const std::vector<Sample32*>& channels,
                        int32_t num_samples);

// time/tempo utilities
void setup_process_context(ProcessContext& context, double sample_rate,
                           int64_t sample_position = 0, double tempo = 120.0,
                           int32_t time_sig_numerator = 4,
                           int32_t time_sig_denominator = 4);

void update_process_context(ProcessContext& context, int32_t block_size);

// plugin info scanning
Result<PluginInfo> scan_plugin(const std::string& plugin_path);
} // namespace util

} // namespace vstk