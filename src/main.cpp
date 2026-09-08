// main.cpp — cppdesk step 01.
//
// Translation of rustdesk/rustdesk@35b260e1 ("Initial commit", 2020-09-28).
// Rust original: README.md only — the default GitHub profile-template text
// ("### Hi there ..." plus commented-out suggestions, 16 lines). No Rust
// sources, no Cargo.toml, no build files, no code of any kind.
//
// C++ translation: project bootstrap placeholder. There is nothing to port
// yet, so the executable only announces the step and exits 0. Real
// functionality starts arriving in later steps (first real upstream sources
// land with d1013487 "source code").
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b --parallel "$JOBS" && /tmp/b/cppdesk

#include <iostream>

int main() {
    std::cout << "cppdesk step 01 (35b260e1): initial commit — no functionality yet.\n";
    return 0;
}
