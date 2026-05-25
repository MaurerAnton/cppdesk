#!/bin/bash
set -e
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build --parallel $(nproc)
cd build && ctest --output-on-failure -j$(nproc)
