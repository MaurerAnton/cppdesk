cmake_minimum_required(VERSION 3.20)
# Frozen snapshot of step 08 (part 5 of upstream d1013487 "source code").
# Version parity: Cargo.toml [package] version = "1.1.2".
project(cppdesk_08_d1013487_source_code_part5 VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    cmake/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cppdesk/version.hpp"
    @ONLY
)

add_subdirectory(libs/hbb_common)

# App crate parity (src/lib.rs module map): first-party app sources compile
# into the cppdesk binary directly.
add_executable(cppdesk src/main.cpp src/common.cpp)
target_include_directories(
    cppdesk
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        include
        libs/hbb_common/protos/gen
)
target_link_libraries(cppdesk PRIVATE hbb_common ${PROTOBUF_LIBRARY})
# NOTE: PROTOBUF_LIBRARY is found in libs/hbb_common (find_library caches
# globally, so it is visible here); common.cpp calls libprotobuf directly.

if(WIN32)
    target_sources(cppdesk PRIVATE src/platform/windows.cc)
endif()

include(CTest)
add_subdirectory(tests)
