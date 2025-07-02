#include "icon_utils.hpp"
#include <redlog/redlog.hpp>

// include the generated icon data
#include "../../assets/icons/vstshill_base_icon.h"

namespace vstk {
namespace util {

void set_application_icon(SDL_Window* window) {
  auto log = redlog::get_logger("vstk::icon");

  if (!window) {
    log.warn("cannot set icon: window is null");
    return;
  }

  if (load_icon_from_data(window, app_icon_data, app_icon_size)) {
    log.dbg("application icon set successfully");
  } else {
    log.warn("failed to set application icon");
  }
}

bool load_icon_from_data(SDL_Window* window, const unsigned char* data,
                         size_t size) {
  auto log = redlog::get_logger("vstk::icon");

  if (!window || !data || size == 0) {
    log.warn("invalid parameters for icon loading");
    return false;
  }

  // create sdl rwops from memory
  SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
  if (!rw) {
    log.warn("failed to create sdl rwops from icon data",
             redlog::field("error", SDL_GetError()));
    return false;
  }

  // try to load image with sdl_image first for png support with transparency
  SDL_Surface* icon_surface = nullptr;

#ifdef HAVE_SDL_IMAGE
  icon_surface =
      IMG_Load_RW(rw, 0); // don't free rwops yet in case we need fallback
  if (icon_surface) {
    SDL_FreeRW(rw); // free rwops after successful load
    log.trc("loaded icon using sdl_image");
  }
#endif

  // fallback to sdl_loadbmp_rw if sdl_image failed or isn't available
  if (!icon_surface) {
    SDL_RWseek(rw, 0, RW_SEEK_SET);       // reset to beginning
    icon_surface = SDL_LoadBMP_RW(rw, 1); // 1 = free rwops after use
    if (icon_surface) {
      log.trc("loaded icon using sdl bmp loader (fallback)");
    }
  }

  if (!icon_surface) {
    log.warn("failed to load icon surface from data",
             redlog::field("error", SDL_GetError()));
    return false;
  }

  // set the window icon
  SDL_SetWindowIcon(window, icon_surface);

  log.trc("icon loaded and set successfully", redlog::field("size", size),
          redlog::field("width", icon_surface->w),
          redlog::field("height", icon_surface->h),
          redlog::field("format", icon_surface->format->format));

  // clean up
  SDL_FreeSurface(icon_surface);

  return true;
}

} // namespace util
} // namespace vstk