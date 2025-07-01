#include "audio_io.hpp"
#include <algorithm>
#include <sndfile.h>
#include <stdexcept>

namespace vstk {

// audio file reader implementation

AudioFileReader::~AudioFileReader() { close(); }

bool AudioFileReader::open(const std::string& file_path) {
  close();

  SF_INFO info;
  info.format = 0; // must be set to 0 for read mode

  SNDFILE* sf = sf_open(file_path.c_str(), SFM_READ, &info);
  if (!sf) {
    return false;
  }

  file_handle_ = sf;
  sample_rate_ = info.samplerate;
  channels_ = info.channels;
  total_frames_ = info.frames;

  return true;
}

void AudioFileReader::close() {
  if (file_handle_) {
    sf_close(static_cast<SNDFILE*>(file_handle_));
    file_handle_ = nullptr;
  }
}

size_t AudioFileReader::read(float* buffer, size_t frames) {
  if (!file_handle_) {
    return 0;
  }

  sf_count_t frames_read =
      sf_readf_float(static_cast<SNDFILE*>(file_handle_), buffer, frames);

  return static_cast<size_t>(std::max(sf_count_t(0), frames_read));
}

bool AudioFileReader::seek(size_t frame) {
  if (!file_handle_) {
    return false;
  }

  sf_count_t result =
      sf_seek(static_cast<SNDFILE*>(file_handle_), frame, SEEK_SET);

  return result == static_cast<sf_count_t>(frame);
}

// audio file writer implementation

AudioFileWriter::~AudioFileWriter() { close(); }

bool AudioFileWriter::open(const std::string& file_path, double sample_rate,
                           int channels, int bit_depth) {
  close();

  SF_INFO info;
  info.samplerate = static_cast<int>(sample_rate);
  info.channels = channels;

  // set format based on bit depth
  switch (bit_depth) {
  case 16:
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    break;
  case 24:
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    break;
  case 32:
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    break;
  default:
    return false;
  }

  SNDFILE* sf = sf_open(file_path.c_str(), SFM_WRITE, &info);
  if (!sf) {
    return false;
  }

  file_handle_ = sf;
  return true;
}

void AudioFileWriter::close() {
  if (file_handle_) {
    sf_close(static_cast<SNDFILE*>(file_handle_));
    file_handle_ = nullptr;
  }
}

size_t AudioFileWriter::write(const float* buffer, size_t frames) {
  if (!file_handle_) {
    return 0;
  }

  sf_count_t frames_written =
      sf_writef_float(static_cast<SNDFILE*>(file_handle_), buffer, frames);

  return static_cast<size_t>(std::max(sf_count_t(0), frames_written));
}

// multi audio reader implementation

bool MultiAudioReader::add_file(const std::string& file_path) {
  auto reader = std::make_unique<AudioFileReader>();
  if (!reader->open(file_path)) {
    return false;
  }

  // verify sample rate consistency
  if (!readers_.empty()) {
    if (std::abs(reader->sample_rate() - readers_[0]->sample_rate()) > 1.0) {
      return false;
    }
  }

  readers_.push_back(std::move(reader));
  return true;
}

size_t MultiAudioReader::read_interleaved(float* buffer, size_t frames) {
  if (readers_.empty()) {
    return 0;
  }

  size_t total_channels = this->total_channels();

  // resize temp buffer if needed
  size_t temp_size = frames * total_channels;
  if (temp_buffer_.size() < temp_size) {
    temp_buffer_.resize(temp_size);
  }

  // clear output buffer
  std::fill(buffer, buffer + temp_size, 0.0f);

  size_t min_frames_read = frames;
  size_t channel_offset = 0;

  for (auto& reader : readers_) {
    // read from this file into temp buffer
    size_t file_frames_read = reader->read(temp_buffer_.data(), frames);
    min_frames_read = std::min(min_frames_read, file_frames_read);

    // interleave channels into output buffer
    for (size_t frame = 0; frame < file_frames_read; ++frame) {
      for (int ch = 0; ch < reader->channels(); ++ch) {
        size_t src_idx = frame * reader->channels() + ch;
        size_t dst_idx = frame * total_channels + channel_offset + ch;
        buffer[dst_idx] = temp_buffer_[src_idx];
      }
    }

    channel_offset += reader->channels();
  }

  return min_frames_read;
}

bool MultiAudioReader::seek_all(size_t frame) {
  bool success = true;
  for (auto& reader : readers_) {
    success &= reader->seek(frame);
  }
  return success;
}

double MultiAudioReader::sample_rate() const {
  if (readers_.empty()) {
    return 0.0;
  }
  return readers_[0]->sample_rate();
}

int MultiAudioReader::total_channels() const {
  int total = 0;
  for (const auto& reader : readers_) {
    total += reader->channels();
  }
  return total;
}

size_t MultiAudioReader::max_frames() const {
  size_t max_frames = 0;
  for (const auto& reader : readers_) {
    max_frames = std::max(max_frames, reader->total_frames());
  }
  return max_frames;
}

} // namespace vstk