cmake_minimum_required(VERSION 3.20)
# Frozen snapshot of step 04 (part 1 of upstream d1013487). Identical to the
# root build at step 04 except for the project name.
# Version parity: Cargo.toml [package] version = "1.1.2".
project(cppdesk_04_d1013487_source_code_part1 VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# build.rs gen_version() parity: version string available to code as a header.
configure_file(
    cmake/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cppdesk/version.hpp"
    @ONLY
)

add_subdirectory(libs/hbb_common)

add_executable(cppdesk src/main.cpp)
target_include_directories(cppdesk PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(cppdesk PRIVATE hbb_common)

if(WIN32)
    # build.rs build_windows() parity: compile src/windows.cc into the binary.
    target_sources(cppdesk PRIVATE src/platform/windows.cc)
    # Upstream leaves WtsApi32 commented out — same here:
    # target_link_libraries(cppdesk PRIVATE WtsApi32)
endif()

include(CTest)
add_subdirectory(tests)
