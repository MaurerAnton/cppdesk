#!/bin/bash
set -e
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build && cpack -G NSIS
echo "Windows installer created in build/"
