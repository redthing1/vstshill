# linux-specific configuration
if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-flto)
    add_link_options(-flto)
endif()