#!/bin/bash
set -e
brew install cmake ninja pkg-config openssl 2>/dev/null || true
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(sysctl -n hw.ncpu)
echo "Build complete. Binaries in build/"
