#pragma once

#include <SDL3/SDL.h>
#ifdef HAVE_SDL_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#include <string>

namespace vstk {
namespace util {

void set_application_icon(SDL_Window* window);
bool load_icon_from_data(SDL_Window* window, const unsigned char* data,
                         size_t size);
bool load_multi_resolution_icon(SDL_Window* window,
                                const unsigned char* base_data,
                                size_t base_size,
                                const unsigned char* hires_data,
                                size_t hires_size);

} // namespace util
} // namespace vstk