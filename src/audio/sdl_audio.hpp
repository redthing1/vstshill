#pragma once

#include "../host/vstk.hpp"
#include <SDL.h>
#include <atomic>
#include <memory>
#include <vector>

namespace vstk {

// SDL2-based real-time audio output engine
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
  // SDL audio callback (static function required by SDL)
  static void audio_callback(void* userdata, Uint8* stream, int len);

  // instance audio callback implementation
  void process_audio_block(float* output, int frames);

  // initialize SDL audio device
  bool open_audio_device();
  void close_audio_device();

  // audio configuration
  int sample_rate_;
  int buffer_size_;
  int channels_;

  // SDL audio device
  SDL_AudioDeviceID device_id_;
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