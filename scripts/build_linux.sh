#!/bin/bash
set -e
sudo apt-get update
sudo apt-get install -y cmake ninja-build g++ pkg-config libgtk-3-dev libx11-dev libxfixes-dev libxrandr-dev libpulse-dev libssl-dev libsqlite3-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
echo "Build complete. Binaries in build/"
