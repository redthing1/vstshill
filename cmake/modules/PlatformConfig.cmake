# platform detection and compiler settings
set(CMAKE_CXX_STANDARD 20)
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
    add_compile_options(/W3 /Zc:char8_t-)
    add_compile_definitions(
        _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
else()
    add_compile_options(-Wall -Wno-unused-parameter -Wno-ignored-qualifiers)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)
    endif()
endif()

# platform-specific helpers
function(add_platform_includes target_name)
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        target_include_directories(${target_name} PRIVATE ${GTK3_INCLUDE_DIRS})
    endif()
endfunction()

function(add_platform_libraries target_name)
    if(APPLE)
        target_link_libraries(${target_name} PRIVATE "-framework CoreFoundation" "-framework Foundation" "-framework Cocoa")
    elseif(WIN32)
        target_link_libraries(${target_name} PRIVATE ole32 shell32 user32)
    elseif(UNIX)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        target_link_libraries(${target_name} PRIVATE ${GTK3_LIBRARIES} dl pthread)
    endif()
endfunction()

function(configure_platform_sources)
    if(APPLE)
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos_platform.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
    endif()
endfunction()