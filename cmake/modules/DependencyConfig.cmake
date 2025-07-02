# dependencies
add_subdirectory(lib/vst3sdk)
add_subdirectory(lib/redlog_cpp)

# disable libsndfile tests and programs to avoid cmake module path conflicts
set(BUILD_TESTING OFF)
set(BUILD_PROGRAMS OFF)
set(ENABLE_PACKAGE_CONFIG OFF)
add_subdirectory(lib/libsndfile)

# find SDL3
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/FindSDL3.cmake)

# dependency helper function
function(apply_vstshill_dependencies target_name)
    target_include_directories(${target_name} PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # add SDL3 include directories only if not using target
    if(SDL3_INCLUDE_DIRS AND NOT TARGET SDL3::SDL3)
        target_include_directories(${target_name} PRIVATE ${SDL3_INCLUDE_DIRS})
    endif()
    
    if(HAS_SDL3_IMAGE AND SDL3_IMAGE_INCLUDE_DIRS AND NOT TARGET SDL3_image::SDL3_image)
        target_include_directories(${target_name} PRIVATE ${SDL3_IMAGE_INCLUDE_DIRS})
    endif()
    
    if(SDL3_LIBRARY_DIRS)
        target_link_directories(${target_name} PRIVATE ${SDL3_LIBRARY_DIRS})
    endif()
    
    target_link_libraries(${target_name} PRIVATE
        sdk_hosting
        redlog::redlog
        sndfile
    )
    
    # link SDL3 - prefer target over variables
    if(TARGET SDL3::SDL3)
        target_link_libraries(${target_name} PRIVATE SDL3::SDL3)
    elseif(SDL3_LIBRARIES)
        target_link_libraries(${target_name} PRIVATE ${SDL3_LIBRARIES})
    endif()
    
    if(HAS_SDL3_IMAGE)
        target_compile_definitions(${target_name} PRIVATE HAVE_SDL_IMAGE=1)
        if(SDL3_image_FOUND AND TARGET SDL3_image::SDL3_image)
            target_link_libraries(${target_name} PRIVATE SDL3_image::SDL3_image)
        elseif(SDL3_IMAGE_FOUND)
            target_link_libraries(${target_name} PRIVATE ${SDL3_IMAGE_LIBRARIES})
            if(SDL3_IMAGE_LIBRARY_DIRS)
                target_link_directories(${target_name} PRIVATE ${SDL3_IMAGE_LIBRARY_DIRS})
            endif()
        endif()
    endif()
endfunction()