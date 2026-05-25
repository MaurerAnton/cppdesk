#!/bin/bash
# Run benchmarks
set -e
echo "=== cppdesk Benchmarks ==="
echo "Build type: $(cmake -L build 2>/dev/null | grep CMAKE_BUILD_TYPE || echo Release)"
echo ""
echo "Compression benchmark:"
time for i in $(seq 1 100); do
    ./build/cppdesk_cli --benchmark-encode /dev/urandom 2>/dev/null
done
echo ""
echo "Crypto benchmark:"
time for i in $(seq 1 1000); do
    ./build/cppdesk_cli --benchmark-sha256 2>/dev/null
done
