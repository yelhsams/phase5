cmake_minimum_required(VERSION 3.16)
project(mitscript)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories(src)

add_executable(mitscript-release ${SOURCES})
set_target_properties(mitscript-release PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release"
    OUTPUT_NAME "mitscript"
)

add_executable(mitscript-debug ${SOURCES})
set_target_properties(mitscript-debug PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/debug"
    OUTPUT_NAME "mitscript"
)

target_compile_options(mitscript-release PRIVATE -Wall -Wextra -pedantic -O3 -DNDEBUG)
target_compile_options(mitscript-debug PRIVATE -Wall -Wextra -pedantic -g -O0 -DDEBUG -fsanitize=address -fsanitize=undefined)
target_link_options(mitscript-debug PRIVATE -fsanitize=address -fsanitize=undefined)

add_custom_command(TARGET mitscript-release POST_BUILD
    COMMAND strip $<TARGET_FILE:mitscript-release>
    COMMENT "Stripping release binary"
)

add_custom_target(all-build
    DEPENDS mitscript-release mitscript-debug
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT mitscript-release)

add_custom_target(clean-build
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/release"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/debug"
    COMMENT "Cleaning build artifacts"
)
