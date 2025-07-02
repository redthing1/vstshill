#pragma once

#include <pluginterfaces/vst/ivstevents.h>
#include <string>
#include <vector>

namespace vstk::util {

struct MidiEvent {
  double timestamp_seconds;
  Steinberg::Vst::Event vst_event;
};

class MidiFileReader {
public:
  MidiFileReader() = default;
  ~MidiFileReader() = default;

  MidiFileReader(const MidiFileReader&) = delete;
  MidiFileReader& operator=(const MidiFileReader&) = delete;

  bool load_file(const std::string& filepath);

  const std::vector<MidiEvent>& events() const { return events_; }
  std::vector<MidiEvent> events_in_range(double start_time,
                                         double end_time) const;

  double duration() const { return duration_; }
  size_t event_count() const { return events_.size(); }
  bool is_loaded() const { return loaded_; }

private:
  std::vector<MidiEvent> events_;
  double duration_ = 0.0;
  bool loaded_ = false;

  bool parse_midi_file(const std::vector<uint8_t>& data);
};

// create a basic test MIDI sequence for instruments
std::vector<MidiEvent> create_basic_midi_sequence(double sample_rate,
                                                  double duration);

} // namespace vstk::util