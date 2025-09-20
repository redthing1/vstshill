#pragma once

#include <cstddef>

namespace vstk::util {

// convert interleaved audio data to planar format
// planar_channels should be pre-allocated array of channel pointers
void deinterleave_audio(const float* interleaved, float** planar_channels,
                        int num_channels, int num_frames);

// convert planar audio data to interleaved format
// interleaved buffer should be pre-allocated with size num_channels *
// num_frames
void interleave_audio(const float* const* planar_channels, float* interleaved,
                      int num_channels, int num_frames);

// clear audio buffer efficiently
void clear_audio_buffer(float* buffer, size_t num_samples);

// copy mono audio to stereo by duplicating to both channels
void mono_to_stereo(const float* mono_input, float* stereo_output,
                    size_t num_frames);

} // namespace vstk::util