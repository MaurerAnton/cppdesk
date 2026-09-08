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
| 03 | [`002fce13`](https://github.com/rustdesk/rustdesk/commit/002fce136c5e32e7c1c4b1cf21e834f4b220c0fa) | 2021-03-17 | funnding (adds .github/FUNDING.yml, still no code) | `examples/03_002fce13_funnding/` |
| 04 | [`d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb) | 2021-03-29 | source code, part 1: workspace + hbb_common core | `examples/04_d1013487_source_code_part1/` |
| 05 | [`d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb) | 2021-03-29 | source code, part 2: config + fs layer | `examples/05_d1013487_source_code_part2/` |
| 06 | [`d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb) | 2021-03-29 | source code, part 3: transport (tcp/udp) | `examples/06_d1013487_source_code_part3/` |
| 07 | [`d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb) | 2021-03-29 | source code, part 4: protobuf structs | `examples/07_d1013487_source_code_part4/` |

One upstream commit may span several steps when it is too large for a single
accurate port (here `d1013487`: 175 files). Parts are numbered in the step
subject until the commit is fully translated.

Next in upstream `master` order: `d1013487` parts 5+ (app modules),
then `f43f5df9`.

Full per-step log: [examples/INDEX.md](examples/INDEX.md).

## Building (root or any snapshot)

Agent builds MUST follow [AGENTS.md](AGENTS.md): gate `-j` on available RAM
(`<1.5 GB` wait, `1.5 GB` → `-j1`, `3 GB` → `-j2`, `4.5 GB+` → `-j3`, never above `-j3`).

```bash
cmake -B /tmp/cppdesk-build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cppdesk-build --parallel "$JOBS"  # $JOBS from AGENTS.md rule
/tmp/cppdesk-build/cppdesk
```

Each `examples/NN_*/` snapshot builds standalone the same way:

```bash
cmake -B /tmp/b-07 -S examples/07_d1013487_source_code_part4 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/b-07 --parallel "$JOBS"
```

## Layout

```
AGENTS.md                   — agent instructions (mandatory RAM-based -j rule)
CMakeLists.txt              — live build (current step state)
cmake/version.hpp.in        — version template (build.rs gen_version() parity)
LICENSE                     — upstream license, verbatim
libs/hbb_common/
  include/hbb_common/       — ported module headers (addr_mangle, bytes_codec, compress, util,
                              toml, config, tcp, udp)
  src/                      — ported module sources
  protos/                   — upstream .proto files (+ C++ scoping fixes, see step 07)
  protos/gen/               — checked-in protoc output (regen: step 07 README)
src/main.cpp                — live code (current step state)
src/platform/windows.cc     — upstream Win32 helpers, verbatim (WIN32-only build)
src/tray-icon.ico           — upstream icon, verbatim
tests/                      — CTest parity for upstream #[cfg(test)] modules
examples/
  INDEX.md                  — step index (upstream hash, date, subject)
  01_35b260e1_initial_commit/
    CMakeLists.txt          — frozen standalone build for step 01
    README.md               — what upstream changed vs previous step + C++ mapping
    src/main.cpp            — frozen code for step 01
  02_53495a72_update_readme/
    ...                     — same frozen shape for step 02
  03_002fce13_funnding/
    ... + .github/FUNDING.yml — same frozen shape for step 03
  04_d1013487_source_code_part1/
    ... + libs/ tests/ cmake/ LICENSE — full frozen workspace for step 04
  05_d1013487_source_code_part2/
    ... + libs/ tests/ cmake/ LICENSE — full frozen workspace for step 05
  06_d1013487_source_code_part3/
    ... + libs/ tests/ cmake/ LICENSE — full frozen workspace for step 06
  07_d1013487_source_code_part4/
    ... + libs/ tests/ cmake/ LICENSE — full frozen workspace for step 07
```

Rule: the repo root always reflects the latest translated step; `examples/`
copies are frozen and never modified after their step lands.
