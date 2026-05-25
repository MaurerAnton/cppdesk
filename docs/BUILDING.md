# Building cppdesk

## Prerequisites

- CMake 3.20+
- C++20 compiler (GCC 13+, Clang 17+, MSVC 2022+)
- Ninja (recommended) or Make

## Platform-specific dependencies

### Linux (Ubuntu/Debian)
```bash
sudo apt-get install cmake ninja-build g++ pkg-config libgtk-3-dev libx11-dev libxfixes-dev libxrandr-dev libpulse-dev libssl-dev libsqlite3-dev
```

### Linux (Fedora/RHEL)
```bash
sudo dnf install cmake ninja-build gcc-c++ pkgconfig gtk3-devel libX11-devel libXfixes-devel libXrandr-devel pulseaudio-libs-devel openssl-devel sqlite-devel
```

### Windows
- Visual Studio 2022 with C++ workload
- CMake 3.20+

### macOS
```bash
brew install cmake ninja pkg-config openssl
```

## Quick Build
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

## Build Options
| Option | Default | Description |
|--------|---------|-------------|
| BUILD_CLIENT | ON | Build client application |
| BUILD_SERVER | ON | Build server application |
| BUILD_CLI | ON | Build CLI tools |
| BUILD_TESTS | ON | Build unit tests |
| ENABLE_ENCRYPTION | ON | Enable libsodium encryption |
| ENABLE_FLUTTER | ON | Enable Flutter UI |

## Cross-compilation
See `scripts/build_android.sh` and `scripts/build_ios.sh` for mobile platform builds.
