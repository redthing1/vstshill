include_guard(GLOBAL)

# macOS specific knobs
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    option(VSTSHILL_UNIVERSAL_BINARY "Build a universal (arm64+x86_64) macOS binary" OFF)
    if(VSTSHILL_UNIVERSAL_BINARY)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
endif()

# Sanitiser wiring
if(VSTSHILL_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Baseline warning setup
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

# Cache GTK discovery for Linux builds
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VSTSHILL_GTK3 REQUIRED gtk+-3.0)
endif()

function(vstshill_apply_platform target)
    if(APPLE)
        target_link_libraries(${target} PRIVATE
            "-framework CoreFoundation"
            "-framework Foundation"
            "-framework Cocoa"
        )
    elseif(WIN32)
        target_link_libraries(${target} PRIVATE ole32 shell32 user32)
    elseif(UNIX)
        target_include_directories(${target} PRIVATE ${VSTSHILL_GTK3_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${VSTSHILL_GTK3_LIBRARIES} dl pthread)
    endif()
endfunction()
