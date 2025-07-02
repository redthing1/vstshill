#include "process_command.hpp"
#include "../host/constants.hpp"
#include "../host/parameter.hpp"
#include "../util/audio_utils.hpp"
#include "../util/midi_utils.hpp"
#include "../util/string_utils.hpp"
#include "../util/vst_discovery.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <redlog/redlog.hpp>

extern redlog::logger log_main;
extern void apply_verbosity();

namespace vstk {

namespace {

// setup audio input configuration and return total frames to process
size_t setup_audio_input(const std::vector<std::string>& input_files,
                         vstk::MultiAudioReader& audio_reader,
                         double& sample_rate, double requested_sample_rate,
                         double custom_duration, const redlog::logger& log) {
  if (!input_files.empty()) {
    log.inf("loading input files", redlog::field("count", input_files.size()));

    for (const auto& input_file : input_files) {
      if (!audio_reader.add_file(input_file)) {
        log.error("failed to load input file",
                  redlog::field("file", input_file));
        return 0;
      }
      log.trc("loaded input file", redlog::field("file", input_file));
    }

    if (sample_rate == 0.0) {
      sample_rate = audio_reader.sample_rate();
    }
    size_t total_frames = audio_reader.max_frames();

    log.inf("audio input configured", redlog::field("sample_rate", sample_rate),
            redlog::field("total_channels", audio_reader.total_channels()),
            redlog::field("total_frames", total_frames));
    return total_frames;
  } else {
    // instrument mode - no input files
    if (sample_rate == 0.0) {
      sample_rate = vstk::constants::DEFAULT_SAMPLE_RATE;
    }
    double duration =
        custom_duration > 0.0
            ? custom_duration
            : vstk::constants::DEFAULT_INSTRUMENT_DURATION_SECONDS;
    size_t total_frames = static_cast<size_t>(sample_rate * duration);
    log.inf("instrument mode - no audio input",
            redlog::field("sample_rate", sample_rate),
            redlog::field("duration_seconds", duration));
    return total_frames;
  }
}

// parse and apply individual parameter settings
void apply_parameter_settings(vstk::Plugin& plugin,
                              const std::vector<std::string>& parameters,
                              const redlog::logger& log) {
  for (const auto& param_str : parameters) {
    auto colon_pos = param_str.find(':');
    if (colon_pos == std::string::npos) {
      log.warn("invalid parameter format, expected name:value",
               redlog::field("parameter", param_str));
      continue;
    }

    std::string param_name = param_str.substr(0, colon_pos);
    std::string param_value = param_str.substr(colon_pos + 1);

    ParameterValue value(param_value);
    if (!plugin.parameters().set_parameter(param_name, value)) {
      log.warn("failed to set parameter", redlog::field("name", param_name),
               redlog::field("value", param_value));
    } else {
      log.trc("set parameter", redlog::field("name", param_name),
              redlog::field("value", param_value));
    }
  }
}

// add MIDI note-on event for instrument plugins
void add_instrument_midi_event(vstk::Plugin& plugin, double sample_rate,
                               const redlog::logger& log) {
  auto* event_list = plugin.get_event_list(vstk::BusDirection::Input, 0);
  if (event_list) {
    auto event = vstk::util::create_note_on_event(
        vstk::constants::MIDI_MIDDLE_C, vstk::constants::MIDI_DEFAULT_VELOCITY,
        vstk::constants::MIDI_DEFAULT_CHANNEL,
        vstk::constants::MIDI_NOTE_DURATION_SECONDS, sample_rate);

    auto add_result = event_list->addEvent(event);
    log.inf("added MIDI note-on event",
            redlog::field("pitch", event.noteOn.pitch),
            redlog::field("velocity", event.noteOn.velocity),
            redlog::field("add_result", static_cast<int>(add_result)));
  } else {
    log.warn("no event list available for MIDI input");
  }
}

// apply parameter automation for current frame position
void apply_parameter_automation(vstk::Plugin& plugin,
                                const vstk::ParameterAutomation& automation,
                                size_t frame_position,
                                const redlog::logger& log) {
  if (!automation.empty()) {
    auto param_values =
        vstk::Automation::get_parameter_values(automation, frame_position);
    for (const auto& [param_name, value] : param_values) {
      ParameterValue param_value(value);
      plugin.parameters().set_parameter(param_name, param_value);
    }
  }
}

// convert input audio from interleaved to plugin's planar format
void prepare_plugin_input_audio(vstk::Plugin& plugin,
                                const vstk::MultiAudioReader& audio_reader,
                                const float* input_buffer,
                                size_t frames_to_process, int output_channels) {
  auto* input_left =
      plugin.get_audio_buffer_32(vstk::BusDirection::Input, 0, 0);
  auto* input_right =
      plugin.get_audio_buffer_32(vstk::BusDirection::Input, 0, 1);

  if (input_left && (output_channels == 1 || input_right)) {
    int input_channels = std::min(audio_reader.total_channels(), 2);

    if (input_channels == 1) {
      // mono input - copy to left channel
      for (size_t i = 0; i < frames_to_process; ++i) {
        input_left[i] = input_buffer[i];
        if (input_right) {
          input_right[i] = input_buffer[i]; // duplicate to stereo
        }
      }
    } else {
      // stereo input - deinterleave
      for (size_t i = 0; i < frames_to_process; ++i) {
        input_left[i] = input_buffer[i * 2];
        if (input_right) {
          input_right[i] = input_buffer[i * 2 + 1];
        }
      }
    }
  }
}

// convert plugin's planar output to interleaved format
void collect_plugin_output_audio(vstk::Plugin& plugin, float* output_buffer,
                                 size_t frames_to_process, int output_channels,
                                 const redlog::logger& log) {
  auto* output_left =
      plugin.get_audio_buffer_32(vstk::BusDirection::Output, 0, 0);
  auto* output_right =
      plugin.get_audio_buffer_32(vstk::BusDirection::Output, 0, 1);

  if (output_left && (output_channels == 1 || output_right)) {
    if (output_channels == 1) {
      // mono output
      for (size_t i = 0; i < frames_to_process; ++i) {
        output_buffer[i] = output_left[i];
      }
    } else {
      // stereo output - interleave
      for (size_t i = 0; i < frames_to_process; ++i) {
        output_buffer[i * 2] = output_left[i];
        output_buffer[i * 2 + 1] =
            output_right ? output_right[i] : output_left[i];
      }
    }
  } else {
    log.warn("failed to access plugin output buffers");
    // output buffer will remain cleared (zeros)
  }
}

} // anonymous namespace

ProcessCommand::ProcessCommand(args::Subparser& parser)
    : parser_(parser),
      input_files_(parser, "input",
                   "input audio files (can specify multiple for multi-bus)",
                   {'i', "input"}),
      output_file_(parser, "output", "output audio file", {'o', "output"}),
      overwrite_(parser, "overwrite", "overwrite existing output file",
                 {'y', "overwrite"}),
      sample_rate_(parser, "sample-rate",
                   "output sample rate (default: input rate or 44100)",
                   {'r', "sample-rate"}),
      block_size_(parser, "block-size", "processing block size (default: 512)",
                  {'b', "block-size"}),
      bit_depth_(parser, "bit-depth",
                 "output bit depth: 16, 24, 32 (default: 32)",
                 {'d', "bit-depth"}),
      duration_(parser, "duration",
                "duration in seconds for instrument mode (default: 10)",
                {'t', "duration"}),
      parameters_(parser, "param", "parameter settings as name:value",
                  {'p', "param"}),
      automation_file_(parser, "automation", "json automation file",
                       {'a', "automation"}),
      preset_file_(parser, "preset", "load plugin preset file", {"preset"}),
      dry_run_(parser, "dry-run", "validate setup without processing",
               {'n', "dry-run"}),
      quiet_(parser, "quiet", "minimal output (errors only)", {'q', "quiet"}),
      progress_(parser, "progress", "show detailed progress information",
                {"progress"}),
      threads_(parser, "threads", "number of processing threads (experimental)",
               {'j', "threads"}),
      plugin_path_(parser, "plugin_path",
                   "path or name of vst3 plugin to use for processing") {}

int ProcessCommand::execute() {
  apply_verbosity();
  parser_.Parse();

  if (!validate_arguments()) {
    return 1;
  }

  // resolve plugin path (supports both paths and names)
  resolved_plugin_path_ =
      vstk::util::resolve_plugin_path(args::get(plugin_path_));
  if (resolved_plugin_path_.empty()) {
    return 1;
  }

  configure_logging();

  if (dry_run_) {
    return perform_dry_run_validation();
  }

  // setup audio input and plugin
  vstk::MultiAudioReader audio_reader;
  double sample_rate = 0.0;
  size_t total_frames = 0;
  vstk::Plugin plugin(log_main.with_name("processor"));

  int result = setup_audio_input_and_plugin(audio_reader, sample_rate,
                                            total_frames, plugin);
  if (result != 0) {
    return result;
  }

  // setup automation
  ParameterAutomation automation;
  result = setup_automation(automation, sample_rate, total_frames);
  if (result != 0) {
    return result;
  }

  // setup output writer
  vstk::AudioFileWriter output_writer;
  int output_channels = vstk::constants::DEFAULT_OUTPUT_CHANNELS;
  result = setup_output_writer(output_writer, sample_rate, output_channels);
  if (result != 0) {
    return result;
  }

  // run main processing loop
  return run_audio_processing_loop(plugin, audio_reader, output_writer,
                                   automation, total_frames, sample_rate);
}

bool ProcessCommand::validate_arguments() {
  // validate required arguments
  if (!plugin_path_ || !output_file_) {
    log_main.err(
        "plugin path or name and output file required for process command");
    std::cerr << parser_;
    return false;
  }

  // validate bit depth
  if (bit_depth_ &&
      (args::get(bit_depth_) != 16 && args::get(bit_depth_) != 24 &&
       args::get(bit_depth_) != 32)) {
    log_main.error("bit depth must be 16, 24, or 32");
    return false;
  }

  // validate block size
  if (block_size_ &&
      (args::get(block_size_) < 32 || args::get(block_size_) > 8192)) {
    log_main.error("block size must be between 32 and 8192");
    return false;
  }

  // validate duration
  if (duration_ && args::get(duration_) <= 0.0) {
    log_main.error("duration must be positive");
    return false;
  }

  return true;
}

void ProcessCommand::configure_logging() {
  // configure logging based on quiet/progress flags
  if (quiet_ && progress_) {
    log_main.error("cannot use both --quiet and --progress");
    return;
  }

  if (quiet_) {
    redlog::set_level(redlog::level::error);
  } else if (progress_) {
    redlog::set_level(redlog::level::trace);
  }
}

int ProcessCommand::perform_dry_run_validation() {
  log_main.inf("dry run mode - validating setup");

  std::vector<std::string> inputs;
  if (input_files_) {
    inputs = args::get(input_files_);
  }

  std::string automation_path;
  if (automation_file_) {
    automation_path = args::get(automation_file_);
  }

  // plugin existence is validated by resolve_plugin_path()

  // validate input files exist
  for (const auto& input : inputs) {
    if (!std::filesystem::exists(input)) {
      log_main.error("input file does not exist", redlog::field("path", input));
      return 1;
    }
  }

  // validate automation file exists
  if (!automation_path.empty() && !std::filesystem::exists(automation_path)) {
    log_main.error("automation file does not exist",
                   redlog::field("path", automation_path));
    return 1;
  }

  log_main.inf("dry run validation passed - setup is valid");
  return 0;
}

int ProcessCommand::setup_audio_input_and_plugin(
    vstk::MultiAudioReader& audio_reader, double& sample_rate,
    size_t& total_frames, vstk::Plugin& plugin) {
  auto log = log_main.with_name("processor");

  // check if output file exists
  if (!overwrite_ && std::ifstream(args::get(output_file_))) {
    log.error("output file already exists (use --overwrite to replace)",
              redlog::field("output", args::get(output_file_)));
    return 1;
  }

  // setup audio input
  std::vector<std::string> inputs;
  if (input_files_) {
    inputs = args::get(input_files_);
  }

  sample_rate = sample_rate_ ? args::get(sample_rate_) : 0.0;
  total_frames = setup_audio_input(inputs, audio_reader, sample_rate,
                                   sample_rate_ ? args::get(sample_rate_) : 0.0,
                                   duration_ ? args::get(duration_) : 0.0, log);
  if (total_frames == 0) {
    return 1;
  }

  // load plugin
  log.inf("loading plugin", redlog::field("path", resolved_plugin_path_));
  vstk::PluginConfig config;
  config.with_process_mode(vstk::ProcessMode::Offline)
      .with_sample_rate(sample_rate)
      .with_block_size(block_size_ ? args::get(block_size_)
                                   : vstk::constants::DEFAULT_BLOCK_SIZE);

  auto load_result = plugin.load(resolved_plugin_path_, config);
  if (!load_result) {
    log.error("failed to load plugin",
              redlog::field("error", load_result.error()));
    return 1;
  }

  log.inf("plugin loaded successfully", redlog::field("name", plugin.name()));

  // apply parameter settings
  std::vector<std::string> params;
  if (parameters_) {
    params = args::get(parameters_);
  }
  apply_parameter_settings(plugin, params, log);

  return 0;
}

int ProcessCommand::setup_automation(ParameterAutomation& automation,
                                     double sample_rate, size_t total_frames) {
  if (!automation_file_) {
    return 0;
  }

  auto log = log_main.with_name("processor");
  std::string automation_path = args::get(automation_file_);

  log.inf("loading automation", redlog::field("file", automation_path));

  std::ifstream file(automation_path);
  if (!file) {
    log.error("failed to open automation file",
              redlog::field("file", automation_path));
    return 1;
  }

  std::string json_content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

  try {
    automation = vstk::Automation::parse_automation_definition(
        json_content, sample_rate, total_frames);
    log.inf("automation loaded",
            redlog::field("parameter_count", automation.size()));
  } catch (const std::exception& e) {
    log.error("failed to parse automation file",
              redlog::field("error", e.what()));
    return 1;
  }

  return 0;
}

int ProcessCommand::setup_output_writer(vstk::AudioFileWriter& output_writer,
                                        double sample_rate,
                                        int output_channels) {
  auto log = log_main.with_name("processor");

  log.inf("creating output writer",
          redlog::field("file", args::get(output_file_)),
          redlog::field("sample_rate", sample_rate),
          redlog::field("bit_depth", bit_depth_
                                         ? args::get(bit_depth_)
                                         : vstk::constants::DEFAULT_BIT_DEPTH));

  if (!output_writer.open(args::get(output_file_), sample_rate, output_channels,
                          bit_depth_ ? args::get(bit_depth_)
                                     : vstk::constants::DEFAULT_BIT_DEPTH)) {
    log.error("failed to create output file",
              redlog::field("file", args::get(output_file_)));
    return 1;
  }

  return 0;
}

int ProcessCommand::run_audio_processing_loop(
    vstk::Plugin& plugin, vstk::MultiAudioReader& audio_reader,
    vstk::AudioFileWriter& output_writer, const ParameterAutomation& automation,
    size_t total_frames, double sample_rate) {
  auto log = log_main.with_name("processor");

  try {
    // setup vst3 processing
    log.inf("preparing vst3 processing");

    int input_buses =
        plugin.bus_count(vstk::MediaType::Audio, vstk::BusDirection::Input);
    int output_buses =
        plugin.bus_count(vstk::MediaType::Audio, vstk::BusDirection::Output);

    log.inf("plugin bus configuration",
            redlog::field("input_buses", input_buses),
            redlog::field("output_buses", output_buses));

    bool has_input_audio = audio_reader.is_valid();

    // prepare plugin for processing
    auto prepare_result = plugin.prepare_processing();
    if (!prepare_result) {
      log.error("failed to prepare processing",
                redlog::field("error", prepare_result.error()));
      return 1;
    }

    // start processing
    auto start_result = plugin.start_processing();
    if (!start_result) {
      log.error("failed to start processing",
                redlog::field("error", start_result.error()));
      return 1;
    }

    log.inf("vst3 processing started successfully");

    // processing loop
    int block_size = block_size_ ? args::get(block_size_)
                                 : vstk::constants::DEFAULT_BLOCK_SIZE;
    int output_channels = vstk::constants::DEFAULT_OUTPUT_CHANNELS;

    log.inf("starting audio processing",
            redlog::field("block_size", block_size),
            redlog::field("total_frames", total_frames));

    std::vector<float> input_buffer(
        block_size *
        (audio_reader.is_valid() ? audio_reader.total_channels() : 2));
    std::vector<float> output_buffer(block_size * output_channels);

    size_t frames_processed = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (frames_processed < total_frames) {
      size_t frames_to_process = std::min(static_cast<size_t>(block_size),
                                          total_frames - frames_processed);

      // clear buffers
      vstk::util::clear_audio_buffer(input_buffer.data(), input_buffer.size());
      vstk::util::clear_audio_buffer(output_buffer.data(),
                                     output_buffer.size());

      // read input audio if available
      if (audio_reader.is_valid()) {
        size_t frames_read = audio_reader.read_interleaved(input_buffer.data(),
                                                           frames_to_process);
        if (frames_read < frames_to_process) {
          log.trc("reached end of input audio",
                  redlog::field("frames_read", frames_read));
        }
      }

      // update process context for this block
      auto* process_context = plugin.get_process_context();
      if (process_context) {
        vstk::util::update_process_context(
            *process_context, static_cast<int32_t>(frames_to_process));
      }

      // apply automation for current position
      apply_parameter_automation(plugin, automation, frames_processed, log);

      // add basic MIDI events for instruments (when no audio input)
      if (!has_input_audio && frames_processed == 0) {
        add_instrument_midi_event(plugin, sample_rate, log);
      }

      // process audio through plugin
      if (plugin.is_loaded()) {
        // prepare input audio for plugin
        if (has_input_audio && audio_reader.is_valid()) {
          prepare_plugin_input_audio(plugin, audio_reader, input_buffer.data(),
                                     frames_to_process, output_channels);
        }

        // call vst3 processing
        auto process_result =
            plugin.process(static_cast<int32_t>(frames_to_process));
        if (!process_result) {
          log.warn("vst3 processing failed",
                   redlog::field("error", process_result.error()),
                   redlog::field("frame", frames_processed));
        }

        // get processed output
        collect_plugin_output_audio(plugin, output_buffer.data(),
                                    frames_to_process, output_channels, log);
      } else {
        log.warn("plugin not ready for processing");

        // fallback: copy input to output or generate silence
        if (has_input_audio && audio_reader.is_valid()) {
          int channels_to_copy =
              std::min(audio_reader.total_channels(), output_channels);
          std::copy(input_buffer.begin(),
                    input_buffer.begin() + frames_to_process * channels_to_copy,
                    output_buffer.begin());
        }
      }

      // write output
      size_t frames_written =
          output_writer.write(output_buffer.data(), frames_to_process);
      if (frames_written != frames_to_process) {
        log.error("failed to write complete block",
                  redlog::field("expected", frames_to_process),
                  redlog::field("written", frames_written));
        break;
      }

      frames_processed += frames_to_process;

      // progress logging
      if (frames_processed %
              static_cast<size_t>(
                  sample_rate *
                  vstk::constants::PROGRESS_LOG_INTERVAL_SECONDS) ==
          0) {
        double progress = static_cast<double>(frames_processed) /
                          static_cast<double>(total_frames) * 100.0;
        log.inf("processing progress", redlog::field("percent", progress));
      }
    }

    // cleanup vst3 processing
    log.inf("stopping vst3 processing");
    plugin.stop_processing();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    log.inf("processing completed",
            redlog::field("frames_processed", frames_processed),
            redlog::field("duration_ms", duration.count()),
            redlog::field("realtime_factor", (frames_processed / sample_rate) /
                                                 (duration.count() / 1000.0)));

  } catch (const std::exception& e) {
    log.error("processing failed", redlog::field("error", e.what()));
    return 1;
  }

  return 0;
}

} // namespace vstk