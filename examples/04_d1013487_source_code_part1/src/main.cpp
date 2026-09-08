// main.cpp — cppdesk step 04 (frozen snapshot, part 1 of upstream d1013487).
//
// src/main.rs (148 lines: argv dispatch into client/server/ui entry points)
// is deferred until its callees are ported, so the binary stays a step
// placeholder for now — but it already links the hbb_common core ported in
// this step and reports the generated version (gen_version() parity).
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk && \
//   cmake --build /tmp/b --target test / ctest --test-dir /tmp/b

#include <cppdesk/version.hpp>

#include <iostream>

int main() {
    std::cout << "cppdesk " << cppdesk::kVersion
              << " step 04 (d1013487 part 1): workspace + hbb_common core"
                 " — app modules pending.\n";
    return 0;
}
