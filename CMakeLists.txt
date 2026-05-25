cmake_minimum_required(VERSION 3.20)
project(cppdesk VERSION 1.3.0 LANGUAGES C CXX
  DESCRIPTION "C++ rewrite of RustDesk — cross-platform remote desktop (client + server)"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Platform detection
if(WIN32)
  set(PLATFORM_WINDOWS ON)
elseif(APPLE)
  set(PLATFORM_MACOS ON)
elseif(ANDROID)
  set(PLATFORM_ANDROID ON)
elseif(IOS)
  set(PLATFORM_IOS ON)
else()
  set(PLATFORM_LINUX ON)
endif()

# Build options
option(BUILD_CLIENT "Build client application" ON)
option(BUILD_SERVER "Build server application" ON)
option(BUILD_CLI "Build CLI tools" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_FLUTTER "Enable Flutter UI" ON)
option(ENABLE_WEBSOCKET "Enable WebSocket support" ON)
option(ENABLE_ENCRYPTION "Enable end-to-end encryption (libsodium)" ON)

include(FetchContent)
include(GNUInstallDirs)

# ---- Dependencies via FetchContent ----

# Protobuf
FetchContent_Declare(protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG v28.3
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(protobuf)

# nlohmann/json
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# libsodium (for crypto if enabled)
if(ENABLE_ENCRYPTION)
  FetchContent_Declare(sodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG 1.0.20-RELEASE
  )
  FetchContent_MakeAvailable(sodium)
endif()

# OpenSSL
find_package(OpenSSL REQUIRED)

# asio (header-only networking)
FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)
if(NOT TARGET asio)
  add_library(asio INTERFACE)
  target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
  target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
endif()

# spdlog
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# CLI11 (for CLI tools)
if(BUILD_CLI)
  FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
  )
  FetchContent_MakeAvailable(cli11)
endif()

# Google Test
if(BUILD_TESTS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  FetchContent_MakeAvailable(googletest)
  enable_testing()
endif()

# SQLite3 (for server)
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip
  )
  FetchContent_MakeAvailable(sqlite3)
  add_library(SQLite::SQLite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
  target_include_directories(SQLite::SQLite3 PUBLIC ${sqlite3_SOURCE_DIR})
endif()

# Generate protobuf files
file(GLOB PROTO_FILES "${CMAKE_SOURCE_DIR}/proto/*.proto")
if(PROTO_FILES)
  protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
endif()

# ---- Platform-specific libraries ----
if(PLATFORM_WINDOWS)
  list(APPEND PLATFORM_LIBS ws2_32 user32 gdi32 ole32 oleaut32 shell32
    comctl32 advapi32 winmm version iphlpapi psapi dwmapi
    d3d11 dxgi setupapi)
elseif(PLATFORM_LINUX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
  pkg_check_modules(X11 REQUIRED x11)
  pkg_check_modules(XFIXES REQUIRED xfixes)
  pkg_check_modules(XRANDR REQUIRED xrandr)
  pkg_check_modules(PULSEAUDIO REQUIRED libpulse)
  list(APPEND PLATFORM_LIBS pthread dl rt)
elseif(PLATFORM_MACOS)
  find_library(COCOA_LIB Cocoa REQUIRED)
  find_library(IOKIT_LIB IOKit REQUIRED)
  find_library(COREGRAPHICS_LIB CoreGraphics REQUIRED)
  find_library(COREVIDEO_LIB CoreVideo REQUIRED)
  find_library(COREMEDIA_LIB CoreMedia REQUIRED)
  find_library(AVFOUNDATION_LIB AVFoundation REQUIRED)
  find_library(SYSTEM_CONFIG SystemConfiguration REQUIRED)
  list(APPEND PLATFORM_LIBS ${COCOA_LIB} ${IOKIT_LIB} ${COREGRAPHICS_LIB}
    ${COREVIDEO_LIB} ${COREMEDIA_LIB} ${AVFOUNDATION_LIB} ${SYSTEM_CONFIG})
endif()

# ---- Include directories ----
include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/scrap/include
  ${CMAKE_SOURCE_DIR}/libs/enigo/include
  ${CMAKE_SOURCE_DIR}/libs/clipboard/include
  ${CMAKE_SOURCE_DIR}/libs/portable/include
  ${CMAKE_SOURCE_DIR}/libs/virtual_display/include
  ${CMAKE_SOURCE_DIR}/libs/remote_printer/include
  ${CMAKE_BINARY_DIR}  # for generated protobuf headers
)

# ---- Subdirectories ----
add_subdirectory(libs/common)
add_subdirectory(libs/scrap)
add_subdirectory(libs/enigo)
add_subdirectory(libs/clipboard)
add_subdirectory(libs/portable)
add_subdirectory(libs/virtual_display)
add_subdirectory(libs/remote_printer)
add_subdirectory(src)

# ---- Install ----
install(TARGETS cppdesk_client cppdesk_server cppdesk_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---- Packaging (CPack) ----
set(CPACK_PACKAGE_NAME "cppdesk")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform remote desktop (client + server)")
set(CPACK_PACKAGE_VENDOR "cppdesk")
set(CPACK_GENERATOR "DEB;RPM;NSIS;DMG;ZIP")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "cppdesk team")
include(CPack)
