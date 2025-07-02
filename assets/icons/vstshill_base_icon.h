// auto-generated combined icon header
#pragma once

#include "vstshill_base_icon_png.h"
#include "vstshill_base_icon_bmp.h"

// cross-platform icon data selection
#ifdef HAVE_SDL_IMAGE
static const unsigned char* const app_icon_data = app_icon_png_data;
static const size_t app_icon_size = app_icon_png_size;
#else
static const unsigned char* const app_icon_data = app_icon_bmp_data;
static const size_t app_icon_size = app_icon_bmp_size;
#endif
