include_guard(GLOBAL)

set(_VSTSHILL_CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/host/vstk.cpp
    ${PROJECT_SOURCE_DIR}/src/host/parameter.cpp
    ${PROJECT_SOURCE_DIR}/src/host/minimal.cpp
    ${PROJECT_SOURCE_DIR}/src/host/module_loader.cpp
    ${PROJECT_SOURCE_DIR}/src/util/vst_discovery.cpp
    ${PROJECT_SOURCE_DIR}/src/util/icon_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/automation/automation.cpp
    ${PROJECT_SOURCE_DIR}/src/util/string_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/util/audio_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/util/midi_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/util/midi_file.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/audio_io.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/sdl_audio.cpp
    ${PROJECT_SOURCE_DIR}/src/commands/command.cpp
    ${PROJECT_SOURCE_DIR}/src/commands/scan_command.cpp
    ${PROJECT_SOURCE_DIR}/src/commands/inspect_command.cpp
    ${PROJECT_SOURCE_DIR}/src/commands/parameters_command.cpp
    ${PROJECT_SOURCE_DIR}/src/commands/gui_command.cpp
    ${PROJECT_SOURCE_DIR}/src/commands/process_command.cpp
    ${PROJECT_SOURCE_DIR}/lib/vst3sdk/public.sdk/source/vst/hosting/plugprovider.cpp
)

if(VSTSHILL_WITNESS)
    list(APPEND _VSTSHILL_CORE_SOURCES
        ${PROJECT_SOURCE_DIR}/src/commands/instrument_command.cpp
        ${PROJECT_SOURCE_DIR}/src/instrumentation/tracer_host.cpp
        ${PROJECT_SOURCE_DIR}/src/instrumentation/vst_operations.cpp
    )
endif()

if(APPLE)
    list(APPEND _VSTSHILL_CORE_SOURCES
        ${PROJECT_SOURCE_DIR}/lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
        ${PROJECT_SOURCE_DIR}/src/platform/macos_platform.mm
    )
elseif(WIN32)
    list(APPEND _VSTSHILL_CORE_SOURCES
        ${PROJECT_SOURCE_DIR}/src/platform/windows_platform.cpp
    )
elseif(UNIX)
    list(APPEND _VSTSHILL_CORE_SOURCES
        ${PROJECT_SOURCE_DIR}/src/platform/linux_platform.cpp
    )
endif()

add_library(vstshill_core STATIC ${_VSTSHILL_CORE_SOURCES})
unset(_VSTSHILL_CORE_SOURCES)

target_include_directories(vstshill_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}
)

target_link_libraries(vstshill_core
    PUBLIC
        vstshill_dependencies
)

if(APPLE)
    set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
        ${PROJECT_SOURCE_DIR}/src/platform/macos_platform.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()
