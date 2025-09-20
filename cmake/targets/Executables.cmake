include_guard(GLOBAL)

add_executable(vstshill ${PROJECT_SOURCE_DIR}/src/main.cpp)

target_link_libraries(vstshill PRIVATE vstshill_core)
vstshill_apply_platform(vstshill)

set_target_properties(vstshill PROPERTIES
    OUTPUT_NAME "vstshill"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(APPLE)
    add_executable(vstshill-app MACOSX_BUNDLE ${PROJECT_SOURCE_DIR}/src/main.cpp)
    target_link_libraries(vstshill-app PRIVATE vstshill_core)
    vstshill_apply_platform(vstshill-app)

    set(MACOSX_BUNDLE_ICON_FILE vstshill_base.icns)
    set_target_properties(vstshill-app PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "vstshill"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vstshill.app"
    )

    set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/assets/icons/vstshill_base.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(vstshill-app PRIVATE ${PROJECT_SOURCE_DIR}/assets/icons/vstshill_base.icns)
elseif(WIN32)
    configure_file(
        ${PROJECT_SOURCE_DIR}/assets/icons/vstshill_base.ico
        ${CMAKE_CURRENT_BINARY_DIR}/vstshill.ico COPYONLY
    )
endif()
