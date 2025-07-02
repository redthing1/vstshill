# windows-specific configuration
set(CMAKE_SYSTEM_VERSION "10.0" CACHE STRING "target windows version")

if(MSVC)
    add_compile_options(/MP)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Ob2)
    endif()
endif()