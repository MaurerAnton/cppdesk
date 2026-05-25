# x86_64 Android NDK toolchain
set(CMAKE_SYSTEM_NAME android)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-linux-android-gcc)
set(CMAKE_CXX_COMPILER x86_64-linux-android-g++)

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
