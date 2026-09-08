// main.cpp — cppdesk step 08 (frozen snapshot, part 5 of upstream d1013487).
//
// src/main.rs argv dispatch is deferred until server/ui/platform land, so the
// binary stays a step placeholder — but it now links the rustdesk::common
// app module (NAT/rendezvous/update tasks, key helpers) alongside hbb_common.
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   ctest --test-dir /tmp/b --output-on-failure

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 08 (d1013487 part 5): + app common layer"
                 " — client/server/ui pending.\n";
    return 0;
}
