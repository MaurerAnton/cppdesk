#!/bin/bash
# Build and package for all platforms
set -e
VERSION=1.3.0

echo "=== Building cppdesk v${VERSION} for all platforms ==="

# Linux DEB
echo "Building Linux DEB..."
./scripts/package_deb.sh

# Linux RPM
echo "Building Linux RPM..."
./scripts/package_rpm.sh

# Linux AppImage
echo "Building AppImage..."
./scripts/build_appimage.sh

# Windows
echo "Building Windows installer..."
./scripts/package_exe.sh

# macOS
echo "Building macOS DMG..."
./scripts/package_dmg.sh

# Portable ZIP
echo "Building portable ZIP..."
./scripts/package_zip.sh

echo "=== All packages built ==="
ls -la build/cppdesk-*
