# CMake script to format source code with clang-format
# Usage: cmake -P scripts/format.cmake

find_program(CLANG_FORMAT NAMES clang-format)
if(NOT CLANG_FORMAT)
    message(FATAL_ERROR "clang-format not found")
endif()

# Find all source files
file(GLOB_RECURSE SOURCE_FILES
    "${CMAKE_CURRENT_LIST_DIR}/../src/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/*.mm"
)

message(STATUS "Formatting ${SOURCE_FILES}")

# Format all files
foreach(FILE ${SOURCE_FILES})
    execute_process(
        COMMAND ${CLANG_FORMAT} -i -style=file ${FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
        RESULT_VARIABLE RESULT
    )
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to format ${FILE}")
    endif()
endforeach()

message(STATUS "Formatting complete")