cmake_minimum_required(VERSION 3.20)

project(vstshill
    VERSION 1.0.0
    DESCRIPTION "the vst inspector"
    LANGUAGES C CXX
)

# standards
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# build configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEVELOPMENT=1)
else()
    add_compile_definitions(RELEASE=1)
endif()

# optional sanitizers
option(VSTSHILL_SANITIZERS "enable address and undefined behavior sanitizers" OFF)
if(VSTSHILL_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# compiler warnings
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wno-unused-parameter -Wno-gnu-zero-variadic-macro-arguments -Wno-ignored-qualifiers)
endif()

# dependencies
add_subdirectory(lib/vst3sdk)
add_subdirectory(lib/redlog_cpp)
add_subdirectory(lib/libsndfile)

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(SDL2 MODULE QUIET)
    if(NOT SDL2_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()
endif()

# optional sdl2_image for better icon quality
find_package(SDL2_image CONFIG QUIET)
if(NOT SDL2_image_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2_IMAGE SDL2_image)
    endif()
endif()

if(SDL2_image_FOUND OR SDL2_IMAGE_FOUND)
    set(HAS_SDL_IMAGE TRUE)
    message(STATUS "sdl2_image found - will use high-quality png icons")
else()
    set(HAS_SDL_IMAGE FALSE)
    message(STATUS "sdl2_image not found - will fallback to bmp icons")
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
endif()

# source files
set(COMMON_SOURCES
    src/main.cpp
    src/host/vstk.cpp
    src/host/parameter.cpp
    src/host/minimal.cpp
    src/util/vst_discovery.cpp
    src/util/icon_utils.cpp
    src/automation/automation.cpp
    src/util/string_utils.cpp
    src/util/audio_utils.cpp
    src/util/midi_utils.cpp
    src/util/midi_file.cpp
    src/audio/audio_io.cpp
    src/audio/sdl_audio.cpp
    src/commands/command.cpp
    src/commands/scan_command.cpp
    src/commands/inspect_command.cpp
    src/commands/parameters_command.cpp
    src/commands/gui_command.cpp
    src/commands/process_command.cpp
    lib/vst3sdk/public.sdk/source/vst/hosting/plugprovider.cpp
)

set(PLATFORM_SOURCES)
if(APPLE)
    list(APPEND PLATFORM_SOURCES
        lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
        src/platform/macos_platform.mm
    )
elseif(WIN32)
    list(APPEND PLATFORM_SOURCES
        lib/vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp
        src/platform/windows_platform.cpp
    )
elseif(UNIX)
    list(APPEND PLATFORM_SOURCES
        lib/vst3sdk/public.sdk/source/vst/hosting/module_linux.cpp
        src/platform/linux_platform.cpp
    )
endif()

# target
add_executable(vstshill ${COMMON_SOURCES} ${PLATFORM_SOURCES})

target_include_directories(vstshill PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
    $<$<BOOL:${HAS_SDL_IMAGE}>:${SDL2_IMAGE_INCLUDE_DIRS}>
    $<$<PLATFORM_ID:Linux>:${GTK3_INCLUDE_DIRS}>
)

target_link_directories(vstshill PRIVATE ${SDL2_LIBRARY_DIRS})

target_link_libraries(vstshill PRIVATE
    sdk_hosting
    redlog::redlog
    sndfile
    ${SDL2_LIBRARIES}
)

# link sdl2_image if available
if(HAS_SDL_IMAGE)
    target_compile_definitions(vstshill PRIVATE HAVE_SDL_IMAGE=1)
    if(SDL2_image_FOUND)
        target_link_libraries(vstshill PRIVATE SDL2_image::SDL2_image)
    elseif(SDL2_IMAGE_FOUND)
        target_link_libraries(vstshill PRIVATE ${SDL2_IMAGE_LIBRARIES})
        target_link_directories(vstshill PRIVATE ${SDL2_IMAGE_LIBRARY_DIRS})
    endif()
endif()

# on macOS, also create an app bundle version
if(APPLE)
    add_executable(vstshill-app MACOSX_BUNDLE ${COMMON_SOURCES} ${PLATFORM_SOURCES})
    target_include_directories(vstshill-app PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR} ${SDL2_INCLUDE_DIRS} $<$<BOOL:${HAS_SDL_IMAGE}>:${SDL2_IMAGE_INCLUDE_DIRS}>)
    target_link_directories(vstshill-app PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(vstshill-app PRIVATE sdk_hosting redlog::redlog sndfile ${SDL2_LIBRARIES})
    if(HAS_SDL_IMAGE)
        target_compile_definitions(vstshill-app PRIVATE HAVE_SDL_IMAGE=1)
        if(SDL2_image_FOUND)
            target_link_libraries(vstshill-app PRIVATE SDL2_image::SDL2_image)
        elseif(SDL2_IMAGE_FOUND)
            target_link_libraries(vstshill-app PRIVATE ${SDL2_IMAGE_LIBRARIES})
            target_link_directories(vstshill-app PRIVATE ${SDL2_IMAGE_LIBRARY_DIRS})
        endif()
    endif()
endif()

# platform-specific configuration
if(APPLE)
    set_source_files_properties(
        lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
        src/platform/macos_platform.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    
    # configure app bundle with icon and metadata
    set(MACOSX_BUNDLE_ICON_FILE vstshill_base.icns)
    set(MACOSX_BUNDLE_BUNDLE_NAME "VST Shill")
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.vstshill.app")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vstshill_base.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    
    target_sources(vstshill-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vstshill_base.icns)
    
    target_link_libraries(vstshill PRIVATE "-framework CoreFoundation" "-framework Foundation" "-framework Cocoa")
    target_link_libraries(vstshill-app PRIVATE "-framework CoreFoundation" "-framework Foundation" "-framework Cocoa")
elseif(WIN32)
    target_compile_options(vstshill PRIVATE /Zc:char8_t-)
    target_compile_options(sdk_hosting PRIVATE /Zc:char8_t-)
    
    # add windows icon resource
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vstshill_base.ico 
                   ${CMAKE_CURRENT_BINARY_DIR}/vstshill.ico COPYONLY)
    
    target_compile_definitions(vstshill PRIVATE
        _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
    target_compile_definitions(sdk_hosting PRIVATE
        _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
    
    target_link_libraries(vstshill PRIVATE ole32 shell32 user32)
elseif(UNIX)
    target_link_libraries(vstshill PRIVATE
        ${GTK3_LIBRARIES}
        dl
        pthread
    )
endif()

# output configuration
set_target_properties(vstshill PROPERTIES
    OUTPUT_NAME "vstshill"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# development tools
find_program(CLANG_FORMAT "clang-format")
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/format.cmake
        COMMENT "formatting source code with clang-format"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    add_custom_target(format-check
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/format-check.cmake
        COMMENT "checking code formatting with clang-format"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()