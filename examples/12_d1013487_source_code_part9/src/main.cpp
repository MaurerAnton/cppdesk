// main.cpp — cppdesk step 12 (frozen snapshot, part 9 of upstream d1013487).
//
// src/main.rs argv dispatch is deferred until server/ui/platform land, so the
// binary stays a step placeholder — now linking the client connection layer
// (rendezvous punch, relay negotiation, encrypted handshake) over the real
// libsodium crypto seam.
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   ctest --test-dir /tmp/b --output-on-failure

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 12 (d1013487 part 9): + client connection and crypto"
                 " — server pending.\n";
    return 0;
}
