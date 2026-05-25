#!/bin/bash
# Install all dependencies for CI
set -e
case "$(uname -s)" in
    Linux)
        sudo apt-get update -qq
        sudo apt-get install -y -qq cmake ninja-build g++ pkg-config \
            libgtk-3-dev libx11-dev libxfixes-dev libxrandr-dev \
            libpulse-dev libssl-dev libsqlite3-dev \
            libglib2.0-dev libcairo2-dev libpango1.0-dev
        ;;
    Darwin)
        brew install cmake ninja pkg-config openssl
        ;;
    MINGW*|MSYS*)
        pacman -S --noconfirm mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
            mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config \
            mingw-w64-x86_64-openssl mingw-w64-x86_64-sqlite3
        ;;
esac
