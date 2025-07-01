#include "gui_command.hpp"
#include "../audio/sdl_audio.hpp"
#include "../host/constants.hpp"
#include "../host/vstk.hpp"
#include <iostream>
#include <redlog/redlog.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

extern redlog::logger log_main;
extern void apply_verbosity();

namespace vstk {

GuiCommand::GuiCommand(args::Subparser& parser)
    : parser_(parser),
      plugin_path_(parser, "plugin_path", "path to vst3 plugin to open in gui"),
      audio_output_(parser, "audio",
                    "enable real-time audio output (experimental)", {"audio"}) {
}

int GuiCommand::execute() {
  apply_verbosity();

  parser_.Parse();

  if (!plugin_path_) {
    log_main.error("plugin path required for gui command");
    std::cerr << parser_;
    return 1;
  }

  open_plugin_gui(args::get(plugin_path_), audio_output_);
  return 0;
}

void GuiCommand::open_plugin_gui(const std::string& plugin_path,
                                 bool with_audio) const {
  auto log = log_main.with_name("gui");
  log.inf("opening plugin editor", redlog::field("path", plugin_path));

  // setup audio engine if requested
  std::unique_ptr<vstk::SDLAudioEngine> audio_engine;
  if (with_audio) {
    log.inf("initializing real-time audio output");

    audio_engine = std::make_unique<vstk::SDLAudioEngine>();
    if (!audio_engine->initialize()) {
      log.error("failed to initialize audio engine - continuing without audio");
      audio_engine.reset();
    } else {
      // list available audio devices
      auto devices = audio_engine->get_audio_devices();
      log.inf("available audio devices",
              redlog::field("count", devices.size()));
      for (size_t i = 0; i < devices.size(); ++i) {
        log.inf("audio device", redlog::field("index", i),
                redlog::field("name", devices[i]));
      }
    }
  }

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

  // connect plugin to audio engine and start playback
  if (audio_engine) {
    if (audio_engine->connect_plugin(plugin)) {
      if (audio_engine->start()) {
        log.inf("real-time audio processing started",
                redlog::field("sample_rate", audio_engine->sample_rate()),
                redlog::field("buffer_size", audio_engine->buffer_size()),
                redlog::field("channels", audio_engine->channels()));
      } else {
        log.error("failed to start audio playback - continuing with GUI only");
      }
    } else {
      log.error("failed to connect plugin to audio engine - continuing with "
                "GUI only");
    }
  }

  log.inf("entering gui event loop (close window to exit)");
  while (window->is_open()) {
    vstk::GuiWindow::process_events();
#ifdef _WIN32
    Sleep(constants::GUI_REFRESH_INTERVAL_MS);
#else
    usleep(constants::GUI_REFRESH_INTERVAL_MS * 1000);
#endif
  }

  // cleanup audio engine
  if (audio_engine && audio_engine->is_playing()) {
    log.inf("stopping real-time audio processing");
    audio_engine->stop();
  }

  log.inf("gui session ended");
}

} // namespace vstk