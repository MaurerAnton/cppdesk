// main.cpp — cppdesk step 10 (frozen snapshot, part 7 of upstream d1013487).
//
// src/main.rs argv dispatch is deferred until server/ui/platform land, so the
// binary stays a step placeholder — now linking the rendezvous mediator
// (registration loop, PK flows, hole-punching setup) alongside hbb_common
// and rustdesk::common.
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   ctest --test-dir /tmp/b --output-on-failure

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 10 (d1013487 part 7): + rendezvous mediator"
                 " — client/server pending.\n";
    return 0;
}
