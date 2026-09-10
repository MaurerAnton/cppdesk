cmake_minimum_required(VERSION 3.20)
# Frozen snapshot of step 09 (part 6 of upstream d1013487 "source code").
# Version parity: Cargo.toml [package] version = "1.1.2".
project(cppdesk_09_d1013487_source_code_part6 VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    cmake/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cppdesk/version.hpp"
    @ONLY
)

# Protobuf: prefer the package config (protobuf >= 22 links Abseil, whose
# transitive DSOs must appear on the link line — a raw find_library() link
# fails with "DSO missing from command line"). The interface target below
# is inherited by libs/hbb_common.
find_package(Protobuf CONFIG QUIET)
add_library(cppdesk_protobuf INTERFACE)
if(TARGET protobuf::libprotobuf)
    target_link_libraries(cppdesk_protobuf INTERFACE protobuf::libprotobuf)
else()
    find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/message.h)
    find_library(PROTOBUF_LIBRARY NAMES protobuf)
    if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
        message(FATAL_ERROR "libprotobuf dev files not found (need message.h + libprotobuf)")
    endif()
    target_include_directories(cppdesk_protobuf INTERFACE ${PROTOBUF_INCLUDE_DIR})
    target_link_libraries(cppdesk_protobuf INTERFACE cppdesk_protobuf)
endif()

add_subdirectory(libs/hbb_common)

# App crate parity (src/lib.rs module map): first-party app sources compile
# into the cppdesk binary directly.
add_executable(cppdesk src/main.cpp src/common.cpp)
target_include_directories(
    cppdesk
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        include
)
target_link_libraries(cppdesk PRIVATE hbb_common cppdesk_protobuf)
# NOTE: PROTOBUF_LIBRARY is found in libs/hbb_common (find_library caches
# globally, so it is visible here); common.cpp calls libprotobuf directly.

if(WIN32)
    target_sources(cppdesk PRIVATE src/platform/windows.cc)
endif()

include(CTest)
add_subdirectory(tests)
