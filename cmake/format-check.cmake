# CMake script to check source code formatting with clang-format
# Usage: cmake -P scripts/format-check.cmake

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

list(FILTER SOURCE_FILES EXCLUDE REGEX "/src/ext/")

list(LENGTH SOURCE_FILES SOURCE_COUNT)
message(STATUS "Checking formatting of ${SOURCE_COUNT} files...")

# Check formatting of all files
foreach(FILE ${SOURCE_FILES})
    execute_process(
        COMMAND ${CLANG_FORMAT} --dry-run --Werror -style=file ${FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
        RESULT_VARIABLE RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "File ${FILE} is not properly formatted. Run 'cmake --build . --target format' to fix.")
    endif()
endforeach()

message(STATUS "All files are properly formatted")
