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
