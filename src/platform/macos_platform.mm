#include "platform_gui.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#include <pluginterfaces/gui/iplugview.h>

namespace vstk {
namespace platform {

void* GuiPlatform::extract_native_view(SDL_Window* window) {
  if (!window) {
    return nullptr;
  }

  // ensure we're on the main thread for gui operations
  ensure_main_thread();

  // get nswindow using SDL3 properties
  NSWindow* ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
      NULL);
  if (!ns_window) {
    return nullptr;
  }

  // get the content view - this is what vst3 plugins expect
  NSView* content_view = [ns_window contentView];
  if (!content_view) {
    return nullptr;
  }

  // use cfbridgingretain to transfer ownership to c++ (arc-compatible)
  return (__bridge_retained void*)content_view;
}

const char* GuiPlatform::get_platform_type() {
  return Steinberg::kPlatformTypeNSView;
}

void GuiPlatform::cleanup_native_view(void* native_view) {
  if (native_view) {
    // use cfbridgingrelease to properly release the retained view
    // (arc-compatible)
    CFBridgingRelease(native_view);
  }
}

void GuiPlatform::ensure_main_thread() {
  if (![NSThread isMainThread]) {
    // this shouldn't happen in our current design, but better safe than sorry
    NSLog(@"Warning: GUI operation called from non-main thread");
  }
}

float GuiPlatform::get_display_scale_factor() {
  NSScreen* main_screen = [NSScreen mainScreen];
  if (main_screen) {
    return static_cast<float>([main_screen backingScaleFactor]);
  }
  return 1.0f; // default scale factor
}

} // namespace platform
} // namespace vstk

#endif // __APPLE__