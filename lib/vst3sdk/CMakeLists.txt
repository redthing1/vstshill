# Minimal VST3 SDK orchestration
cmake_minimum_required(VERSION 3.16)

# Set SDK root for internal references
set(SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Enable VST SDK mode (not standalone plugins)
set(VST_SDK ON)

# Add cmake modules to path FIRST
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Include essential VST3 SDK configuration
include(SMTG_Global)
include(SMTG_AddSMTGLibrary)
include(SMTG_UniversalBinary)

# Add essential VST3 SDK components
add_subdirectory(base)
add_subdirectory(pluginterfaces)
add_subdirectory(public.sdk)