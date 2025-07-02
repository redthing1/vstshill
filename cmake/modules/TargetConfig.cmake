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

apply_vstshill_dependencies(vstshill)
add_platform_libraries(vstshill)
add_platform_includes(vstshill)

set_target_properties(vstshill PROPERTIES
    OUTPUT_NAME "vstshill"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# on macOS, also create an app bundle version
if(APPLE)
    add_executable(vstshill-app MACOSX_BUNDLE ${COMMON_SOURCES} ${PLATFORM_SOURCES})
    apply_vstshill_dependencies(vstshill-app)
    add_platform_libraries(vstshill-app)
    add_platform_includes(vstshill-app)
    
    # configure app bundle with icon and metadata
    set(MACOSX_BUNDLE_ICON_FILE vstshill_base.icns)
    set(MACOSX_BUNDLE_BUNDLE_NAME "vstshill")
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.vstshill.app")
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vstshill_base.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(vstshill-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vstshill_base.icns)
elseif(WIN32)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/vstshill_base.ico 
                   ${CMAKE_CURRENT_BINARY_DIR}/vstshill.ico COPYONLY)
endif()

configure_platform_sources()