cmake_minimum_required(VERSION 3.20)

project(vstshill
    VERSION 1.0.0
    DESCRIPTION "the vst inspector"
    LANGUAGES C CXX
)

# Add cmake modules to path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Load configuration modules in order
include(PlatformConfig)        # Platform detection and compiler settings
include(DependencyConfig)      # Find and configure all dependencies  
include(TargetConfig)          # Create and configure targets

# Load platform-specific configuration
if(APPLE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/platforms/Darwin.cmake)
elseif(WIN32)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/platforms/Windows.cmake)
elseif(UNIX)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/platforms/Linux.cmake)
endif()

# Development tools  
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
    message(STATUS "Code formatting targets available: format, format-check")
endif()