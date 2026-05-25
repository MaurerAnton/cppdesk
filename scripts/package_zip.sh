#!/bin/bash
set -e
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && cpack -G ZIP
echo "ZIP archive created in build/"
