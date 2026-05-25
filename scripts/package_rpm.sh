#!/bin/bash
set -e
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cd build && cpack -G RPM
echo "RPM package created in build/"
