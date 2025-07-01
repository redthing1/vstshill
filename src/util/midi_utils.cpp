#include "midi_utils.hpp"
#include "../host/constants.hpp"

using namespace Steinberg::Vst;

namespace vstk::util {

Event create_note_on_event(int pitch, float velocity, int channel, 
                          double note_duration_seconds, double sample_rate,
                          int32_t sample_offset) {
  Event event = {};
  event.busIndex = 0;
  event.sampleOffset = sample_offset;
  event.ppqPosition = 0.0;
  event.flags = Event::kIsLive;
  event.type = Event::kNoteOnEvent;
  event.noteOn.channel = static_cast<Steinberg::int16>(channel);
  event.noteOn.pitch = static_cast<Steinberg::int16>(pitch);
  event.noteOn.tuning = constants::MIDI_DEFAULT_TUNING;
  event.noteOn.velocity = velocity;
  event.noteOn.length = static_cast<Steinberg::int32>(sample_rate * note_duration_seconds);
  event.noteOn.noteId = constants::MIDI_DEFAULT_NOTE_ID;
  
  return event;
}

Event create_note_off_event(int pitch, int channel, int32_t sample_offset) {
  Event event = {};
  event.busIndex = 0;
  event.sampleOffset = sample_offset;
  event.ppqPosition = 0.0;
  event.flags = Event::kIsLive;
  event.type = Event::kNoteOffEvent;
  event.noteOff.channel = static_cast<Steinberg::int16>(channel);
  event.noteOff.pitch = static_cast<Steinberg::int16>(pitch);
  event.noteOff.tuning = constants::MIDI_DEFAULT_TUNING;
  event.noteOff.velocity = 0.0f;
  event.noteOff.noteId = constants::MIDI_DEFAULT_NOTE_ID;
  
  return event;
}

} // namespace vstk::util