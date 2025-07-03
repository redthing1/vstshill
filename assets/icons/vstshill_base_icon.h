// auto-generated combined icon header
#pragma once

#include "vstshill_base_icon_32_png.h"
#include "vstshill_base_icon_png.h"
#include "vstshill_base_icon_bmp.h"

// platform-specific icon data selection
#ifdef _WIN32
// Windows: use BMP format (reliable cross-platform support)
static const unsigned char* const app_icon_base_data = app_icon_bmp_data;
static const size_t app_icon_base_size = app_icon_bmp_size;
static const unsigned char* const app_icon_hires_data = nullptr;
static const size_t app_icon_hires_size = 0;
#elif defined(HAVE_SDL_IMAGE)
// Non-Windows with SDL_Image: use multi-resolution PNG
static const unsigned char* const app_icon_base_data = app_icon_32_png_data;
static const size_t app_icon_base_size = app_icon_32_png_size;
static const unsigned char* const app_icon_hires_data = app_icon_png_data;
static const size_t app_icon_hires_size = app_icon_png_size;
#else
// Fallback: single resolution BMP
static const unsigned char* const app_icon_base_data = app_icon_bmp_data;
static const size_t app_icon_base_size = app_icon_bmp_size;
static const unsigned char* const app_icon_hires_data = nullptr;
static const size_t app_icon_hires_size = 0;
#endif
