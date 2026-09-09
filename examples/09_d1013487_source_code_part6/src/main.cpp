// main.cpp — cppdesk step 09 (frozen snapshot, part 6 of upstream d1013487).
//
// src/main.rs argv dispatch is deferred until server/ui/platform land, so the
// binary stays a step placeholder — now linking hbb_common with the fs
// file-transfer jobs alongside core/config/transport/protos + rustdesk::common.
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   ctest --test-dir /tmp/b --output-on-failure

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 09 (d1013487 part 6): + fs file-transfer jobs"
                 " — rendezvous/client/server pending.\n";
    return 0;
}
