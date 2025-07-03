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

  // try multi-resolution icon first (SDL3 proper way)
  if (load_multi_resolution_icon(window, app_icon_base_data, app_icon_base_size,
                                 app_icon_hires_data, app_icon_hires_size)) {
    log.dbg("application multi-resolution icon set successfully");
  } else {
    log.warn("failed to set multi-resolution icon, falling back to single "
             "resolution");
    // fallback to single resolution
    if (load_icon_from_data(window, app_icon_base_data, app_icon_base_size)) {
      log.dbg("application icon set successfully (fallback)");
    } else {
      log.warn("failed to set application icon");
    }
  }
}

bool load_icon_from_data(SDL_Window* window, const unsigned char* data,
                         size_t size) {
  auto log = redlog::get_logger("vstk::icon");

  if (!window || !data || size == 0) {
    log.warn("invalid parameters for icon loading");
    return false;
  }

  // create sdl iostream from memory
  SDL_IOStream* rw = SDL_IOFromConstMem(data, static_cast<size_t>(size));
  if (!rw) {
    log.warn("failed to create sdl rwops from icon data",
             redlog::field("error", SDL_GetError()));
    return false;
  }

  // try to load image with sdl_image first for png support with transparency
  SDL_Surface* icon_surface = nullptr;

#ifdef HAVE_SDL_IMAGE
  icon_surface = IMG_Load_IO(
      rw, false); // don't close iostream yet in case we need fallback
  if (icon_surface) {
    SDL_CloseIO(rw); // close iostream after successful load
    log.trc("loaded icon using sdl_image");
  }
#endif

  // fallback to sdl_loadbmp_rw if sdl_image failed or isn't available
  if (!icon_surface) {
    SDL_SeekIO(rw, 0, SDL_IO_SEEK_SET);      // reset to beginning
    icon_surface = SDL_LoadBMP_IO(rw, true); // true = close iostream after use
    if (icon_surface) {
      log.trc("loaded icon using sdl bmp loader (fallback)");
    }
  }

  if (!icon_surface) {
    log.warn("failed to load icon surface from data",
             redlog::field("error", SDL_GetError()));
    return false;
  }

  // set the window icon using proper SDL3 multi-resolution support
  if (!SDL_SetWindowIcon(window, icon_surface)) {
    log.warn("failed to set window icon",
             redlog::field("error", SDL_GetError()));
    SDL_DestroySurface(icon_surface);
    return false;
  }

  log.trc("icon loaded and set successfully", redlog::field("size", size),
          redlog::field("width", icon_surface->w),
          redlog::field("height", icon_surface->h),
          redlog::field("format", icon_surface->format));

  // clean up
  SDL_DestroySurface(icon_surface);

  return true;
}

bool load_multi_resolution_icon(SDL_Window* window,
                                const unsigned char* base_data,
                                size_t base_size,
                                const unsigned char* hires_data,
                                size_t hires_size) {
  auto log = redlog::get_logger("vstk::icon");

  if (!window || !base_data || base_size == 0) {
    log.warn("invalid parameters for multi-resolution icon loading");
    return false;
  }

  // load base resolution icon (32x32)
  SDL_IOStream* base_rw =
      SDL_IOFromConstMem(base_data, static_cast<size_t>(base_size));
  if (!base_rw) {
    log.warn("failed to create sdl rwops from base icon data",
             redlog::field("error", SDL_GetError()));
    return false;
  }

  SDL_Surface* base_icon = nullptr;
#ifdef HAVE_SDL_IMAGE
  base_icon = IMG_Load_IO(base_rw, true);
  if (base_icon) {
    log.trc("loaded base icon using sdl_image");
  }
#endif

  if (!base_icon) {
    // fallback to bmp loader
    SDL_SeekIO(base_rw, 0, SDL_IO_SEEK_SET);
    base_icon = SDL_LoadBMP_IO(base_rw, true);
    if (base_icon) {
      log.trc("loaded base icon using sdl bmp loader (fallback)");
    }
  }

  if (!base_icon) {
    log.warn("failed to load base icon surface from data",
             redlog::field("error", SDL_GetError()));
    return false;
  }

  // load high resolution icon (64x64) if provided
  SDL_Surface* hires_icon = nullptr;
  if (hires_data && hires_size > 0) {
    SDL_IOStream* hires_rw =
        SDL_IOFromConstMem(hires_data, static_cast<size_t>(hires_size));
    if (hires_rw) {
#ifdef HAVE_SDL_IMAGE
      hires_icon = IMG_Load_IO(hires_rw, true);
      if (hires_icon) {
        log.trc("loaded high-res icon using sdl_image");
      }
#endif

      if (!hires_icon) {
        SDL_SeekIO(hires_rw, 0, SDL_IO_SEEK_SET);
        hires_icon = SDL_LoadBMP_IO(hires_rw, true);
        if (hires_icon) {
          log.trc("loaded high-res icon using sdl bmp loader (fallback)");
        }
      }
    }
  }

  // add high resolution version as alternate image if available
  if (hires_icon) {
    if (!SDL_AddSurfaceAlternateImage(base_icon, hires_icon)) {
      log.warn("failed to add high-res icon as alternate image",
               redlog::field("error", SDL_GetError()));
      SDL_DestroySurface(hires_icon);
    } else {
      log.trc("added high-res icon as alternate image");
    }
  }

  // set the window icon with multi-resolution support
  if (!SDL_SetWindowIcon(window, base_icon)) {
    log.warn("failed to set window icon",
             redlog::field("error", SDL_GetError()));
    SDL_DestroySurface(base_icon);
    return false;
  }

  log.trc("multi-resolution icon set successfully",
          redlog::field("base_width", base_icon->w),
          redlog::field("base_height", base_icon->h),
          redlog::field("has_hires", hires_icon != nullptr));

  // clean up
  SDL_DestroySurface(base_icon);

  return true;
}

} // namespace util
} // namespace vstk