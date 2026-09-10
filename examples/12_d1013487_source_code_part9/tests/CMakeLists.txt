cmake_minimum_required(VERSION 3.20)
# Mirrors the matching test executables' sources 1:1 (see ../test_*.cpp).
# New tests for newly ported modules get their own executable + add_test here.

function(cppdesk_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE hbb_common)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# libs/hbb_common/src/bytes_codec.rs #[cfg(test)] parity (test_codec1..6).
cppdesk_test(test_bytes_codec)
# libs/hbb_common/src/lib.rs test_mangle parity.
cppdesk_test(test_addr_mangle)
# compress round-trip (upstream has no compress test; guards the zstd wiring).
cppdesk_test(test_compress)
# toml parser/emitter round-trip (guards confy parity).
cppdesk_test(test_toml)
# Config/Config2/PeerConfig TOML round-trip (upstream test_serialize).
cppdesk_test(test_config)
# TCP transport: framing, listener, raw mode.
cppdesk_test(test_tcp)
# UDP transport: bind, bind_reuse.
cppdesk_test(test_udp)
# protoc-generated structs: wire round-trips (upstream has no proto tests).
cppdesk_test(test_protos)
# src/common.rs pure helpers (key predicates, version/port strings).
# Needs the app include dir (rustdesk/common.hpp lives at the root).
cppdesk_test(test_common)
target_include_directories(test_common PRIVATE ../include)
# The app object itself (src/common.cpp) is compiled into the test so the
# helpers are exercised, not stubbed.
target_sources(test_common PRIVATE ../src/common.cpp)
target_include_directories(
    test_common
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)
# fs.rs file-transfer jobs (read_dir, recursive enum, block round-trip).
cppdesk_test(test_fs)
# crypto seam round-trips (upstream has no crypto unit tests).
cppdesk_test(test_crypto)
# client.rs login config + handshake helpers (hermetic HOME redirect +
# loopback TCP). Compiles the app objects it exercises.
cppdesk_test(test_client)
target_include_directories(test_client PRIVATE ../include)
target_sources(test_client PRIVATE ../src/client.cpp ../src/common.cpp ../src/key_map.cpp
                                   ../src/rendezvous_mediator.cpp)
target_include_directories(
    test_client
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)
find_path(TEST_OPENSSL_INCLUDE_DIR NAMES openssl/sha.h)
find_library(TEST_CRYPTO_LIBRARY NAMES crypto)
target_include_directories(test_client PRIVATE ${TEST_OPENSSL_INCLUDE_DIR})
target_link_libraries(test_client PRIVATE ${TEST_CRYPTO_LIBRARY} ${PROTOBUF_LIBRARY})
# rendezvous_mediator.rs helpers (prefix, uuid, wire shapes; loopback
# integration deferred to crypto step). Needs app + generated includes.
cppdesk_test(test_mediator)
target_include_directories(test_mediator PRIVATE ../include)
target_sources(test_mediator PRIVATE ../src/rendezvous_mediator.cpp ../src/common.cpp)
target_include_directories(
    test_mediator
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)
