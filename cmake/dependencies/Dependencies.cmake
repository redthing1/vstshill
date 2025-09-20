include_guard(GLOBAL)

set(_VSTSHILL_THIRD_PARTY_DIR "${PROJECT_SOURCE_DIR}/lib")

# Core dependencies
add_subdirectory("${_VSTSHILL_THIRD_PARTY_DIR}/vst3sdk")
add_subdirectory("${_VSTSHILL_THIRD_PARTY_DIR}/redlog_cpp")

# libsndfile configuration
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_PACKAGE_CONFIG OFF CACHE BOOL "" FORCE)
add_subdirectory("${_VSTSHILL_THIRD_PARTY_DIR}/libsndfile")

# SDL - prefers config packages with fallbacks for pkg-config
include(${CMAKE_CURRENT_LIST_DIR}/FindSDL3.cmake)

# Optional witness integration
if(VSTSHILL_WITNESS)
    set(WITNESS_SCRIPT ON CACHE BOOL "" FORCE)
    set(WITNESS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(WITNESS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(WITNESS_QBDI_EXTRAS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

    add_subdirectory("${_VSTSHILL_THIRD_PARTY_DIR}/w1tn3ss")
endif()

# Consolidated dependency interface target
add_library(vstshill_dependencies INTERFACE)

target_link_libraries(vstshill_dependencies INTERFACE
    sdk_hosting
    redlog::redlog
    sndfile
)

# SDL integration
if(TARGET SDL3::SDL3)
    target_link_libraries(vstshill_dependencies INTERFACE SDL3::SDL3)
elseif(SDL3_LIBRARIES)
    target_link_libraries(vstshill_dependencies INTERFACE ${SDL3_LIBRARIES})
endif()

if(SDL3_INCLUDE_DIRS)
    target_include_directories(vstshill_dependencies INTERFACE ${SDL3_INCLUDE_DIRS})
endif()

if(HAS_SDL3_IMAGE)
    if(TARGET SDL3_image::SDL3_image)
        target_link_libraries(vstshill_dependencies INTERFACE SDL3_image::SDL3_image)
    elseif(SDL3_IMAGE_LIBRARIES)
        target_link_libraries(vstshill_dependencies INTERFACE ${SDL3_IMAGE_LIBRARIES})
    endif()
    if(SDL3_IMAGE_INCLUDE_DIRS)
        target_include_directories(vstshill_dependencies INTERFACE ${SDL3_IMAGE_INCLUDE_DIRS})
    endif()
    target_compile_definitions(vstshill_dependencies INTERFACE HAVE_SDL_IMAGE=1)
endif()

# Witness linkage and definitions
if(VSTSHILL_WITNESS)
    target_link_libraries(vstshill_dependencies INTERFACE
        w1tn3ss
        w1cov_static
        w1xfer_static
        w1script_static
    )
    target_compile_definitions(vstshill_dependencies INTERFACE VSTSHILL_WITNESS=1)
endif()

function(vstshill_link_dependencies target)
    target_link_libraries(${target} PRIVATE vstshill_dependencies)
endfunction()
