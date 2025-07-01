#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vstk {

// audio file reader using libsndfile
class AudioFileReader {
public:
  AudioFileReader() = default;
  ~AudioFileReader();

  // open audio file for reading
  bool open(const std::string& file_path);

  // close audio file
  void close();

  // read audio samples from file
  size_t read(float* buffer, size_t frames);

  // seek to specific frame
  bool seek(size_t frame);

  // getters
  double sample_rate() const { return sample_rate_; }
  int channels() const { return channels_; }
  size_t total_frames() const { return total_frames_; }
  bool is_open() const { return file_handle_ != nullptr; }

private:
  void* file_handle_ = nullptr; // sndfile handle
  double sample_rate_ = 0.0;
  int channels_ = 0;
  size_t total_frames_ = 0;
};

// audio file writer using libsndfile
class AudioFileWriter {
public:
  AudioFileWriter() = default;
  ~AudioFileWriter();

  // open audio file for writing
  bool open(const std::string& file_path, double sample_rate, int channels,
            int bit_depth = 16);

  // close audio file
  void close();

  // write audio samples to file
  size_t write(const float* buffer, size_t frames);

  bool is_open() const { return file_handle_ != nullptr; }

private:
  void* file_handle_ = nullptr; // sndfile handle
};

// utility for managing multiple audio input files
class MultiAudioReader {
public:
  // add audio file to reader
  bool add_file(const std::string& file_path);

  // read interleaved samples from all files
  size_t read_interleaved(float* buffer, size_t frames);

  // seek all files to specific frame
  bool seek_all(size_t frame);

  // getters
  double sample_rate() const;
  int total_channels() const;
  size_t max_frames() const;
  bool is_valid() const { return !readers_.empty(); }

private:
  std::vector<std::unique_ptr<AudioFileReader>> readers_;
  std::vector<float> temp_buffer_;
};

} // namespace vstk