#include "audio_utils.hpp"
#include "../host/constants.hpp"
#include <algorithm>
#include <cstring>

namespace vstk::util {

void deinterleave_audio(const float* interleaved, float** planar_channels, 
                       int num_channels, int num_frames) {
  for (int frame = 0; frame < num_frames; ++frame) {
    for (int ch = 0; ch < num_channels; ++ch) {
      planar_channels[ch][frame] = interleaved[frame * num_channels + ch];
    }
  }
}

void interleave_audio(const float* const* planar_channels, float* interleaved,
                     int num_channels, int num_frames) {
  for (int frame = 0; frame < num_frames; ++frame) {
    for (int ch = 0; ch < num_channels; ++ch) {
      interleaved[frame * num_channels + ch] = planar_channels[ch][frame];
    }
  }
}

void clear_audio_buffer(float* buffer, size_t num_samples) {
  std::memset(buffer, 0, num_samples * sizeof(float));
}

void mono_to_stereo(const float* mono_input, float* stereo_output, size_t num_frames) {
  for (size_t i = 0; i < num_frames; ++i) {
    stereo_output[i * constants::STEREO_CHANNELS] = mono_input[i];     // left
    stereo_output[i * constants::STEREO_CHANNELS + 1] = mono_input[i]; // right
  }
}

} // namespace vstk::util