#include "sdl_audio.hpp"
#include "../host/constants.hpp"
#include "../util/audio_utils.hpp"
#include "../util/midi_utils.hpp"
#include <algorithm>
#include <cstring>
#include <redlog/redlog.hpp>

namespace vstk {

namespace {
auto log_audio = redlog::get_logger("audio");
}

// audio processing context for thread-safe communication
struct AudioProcessingContext {
  Plugin* plugin =
      nullptr; // non-owning pointer - plugin lifetime managed by caller
  int sample_rate = 44100;
  int buffer_size = 512;
  int channels = 2;
  bool add_midi_events = false;
  bool midi_events_added = false;
  std::atomic<bool> processing_enabled{false};
};

SDLAudioEngine::SDLAudioEngine()
    : sample_rate_(44100), buffer_size_(512), channels_(2), device_id_(0),
      plugin_(nullptr), is_playing_(false), is_initialized_(false),
      context_(std::make_unique<AudioProcessingContext>()) {}

SDLAudioEngine::~SDLAudioEngine() {
  stop();
  close_audio_device();

  if (is_initialized_.load()) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
}

bool SDLAudioEngine::initialize(int sample_rate, int buffer_size,
                                int channels) {
  log_audio.inf("initializing SDL audio engine",
                redlog::field("sample_rate", sample_rate),
                redlog::field("buffer_size", buffer_size),
                redlog::field("channels", channels));

  // initialize SDL audio subsystem
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
    log_audio.error("failed to initialize SDL audio",
                    redlog::field("error", SDL_GetError()));
    return false;
  }

  sample_rate_ = sample_rate;
  buffer_size_ = buffer_size;
  channels_ = channels;

  // update processing context
  context_->sample_rate = sample_rate_;
  context_->buffer_size = buffer_size_;
  context_->channels = channels_;

  // pre-allocate audio buffers for real-time safety
  temp_buffer_.resize(buffer_size_ * channels_);

  is_initialized_.store(true);
  log_audio.inf("SDL audio engine initialized successfully");
  return true;
}

bool SDLAudioEngine::connect_plugin(Plugin& plugin) {
  if (!is_initialized_.load()) {
    log_audio.error("audio engine not initialized");
    return false;
  }

  log_audio.inf("connecting plugin to audio engine",
                redlog::field("plugin", plugin.name()));

  plugin_ = &plugin;
  context_->plugin = &plugin;

  // configure plugin for real-time processing
  PluginConfig config;
  config.with_process_mode(ProcessMode::Realtime)
      .with_sample_rate(sample_rate_)
      .with_block_size(buffer_size_);

  // prepare plugin for processing
  auto prepare_result = plugin.prepare_processing();
  if (!prepare_result) {
    log_audio.error("failed to prepare plugin for processing",
                    redlog::field("error", prepare_result.error()));
    return false;
  }

  // check if plugin is an instrument (no audio inputs)
  int input_buses = plugin.bus_count(MediaType::Audio, BusDirection::Input);
  context_->add_midi_events = (input_buses == 0);

  log_audio.inf("plugin connected successfully",
                redlog::field("input_buses", input_buses),
                redlog::field("is_instrument", context_->add_midi_events));

  return true;
}

bool SDLAudioEngine::start() {
  if (!is_initialized_.load() || !plugin_) {
    log_audio.error(
        "audio engine not ready - initialize and connect plugin first");
    return false;
  }

  if (is_playing_.load()) {
    log_audio.warn("audio already playing");
    return true;
  }

  // open SDL audio device
  if (!open_audio_device()) {
    return false;
  }

  // start VST3 processing
  auto start_result = plugin_->start_processing();
  if (!start_result) {
    log_audio.error("failed to start VST3 processing",
                    redlog::field("error", start_result.error()));
    close_audio_device();
    return false;
  }

  context_->processing_enabled.store(true);

  // start SDL audio playback
  SDL_PauseAudioDevice(device_id_, 0);
  is_playing_.store(true);

  log_audio.inf("real-time audio playback started");
  return true;
}

void SDLAudioEngine::stop() {
  if (!is_playing_.load()) {
    return;
  }

  log_audio.inf("stopping real-time audio playback");

  // stop SDL audio playback
  if (device_id_ > 0) {
    SDL_PauseAudioDevice(device_id_, 1);
  }

  context_->processing_enabled.store(false);
  is_playing_.store(false);

  // stop VST3 processing
  if (plugin_) {
    plugin_->stop_processing();
  }

  close_audio_device();
  log_audio.inf("real-time audio playback stopped");
}

std::vector<std::string> SDLAudioEngine::get_audio_devices() {
  std::vector<std::string> devices;

  if (!is_initialized_.load()) {
    return devices;
  }

  int num_devices = SDL_GetNumAudioDevices(0); // 0 for playback devices
  for (int i = 0; i < num_devices; ++i) {
    const char* device_name = SDL_GetAudioDeviceName(i, 0);
    if (device_name) {
      devices.emplace_back(device_name);
    }
  }

  return devices;
}

bool SDLAudioEngine::open_audio_device() {
  // configure SDL audio specification
  SDL_AudioSpec desired_spec = {};
  desired_spec.freq = sample_rate_;
  desired_spec.format = AUDIO_F32SYS; // 32-bit float, system endian
  desired_spec.channels = static_cast<Uint8>(channels_);
  desired_spec.samples = static_cast<Uint16>(buffer_size_);
  desired_spec.callback = audio_callback;
  desired_spec.userdata = this;

  // open audio device
  device_id_ = SDL_OpenAudioDevice(nullptr, 0, &desired_spec, &audio_spec_,
                                   SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                       SDL_AUDIO_ALLOW_SAMPLES_CHANGE);

  if (device_id_ == 0) {
    log_audio.error("failed to open SDL audio device",
                    redlog::field("error", SDL_GetError()));
    return false;
  }

  // log actual audio configuration
  log_audio.inf("SDL audio device opened",
                redlog::field("device_id", device_id_),
                redlog::field("actual_freq", audio_spec_.freq),
                redlog::field("actual_samples", audio_spec_.samples),
                redlog::field("actual_channels", audio_spec_.channels));

  // update configuration with actual values
  if (audio_spec_.freq != sample_rate_ || audio_spec_.samples != buffer_size_) {
    log_audio.warn("audio configuration adjusted by SDL",
                   redlog::field("requested_freq", sample_rate_),
                   redlog::field("actual_freq", audio_spec_.freq),
                   redlog::field("requested_samples", buffer_size_),
                   redlog::field("actual_samples", audio_spec_.samples));

    sample_rate_ = audio_spec_.freq;
    buffer_size_ = audio_spec_.samples;
    context_->sample_rate = sample_rate_;
    context_->buffer_size = buffer_size_;

    // resize buffer for new configuration
    temp_buffer_.resize(buffer_size_ * channels_);
  }

  return true;
}

void SDLAudioEngine::close_audio_device() {
  if (device_id_ > 0) {
    SDL_CloseAudioDevice(device_id_);
    device_id_ = 0;
  }
}

// static SDL audio callback
void SDLAudioEngine::audio_callback(void* userdata, Uint8* stream, int len) {
  auto* engine = static_cast<SDLAudioEngine*>(userdata);

  // calculate number of frames
  int frames = len / (sizeof(float) * engine->channels_);

  // process audio block
  engine->process_audio_block(reinterpret_cast<float*>(stream), frames);
}

void SDLAudioEngine::process_audio_block(float* output, int frames) {
  // clear output buffer
  util::clear_audio_buffer(output, frames * channels_);

  // check if processing is enabled and plugin is ready
  if (!context_->processing_enabled.load() || !context_->plugin ||
      !context_->plugin->is_processing()) {
    return; // output remains silent (already cleared)
  }

  Plugin& plugin = *context_->plugin;

  // update process context for VST3 timing
  auto* process_context = plugin.get_process_context();
  if (process_context) {
    util::update_process_context(*process_context,
                                 static_cast<int32_t>(frames));
  }

  // add MIDI events for instrument plugins (once per session)
  if (context_->add_midi_events && !context_->midi_events_added) {
    auto* event_list = plugin.get_event_list(BusDirection::Input, 0);
    if (event_list) {
      auto event = util::create_note_on_event(
          constants::MIDI_MIDDLE_C, constants::MIDI_DEFAULT_VELOCITY,
          constants::MIDI_DEFAULT_CHANNEL,
          constants::MIDI_NOTE_DURATION_SECONDS, context_->sample_rate);
      event_list->addEvent(event);
      context_->midi_events_added = true;
    }
  }

  // process audio through VST3 plugin
  auto process_result = plugin.process(static_cast<int32_t>(frames));
  if (!process_result) {
    return;
  }

  // collect plugin output (convert planar to interleaved)
  auto* output_left = plugin.get_audio_buffer_32(BusDirection::Output, 0, 0);
  auto* output_right = plugin.get_audio_buffer_32(BusDirection::Output, 0, 1);

  if (output_left) {
    if (channels_ == 1) {
      // mono output with bounds checking
      const int safe_frames = std::min(frames, buffer_size_);
      for (int i = 0; i < safe_frames; ++i) {
        output[i] = output_left[i];
      }
    } else if (channels_ == 2) {
      // stereo output - interleave with bounds checking
      const int safe_frames = std::min(frames, buffer_size_);
      for (int i = 0; i < safe_frames; ++i) {
        output[i * 2] = output_left[i];
        output[i * 2 + 1] = output_right ? output_right[i] : output_left[i];
      }
    }
  }
}

} // namespace vstk