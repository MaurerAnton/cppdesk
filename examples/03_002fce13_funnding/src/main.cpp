// main.cpp — cppdesk step 03 (frozen snapshot).
//
// Translation of rustdesk/rustdesk@002fce13 ("funnding", 2021-03-17).
// Rust original: adds `.github/FUNDING.yml` only (one line:
// `github: [rustdesk]`). Still no Rust sources, no Cargo.toml, no code.
//
// C++ translation: project bootstrap placeholder (unchanged shape vs step 02),
// plus the same `.github/FUNDING.yml` carried over verbatim (GitHub metadata
// has no C++ equivalent). The executable only announces the step and exits 0.
// Real functionality starts arriving in later steps (first real upstream
// sources land with d1013487 "source code").
//
// Smoke test (JOBS per AGENTS.md RAM rule):
//   cmake -B /tmp/b-03 -S . -DCMAKE_BUILD_TYPE=Release && \
//   cmake --build /tmp/b-03 --parallel "$JOBS" && /tmp/b-03/cppdesk

#include <iostream>

int main() {
    std::cout << "cppdesk step 03 (002fce13): funnding — no functionality yet.\n";
    return 0;
}
