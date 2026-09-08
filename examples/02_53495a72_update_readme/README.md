# Step 2 — "Update README.md" (RustDesk `53495a72`)

Source: [`rustdesk/rustdesk@53495a72`](https://github.com/rustdesk/rustdesk/commit/53495a72e4c215277c192aa0e52522c28d3dc439)
(AuthorDate 2020-09-28, 13 seconds after step 01; parent `35b260e1`.)

## What changed vs step 01

| Rust change | C++ translation |
|---|---|
| `README.md`: deletes the 16-line GitHub profile template (`### Hi there 👋` + commented suggestions), writes the real project header — `### RustDesk \| Your Remote Desktop Software`, one-line description ("This is a repository used to release RustDesk software and track issues."), and a `DOWNLOAD` link to `/releases`. Still no code, no build files. | Same state in C++ terms: placeholder `src/main.cpp` message advances `step 01 → step 02` (new hash + subject); CMake skeleton, step README, and index gain the step-02 entry. Still no dependencies, still offline build. |

New upstream `README.md` in full (prose only, quoted for reference):

```md
### RustDesk | Your Remote Desktop Software

This is a repository used to release RustDesk software and track issues.

[**DOWNLOAD**](https://github.com/rustdesk/rustdesk/releases)
```

## Implementation details

- Upstream diff is README prose, not code, so again a 1:1 state translation,
  not a line-by-line port. The C++ side has no behavior to change; only the
  step identity advances.
- `examples/01_*` stays frozen; this step freezes as `examples/02_*`.

## Smoke test

```console
$ cmake -B /tmp/b-02 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-02 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-02/cppdesk
cppdesk step 02 (53495a72): update README — no functionality yet.
```
