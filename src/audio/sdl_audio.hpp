#pragma once

#include "../host/vstk.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <memory>
#include <vector>

namespace vstk {

// SDL3-based real-time audio output engine
class SDLAudioEngine {
public:
  SDLAudioEngine();
  ~SDLAudioEngine();

  // initialize SDL audio subsystem
  bool initialize(int sample_rate = 44100, int buffer_size = 512,
                  int channels = 2);

  // connect plugin for real-time processing
  bool connect_plugin(Plugin& plugin);

  // start/stop real-time audio playback
  bool start();
  void stop();

  // check if audio is currently playing
  bool is_playing() const { return is_playing_.load(); }

  // get audio device information
  std::vector<std::string> get_audio_devices();

  // audio configuration
  int sample_rate() const { return sample_rate_; }
  int buffer_size() const { return buffer_size_; }
  int channels() const { return channels_; }

private:
  // SDL3 audio stream callback (static function required by SDL)
  static void audio_stream_callback(void* userdata, SDL_AudioStream* stream,
                                    int additional_amount, int total_amount);

  // instance audio generation implementation
  void generate_audio_chunk(float* output, int frames);

  // initialize SDL audio device
  bool open_audio_device();
  void close_audio_device();

  // audio configuration
  int sample_rate_;
  int buffer_size_;
  int channels_;

  // SDL audio stream (SDL3)
  SDL_AudioStream* audio_stream_;
  SDL_AudioSpec audio_spec_;

  // plugin connection
  Plugin* plugin_;

  // audio processing state
  std::atomic<bool> is_playing_;
  std::atomic<bool> is_initialized_;

  // audio buffers (pre-allocated for real-time safety)
  std::vector<float> temp_buffer_;

  // processing context for VST3 (opaque pointer to internal context)
  std::unique_ptr<struct AudioProcessingContext> context_;
};

} // namespace vstk