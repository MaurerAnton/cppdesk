# RISC-V 64-bit Linux cross-compilation toolchain
set(CMAKE_SYSTEM_NAME linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_CXX_STANDARD 20)

# Platform-specific flags
if(CMAKE_SYSTEM_NAME STREQUAL "android")
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK})
    set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION clang)
    set(CMAKE_ANDROID_STL_TYPE c++_shared)
endif()

# Disable tests in cross-compilation
set(BUILD_TESTS OFF CACHE BOOL "Disable tests for cross-compilation")
set(ENABLE_FLUTTER OFF CACHE BOOL "Disable Flutter for cross-compilation")
