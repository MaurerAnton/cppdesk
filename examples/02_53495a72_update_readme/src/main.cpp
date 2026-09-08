// main.cpp — cppdesk step 02 (frozen snapshot).
//
// Translation of rustdesk/rustdesk@53495a72 ("Update README.md", 2020-09-28).
// Rust original: README.md only — replaces the GitHub profile template with
// the real project tagline ("### RustDesk | Your Remote Desktop Software",
// one-line description, DOWNLOAD link). Still no Rust sources, no Cargo.toml,
// no code of any kind.
//
// C++ translation: project bootstrap placeholder (unchanged shape vs step 01).
// There is still nothing to port, so the executable only announces the step
// and exits 0. Real functionality starts arriving in later steps (first real
// upstream sources land with d1013487 "source code").
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b-02 -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b-02 --parallel "$JOBS" && /tmp/b-02/cppdesk

#include <iostream>

int main() {
    std::cout << "cppdesk step 02 (53495a72): update README — no functionality yet.\n";
    return 0;
}
