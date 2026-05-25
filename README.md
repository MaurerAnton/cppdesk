# cppdesk

**C++ rewrite of RustDesk — cross-platform remote desktop (client + server)**

> Line-for-line translation from Rust to modern C++20.  
> Supports all platforms: Windows, Linux, macOS, Android, iOS.

## Supported Platforms

| Platform    | Client | Server | Status        |
|-------------|--------|--------|---------------|
| Windows     | ✓      | ✓      | Native C++    |
| Linux       | ✓      | ✓      | Native C++    |
| macOS       | ✓      | ✓      | Native C++/ObjC |
| Android     | ✓      | -      | Flutter + NDK |
| iOS         | ✓      | -      | Flutter       |

## Building

### Prerequisites

- CMake 3.20+
- C++20 compiler (GCC 13+, Clang 17+, MSVC 2022+)
- Ninja (recommended)
- Platform-specific dependencies (see CI workflow)

### Quick Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

### Build Options

- `-DBUILD_CLIENT=ON/OFF` — Build client application
- `-DBUILD_SERVER=ON/OFF` — Build server application
- `-DBUILD_CLI=ON/OFF` — Build CLI tools
- `-DBUILD_TESTS=ON/OFF` — Build unit tests
- `-DENABLE_ENCRYPTION=ON/OFF` — Enable libsodium encryption
- `-DENABLE_FLUTTER=ON/OFF` — Enable Flutter UI

### Packaging

```bash
cmake --build build
cd build
cpack -G DEB    # Debian/Ubuntu
cpack -G RPM    # Fedora/RHEL
cpack -G ZIP    # Portable
cpack -G NSIS   # Windows installer
cpack -G DMG    # macOS
```

## Architecture

```
include/cppdesk/        — Public headers
  common/               — Shared utilities (config, crypto, protocol)
  client/               — Client module
  server/               — Server module
  rendezvous/           — Rendezvous protocol
  platform/             — Platform abstractions (win/linux/mac)
  ipc/                  — Inter-process communication
  plugin/               — Plugin framework
  ui/                   — Legacy UI (deprecated)
  cli/                  — Command-line interface
  lang/                 — Localization
  updater/              — Auto-update
  clipboard/            — Clipboard sync
  privacy/              — Privacy mode
  whiteboard/           — Whiteboard
  auth/                 — Authentication (2FA)
  network/              — Networking (KCP, LAN)
  codec/                — Video/audio codecs
  ffi/                  — Flutter FFI bridge
  hbbs_http/            — HBBS HTTP server

libs/
  common/               — Core library (config, proto, crypto, stream)
  scrap/                — Screen capture
  enigo/                — Input simulation
  clipboard/            — Clipboard management
  portable/             — Portable app support
  virtual_display/      — Virtual display driver
  remote_printer/       — Remote printer

src/                    — Implementation files
proto/                  — Protocol Buffers definitions
flutter/                — Flutter UI (Dart)
tests/                  — Unit tests
scripts/                — Build/CI scripts
cmake/                  — CMake modules
```

## Download

Pre-built binaries available on [GitHub Releases](https://github.com/MaurerAnton/cppdesk/releases).

Packages available:
- `.deb` (Debian/Ubuntu)
- `.rpm` (Fedora/RHEL)
- `.exe` / `.msi` (Windows)
- `.dmg` (macOS)
- `.apk` (Android)
- `.zip` (Portable)

## License

Same as RustDesk. See LICENSE file.

## Original Reference

This project is a C++20 translation of [RustDesk](https://github.com/rustdesk/rustdesk),  
a Rust-based open-source remote desktop application.
