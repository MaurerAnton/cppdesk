# Frozen snapshot of step 13 (part 10 of upstream d1013487). Identical to the
# root build at step 13 except for the project name.
cmake_minimum_required(VERSION 3.20)
# Version parity: Cargo.toml [package] version = "1.1.2".
project(cppdesk_13_d1013487_source_code_part10 VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# build.rs gen_version() parity: version string available to code as a header.
configure_file(
    cmake/version.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/cppdesk/version.hpp"
    @ONLY
)

# Workspace parity: Cargo.toml [workspace] members scrap/hbb_common/enigo.
# Only hbb_common exists yet (part 1 of d1013487); scrap/enigo arrive as
# native C++ modules in later steps, mirroring the old cppdesk libs/ layout.
add_subdirectory(libs/hbb_common)

# App crate parity (src/lib.rs module map): first-party app sources compile
# into the cppdesk binary directly. Later steps add client/server/ipc/ui here;
# a librustdesk split happens only if link times demand it.
#
# OpenSSL libcrypto (sha2 crate parity for the login handshake hashes).
# System package, offline-safe lookup like zstd/protobuf.
find_path(OPENSSL_INCLUDE_DIR NAMES openssl/sha.h)
find_library(CRYPTO_LIBRARY NAMES crypto)
if(NOT OPENSSL_INCLUDE_DIR OR NOT CRYPTO_LIBRARY)
    message(FATAL_ERROR "libcrypto dev files not found (need openssl/sha.h + libcrypto)")
endif()

add_executable(cppdesk src/main.cpp src/common.cpp src/rendezvous_mediator.cpp src/client.cpp
                       src/key_map.cpp src/connection.cpp src/server.cpp src/audio_service.cpp
                       src/video_service.cpp src/clipboard_service.cpp src/input_service.cpp)
target_include_directories(
    cppdesk
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        include
        # generated protobuf headers (common.hpp includes message.pb.h)
        libs/hbb_common/protos/gen
        ${OPENSSL_INCLUDE_DIR}
)
target_link_libraries(cppdesk PRIVATE hbb_common ${PROTOBUF_LIBRARY} ${CRYPTO_LIBRARY})
# NOTE: PROTOBUF_LIBRARY is found in libs/hbb_common (find_library caches
# globally, so it is visible here); common.cpp calls libprotobuf directly.

if(WIN32)
    # build.rs build_windows() parity: compile src/windows.cc into the binary.
    target_sources(cppdesk PRIVATE src/platform/windows.cc)
    # Upstream leaves WtsApi32 commented out — same here:
    # target_link_libraries(cppdesk PRIVATE WtsApi32)
endif()

include(CTest)
add_subdirectory(tests)
