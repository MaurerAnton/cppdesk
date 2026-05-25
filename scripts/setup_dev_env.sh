#!/bin/bash
# Setup development environment
set -e

echo "Setting up cppdesk development environment..."

# Install build dependencies
if [ -f /etc/debian_version ]; then
    sudo apt-get update
    sudo apt-get install -y cmake ninja-build g++-13 pkg-config \
        libgtk-3-dev libx11-dev libxfixes-dev libxrandr-dev \
        libpulse-dev libssl-dev libsqlite3-dev \
        git curl wget clang-format clang-tidy gdb
elif [ -f /etc/redhat-release ]; then
    sudo dnf install -y cmake ninja-build gcc-c++ pkgconfig \
        gtk3-devel libX11-devel libXfixes-devel libXrandr-devel \
        pulseaudio-libs-devel openssl-devel sqlite-devel \
        git curl clang
elif [ "$(uname)" = "Darwin" ]; then
    brew install cmake ninja pkg-config openssl clang-format
fi

# Setup git hooks
cp scripts/pre-commit .git/hooks/
chmod +x .git/hooks/pre-commit

echo "Development environment ready"
