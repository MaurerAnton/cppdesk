cmake_minimum_required(VERSION 3.20)
# Frozen snapshot of step 06 (part 3 of upstream d1013487 "source code").
# Version parity: Cargo.toml [package] version = "1.1.2".
project(cppdesk_06_d1013487_source_code_part3 VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    target_sources(cppdesk PRIVATE src/platform/windows.cc)
endif()

include(CTest)
add_subdirectory(tests)