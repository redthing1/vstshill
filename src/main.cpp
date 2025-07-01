// cross-platform vst3 host application

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <filesystem>

#include "ext/args.hpp"
#include <redlog/redlog.hpp>

#include "host/minimal.hpp"
#include "host/vstk.hpp"
#include "host/parameter.hpp"
#include "host/constants.hpp"
#include "util/vst_discovery.hpp"
#include "util/audio_utils.hpp"
#include "util/midi_utils.hpp"
#include "automation/automation.hpp"
#include "audio/audio_io.hpp"

#include <pluginterfaces/vst/ivstevents.h>

args::Group arguments("arguments");
args::HelpFlag help_flag(arguments, "help", "help", {'h', "help"});
args::CounterFlag verbosity_flag(arguments, "verbosity", "verbosity level",
                                 {'v'});

namespace {
auto log_main = redlog::get_logger("vstshill");
}

void apply_verbosity() {
  // apply verbosity
  int verbosity = args::get(verbosity_flag);
  redlog::set_level(redlog::level::info);
  if (verbosity == 1) {
    redlog::set_level(redlog::level::verbose);
  } else if (verbosity == 2) {
    redlog::set_level(redlog::level::trace);
  } else if (verbosity >= 3) {
    redlog::set_level(redlog::level::debug);
  }
}

void open_plugin_gui(const std::string& plugin_path) {
  apply_verbosity();

  auto log = log_main.with_name("gui");
  log.inf("opening plugin editor", redlog::field("path", plugin_path));

  vstk::Plugin plugin(log);
  auto load_result = plugin.load(plugin_path);
  if (!load_result) {
    log.error("failed to load plugin",
              redlog::field("error", load_result.error()));
    return;
  }

  log.inf("plugin loaded successfully", redlog::field("name", plugin.name()));

  if (!plugin.has_editor()) {
    log.warn("plugin does not have an editor interface (headless plugin)");
    return;
  }

  auto window_result = plugin.create_editor_window();
  if (!window_result) {
    log.error("failed to create editor window",
              redlog::field("error", window_result.error()));
    return;
  }

  auto window = std::move(window_result.value());
  log.inf("editor window opened successfully");

  log.inf("entering gui event loop (close window to exit)");
  while (window->is_open()) {
    vstk::GuiWindow::process_events();
#ifdef _WIN32
    Sleep(16);
#else
    usleep(16000);
#endif
  }

  log.inf("gui session ended");
}

namespace {

// setup audio input configuration and return total frames to process
size_t setup_audio_input(const std::vector<std::string>& input_files,
                         vstk::MultiAudioReader& audio_reader,
                         double& sample_rate,
                         double requested_sample_rate,
                         double custom_duration,
                         const redlog::logger& log) {
  if (!input_files.empty()) {
    log.inf("loading input files", redlog::field("count", input_files.size()));
    
    for (const auto& input_file : input_files) {
      if (!audio_reader.add_file(input_file)) {
        log.error("failed to load input file", redlog::field("file", input_file));
        return 0;
      }
      log.trc("loaded input file", redlog::field("file", input_file));
    }
    
    if (sample_rate == 0.0) {
      sample_rate = audio_reader.sample_rate();
    }
    size_t total_frames = audio_reader.max_frames();
    
    log.inf("audio input configured",
            redlog::field("sample_rate", sample_rate),
            redlog::field("total_channels", audio_reader.total_channels()),
            redlog::field("total_frames", total_frames));
    return total_frames;
  } else {
    // instrument mode - no input files
    if (sample_rate == 0.0) {
      sample_rate = vstk::constants::DEFAULT_SAMPLE_RATE;
    }
    double duration = custom_duration > 0.0 ? custom_duration : vstk::constants::DEFAULT_INSTRUMENT_DURATION_SECONDS;
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
    
    vstk::ParameterValue value(param_value);
    if (!plugin.parameters().set_parameter(param_name, value)) {
      log.warn("failed to set parameter", 
               redlog::field("name", param_name),
               redlog::field("value", param_value));
    } else {
      log.trc("set parameter",
              redlog::field("name", param_name), 
              redlog::field("value", param_value));
    }
  }
}

// add MIDI note-on event for instrument plugins
void add_instrument_midi_event(vstk::Plugin& plugin, 
                              double sample_rate,
                              const redlog::logger& log) {
  auto* event_list = plugin.get_event_list(vstk::BusDirection::Input, 0);
  if (event_list) {
    auto event = vstk::util::create_note_on_event(
      vstk::constants::MIDI_MIDDLE_C,
      vstk::constants::MIDI_DEFAULT_VELOCITY,
      vstk::constants::MIDI_DEFAULT_CHANNEL,
      vstk::constants::MIDI_NOTE_DURATION_SECONDS,
      sample_rate
    );
    
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
    auto param_values = vstk::Automation::get_parameter_values(automation, frame_position);
    for (const auto& [param_name, value] : param_values) {
      vstk::ParameterValue param_value(value);
      plugin.parameters().set_parameter(param_name, param_value);
    }
  }
}

// convert input audio from interleaved to plugin's planar format
void prepare_plugin_input_audio(vstk::Plugin& plugin,
                                const vstk::MultiAudioReader& audio_reader,
                                const float* input_buffer,
                                size_t frames_to_process,
                                int output_channels) {
  auto* input_left = plugin.get_audio_buffer_32(vstk::BusDirection::Input, 0, 0);
  auto* input_right = plugin.get_audio_buffer_32(vstk::BusDirection::Input, 0, 1);
  
  if (input_left && (output_channels == 1 || input_right)) {
    int input_channels = std::min(audio_reader.total_channels(), 2);
    
    if (input_channels == 1) {
      // mono input - copy to left channel
      for (size_t i = 0; i < frames_to_process; ++i) {
        input_left[i] = input_buffer[i];
        if (input_right) input_right[i] = input_buffer[i]; // duplicate to stereo
      }
    } else {
      // stereo input - deinterleave
      for (size_t i = 0; i < frames_to_process; ++i) {
        input_left[i] = input_buffer[i * 2];
        if (input_right) input_right[i] = input_buffer[i * 2 + 1];
      }
    }
  }
}

// convert plugin's planar output to interleaved format
void collect_plugin_output_audio(vstk::Plugin& plugin,
                                 float* output_buffer,
                                 size_t frames_to_process,
                                 int output_channels,
                                 const redlog::logger& log) {
  auto* output_left = plugin.get_audio_buffer_32(vstk::BusDirection::Output, 0, 0);
  auto* output_right = plugin.get_audio_buffer_32(vstk::BusDirection::Output, 0, 1);
  
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
        output_buffer[i * 2 + 1] = output_right ? output_right[i] : output_left[i];
      }
    }
  } else {
    log.warn("failed to access plugin output buffers");
    // output buffer will remain cleared (zeros)
  }
}

} // anonymous namespace

void process_audio_file(const std::vector<std::string>& input_files,
                        const std::string& output_file,
                        const std::string& plugin_path,
                        const std::string& automation_file,
                        const std::vector<std::string>& parameters,
                        double requested_sample_rate,
                        int block_size,
                        int bit_depth,
                        bool overwrite,
                        double custom_duration = 0.0) {
  apply_verbosity();

  auto log = log_main.with_name("processor");
  
  try {
    // check if output file exists
    if (!overwrite && std::ifstream(output_file)) {
      log.error("output file already exists (use --overwrite to replace)",
                redlog::field("output", output_file));
      return;
    }

    // setup audio input
    vstk::MultiAudioReader audio_reader;
    double sample_rate = requested_sample_rate;
    size_t total_frames = setup_audio_input(input_files, audio_reader, sample_rate, 
                                           requested_sample_rate, custom_duration, log);
    if (total_frames == 0) {
      return; // error already logged
    }

    // load plugin
    log.inf("loading plugin", redlog::field("path", plugin_path));
    vstk::Plugin plugin(log);
    vstk::PluginConfig config;
    config.with_process_mode(vstk::ProcessMode::Offline)
          .with_sample_rate(sample_rate)
          .with_block_size(block_size);

    auto load_result = plugin.load(plugin_path, config);
    if (!load_result) {
      log.error("failed to load plugin", redlog::field("error", load_result.error()));
      return;
    }

    log.inf("plugin loaded successfully", redlog::field("name", plugin.name()));

    // apply parameter settings
    apply_parameter_settings(plugin, parameters, log);

    // parse automation file
    vstk::ParameterAutomation automation;
    if (!automation_file.empty()) {
      log.inf("loading automation", redlog::field("file", automation_file));
      
      std::ifstream file(automation_file);
      if (!file) {
        log.error("failed to open automation file", redlog::field("file", automation_file));
        return;
      }
      
      std::string json_content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
      
      try {
        automation = vstk::Automation::parse_automation_definition(
            json_content, sample_rate, total_frames);
        log.inf("automation loaded", redlog::field("parameter_count", automation.size()));
      } catch (const std::exception& e) {
        log.error("failed to parse automation file", redlog::field("error", e.what()));
        return;
      }
    }

    // setup output writer
    log.inf("creating output writer",
            redlog::field("file", output_file),
            redlog::field("sample_rate", sample_rate),
            redlog::field("bit_depth", bit_depth));
            
    int output_channels = vstk::constants::DEFAULT_OUTPUT_CHANNELS;
    // todo: get actual output channel count from plugin configuration
    
    vstk::AudioFileWriter output_writer;
    if (!output_writer.open(output_file, sample_rate, output_channels, bit_depth)) {
      log.error("failed to create output file", redlog::field("file", output_file));
      return;
    }

    // setup vst3 processing
    log.inf("preparing vst3 processing");
    
    // query plugin bus configuration
    int input_buses = plugin.bus_count(vstk::MediaType::Audio, vstk::BusDirection::Input);
    int output_buses = plugin.bus_count(vstk::MediaType::Audio, vstk::BusDirection::Output);
    
    log.inf("plugin bus configuration",
            redlog::field("input_buses", input_buses),
            redlog::field("output_buses", output_buses));
    
    // audio buses are activated automatically during plugin loading
    bool has_input_audio = audio_reader.is_valid();
    
    // prepare plugin for processing
    auto prepare_result = plugin.prepare_processing();
    if (!prepare_result) {
      log.error("failed to prepare processing", redlog::field("error", prepare_result.error()));
      return;
    }
    
    // start processing
    log.inf("about to start processing", redlog::field("is_processing_before", plugin.is_processing()));
    auto start_result = plugin.start_processing();
    if (!start_result) {
      log.error("failed to start processing", redlog::field("error", start_result.error()));
      return;
    }
    
    log.inf("vst3 processing started successfully",
            redlog::field("is_processing_after", plugin.is_processing()));
    
    // additional debug - check processing state right before main loop
    log.inf("final processing state check",
            redlog::field("is_loaded", plugin.is_loaded()),
            redlog::field("is_processing", plugin.is_processing()));

    // processing loop
    log.inf("starting audio processing",
            redlog::field("block_size", block_size),
            redlog::field("total_frames", total_frames));

    std::vector<float> input_buffer(block_size * (audio_reader.is_valid() ? audio_reader.total_channels() : 2));
    std::vector<float> output_buffer(block_size * output_channels);
    
    size_t frames_processed = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (frames_processed < total_frames) {
      size_t frames_to_process = std::min(static_cast<size_t>(block_size), 
                                          total_frames - frames_processed);
      
      // clear buffers
      vstk::util::clear_audio_buffer(input_buffer.data(), input_buffer.size());
      vstk::util::clear_audio_buffer(output_buffer.data(), output_buffer.size());
      
      // read input audio if available
      if (audio_reader.is_valid()) {
        size_t frames_read = audio_reader.read_interleaved(input_buffer.data(), frames_to_process);
        if (frames_read < frames_to_process) {
          log.trc("reached end of input audio", redlog::field("frames_read", frames_read));
        }
      }
      
      // update process context for this block (VST3 SDK pattern)
      auto* process_context = plugin.get_process_context();
      if (process_context) {
        vstk::util::update_process_context(*process_context, static_cast<int32_t>(frames_to_process));
      }

      // apply automation for current position
      apply_parameter_automation(plugin, automation, frames_processed, log);
      
      // add basic MIDI events for instruments (when no audio input)
      if (!has_input_audio && frames_processed == 0) {
        add_instrument_midi_event(plugin, sample_rate, log);
      }
      
      // process audio through plugin (real VST3 processing)
      if (plugin.is_loaded()) {
        // prepare input audio for plugin (convert interleaved to planar)
        if (has_input_audio && audio_reader.is_valid()) {
          prepare_plugin_input_audio(plugin, audio_reader, input_buffer.data(), 
                                   frames_to_process, output_channels);
        }
        
        // call vst3 processing
        auto process_result = plugin.process(static_cast<int32_t>(frames_to_process));
        if (!process_result) {
          log.warn("vst3 processing failed", 
                   redlog::field("error", process_result.error()),
                   redlog::field("frame", frames_processed));
          // continue processing even if one block fails
        }
        
        // get processed output (convert planar to interleaved)
        collect_plugin_output_audio(plugin, output_buffer.data(), frames_to_process, 
                                   output_channels, log);
      } else {
        // plugin not ready - log once per session
        static bool logged_not_ready = false;
        if (!logged_not_ready) {
          log.warn("plugin not ready for processing", 
                   redlog::field("is_loaded", plugin.is_loaded()),
                   redlog::field("is_processing", plugin.is_processing()));
          logged_not_ready = true;
        }
        
        // fallback: copy input to output or generate silence
        if (has_input_audio && audio_reader.is_valid()) {
          int channels_to_copy = std::min(audio_reader.total_channels(), output_channels);
          std::copy(input_buffer.begin(), 
                    input_buffer.begin() + frames_to_process * channels_to_copy,
                    output_buffer.begin());
        }
        // if no input and plugin not ready, output buffer stays silent (already cleared)
      }
      
      // write output
      size_t frames_written = output_writer.write(output_buffer.data(), frames_to_process);
      if (frames_written != frames_to_process) {
        log.error("failed to write complete block", 
                  redlog::field("expected", frames_to_process),
                  redlog::field("written", frames_written));
        break;
      }
      
      frames_processed += frames_to_process;
      
      // progress logging
      if (frames_processed % static_cast<size_t>(sample_rate * vstk::constants::PROGRESS_LOG_INTERVAL_SECONDS) == 0) {
        double progress = static_cast<double>(frames_processed) / static_cast<double>(total_frames) * 100.0;
        log.inf("processing progress", redlog::field("percent", progress));
      }
    }
    
    // cleanup vst3 processing
    log.inf("stopping vst3 processing");
    plugin.stop_processing();
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    log.inf("processing completed",
            redlog::field("frames_processed", frames_processed),
            redlog::field("duration_ms", duration.count()),
            redlog::field("realtime_factor", (frames_processed / sample_rate) / (duration.count() / 1000.0)));
            
  } catch (const std::exception& e) {
    log.error("processing failed", redlog::field("error", e.what()));
  }
}

void cmd_inspect(args::Subparser& parser) {
  apply_verbosity();

  args::Positional<std::string> plugin_path(parser, "plugin_path",
                                            "path to vst3 plugin to inspect");
  parser.Parse();

  if (!plugin_path) {
    log_main.error("plugin path required for inspect command");
    std::cerr << parser;
    return;
  }

  vstk::host::MinimalHost host(log_main);
  host.inspect_plugin(args::get(plugin_path));
}

void cmd_gui(args::Subparser& parser) {
  apply_verbosity();

  args::Positional<std::string> plugin_path(
      parser, "plugin_path", "path to vst3 plugin to open in gui");
  parser.Parse();

  if (!plugin_path) {
    log_main.error("plugin path required for gui command");
    std::cerr << parser;
    return;
  }

  open_plugin_gui(args::get(plugin_path));
}

void cmd_process(args::Subparser& parser) {
  apply_verbosity();

  // input/output options
  args::ValueFlagList<std::string> input_files(parser, "input", "input audio files (can specify multiple for multi-bus)",
                                               {'i', "input"});
  args::ValueFlag<std::string> output_file(
      parser, "output", "output audio file", {'o', "output"});
  args::Flag overwrite(
      parser, "overwrite", "overwrite existing output file", {'y', "overwrite"});
  
  // processing options
  args::ValueFlag<double> sample_rate(
      parser, "sample-rate", "output sample rate (default: input rate or 44100)", {'r', "sample-rate"});
  args::ValueFlag<int> block_size(
      parser, "block-size", "processing block size (default: 512)", {'b', "block-size"});
  args::ValueFlag<int> bit_depth(
      parser, "bit-depth", "output bit depth: 16, 24, 32 (default: 32)", {'d', "bit-depth"});
  args::ValueFlag<double> duration(
      parser, "duration", "duration in seconds for instrument mode (default: 10)", {'t', "duration"});
  
  // plugin control options
  args::ValueFlagList<std::string> parameters(
      parser, "param", "parameter settings as name:value", {'p', "param"});
  args::ValueFlag<std::string> automation_file(
      parser, "automation", "json automation file", {'a', "automation"});
  args::ValueFlag<std::string> preset_file(
      parser, "preset", "load plugin preset file", {"preset"});
  
  // advanced options
  args::Flag dry_run(
      parser, "dry-run", "validate setup without processing", {'n', "dry-run"});
  args::Flag quiet(
      parser, "quiet", "minimal output (errors only)", {'q', "quiet"});
  args::Flag progress(
      parser, "progress", "show detailed progress information", {"progress"});
  args::ValueFlag<int> threads(
      parser, "threads", "number of processing threads (experimental)", {'j', "threads"});
  
  // plugin path (required)
  args::Positional<std::string> plugin_path(
      parser, "plugin_path", "path to vst3 plugin to use for processing");
  
  parser.Parse();

  // validate required arguments
  if (!plugin_path || !output_file) {
    log_main.error("plugin path and output file required for process command");
    std::cerr << parser;
    return;
  }
  
  // validate bit depth
  if (bit_depth && (args::get(bit_depth) != 16 && args::get(bit_depth) != 24 && args::get(bit_depth) != 32)) {
    log_main.error("bit depth must be 16, 24, or 32");
    return;
  }
  
  // validate block size
  if (block_size && (args::get(block_size) < 32 || args::get(block_size) > 8192)) {
    log_main.error("block size must be between 32 and 8192");
    return;
  }
  
  // validate duration
  if (duration && args::get(duration) <= 0.0) {
    log_main.error("duration must be positive");
    return;
  }
  
  // configure logging based on quiet/progress flags
  if (quiet && progress) {
    log_main.error("cannot use both --quiet and --progress");
    return;
  }
  
  if (quiet) {
    redlog::set_level(redlog::level::error);
  } else if (progress) {
    redlog::set_level(redlog::level::trace);
  }

  // prepare arguments
  std::vector<std::string> inputs;
  if (input_files) {
    inputs = args::get(input_files);
  }

  std::vector<std::string> params;
  if (parameters) {
    params = args::get(parameters);
  }

  std::string automation_path;
  if (automation_file) {
    automation_path = args::get(automation_file);
  }
  
  // dry run mode - validate setup only
  if (dry_run) {
    log_main.inf("dry run mode - validating setup");
    
    // validate plugin exists
    if (!std::filesystem::exists(args::get(plugin_path))) {
      log_main.error("plugin file does not exist", redlog::field("path", args::get(plugin_path)));
      return;
    }
    
    // validate input files exist
    for (const auto& input : inputs) {
      if (!std::filesystem::exists(input)) {
        log_main.error("input file does not exist", redlog::field("path", input));
        return;
      }
    }
    
    // validate automation file exists
    if (!automation_path.empty() && !std::filesystem::exists(automation_path)) {
      log_main.error("automation file does not exist", redlog::field("path", automation_path));
      return;
    }
    
    log_main.inf("dry run validation passed - setup is valid");
    return;
  }

  // call process function with enhanced parameters
  process_audio_file(inputs, args::get(output_file), args::get(plugin_path),
                     automation_path, params,
                     sample_rate ? args::get(sample_rate) : 0.0,
                     block_size ? args::get(block_size) : vstk::constants::DEFAULT_BLOCK_SIZE,
                     bit_depth ? args::get(bit_depth) : vstk::constants::DEFAULT_BIT_DEPTH,
                     overwrite,
                     duration ? args::get(duration) : 0.0);
}

void cmd_scan(args::Subparser& parser) {
  apply_verbosity();

  args::ValueFlagList<std::string> search_paths(
      parser, "paths", "additional search paths", {'p', "path"});
  args::Flag detailed(parser, "detailed", "show detailed plugin information",
                      {'d', "detailed"});
  parser.Parse();

  std::vector<std::string> paths;
  if (search_paths) {
    paths = args::get(search_paths);
  }

  if (detailed) {
    auto plugins = vstk::util::discover_vst3_plugins(paths);
    log_main.inf("discovered plugins", redlog::field("count", plugins.size()));

    for (const auto& plugin : plugins) {
      log_main.inf("plugin found", redlog::field("name", plugin.name),
                   redlog::field("path", plugin.path),
                   redlog::field("valid", plugin.is_valid_bundle),
                   redlog::field("size_bytes", plugin.file_size));
    }
  } else {
    auto plugin_paths = vstk::util::find_vst3_plugins(paths);
    log_main.inf("found plugins", redlog::field("count", plugin_paths.size()));

    for (const auto& path : plugin_paths) {
      log_main.inf("plugin", redlog::field("path", path));
    }
  }
}

void cmd_parameters(args::Subparser& parser) {
  apply_verbosity();

  args::Positional<std::string> plugin_path(parser, "plugin_path",
                                            "path to vst3 plugin to analyze parameters");
  parser.Parse();

  if (!plugin_path) {
    log_main.error("plugin path required for parameters command");
    std::cerr << parser;
    return;
  }

  try {
    // Create a plugin instance
    vstk::Plugin plugin(log_main);
    
    // Load the plugin
    auto result = plugin.load(args::get(plugin_path));
    if (!result) {
      log_main.error("failed to load plugin", redlog::field("error", result.error()));
      return;
    }
    
    log_main.info("plugin loaded successfully", redlog::field("name", plugin.name()));
    
    // test parameter discovery
    const auto& params = plugin.parameters().parameters();
    log_main.info("parameter discovery", redlog::field("parameter_count", params.size()));
    
    if (params.empty()) {
      log_main.info("no parameters found in plugin");
      return;
    }
    
    // show all parameters
    for (size_t i = 0; i < params.size(); ++i) {
      const auto& param = params[i];
      
      log_main.info("parameter details",
                    redlog::field("index", i),
                    redlog::field("name", param.name),
                    redlog::field("id", param.id),
                    redlog::field("discrete", param.is_discrete),
                    redlog::field("text_conversion", param.supports_text_conversion),
                    redlog::field("default_value", param.default_normalized_value));
      
      // get current values
      auto current_norm = plugin.parameters().get_parameter_normalized(param.name);
      auto current_text = plugin.parameters().get_parameter_text(param.name);
      
      if (current_norm) {
        log_main.debug("parameter values",
                       redlog::field("parameter", param.name),
                       redlog::field("normalized", *current_norm),
                       redlog::field("text", current_text.value_or("(no text)")));
      }
      
      // if discrete, show available values
      if (param.is_discrete && !param.value_strings.empty()) {
        std::string values_list;
        for (size_t j = 0; j < std::min(param.value_strings.size(), size_t(5)); ++j) {
          if (j > 0) values_list += ", ";
          values_list += param.value_strings[j];
        }
        if (param.value_strings.size() > 5) {
          values_list += " ...";
        }
        
        log_main.debug("discrete values",
                       redlog::field("parameter", param.name),
                       redlog::field("values", values_list));
      }
    }
    
  } catch (const std::exception& e) {
    log_main.error("exception during parameter analysis", redlog::field("what", e.what()));
  }
}

int main(int argc, char* argv[]) {
  args::ArgumentParser parser("vstshill - cross-platform vst3 host",
                              "analyze, host, and process vst3 plugins");
  parser.helpParams.showTerminator = false;
  parser.SetArgumentSeparations(false, false, true, true);
  parser.LongSeparator(" ");

  args::GlobalOptions globals(parser, arguments);
  args::Group commands(parser, "commands");

  args::Command inspect_cmd(commands, "inspect",
                            "inspect and analyze a vst3 plugin", &cmd_inspect);
  args::Command gui_cmd(commands, "gui", "open plugin editor gui window",
                        &cmd_gui);
  args::Command process_cmd(commands, "process",
                            "process audio files through plugin", &cmd_process);
  args::Command scan_cmd(commands, "scan",
                         "scan for vst3 plugins in standard directories",
                         &cmd_scan);
  args::Command parameters_cmd(commands, "parameters",
                              "analyze and list plugin parameters", &cmd_parameters);

  try {
    parser.ParseCLI(argc, argv);
  } catch (args::Help) {
    std::cout << parser;
    return 0;
  } catch (args::ParseError& e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  return 0;
}