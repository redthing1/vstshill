#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

namespace vstk {
namespace platform {

// platform-specific vst3 gui integration
class GuiPlatform {
public:
  // extract native view handle suitable for vst3 plugin attachment
  // returns nullptr on failure
  static void* extract_native_view(SDL_Window* window);

  // get platform type for vst3 attachment
  static const char* get_platform_type();

  // platform-specific cleanup if needed
  static void cleanup_native_view(void* native_view);

  // ensure gui operations happen on correct thread
  static void ensure_main_thread();

  // get display scale factor for proper dpi handling
  static float get_display_scale_factor();
};

} // namespace platform
} // namespace vstk