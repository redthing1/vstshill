#pragma once

#include <pluginterfaces/vst/ivstevents.h>

namespace vstk::util {

// create a basic MIDI note-on event for instrument testing
Steinberg::Vst::Event create_note_on_event(
  int pitch = 60,           // middle C
  float velocity = 0.8f,    // 80% velocity  
  int channel = 0,          // MIDI channel 0
  double note_duration_seconds = 8.0,
  double sample_rate = 44100.0,
  int32_t sample_offset = 0
);

// create a MIDI note-off event
Steinberg::Vst::Event create_note_off_event(
  int pitch = 60,
  int channel = 0,
  int32_t sample_offset = 0
);

} // namespace vstk::util