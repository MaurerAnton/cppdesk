# cppdesk — step-by-step C++ translation of RustDesk

Line-for-line C++20 translation of [rustdesk/rustdesk](https://github.com/rustdesk/rustdesk),
replayed commit-by-commit along upstream `master`, starting from the first
commit. Each upstream commit becomes one step here; frozen, standalone
snapshots live under `examples/NN_<short-hash>_<slug>/`.

Method mirrors the `stepbystep` branch of `progressive-server`
(one upstream commit ≈ one step + one snapshot).

## Status

| Step | Upstream commit | Date | Description | Snapshot |
|------|-----------------|------|-------------|----------|
| 01 | [`35b260e1`](https://github.com/rustdesk/rustdesk/commit/35b260e13a7b135f0a9844c27a05316eceeadbcd) | 2020-09-28 | Initial commit (README profile template, no code) | `examples/01_35b260e1_initial_commit/` |
| 02 | [`53495a72`](https://github.com/rustdesk/rustdesk/commit/53495a72e4c215277c192aa0e52522c28d3dc439) | 2020-09-28 | Update README.md (real tagline, still no code) | `examples/02_53495a72_update_readme/` |

Next in upstream `master` order: `002fce13` ("funnding"), then `d1013487`
("source code" — first real Rust sources).

Full per-step log: [examples/INDEX.md](examples/INDEX.md).

## Building (root or any snapshot)

Agent builds MUST follow [AGENTS.md](AGENTS.md): gate `-j` on available RAM
(`<1 GB` wait, `1 GB` → `-j1`, `2 GB` → `-j2`, `3 GB+` → `-j3`, never above `-j3`).

```bash
cmake -B /tmp/cppdesk-build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cppdesk-build --parallel "$JOBS"  # $JOBS from AGENTS.md rule
/tmp/cppdesk-build/cppdesk
```

Each `examples/NN_*/` snapshot builds standalone the same way:

```bash
cmake -B /tmp/b-01 -S examples/01_35b260e1_initial_commit -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/b-01 --parallel "$JOBS"
```

## Layout

```
AGENTS.md                   — agent instructions (mandatory RAM-based -j rule)
CMakeLists.txt              — live build (current step state)
src/main.cpp                — live code (current step state)
examples/
  INDEX.md                  — step index (upstream hash, date, subject)
  01_35b260e1_initial_commit/
    CMakeLists.txt          — frozen standalone build for step 01
    README.md               — what upstream changed vs previous step + C++ mapping
    src/main.cpp            — frozen code for step 01
  02_53495a72_update_readme/
    ...                     — same frozen shape for step 02
```

Rule: the repo root always reflects the latest translated step; `examples/`
copies are frozen and never modified after their step lands.
