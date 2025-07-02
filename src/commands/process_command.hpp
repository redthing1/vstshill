#pragma once

#include "../audio/audio_io.hpp"
#include "../automation/automation.hpp"
#include "../ext/args.hpp"
#include "../host/vstk.hpp"
#include "command.hpp"
#include <string>
#include <vector>

namespace vstk {

class ProcessCommand : public Command {
public:
  explicit ProcessCommand(args::Subparser& parser);
  ~ProcessCommand() = default;

  int execute() override;
  const char* name() const override { return "process"; }
  const char* description() const override {
    return "process audio through vst3 plugin";
  }

private:
  args::Subparser& parser_;

  // argument flags
  args::ValueFlagList<std::string> input_files_;
  args::ValueFlag<std::string> output_file_;
  args::Flag overwrite_;

  args::ValueFlag<double> sample_rate_;
  args::ValueFlag<int> block_size_;
  args::ValueFlag<int> bit_depth_;
  args::ValueFlag<double> duration_;

  args::ValueFlagList<std::string> parameters_;
  args::ValueFlag<std::string> automation_file_;
  args::ValueFlag<std::string> preset_file_;
  args::ValueFlag<std::string> midi_file_;

  args::Flag dry_run_;
  args::Flag quiet_;
  args::Flag progress_;
  args::ValueFlag<int> threads_;

  args::Positional<std::string> plugin_path_;

  // resolved plugin path
  std::string resolved_plugin_path_;

  // validation and setup
  bool validate_arguments();
  void configure_logging();
  int perform_dry_run_validation();

  // audio processing setup
  int setup_audio_input_and_plugin(vstk::MultiAudioReader& audio_reader,
                                   double& sample_rate, size_t& total_frames,
                                   vstk::Plugin& plugin);
  int setup_automation(ParameterAutomation& automation, double sample_rate,
                       size_t total_frames);
  int setup_output_writer(vstk::AudioFileWriter& output_writer,
                          double sample_rate, int output_channels);

  // main processing
  int run_audio_processing_loop(vstk::Plugin& plugin,
                                vstk::MultiAudioReader& audio_reader,
                                vstk::AudioFileWriter& output_writer,
                                const ParameterAutomation& automation,
                                size_t total_frames, double sample_rate);
};

} // namespace vstk