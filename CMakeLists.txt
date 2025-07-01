# the vst inspector
cmake_minimum_required(VERSION 3.16)

project(vstshill
    VERSION 1.0.0
    DESCRIPTION "the vst inspector"
    LANGUAGES CXX
)

# c++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vst3 sdk configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEVELOPMENT=1)
else()
    add_compile_definitions(RELEASE=1)
endif()

# compiler warnings
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wno-unused-parameter -Wno-gnu-zero-variadic-macro-arguments -Wno-ignored-qualifiers)
endif()

# third-party dependencies
add_subdirectory(lib/vst3sdk)
add_subdirectory(lib/redlog_cpp)

# Find SDL2 cross-platform
find_package(SDL2 REQUIRED)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# source files
set(vstshill_sources 
    src/main.cpp
    src/host/vstk.cpp
    src/host/minimal.cpp
    src/util/vst_discovery.cpp
    lib/vst3sdk/public.sdk/source/vst/hosting/plugprovider.cpp
)

# platform-specific sources
if(APPLE)
    list(APPEND vstshill_sources 
        lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
        src/platform/macos_platform.mm
    )
    set_source_files_properties(lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(src/platform/macos_platform.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(UNIX)
    list(APPEND vstshill_sources 
        lib/vst3sdk/public.sdk/source/vst/hosting/module_linux.cpp
        src/platform/linux_platform.cpp
    )
elseif(WIN32)
    list(APPEND vstshill_sources 
        lib/vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp
        src/platform/windows_platform.cpp
    )
endif()

# executable target
add_executable(vstshill ${vstshill_sources})

# link libraries
target_link_libraries(vstshill PRIVATE 
    sdk_hosting 
    redlog::redlog
    ${SDL2_LIBRARIES}
)

# include directories
target_include_directories(vstshill PRIVATE 
    src
    ${SDL2_INCLUDE_DIRS}
)

target_link_directories(vstshill PRIVATE ${SDL2_LIBRARY_DIRS})

# platform-specific system libraries
if(APPLE)
    target_link_libraries(vstshill PRIVATE 
        "-framework CoreFoundation" 
        "-framework Foundation"
        "-framework Cocoa"
    )
elseif(WIN32)
    target_link_libraries(vstshill PRIVATE ole32 shell32 user32)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_link_libraries(vstshill PRIVATE ${GTK3_LIBRARIES} dl pthread)
    target_include_directories(vstshill PRIVATE ${GTK3_INCLUDE_DIRS})
endif()

# output configuration
set_target_properties(vstshill PROPERTIES
    OUTPUT_NAME "vstshill"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)