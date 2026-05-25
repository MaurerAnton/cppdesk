#!/bin/bash
set -e
NDK=${ANDROID_NDK:-$HOME/android-sdk/ndk/27.0.12077973}
cmake -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-34 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
echo "Android native libs built in build-android/"
