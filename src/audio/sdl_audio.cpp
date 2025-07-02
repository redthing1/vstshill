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
  std::atomic<bool> processing_enabled{false};
};

SDLAudioEngine::SDLAudioEngine()
    : sample_rate_(44100), buffer_size_(512), channels_(2),
      audio_stream_(nullptr), plugin_(nullptr), is_playing_(false),
      is_initialized_(false),
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
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
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

  int input_buses = plugin.bus_count(MediaType::Audio, BusDirection::Input);

  log_audio.inf("plugin connected successfully",
                redlog::field("input_buses", input_buses),
                redlog::field("is_instrument", input_buses == 0));

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

  // start SDL audio stream playback
  if (!SDL_ResumeAudioStreamDevice(audio_stream_)) {
    log_audio.error("failed to resume audio stream device",
                    redlog::field("error", SDL_GetError()));
    return false;
  }
  is_playing_.store(true);

  log_audio.inf("real-time audio playback started");
  return true;
}

void SDLAudioEngine::stop() {
  if (!is_playing_.load()) {
    return;
  }

  log_audio.inf("stopping real-time audio playback");

  // stop SDL audio stream playback
  if (audio_stream_) {
    SDL_PauseAudioStreamDevice(audio_stream_);
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

  SDL_AudioDeviceID* device_ids = SDL_GetAudioPlaybackDevices(nullptr);
  if (device_ids) {
    for (int i = 0; device_ids[i] != 0; ++i) {
      const char* device_name = SDL_GetAudioDeviceName(device_ids[i]);
      if (device_name) {
        devices.emplace_back(device_name);
      }
    }
    SDL_free(device_ids);
  }

  return devices;
}

bool SDLAudioEngine::open_audio_device() {
  // configure SDL audio specification
  SDL_AudioSpec desired_spec = {};
  desired_spec.freq = sample_rate_;
  desired_spec.format = SDL_AUDIO_F32; // 32-bit float, system endian
  desired_spec.channels = static_cast<Uint8>(channels_);
  // SDL3 removed samples, callback, and userdata from SDL_AudioSpec
  // These are now handled differently in SDL3

  // open audio device stream with callback in SDL3
  audio_stream_ =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                &desired_spec, audio_stream_callback, this);

  if (!audio_stream_) {
    log_audio.error("failed to open SDL audio device stream",
                    redlog::field("error", SDL_GetError()));
    return false;
  }

  // get the actual audio format negotiated by SDL
  if (!SDL_GetAudioStreamFormat(audio_stream_, nullptr, &audio_spec_)) {
    log_audio.error("failed to get audio stream format",
                    redlog::field("error", SDL_GetError()));
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
    return false;
  }

  // audio_stream_ already checked above, continue with success path

  // log actual audio configuration
  log_audio.inf("SDL audio device stream opened",
                redlog::field("actual_freq", audio_spec_.freq),
                redlog::field("actual_format", audio_spec_.format),
                redlog::field("actual_channels", audio_spec_.channels));

  // update configuration with actual values
  if (audio_spec_.freq != sample_rate_) {
    log_audio.warn("audio configuration adjusted by SDL",
                   redlog::field("requested_freq", sample_rate_),
                   redlog::field("actual_freq", audio_spec_.freq));

    sample_rate_ = audio_spec_.freq;
    context_->sample_rate = sample_rate_;
    context_->buffer_size = buffer_size_;

    // resize buffer for new configuration
    temp_buffer_.resize(buffer_size_ * channels_);
  }

  return true;
}

void SDLAudioEngine::close_audio_device() {
  if (audio_stream_) {
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
  }
}

// static SDL3 audio stream callback
void SDLAudioEngine::audio_stream_callback(void* userdata,
                                           SDL_AudioStream* stream,
                                           int additional_amount,
                                           int total_amount) {
  auto* engine = static_cast<SDLAudioEngine*>(userdata);

  // convert bytes to samples
  int samples_needed = additional_amount / (sizeof(float) * engine->channels_);

  if (samples_needed <= 0) {
    return; // stream has enough data
  }

  // process audio in chunks
  while (samples_needed > 0) {
    // use smaller buffer size to avoid excessive buffering
    const int chunk_size = std::min(samples_needed, engine->buffer_size_);
    const int bytes_to_generate =
        chunk_size * engine->channels_ * sizeof(float);

    // generate audio data
    engine->generate_audio_chunk(engine->temp_buffer_.data(), chunk_size);

    // feed data to the audio stream
    if (!SDL_PutAudioStreamData(stream, engine->temp_buffer_.data(),
                                bytes_to_generate)) {
      log_audio.error("failed to put audio stream data",
                      redlog::field("error", SDL_GetError()));
      break;
    }

    samples_needed -= chunk_size;
  }
}

void SDLAudioEngine::generate_audio_chunk(float* output, int frames) {
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