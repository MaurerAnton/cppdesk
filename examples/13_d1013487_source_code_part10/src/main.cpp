// main.cpp — cppdesk step 13 (frozen snapshot, part 10 of upstream d1013487).
//
// src/main.rs argv dispatch is deferred until ipc/platform/ui land, so the
// binary stays a step placeholder — now linking the server foundation
// (service framework, ConnInner queue, service shells, Server registry,
// zombie reaper) alongside client/mediator/hbb_common.
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   ctest --test-dir /tmp/b --output-on-failure

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 13 (d1013487 part 10): + server foundation"
                 " — connection/ui/platform pending.\n";
    return 0;
}
