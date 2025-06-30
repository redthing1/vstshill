#include "platform_gui.hpp"

#ifdef _WIN32

#include <pluginterfaces/gui/iplugview.h>
#include <windows.h>

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

  // on windows, we use the hwnd directly
  HWND hwnd = wm_info.info.win.window;
  return (void*)hwnd;
}

const char* GuiPlatform::get_platform_type() {
  return Steinberg::kPlatformTypeHWND;
}

void GuiPlatform::cleanup_native_view(void* native_view) {
  // no cleanup needed on windows
}

void GuiPlatform::ensure_main_thread() {
  // on windows, we don't need special main thread handling for this use case
}

float GuiPlatform::get_display_scale_factor() {
  // get dpi awareness on windows
  HDC hdc = GetDC(NULL);
  if (hdc) {
    int dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return static_cast<float>(dpi_x) / 96.0f; // 96 dpi is 100% scale
  }
  return 1.0f; // default scale factor
}

} // namespace platform
} // namespace vstk

#endif // _WIN32