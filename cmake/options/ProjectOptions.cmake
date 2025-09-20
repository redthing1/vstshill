include_guard(GLOBAL)

# Core project language settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Public build switches
option(VSTSHILL_SANITIZERS "Enable address and undefined behaviour sanitizers" OFF)
option(VSTSHILL_WITNESS "Enable w1tn3ss dynamic binary tracing and instrumentation framework" OFF)

# Ensure a sensible default for single-config generators
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(DEFAULT_BUILD_TYPE "Release")
    message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified")
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Build type" FORCE)
endif()

# Normalise release/debug preprocessor toggles
if(CMAKE_CONFIGURATION_TYPES)
    add_compile_definitions(
        $<$<CONFIG:Debug>:DEVELOPMENT=1>
        $<$<NOT:$<CONFIG:Debug>>:RELEASE=1>
    )
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_definitions(DEVELOPMENT=1)
    else()
        add_compile_definitions(RELEASE=1)
    endif()
endif()
