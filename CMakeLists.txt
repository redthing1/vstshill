# Minimal cross-platform VST3 host
cmake_minimum_required(VERSION 3.16)

project(vstshill
    VERSION 1.0.0
    DESCRIPTION "vstshill"
    LANGUAGES CXX
)

# Modern C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# VST3 SDK requires debug configuration macros
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEVELOPMENT=1)
else()
    add_compile_definitions(RELEASE=1)
endif()

# Relaxed warnings for third-party VST3 SDK
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wno-unused-parameter -Wno-gnu-zero-variadic-macro-arguments -Wno-ignored-qualifiers)
endif()

# Include VST3 SDK
add_subdirectory(lib/vst3sdk)

# Include redlog logging library
add_subdirectory(lib/redlog_cpp)

# Platform-specific VST3 module sources
set(vstshill_sources src/main.cpp)

if(APPLE)
    list(APPEND vstshill_sources lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm)
    set_source_files_properties(lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(UNIX)
    list(APPEND vstshill_sources lib/vst3sdk/public.sdk/source/vst/hosting/module_linux.cpp)
elseif(WIN32)
    list(APPEND vstshill_sources lib/vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp)
endif()

# Create executable
add_executable(vstshill ${vstshill_sources})

# Link VST3 SDK hosting library and redlog
target_link_libraries(vstshill PRIVATE sdk_hosting redlog::redlog)

# Add external headers directory for args library
target_include_directories(vstshill PRIVATE src)

# Platform-specific system libraries
if(APPLE)
    target_link_libraries(vstshill PRIVATE "-framework CoreFoundation" "-framework Foundation")
elseif(WIN32)
    target_link_libraries(vstshill PRIVATE ole32 shell32 user32)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_link_libraries(vstshill PRIVATE ${GTK3_LIBRARIES} dl pthread)
    target_include_directories(vstshill PRIVATE ${GTK3_INCLUDE_DIRS})
endif()

# Output configuration
set_target_properties(vstshill PROPERTIES
    OUTPUT_NAME "vstshill"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)