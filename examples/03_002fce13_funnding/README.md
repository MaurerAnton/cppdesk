# Step 3 — "funnding" (RustDesk `002fce13`)

Source: [`rustdesk/rustdesk@002fce13`](https://github.com/rustdesk/rustdesk/commit/002fce136c5e32e7c1c4b1cf21e834f4b220c0fa)
(AuthorDate 2021-03-17; parent `53495a72`. Subject typo is upstream's.)

## What changed vs step 02

| Rust change | C++ translation |
|---|---|
| New file `.github/FUNDING.yml`, one line: `github: [rustdesk]`. GitHub Sponsors metadata only — no code, no build files. | Carried over verbatim: `.github/FUNDING.yml` with identical bytes (GitHub metadata has no C++ equivalent, so a 1:1 copy is the faithful translation). Placeholder `src/main.cpp` message advances `step 02 → step 03`; CMake skeleton unchanged. |

## Implementation details

- Verified byte-identical: `diff` of upstream blob vs this copy is empty.
- `examples/01_*` and `examples/02_*` stay frozen; this step freezes as
  `examples/03_*` (including its own `.github/FUNDING.yml` copy so the
  snapshot reflects full repo state).

## Smoke test

```console
$ cmake -B /tmp/b-03 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-03 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-03/cppdesk
cppdesk step 03 (002fce13): funnding — no functionality yet.
```
