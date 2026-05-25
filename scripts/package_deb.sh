#!/bin/bash
set -e
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cd build && cpack -G DEB
echo "DEB package created in build/"
