// auto-generated combined icon header
#pragma once

#include "vstshill_base_icon_32_png.h"
#include "vstshill_base_icon_png.h"
#include "vstshill_base_icon_bmp.h"

// multi-resolution icon data selection
#ifdef HAVE_SDL_IMAGE
// 32x32 base icon (app_icon_32_png_data, app_icon_32_png_size)
// 64x64 high-res icon (app_icon_png_data, app_icon_png_size)
static const unsigned char* const app_icon_base_data = app_icon_32_png_data;
static const size_t app_icon_base_size = app_icon_32_png_size;
static const unsigned char* const app_icon_hires_data = app_icon_png_data;
static const size_t app_icon_hires_size = app_icon_png_size;
#else
// fallback to single resolution BMP
static const unsigned char* const app_icon_base_data = app_icon_bmp_data;
static const size_t app_icon_base_size = app_icon_bmp_size;
static const unsigned char* const app_icon_hires_data = nullptr;
static const size_t app_icon_hires_size = 0;
#endif
