#pragma once

namespace vstk::constants {

// audio processing defaults
constexpr double DEFAULT_SAMPLE_RATE = 44100.0;
constexpr int DEFAULT_BLOCK_SIZE = 512;
constexpr int DEFAULT_BIT_DEPTH = 32;
constexpr int DEFAULT_OUTPUT_CHANNELS = 2;

// instrument mode defaults  
constexpr double DEFAULT_INSTRUMENT_DURATION_SECONDS = 10.0;

// midi event constants
constexpr int MIDI_MIDDLE_C = 60;
constexpr float MIDI_DEFAULT_VELOCITY = 0.8f;
constexpr double MIDI_NOTE_DURATION_SECONDS = 8.0;
constexpr int MIDI_DEFAULT_CHANNEL = 0;
constexpr int MIDI_DEFAULT_NOTE_ID = -1;
constexpr float MIDI_DEFAULT_TUNING = 0.0f;

// processing loop constants
constexpr size_t PROGRESS_LOG_INTERVAL_SECONDS = 5;

// buffer limits
constexpr int MAX_AUDIO_CHANNELS = 8;
constexpr size_t MAX_BLOCK_SIZE = 8192;

// gui event loop timing
constexpr int GUI_REFRESH_INTERVAL_MS = 16;

// audio format constants
constexpr int STEREO_CHANNELS = 2;

// verbosity level constants
constexpr int VERBOSITY_LEVEL_VERBOSE = 1;
constexpr int VERBOSITY_LEVEL_TRACE = 2; 
constexpr int VERBOSITY_LEVEL_DEBUG = 3;
constexpr int VERBOSITY_LEVEL_PEDANTIC = 4;

} // namespace vstk::constants