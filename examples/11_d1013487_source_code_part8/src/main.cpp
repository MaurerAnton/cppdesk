// main.cpp — cppdesk step 11 (frozen snapshot, part 8 of upstream d1013487).
//
// src/main.rs argv dispatch is deferred until server/ui/platform land, so the
// binary stays a step placeholder — now linking the client login layer
// (LoginConfigHandler, handshake helpers, KEY_MAP) alongside the mediator,
// hbb_common and rustdesk::common.
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   ctest --test-dir /tmp/b --output-on-failure

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 11 (d1013487 part 8): + client login layer"
                 " — connection/server pending.\n";
    return 0;
}
