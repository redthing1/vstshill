#include "midi_file.hpp"
#include "../host/constants.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>

using namespace Steinberg::Vst;

namespace vstk::util {

namespace {

constexpr size_t MIDI_HEADER_SIZE = 14;
constexpr size_t TRACK_HEADER_SIZE = 8;
constexpr uint32_t DEFAULT_MICROSECONDS_PER_QUARTER = 500000; // 120 BPM

uint32_t read_variable_length(const std::vector<uint8_t>& data, size_t& pos) {
  uint32_t value = 0;
  for (int i = 0; i < 4 && pos < data.size(); ++i) {
    uint8_t byte = data[pos++];
    value = (value << 7) | (byte & 0x7F);
    if ((byte & 0x80) == 0) {
      break;
    }
  }
  return value;
}

Event create_note_event(bool is_note_on, uint8_t channel, uint8_t pitch,
                        uint8_t velocity) {
  Event event = {};
  event.busIndex = 0;
  event.sampleOffset = 0;
  event.ppqPosition = 0.0;
  event.flags = Event::kIsLive;

  if (is_note_on) {
    event.type = Event::kNoteOnEvent;
    event.noteOn.channel = channel;
    event.noteOn.pitch = pitch;
    event.noteOn.velocity = velocity / 127.0f;
    event.noteOn.tuning = 0.0f;
    event.noteOn.noteId = -1;
    event.noteOn.length = 0;
  } else {
    event.type = Event::kNoteOffEvent;
    event.noteOff.channel = channel;
    event.noteOff.pitch = pitch;
    event.noteOff.velocity = velocity / 127.0f;
    event.noteOff.tuning = 0.0f;
    event.noteOff.noteId = -1;
  }

  return event;
}

} // anonymous namespace

bool MidiFileReader::load_file(const std::string& filepath) {
  events_.clear();
  duration_ = 0.0;
  loaded_ = false;

  std::ifstream file(filepath, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  file.seekg(0, std::ios::end);
  const auto file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (file_size < static_cast<std::streampos>(MIDI_HEADER_SIZE)) {
    return false;
  }

  std::vector<uint8_t> data(static_cast<size_t>(file_size));
  file.read(reinterpret_cast<char*>(data.data()), file_size);

  loaded_ = parse_midi_file(data);
  return loaded_;
}

std::vector<MidiEvent> MidiFileReader::events_in_range(double start_time,
                                                       double end_time) const {
  std::vector<MidiEvent> result;
  result.reserve(events_.size() / 4); // rough estimate

  for (const auto& event : events_) {
    if (event.timestamp_seconds >= start_time &&
        event.timestamp_seconds <= end_time) {
      result.push_back(event);
    }
  }

  return result;
}

bool MidiFileReader::parse_midi_file(const std::vector<uint8_t>& data) {
  if (data.size() < MIDI_HEADER_SIZE ||
      std::memcmp(data.data(), "MThd", 4) != 0) {
    return false;
  }

  // parse header
  const uint32_t header_length =
      (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
  if (header_length != 6) {
    return false;
  }

  const uint16_t format = (data[8] << 8) | data[9];
  const uint16_t tracks = (data[10] << 8) | data[11];
  const uint16_t division = (data[12] << 8) | data[13];

  if (format > 1 || tracks == 0) {
    return false;
  }

  const double ticks_per_quarter = static_cast<double>(division & 0x7FFF);
  double microseconds_per_quarter = DEFAULT_MICROSECONDS_PER_QUARTER;

  size_t pos = MIDI_HEADER_SIZE;

  for (uint16_t track = 0; track < tracks && pos < data.size(); ++track) {
    if (pos + TRACK_HEADER_SIZE > data.size() ||
        std::memcmp(&data[pos], "MTrk", 4) != 0) {
      break;
    }

    const uint32_t track_length = (data[pos + 4] << 24) |
                                  (data[pos + 5] << 16) | (data[pos + 6] << 8) |
                                  data[pos + 7];
    pos += TRACK_HEADER_SIZE;

    const size_t track_end = pos + track_length;
    if (track_end > data.size()) {
      break;
    }

    double current_time = 0.0;
    uint8_t running_status = 0;

    while (pos < track_end) {
      const uint32_t delta_time = read_variable_length(data, pos);
      current_time += delta_time * (microseconds_per_quarter /
                                    (ticks_per_quarter * 1000000.0));

      if (pos >= track_end) {
        break;
      }

      uint8_t status = data[pos];
      if (status < 0x80) {
        status = running_status;
      } else {
        running_status = status;
        pos++;
      }

      if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80) {
        // note events
        if (pos + 1 >= track_end) {
          break;
        }

        const uint8_t pitch = data[pos++];
        const uint8_t velocity = data[pos++];
        const bool is_note_on = (status & 0xF0) == 0x90 && velocity > 0;

        events_.push_back(
            {current_time,
             create_note_event(is_note_on, status & 0x0F, pitch, velocity)});

      } else if ((status & 0xF0) == 0xB0) {
        // control change - skip for now
        pos += 2;
      } else if (status == 0xFF) {
        // meta event
        if (pos >= track_end) {
          break;
        }

        const uint8_t meta_type = data[pos++];
        const uint32_t meta_length = read_variable_length(data, pos);

        if (meta_type == 0x51 && meta_length == 3 && pos + 2 < track_end) {
          // tempo change
          microseconds_per_quarter =
              (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2];
        }

        pos += meta_length;
      } else {
        // skip unknown events
        pos++;
      }
    }
  }

  std::sort(events_.begin(), events_.end(),
            [](const MidiEvent& a, const MidiEvent& b) {
              return a.timestamp_seconds < b.timestamp_seconds;
            });

  if (!events_.empty()) {
    duration_ = events_.back().timestamp_seconds + 1.0;
  }

  return true;
}

std::vector<MidiEvent> create_basic_midi_sequence(double sample_rate,
                                                  double duration) {
  std::vector<MidiEvent> sequence;
  sequence.reserve(12); // pre-allocate for efficiency

  const std::vector<std::pair<int, std::pair<double, double>>> notes = {
      {60, {0.0, 2.0}}, // C major chord
      {64, {0.0, 2.0}}, // E
      {67, {0.0, 2.0}}, // G
      {72, {2.5, 1.5}}, // higher octave
      {76, {4.5, 1.5}}, // E
      {79, {6.5, 1.5}}, // G
  };

  for (const auto& [pitch, timing] : notes) {
    const auto [start_time, note_length] = timing;

    if (start_time >= duration) {
      break;
    }

    // note on
    sequence.push_back(
        {start_time,
         create_note_event(true, 0, static_cast<uint8_t>(pitch), 102)});

    // note off
    const double note_off_time = start_time + note_length;
    if (note_off_time < duration) {
      sequence.push_back(
          {note_off_time,
           create_note_event(false, 0, static_cast<uint8_t>(pitch), 0)});
    }
  }

  return sequence;
}

} // namespace vstk::util