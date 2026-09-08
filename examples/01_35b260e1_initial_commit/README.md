# Step 1 — "Initial commit" (RustDesk `35b260e1`)

Source: [`rustdesk/rustdesk@35b260e1`](https://github.com/rustdesk/rustdesk/commit/35b260e13a7b135f0a9844c27a05316eceeadbcd)
(AuthorDate 2020-09-28; root commit of upstream `master`.)

Upstream `master` parent chain from here:
`35b260e1` → `53495a72` ("Update README.md") → `002fce13` ("funnding") →
`d1013487` ("source code" — first real Rust sources).

## What changed vs step 0

| Rust change | C++ translation |
|---|---|
| New repository containing only `README.md` (16 lines: the default GitHub profile template — `### Hi there 👋` header plus commented-out suggestion list). No code, no Cargo files, no build system. | Same state in C++ terms: minimal `CMakeLists.txt` (C++20, single `cppdesk` target), placeholder `src/main.cpp` that prints the step and exits 0, plus this step README. No external dependencies (there were none upstream, so no `FetchContent`). |

Original `README.md` for reference (nothing to port, prose only):

```md
### Hi there 👋

<!--
**rustdesk/rustdesk** is a ✨ _special_ ✨ repository because its `README.md` (this file) appears on your GitHub profile.

Here are some ideas to get you started:

- 🔭 I'm currently working on ...
- 🌱 I'm currently learning ...
- 👯 I'm looking to collaborate on ...
- 🤔 I'm looking for help with ...
- 💬 Ask me about ...
- 📫 How to reach me: ...
- 😄 Pronouns: ...
- ⚡ Fun fact: ...
-->
```

## Implementation details

- Upstream file is README prose, not code, so this is a 1:1 state
  translation rather than a line-by-line port: empty program + buildable
  CMake skeleton that later steps extend.
- Offline build by design: no network fetches at configure time.
- Live code at the repo root mirrors this snapshot; future steps mutate the
  root and freeze a new copy under `examples/NN_*/`.

## Smoke test

```console
$ cmake -B /tmp/b-01 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-01 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-01/cppdesk
cppdesk step 01 (35b260e1): initial commit — no functionality yet.
```
