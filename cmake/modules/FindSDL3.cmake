# Option to control static linking
option(VSTSHILL_STATIC_SDL "Link SDL3 statically" OFF)

# find SDL3 with multiple methods for cross-platform compatibility
if(VSTSHILL_STATIC_SDL)
    set(SDL3_SHARED_ENABLED_BY_DEFAULT OFF)
    set(SDL3_STATIC_ENABLED_BY_DEFAULT ON)
endif()

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)
    find_package(SDL3 MODULE QUIET)
    if(NOT SDL3_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            if(VSTSHILL_STATIC_SDL)
                pkg_check_modules(SDL3 REQUIRED sdl3 --static)
            else()
                pkg_check_modules(SDL3 REQUIRED sdl3)
            endif()
        endif()
    endif()
endif()

# optional SDL3_image for better icon quality
find_package(SDL3_image CONFIG QUIET)
if(NOT SDL3_image_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        if(VSTSHILL_STATIC_SDL)
            pkg_check_modules(SDL3_IMAGE sdl3-image --static)
        else()
            pkg_check_modules(SDL3_IMAGE sdl3-image)
        endif()
    endif()
endif()

# set availability flag
if(SDL3_image_FOUND OR SDL3_IMAGE_FOUND)
    set(HAS_SDL3_IMAGE TRUE)
    message(STATUS "sdl3_image found - will use high-quality png icons")
else()
    set(HAS_SDL3_IMAGE FALSE)
    message(STATUS "sdl3_image not found - will fallback to bmp icons")
endif()

# ensure consistent variable names for different find methods
if(SDL3_FOUND AND TARGET SDL3::SDL3 AND NOT SDL3_INCLUDE_DIRS)
    get_target_property(SDL3_INCLUDE_DIRS SDL3::SDL3 INTERFACE_INCLUDE_DIRECTORIES)
    set(SDL3_LIBRARIES SDL3::SDL3)
elseif(SDL3_FOUND AND SDL3_INCLUDE_DIRS)
    # pkg-config case - variables are already set by pkg_check_modules
    set(SDL3_LIBRARIES ${SDL3_LIBRARIES})
endif()

if(HAS_SDL3_IMAGE AND SDL3_image_FOUND AND TARGET SDL3_image::SDL3_image)
    get_target_property(SDL3_IMAGE_INCLUDE_DIRS SDL3_image::SDL3_image INTERFACE_INCLUDE_DIRECTORIES)
    set(SDL3_IMAGE_LIBRARIES SDL3_image::SDL3_image)
endif()

# Display SDL3 linking status
if(VSTSHILL_STATIC_SDL)
    message(STATUS "SDL3 will be linked statically")
else()
    message(STATUS "SDL3 will be linked dynamically")
endif()

