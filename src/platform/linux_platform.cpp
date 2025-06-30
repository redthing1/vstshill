#include "platform_gui.hpp"

#if defined(__linux__)

#include <X11/Xlib.h>
#include <cstdlib>
#include <pluginterfaces/gui/iplugview.h>

namespace vstk {
namespace platform {

void* GuiPlatform::extract_native_view(SDL_Window* window) {
  if (!window) {
    return nullptr;
  }

  // get sdl window system info
  SDL_SysWMinfo wm_info;
  SDL_VERSION(&wm_info.version);

  if (!SDL_GetWindowWMInfo(window, &wm_info)) {
    return nullptr;
  }

  // on linux, we use the x11 window id
  Window x11_window = wm_info.info.x11.window;
  return (void*)(uintptr_t)x11_window;
}

const char* GuiPlatform::get_platform_type() {
  return Steinberg::kPlatformTypeX11EmbedWindowID;
}

void GuiPlatform::cleanup_native_view(void* native_view) {
  // no cleanup needed on linux
}

void GuiPlatform::ensure_main_thread() {
  // on linux, we don't need special main thread handling for this use case
}

float GuiPlatform::get_display_scale_factor() {
  // on linux, try to get scale factor from environment or x11
  const char* scale_env = getenv("GDK_SCALE");
  if (scale_env) {
    float scale = std::atof(scale_env);
    if (scale > 0.0f) {
      return scale;
    }
  }

  // could also try to get from x11 dpi, but 1.0 is a reasonable default
  return 1.0f;
}

} // namespace platform
} // namespace vstk

#endif // __linux__