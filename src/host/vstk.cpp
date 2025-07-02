#include "vstk.hpp"
#include "parameter.hpp"
#include "platform/platform_gui.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>
#include <public.sdk/source/vst/utility/stringconvert.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vstk {

// static member definitions
std::vector<GuiWindow*> GuiWindow::_active_windows;

// hostcontext implementation

HostContext::HostContext() : _log(redlog::get_logger("vstk::host")) {
  _log.dbg("creating vst3 host context");
  _context = std::make_unique<HostApplication>();

  // set the global plugin context for vst3 sdk
  PluginContextFactory::instance().setPluginContext(_context.get());

  _log.dbg("host context created successfully");
}

HostContext& HostContext::instance() {
  static HostContext instance;
  return instance;
}

// plugin implementation

Plugin::Plugin(const redlog::logger& logger)
    : _log(logger.with_name("plugin")) {
  _log.trc("plugin instance created");

  // initialize parameter manager
  _parameter_manager = std::make_unique<ParameterManager>(*this);

  // initialize sdl for gui support
  static bool sdl_initialized = false;
  if (!sdl_initialized) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      _log.warn("failed to initialize sdl for gui support",
                redlog::field("error", SDL_GetError()));
    } else {
      _log.trc("sdl initialized for gui support");
      sdl_initialized = true;
    }
  }
}

Plugin::~Plugin() {
  unload();
  _log.trc("plugin instance destroyed");
}

Result<bool> Plugin::load(const std::string& plugin_path,
                          const PluginConfig& config) {
  _log.inf("loading vst3 plugin", redlog::field("path", plugin_path));

  // ensure we have host context
  HostContext::instance();

  // clean up any existing plugin
  if (is_loaded()) {
    unload();
  }

  _config = config;
  _info.path = plugin_path;

  // load vst3 module
  std::string error_description;
  _module = VST3::Hosting::Module::create(plugin_path, error_description);
  if (!_module) {
    _log.error("failed to load vst3 module", redlog::field("path", plugin_path),
               redlog::field("error", error_description));
    return Result<bool>("Failed to load VST3 module: " + error_description);
  }

  _log.dbg("module loaded successfully",
           redlog::field("module_path", _module->getPath()),
           redlog::field("module_name", _module->getName()));

  // get plugin factory and find audio effect
  auto factory = _module->getFactory();
  auto factory_info = factory.info();

  _log.trc("factory information",
           redlog::field("vendor", factory_info.vendor()),
           redlog::field("url", factory_info.url()),
           redlog::field("class_count", factory.classCount()));

  bool found_audio_effect = false;
  for (auto& class_info : factory.classInfos()) {
    if (class_info.category() == kVstAudioEffectClass) {
      found_audio_effect = true;

      // store plugin info
      _info.name = class_info.name();
      _info.vendor = class_info.vendor();
      _info.version = class_info.version();
      _info.category = class_info.subCategoriesString();

      _log.inf("found audio effect plugin", redlog::field("name", _info.name),
               redlog::field("vendor", _info.vendor),
               redlog::field("version", _info.version));

      // create plugin provider
      _plugin_provider = owned(new PlugProvider(factory, class_info, true));
      if (!_plugin_provider) {
        return Result<bool>("Failed to create plugin provider");
      }

      // initialize the plugin provider
      if (!_plugin_provider->initialize()) {
        return Result<bool>("Failed to initialize plugin provider");
      }

      // get component interfaces
      _component = _plugin_provider->getComponentPtr();
      if (!_component) {
        return Result<bool>("Failed to get plugin component");
      }

      _audio_processor = FUnknownPtr<IAudioProcessor>(_component);
      if (!_audio_processor) {
        return Result<bool>("Plugin does not support audio processing");
      }

      _edit_controller = _plugin_provider->getControllerPtr();
      _info.has_editor = (_edit_controller != nullptr);

      _log.dbg("plugin interfaces created successfully",
               redlog::field("has_editor", _info.has_editor));

      // setup bus information
      auto bus_result = setup_buses();
      if (!bus_result) {
        return bus_result;
      }

      // configure processing setup before activation
      auto config_result = configure_processing();
      if (!config_result) {
        return config_result;
      }

      // activate component before bus operations
      if (_component->setActive(true) != kResultOk) {
        return Result<bool>("Failed to activate plugin component");
      }
      _is_active = true;

      // activate default buses
      auto bus_activate_result = activate_default_buses();
      if (!bus_activate_result) {
        return bus_activate_result;
      }

      // discover parameters after successful initialization
      if (_edit_controller && !_parameter_manager->discover_parameters()) {
        _log.warn("failed to discover plugin parameters");
      } else if (_edit_controller) {
        _log.trc("discovered parameters",
                 redlog::field("parameter_count",
                               _parameter_manager->parameters().size()));
      }

      _log.inf("plugin loaded successfully", redlog::field("name", _info.name));
      break;
    }
  }

  if (!found_audio_effect) {
    return Result<bool>("No audio effect found in plugin");
  }

  return Result<bool>(true);
}

void Plugin::unload() {
  if (!is_loaded()) {
    return;
  }

  _log.dbg("unloading plugin", redlog::field("name", _info.name));

  stop_processing();

  if (_is_active && _component) {
    _component->setActive(false);
    _is_active = false;
  }

  reset_state();
  _log.dbg("plugin unloaded successfully");
}

Result<bool> Plugin::setup_buses() {
  if (!_component) {
    return Result<bool>("No component available");
  }

  // get bus counts
  int32_t num_audio_inputs = _component->getBusCount(kAudio, kInput);
  int32_t num_audio_outputs = _component->getBusCount(kAudio, kOutput);
  int32_t num_event_inputs = _component->getBusCount(kEvent, kInput);
  int32_t num_event_outputs = _component->getBusCount(kEvent, kOutput);

  _log.trc("bus configuration", redlog::field("audio_inputs", num_audio_inputs),
           redlog::field("audio_outputs", num_audio_outputs),
           redlog::field("event_inputs", num_event_inputs),
           redlog::field("event_outputs", num_event_outputs));

  // setup audio input buses
  _info.audio_inputs.clear();
  _input_arrangements.clear();
  for (int32_t i = 0; i < num_audio_inputs; ++i) {
    BusInfo bus_info;
    if (_component->getBusInfo(kAudio, kInput, i, bus_info) == kResultOk) {
      BusConfiguration config;
      config.name = StringConvert::convert(bus_info.name);
      config.channel_count = bus_info.channelCount;
      config.is_active = false;

      SpeakerArrangement arrangement;
      _audio_processor->getBusArrangement(kInput, i, arrangement);
      config.speaker_arrangement = arrangement;

      _info.audio_inputs.push_back(config);
      _input_arrangements.push_back(arrangement);

      _log.dbg("audio input bus", redlog::field("index", i),
               redlog::field("name", config.name),
               redlog::field("channels", config.channel_count));
    }
  }

  // setup audio output buses
  _info.audio_outputs.clear();
  _output_arrangements.clear();
  for (int32_t i = 0; i < num_audio_outputs; ++i) {
    BusInfo bus_info;
    if (_component->getBusInfo(kAudio, kOutput, i, bus_info) == kResultOk) {
      BusConfiguration config;
      config.name = StringConvert::convert(bus_info.name);
      config.channel_count = bus_info.channelCount;
      config.is_active = false;

      SpeakerArrangement arrangement;
      _audio_processor->getBusArrangement(kOutput, i, arrangement);
      config.speaker_arrangement = arrangement;

      _info.audio_outputs.push_back(config);
      _output_arrangements.push_back(arrangement);

      _log.dbg("audio output bus", redlog::field("index", i),
               redlog::field("name", config.name),
               redlog::field("channels", config.channel_count));
    }
  }

  // setup event buses
  _info.event_inputs.clear();
  for (int32_t i = 0; i < num_event_inputs; ++i) {
    BusInfo bus_info;
    if (_component->getBusInfo(kEvent, kInput, i, bus_info) == kResultOk) {
      BusConfiguration config;
      config.name = StringConvert::convert(bus_info.name);
      config.channel_count = bus_info.channelCount;
      config.is_active = false;
      _info.event_inputs.push_back(config);
    }
  }

  _info.event_outputs.clear();
  for (int32_t i = 0; i < num_event_outputs; ++i) {
    BusInfo bus_info;
    if (_component->getBusInfo(kEvent, kOutput, i, bus_info) == kResultOk) {
      BusConfiguration config;
      config.name = StringConvert::convert(bus_info.name);
      config.channel_count = bus_info.channelCount;
      config.is_active = false;
      _info.event_outputs.push_back(config);
    }
  }

  return Result<bool>(true);
}

Result<bool> Plugin::activate_default_buses() {
  if (!_component) {
    return Result<bool>("No component available");
  }

  _log.trc("activating default buses before component activation");

  // activate first input bus if available
  if (!_info.audio_inputs.empty()) {
    tresult result = _component->activateBus(kAudio, kInput, 0, true);
    if (result != kResultOk) {
      return Result<bool>("Failed to activate default input bus");
    }
    _info.audio_inputs[0].is_active = true;
    _log.trc("activated default input bus");
  }

  // activate first output bus if available
  if (!_info.audio_outputs.empty()) {
    tresult result = _component->activateBus(kAudio, kOutput, 0, true);
    if (result != kResultOk) {
      return Result<bool>("Failed to activate default output bus");
    }
    _info.audio_outputs[0].is_active = true;
    _log.trc("activated default output bus");
  }

  // activate first event input bus if available
  if (!_info.event_inputs.empty()) {
    tresult result = _component->activateBus(kEvent, kInput, 0, true);
    if (result != kResultOk) {
      _log.warn("failed to activate default event input bus");
      // don't fail, event buses are optional
    } else {
      _info.event_inputs[0].is_active = true;
      _log.trc("activated default event input bus");
    }
  }

  _log.trc("default bus activation completed");
  return Result<bool>(true);
}

Result<bool> Plugin::configure_processing() {
  if (!_audio_processor) {
    return Result<bool>("No audio processor available");
  }

  // setup process configuration
  _process_setup.processMode = static_cast<int32>(_config.process_mode);
  _process_setup.symbolicSampleSize = static_cast<int32>(_config.sample_size);
  _process_setup.sampleRate = _config.sample_rate;
  _process_setup.maxSamplesPerBlock = _config.max_block_size;

  _log.trc(
      "configuring audio processing",
      redlog::field("sample_rate", _config.sample_rate),
      redlog::field("block_size", _config.max_block_size),
      redlog::field("sample_size", static_cast<int>(_config.sample_size)),
      redlog::field("process_mode", static_cast<int>(_config.process_mode)));

  // set bus arrangements
  tresult result = _audio_processor->setBusArrangements(
      _input_arrangements.empty() ? nullptr : _input_arrangements.data(),
      static_cast<int32>(_input_arrangements.size()),
      _output_arrangements.empty() ? nullptr : _output_arrangements.data(),
      static_cast<int32>(_output_arrangements.size()));

  if (result != kResultOk) {
    return Result<bool>("Failed to set bus arrangements");
  }

  // setup processing
  result = _audio_processor->setupProcessing(_process_setup);
  if (result != kResultOk) {
    return Result<bool>("Failed to setup audio processing");
  }

  // prepare process data
  _process_data.prepare(*_component, _config.max_block_size,
                        static_cast<int32>(_config.sample_size));
  _process_data.processContext = &_process_context;

  // setup event lists
  if (!_info.event_inputs.empty()) {
    _log.trc("allocating input event lists",
             redlog::field("event_input_count", _info.event_inputs.size()));
    _input_events = std::make_unique<EventList[]>(_info.event_inputs.size());
    _process_data.inputEvents = _input_events.get();
    _log.trc("input event lists allocated successfully");
  } else {
    _log.trc("no event inputs detected - skipping event list allocation");
  }

  if (!_info.event_outputs.empty()) {
    _log.trc("allocating output event lists",
             redlog::field("event_output_count", _info.event_outputs.size()));
    _output_events = std::make_unique<EventList[]>(_info.event_outputs.size());
    _process_data.outputEvents = _output_events.get();
  }

  // get parameter count
  if (_edit_controller) {
    _info.parameter_count = _edit_controller->getParameterCount();
    _log.trc("controller information",
             redlog::field("parameter_count", _info.parameter_count));
  }

  _log.dbg("audio processing configured successfully");
  return Result<bool>(true);
}

Result<bool> Plugin::prepare_processing() {
  if (!is_loaded()) {
    return Result<bool>("Plugin not loaded");
  }

  if (_is_processing) {
    return Result<bool>(true);
  }

  // default process context setup
  util::setup_process_context(_process_context, _config.sample_rate);

  _log.dbg("processing prepared");
  return Result<bool>(true);
}

Result<bool> Plugin::refresh_audio_buffers() {
  if (!is_loaded()) {
    return Result<bool>("Plugin not loaded");
  }

  _log.trc("refreshing audio buffers after bus activation");

  // re-setup processing with activated bus configuration
  tresult result = _audio_processor->setupProcessing(_process_setup);
  if (result != kResultOk) {
    _log.error("failed to re-setup processing after bus activation",
               redlog::field("result", static_cast<int>(result)));
    return Result<bool>("Failed to re-setup processing after bus activation");
  }

  // re-prepare process data with current bus configuration
  // this ensures buffers are allocated for active buses
  _process_data.prepare(*_component, _config.max_block_size,
                        static_cast<int32>(_config.sample_size));

  _log.trc("audio buffers refreshed successfully");
  return Result<bool>(true);
}

Result<bool> Plugin::start_processing() {
  auto prepare_result = prepare_processing();
  if (!prepare_result) {
    return prepare_result;
  }

  // Follow Steinberg's official VST3 SDK pattern (audioclient.cpp lines
  // 348-378)
  _log.trc("stopping any existing processing before restart");
  if (_is_processing && _audio_processor) {
    _audio_processor->setProcessing(false);
    _is_processing = false;
  }
  if (_is_active && _component) {
    _component->setActive(false);
    _is_active = false;
  }

  // Setup processing configuration (Steinberg's exact approach)
  ProcessSetup setup{static_cast<int32>(_config.process_mode),
                     static_cast<int32>(_config.sample_size),
                     _config.max_block_size,
                     static_cast<SampleRate>(_config.sample_rate)};

  _log.trc("calling setupProcessing with Steinberg's pattern");
  if (_audio_processor->setupProcessing(setup) != kResultOk) {
    return Result<bool>("Failed to setup processing");
  }

  // Activate component (always check this return value)
  _log.trc("activating component");
  if (_component->setActive(true) != kResultOk) {
    return Result<bool>("Failed to activate component");
  }
  _is_active = true;

  // Prepare process data AFTER component is active (Steinberg's pattern)
  _log.trc("preparing process data after activation");
  _process_data.prepare(*_component, _config.max_block_size,
                        static_cast<int32>(_config.sample_size));

  // Re-setup event lists after prepare (prepare() may clear
  // inputEvents/outputEvents) Force re-allocation of event lists if needed
  if (!_info.event_inputs.empty()) {
    if (!_input_events) {
      _log.trc("re-allocating input event lists after prepare",
               redlog::field("event_input_count", _info.event_inputs.size()));
      _input_events = std::make_unique<EventList[]>(_info.event_inputs.size());
    }
    _process_data.inputEvents = _input_events.get();
    _log.trc("restored input event list after prepare");
  }
  if (!_info.event_outputs.empty()) {
    if (!_output_events) {
      _log.trc("re-allocating output event lists after prepare",
               redlog::field("event_output_count", _info.event_outputs.size()));
      _output_events =
          std::make_unique<EventList[]>(_info.event_outputs.size());
    }
    _process_data.outputEvents = _output_events.get();
    _log.trc("restored output event list after prepare");
  }

  // Start processing - IGNORE RETURN VALUE like Steinberg's official examples
  // do! From audioclient.cpp: processor->setProcessing(true); // != kResultOk
  // (commented out error check)
  _log.trc("calling setProcessing(true) - ignoring return value per VST3 SDK "
           "pattern");
  _audio_processor->setProcessing(true); // Deliberately ignore return value

  _is_processing = true; // Assume success like Steinberg does
  _log.dbg("processing started using Steinberg's pattern",
           redlog::field("_is_processing", _is_processing));
  return Result<bool>(true);
}

void Plugin::stop_processing() {
  if (!_is_processing) {
    return;
  }

  if (_audio_processor) {
    _audio_processor->setProcessing(false);
  }

  _is_processing = false;
  _log.dbg("processing stopped");
}

Result<bool> Plugin::process(int32_t num_samples) {
  if (!_is_processing) {
    return Result<bool>("Processing not started");
  }

  if (num_samples > _config.max_block_size) {
    return Result<bool>("Number of samples exceeds maximum block size");
  }

  _process_data.numSamples = num_samples;

  tresult result = _audio_processor->process(_process_data);
  if (result != kResultOk) {
    return Result<bool>("Audio processing failed");
  }

  return Result<bool>(true);
}

Result<bool> Plugin::set_bus_active(MediaType type, BusDirection direction,
                                    int32_t index, bool active) {
  if (!_component) {
    return Result<bool>("No component available");
  }

  tresult result = _component->activateBus(
      static_cast<Steinberg::Vst::MediaType>(type),
      static_cast<Steinberg::Vst::BusDirection>(direction), index, active);

  if (result != kResultOk) {
    return Result<bool>("Failed to set bus active state");
  }

  // update our bus info
  std::vector<BusConfiguration>* bus_configs = nullptr;
  if (type == MediaType::Audio) {
    bus_configs = (direction == BusDirection::Input) ? &_info.audio_inputs
                                                     : &_info.audio_outputs;
  } else {
    bus_configs = (direction == BusDirection::Input) ? &_info.event_inputs
                                                     : &_info.event_outputs;
  }

  if (bus_configs && index >= 0 &&
      index < static_cast<int32_t>(bus_configs->size())) {
    (*bus_configs)[index].is_active = active;
  }

  _log.trc("bus activation changed",
           redlog::field("type", static_cast<int>(type)),
           redlog::field("direction", static_cast<int>(direction)),
           redlog::field("index", index), redlog::field("active", active));

  return Result<bool>(true);
}

int32_t Plugin::bus_count(MediaType type, BusDirection direction) const {
  if (!_component) {
    return 0;
  }

  return _component->getBusCount(
      static_cast<Steinberg::Vst::MediaType>(type),
      static_cast<Steinberg::Vst::BusDirection>(direction));
}

std::optional<BusConfiguration>
Plugin::bus_info(MediaType type, BusDirection direction, int32_t index) const {
  const std::vector<BusConfiguration>* bus_configs = nullptr;

  if (type == MediaType::Audio) {
    bus_configs = (direction == BusDirection::Input) ? &_info.audio_inputs
                                                     : &_info.audio_outputs;
  } else {
    bus_configs = (direction == BusDirection::Input) ? &_info.event_inputs
                                                     : &_info.event_outputs;
  }

  if (!bus_configs || index < 0 ||
      index >= static_cast<int32_t>(bus_configs->size())) {
    return std::nullopt;
  }

  return (*bus_configs)[index];
}

Sample32* Plugin::get_audio_buffer_32(BusDirection direction, int32_t bus_index,
                                      int32_t channel_index) const {
  if (direction == BusDirection::Input && _process_data.inputs) {
    return _process_data.inputs[bus_index].channelBuffers32[channel_index];
  } else if (direction == BusDirection::Output && _process_data.outputs) {
    return _process_data.outputs[bus_index].channelBuffers32[channel_index];
  }
  return nullptr;
}

Sample64* Plugin::get_audio_buffer_64(BusDirection direction, int32_t bus_index,
                                      int32_t channel_index) const {
  if (direction == BusDirection::Input && _process_data.inputs) {
    return _process_data.inputs[bus_index].channelBuffers64[channel_index];
  } else if (direction == BusDirection::Output && _process_data.outputs) {
    return _process_data.outputs[bus_index].channelBuffers64[channel_index];
  }
  return nullptr;
}

EventList* Plugin::get_event_list(BusDirection direction,
                                  int32_t bus_index) const {
  if (direction == BusDirection::Input && _input_events) {
    // add bounds checking for event input buses
    if (bus_index >= 0 &&
        bus_index < static_cast<int32_t>(_info.event_inputs.size())) {
      _log.dbg("returning event input list",
               redlog::field("bus_index", bus_index),
               redlog::field("total_event_inputs", _info.event_inputs.size()));
      return &_input_events[bus_index];
    } else {
      _log.warn("event input bus index out of bounds",
                redlog::field("requested_index", bus_index),
                redlog::field("total_event_inputs", _info.event_inputs.size()));
      return nullptr;
    }
  } else if (direction == BusDirection::Output && _output_events) {
    // add bounds checking for event output buses
    if (bus_index >= 0 &&
        bus_index < static_cast<int32_t>(_info.event_outputs.size())) {
      _log.dbg(
          "returning event output list", redlog::field("bus_index", bus_index),
          redlog::field("total_event_outputs", _info.event_outputs.size()));
      return &_output_events[bus_index];
    } else {
      _log.warn(
          "event output bus index out of bounds",
          redlog::field("requested_index", bus_index),
          redlog::field("total_event_outputs", _info.event_outputs.size()));
      return nullptr;
    }
  }

  // log detailed reason for returning nullptr
  if (direction == BusDirection::Input) {
    if (!_input_events) {
      _log.warn("no event input list available - _input_events is null",
                redlog::field("bus_index", bus_index),
                redlog::field("event_inputs_count", _info.event_inputs.size()));
    }
  } else {
    if (!_output_events) {
      _log.warn(
          "no event output list available - _output_events is null",
          redlog::field("bus_index", bus_index),
          redlog::field("event_outputs_count", _info.event_outputs.size()));
    }
  }

  return nullptr;
}

ParameterChanges* Plugin::get_parameter_changes(BusDirection direction) const {
  if (direction == BusDirection::Input) {
    return static_cast<ParameterChanges*>(_process_data.inputParameterChanges);
  } else if (direction == BusDirection::Output) {
    return static_cast<ParameterChanges*>(_process_data.outputParameterChanges);
  }
  return nullptr;
}

Result<std::unique_ptr<GuiWindow>> Plugin::create_editor_window() {
  if (!has_editor()) {
    return Result<std::unique_ptr<GuiWindow>>("Plugin does not have an editor");
  }

  auto window = std::make_unique<GuiWindow>(*this, _log);
  auto create_result = window->create();
  if (!create_result) {
    return Result<std::unique_ptr<GuiWindow>>(create_result.error());
  }

  return Result<std::unique_ptr<GuiWindow>>(std::move(window));
}

ParameterManager& Plugin::parameters() { return *_parameter_manager; }

const ParameterManager& Plugin::parameters() const {
  return *_parameter_manager;
}

void Plugin::reset_state() {
  _input_events.reset();
  _output_events.reset();
  _process_data.unprepare();

  _edit_controller = nullptr;
  _audio_processor = nullptr;
  _component = nullptr;
  _plugin_provider = nullptr;
  _module = nullptr;

  _input_arrangements.clear();
  _output_arrangements.clear();

  _info = {};
  _is_active = false;
  _is_processing = false;
}

// guiplugframe implementation

using namespace Steinberg;

tresult PLUGIN_API GuiPlugFrame::resizeView(IPlugView* view,
                                            ViewRect* newSize) {
  if (!_window || !view || !newSize) {
    return kInvalidArgument;
  }
  return _window->handle_plugin_resize(view, newSize);
}

tresult PLUGIN_API GuiPlugFrame::queryInterface(const TUID _iid, void** obj) {
  QUERY_INTERFACE(_iid, obj, FUnknown::iid, IPlugFrame)
  QUERY_INTERFACE(_iid, obj, IPlugFrame::iid, IPlugFrame)
  return kNoInterface;
}

// guiwindow implementation

GuiWindow::GuiWindow(Plugin& plugin, const redlog::logger& logger)
    : _log(logger.with_name("gui")), _plugin(plugin), _plug_frame(this) {
  _log.trc("gui window instance created");
}

GuiWindow::~GuiWindow() {
  destroy();
  _log.trc("gui window instance destroyed");
}

Result<bool> GuiWindow::create() {
  if (is_open()) {
    return Result<bool>("Window already open");
  }

  if (!_plugin.has_editor()) {
    return Result<bool>("Plugin does not have an editor");
  }

  _log.dbg("creating plugin editor window");

  // create plugin view
  _plugin_view = _plugin._edit_controller->createView(ViewType::kEditor);
  if (!_plugin_view) {
    _log.warn("plugin does not provide an editor view (headless plugin)");
    return Result<bool>(true); // Not an error, just headless
  }

  // get view size
  ViewRect view_rect;
  if (_plugin_view->getSize(&view_rect) != kResultOk) {
    return Result<bool>("Failed to get editor view size");
  }

  int logical_width = view_rect.getWidth();
  int logical_height = view_rect.getHeight();

  _log.dbg("plugin view size", redlog::field("logical_width", logical_width),
           redlog::field("logical_height", logical_height),
           redlog::field("bounds", std::to_string(view_rect.left) + " " +
                                       std::to_string(view_rect.top) + " " +
                                       std::to_string(view_rect.right) + " " +
                                       std::to_string(view_rect.bottom)));

  // create sdl window with vst3-compatible flags
  Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

#if SDL_VERSION_ATLEAST(2, 0, 1)
  window_flags |= SDL_WINDOW_ALLOW_HIGHDPI; // enable high-dpi support
#endif

  std::string title = _plugin.name() + " - " + _plugin.vendor() + " - vstshill";
  _window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, logical_width,
                             logical_height, window_flags);

  if (!_window) {
    return Result<bool>("Failed to create SDL window: " +
                        std::string(SDL_GetError()));
  }

  // log window creation details
  int actual_width, actual_height;
  SDL_GetWindowSize(_window, &actual_width, &actual_height);

  _log.dbg("sdl window created",
           redlog::field("requested_size", std::to_string(logical_width) + "x" +
                                               std::to_string(logical_height)),
           redlog::field("actual_size", std::to_string(actual_width) + "x" +
                                            std::to_string(actual_height)));

  // attach plugin view to native window
  auto attach_result = attach_plugin_view();
  if (!attach_result) {
    SDL_DestroyWindow(_window);
    _window = nullptr;
    return attach_result;
  }

  // setup content scaling after attaching the view
  auto scaling_result = setup_content_scaling();
  if (!scaling_result) {
    _log.warn("content scaling setup failed",
              redlog::field("error", scaling_result.error()));
    // continue anyway, scaling is optional
  }

  // add to active windows for event processing
  _active_windows.push_back(this);

  _log.inf("editor window created successfully",
           redlog::field("plugin", _plugin.name()));
  return Result<bool>(true);
}

void GuiWindow::destroy() {
  if (!is_open()) {
    return;
  }

  _log.dbg("destroying editor window");

  // remove from active windows
  auto it = std::find(_active_windows.begin(), _active_windows.end(), this);
  if (it != _active_windows.end()) {
    _active_windows.erase(it);
  }

  // detach and destroy plugin view
  if (_plugin_view) {
    _plugin_view->setFrame(nullptr);
    _plugin_view->removed();
    _plugin_view = nullptr;
  }

  // clean up native view
  if (_native_view) {
    platform::GuiPlatform::cleanup_native_view(_native_view);
    _native_view = nullptr;
  }

  // destroy sdl window
  if (_window) {
    SDL_DestroyWindow(_window);
    _window = nullptr;
  }

  _log.dbg("editor window destroyed");
}

Result<bool> GuiWindow::attach_plugin_view() {
  if (!_window || !_plugin_view) {
    return Result<bool>("Window or plugin view not available");
  }

  // extract platform-specific native view
  void* native_view = platform::GuiPlatform::extract_native_view(_window);
  if (!native_view) {
    return Result<bool>("Failed to extract native view from SDL window");
  }

  // get the platform type string
  const char* platform_type = platform::GuiPlatform::get_platform_type();

  // verify platform compatibility
  if (_plugin_view->isPlatformTypeSupported(platform_type) != kResultTrue) {
    platform::GuiPlatform::cleanup_native_view(native_view);
    return Result<bool>("Plugin editor does not support this platform type");
  }

  // set iplugframe before attaching
  _plugin_view->setFrame(&_plug_frame);

  // attach plugin view to native window
  tresult result = _plugin_view->attached(native_view, platform_type);

  if (result != kResultOk) {
    platform::GuiPlatform::cleanup_native_view(native_view);
    return Result<bool>("Failed to attach plugin view to native window");
  }

  _log.dbg("plugin view attached successfully",
           redlog::field("platform_type", platform_type));

  // store native view for cleanup
  _native_view = native_view;

  return Result<bool>(true);
}

Result<bool> GuiWindow::setup_content_scaling() {
  if (!_plugin_view) {
    return Result<bool>("No plugin view available for content scaling");
  }

  // get the current plugin view size
  ViewRect current_rect;
  if (_plugin_view->getSize(&current_rect) != kResultOk) {
    return Result<bool>("Failed to get plugin view size for scaling");
  }

  int current_width = current_rect.getWidth();
  int current_height = current_rect.getHeight();

  // use configured maximum window size
  const int max_width = gui::max_window_width;
  const int max_height = gui::max_window_height;

  // calculate if we need to scale down
  bool needs_scaling =
      (current_width > max_width || current_height > max_height);

  if (!needs_scaling) {
    _log.dbg("plugin size is acceptable, no scaling needed",
             redlog::field("current_size", std::to_string(current_width) + "x" +
                                               std::to_string(current_height)));
    return Result<bool>(true);
  }

  // calculate the scale factor to fit within bounds
  float width_scale = static_cast<float>(max_width) / current_width;
  float height_scale = static_cast<float>(max_height) / current_height;
  float content_scale = std::min(width_scale, height_scale);

  _log.dbg("plugin size exceeds maximum, attempting to scale down",
           redlog::field("current_size", std::to_string(current_width) + "x" +
                                             std::to_string(current_height)),
           redlog::field("max_size", std::to_string(max_width) + "x" +
                                         std::to_string(max_height)),
           redlog::field("scale_factor", content_scale));

  // try vst3 content scaling first
  FUnknownPtr<IPlugViewContentScaleSupport> scale_support(_plugin_view);
  if (scale_support) {
    tresult scale_result = scale_support->setContentScaleFactor(content_scale);
    _log.dbg("attempting vst3 content scaling",
             redlog::field("scale_result",
                           scale_result == kResultOk ? "success" : "failed"),
             redlog::field("content_scale", content_scale));

    if (scale_result == kResultOk) {
      _log.inf("plugin successfully scaled using vst3 content scaling",
               redlog::field("scale_factor", content_scale));
      // no need to resize window - we created it at the right size already
      return Result<bool>(true);
    }
  }

  // fallback: force resize with onsize
  _log.dbg("vst3 content scaling not supported, attempting forced resize");

  int target_width = static_cast<int>(current_width * content_scale);
  int target_height = static_cast<int>(current_height * content_scale);

  // ensure we don't go below minimums
  target_width = std::max(target_width, gui::min_window_width);
  target_height = std::max(target_height, gui::min_window_height);

  ViewRect forced_rect;
  forced_rect.left = 0;
  forced_rect.top = 0;
  forced_rect.right = target_width;
  forced_rect.bottom = target_height;

  tresult resize_result = _plugin_view->onSize(&forced_rect);
  _log.dbg("forcing plugin resize with onSize",
           redlog::field("target_size", std::to_string(target_width) + "x" +
                                            std::to_string(target_height)),
           redlog::field("resize_result",
                         resize_result == kResultOk ? "success" : "failed"));

  if (resize_result == kResultOk) {
    // no need to resize window - we created it at the right size already
    _log.inf("plugin successfully resized using forced onSize method",
             redlog::field("target_size", std::to_string(target_width) + "x" +
                                              std::to_string(target_height)));
    return Result<bool>(true);
  }

  // last resort: partial success
  _log.warn("plugin rejected forced resize, but window size is correct");

  return Result<bool>("Plugin scaling partially successful (window resized but "
                      "plugin may not scale properly)");
}

void GuiWindow::process_events() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    handle_window_event(event);
  }
}

void GuiWindow::handle_window_event(const SDL_Event& event) {
  if (event.type == SDL_WINDOWEVENT &&
      event.window.event == SDL_WINDOWEVENT_CLOSE) {
    SDL_Window* window = SDL_GetWindowFromID(event.window.windowID);

    // find the corresponding guiwindow instance
    for (auto* gui_window : _active_windows) {
      if (gui_window->_window == window) {
        gui_window->destroy();
        break;
      }
    }
  } else if (event.type == SDL_KEYDOWN) {
    bool should_close = false;

    if (event.key.keysym.sym == SDLK_ESCAPE) {
      should_close = true;
    } else if (event.key.keysym.sym == SDLK_q &&
               (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
      should_close = true;
    }

    if (should_close) {
      SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);

      // find the corresponding guiwindow instance and close
      for (auto* gui_window : _active_windows) {
        if (gui_window->_window == window) {
          gui_window->destroy();
          break;
        }
      }
    }
  }
}

std::pair<int, int> GuiWindow::size() const {
  if (!_window) {
    return {0, 0};
  }

  int width, height;
  SDL_GetWindowSize(_window, &width, &height);
  return {width, height};
}

void GuiWindow::set_title(const std::string& title) {
  if (_window) {
    SDL_SetWindowTitle(_window, title.c_str());
  }
}

tresult GuiWindow::handle_plugin_resize(IPlugView* view, ViewRect* newSize) {
  if (!_window || !view || !newSize || view != _plugin_view) {
    return kInvalidArgument;
  }

  if (_resize_recursion_guard) {
    return kResultFalse;
  }

  _resize_recursion_guard = true;

  // get current size
  ViewRect current_rect;
  if (_plugin_view->getSize(&current_rect) != kResultTrue) {
    _resize_recursion_guard = false;
    return kInternalError;
  }

  // check if size actually changed
  if (current_rect.left == newSize->left && current_rect.top == newSize->top &&
      current_rect.right == newSize->right &&
      current_rect.bottom == newSize->bottom) {
    _resize_recursion_guard = false;
    return kResultTrue;
  }

  // resize sdl window
  int new_width = newSize->right - newSize->left;
  int new_height = newSize->bottom - newSize->top;

  _log.dbg("plugin requested resize",
           redlog::field("current_size",
                         std::to_string(current_rect.getWidth()) + "x" +
                             std::to_string(current_rect.getHeight())),
           redlog::field("new_size", std::to_string(new_width) + "x" +
                                         std::to_string(new_height)));

  SDL_SetWindowSize(_window, new_width, new_height);

  // update plugin view if final size differs
  ViewRect final_rect;
  if (_plugin_view->getSize(&final_rect) == kResultTrue) {
    if (final_rect.left != newSize->left || final_rect.top != newSize->top ||
        final_rect.right != newSize->right ||
        final_rect.bottom != newSize->bottom) {
      _plugin_view->onSize(newSize);
    }
  }

  _resize_recursion_guard = false;
  return kResultTrue;
}

// audio utility functions

namespace util {

Result<PluginInfo> scan_plugin(const std::string& plugin_path) {
  Plugin plugin(redlog::get_logger("vstk::scanner"));
  auto load_result = plugin.load(plugin_path);
  if (!load_result) {
    return Result<PluginInfo>(load_result.error());
  }

  return Result<PluginInfo>(plugin.info());
}

void setup_process_context(ProcessContext& context, double sample_rate,
                           int64_t sample_position, double tempo,
                           int32_t time_sig_numerator,
                           int32_t time_sig_denominator) {
  std::memset(&context, 0, sizeof(ProcessContext));

  // Essential state flags for synthesizer compatibility (VST3 SDK pattern)
  context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid |
                  ProcessContext::kTimeSigValid |
                  ProcessContext::kProjectTimeMusicValid |
                  ProcessContext::kContTimeValid;

  context.sampleRate = sample_rate;
  context.projectTimeSamples = sample_position;
  context.systemTime = 0;
  context.continousTimeSamples = sample_position; // Critical for synthesizers

  // Musical timing calculations (VST3 SDK pattern)
  double samples_per_quarter_note = 60.0 * sample_rate / tempo;
  context.projectTimeMusic =
      static_cast<double>(sample_position) / samples_per_quarter_note;

  // Bar position for 4/4 time (VST3 SDK standard)
  double quarter_notes_per_bar =
      time_sig_numerator * (4.0 / time_sig_denominator);
  context.barPositionMusic =
      fmod(context.projectTimeMusic, quarter_notes_per_bar);

  context.cycleStartMusic = 0.0;
  context.cycleEndMusic = 0.0;
  context.tempo = tempo;
  context.timeSigNumerator = time_sig_numerator;
  context.timeSigDenominator = time_sig_denominator;
  context.chord = {0}; // no chord info
  context.smpteOffsetSubframes = 0;
  context.frameRate = {};
}

void update_process_context(ProcessContext& context, int32_t block_size) {
  // Update continuous time samples (VST3 SDK pattern)
  context.continousTimeSamples += block_size;
  context.projectTimeSamples += block_size;

  // Update musical time position (VST3 SDK calculation)
  double samples_per_quarter_note = 60.0 * context.sampleRate / context.tempo;
  double quarter_notes_this_block =
      static_cast<double>(block_size) / samples_per_quarter_note;
  context.projectTimeMusic += quarter_notes_this_block;

  // Update bar position for 4/4 time (VST3 SDK pattern)
  double quarter_notes_per_bar =
      context.timeSigNumerator * (4.0 / context.timeSigDenominator);
  context.barPositionMusic =
      fmod(context.projectTimeMusic, quarter_notes_per_bar);
}

void interleave_audio(const std::vector<Sample32*>& channels,
                      Sample32* interleaved, int32_t num_samples) {
  const int32_t num_channels = static_cast<int32_t>(channels.size());
  for (int32_t sample = 0; sample < num_samples; ++sample) {
    for (int32_t channel = 0; channel < num_channels; ++channel) {
      interleaved[sample * num_channels + channel] = channels[channel][sample];
    }
  }
}

void deinterleave_audio(const Sample32* interleaved,
                        const std::vector<Sample32*>& channels,
                        int32_t num_samples) {
  const int32_t num_channels = static_cast<int32_t>(channels.size());
  for (int32_t sample = 0; sample < num_samples; ++sample) {
    for (int32_t channel = 0; channel < num_channels; ++channel) {
      channels[channel][sample] = interleaved[sample * num_channels + channel];
    }
  }
}

} // namespace util

} // namespace vstk