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
