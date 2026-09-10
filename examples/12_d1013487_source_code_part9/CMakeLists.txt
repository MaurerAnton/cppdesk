# Frozen snapshot of step 12 (part 9 of upstream d1013487). Identical to the
# root build at step 12 except for the project name.
cmake_minimum_required(VERSION 3.20)
# Version parity: Cargo.toml [package] version = "1.1.2".
project(cppdesk_12_d1013487_source_code_part9 VERSION 1.1.2 LANGUAGES CXX)

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
# Protobuf: prefer the package config (protobuf >= 22 links Abseil, whose
# transitive DSOs must appear on the link line — a raw find_library() link
# fails with "DSO missing from command line"). The interface target below
# is inherited by libs/hbb_common.
find_package(Protobuf CONFIG QUIET)
add_library(cppdesk_protobuf INTERFACE)
if(TARGET protobuf::libprotobuf)
    target_link_libraries(cppdesk_protobuf INTERFACE protobuf::libprotobuf)
else()
    find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/message.h)
    find_library(PROTOBUF_LIBRARY NAMES protobuf)
    if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
        message(FATAL_ERROR "libprotobuf dev files not found (need message.h + libprotobuf)")
    endif()
    target_include_directories(cppdesk_protobuf INTERFACE ${PROTOBUF_INCLUDE_DIR})
    target_link_libraries(cppdesk_protobuf INTERFACE cppdesk_protobuf)
endif()

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
                       src/key_map.cpp)
target_include_directories(
    cppdesk
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        include
        # generated protobuf headers (common.hpp includes message.pb.h)
        ${OPENSSL_INCLUDE_DIR}
)
target_link_libraries(cppdesk PRIVATE hbb_common cppdesk_protobuf ${CRYPTO_LIBRARY})
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
