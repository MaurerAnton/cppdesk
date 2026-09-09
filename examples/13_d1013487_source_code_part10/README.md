# Step 13 — "source code", part 10 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 10 of upstream commit d1013487**: the **server foundation** —
`service.rs` framework, `ConnInner`, the five service shells, the `Server`
registry, and the zombie reaper. The `Connection` event loop, `start_server`,
and the relay/accept entry points need `ipc` + `platform` + capture/playback
and arrive in later parts.

## What changed vs step 12

| Upstream item | C++ translation |
|---|---|
| `service.rs` (249L: Service/ServiceTmpl/ServiceSwap/Subscriber/Reset) | `include/rustdesk/service.hpp`: full template port. `Arc<RwLock>` → shared state + `shared_mutex`; `Box<dyn Service>` → `shared_ptr<Service>`; `Result<()>` callbacks → void + exceptions; `id()` renamed `get_id()` (C++ cannot share the field's name); `ServiceSwap::has_subscribes` keeps its main-set-only quirk verbatim |
| `ConnInner` + sender queue (`connection.rs` 27–70) | `include/rustdesk/connection.hpp` + `src/connection.cpp`: id + unbounded `ConnQueue` (mutex/deque/condvar; send never blocks, `None` queue is a silent no-op) |
| `audio/video/clipboard/input_service::new_*` + NAMEs | shells in `src/{audio,video,clipboard,input}_service.cpp`: exact names (`audio`, `video`, `clipboard`, `mouse_cursor`, `mouse_pos`) and `need_snapshot` flags; worker bodies deferred with their native modules |
| `MouseCursorSub` (cursor-id cache) | ported in full (pure message logic): first delivery forwards whole, repeats forward cached id-only |
| `Server` + `new()` + add/remove/subscribe + `Drop` | `src/server.cpp`: `shared_ptr<Server>` with internal locks (each op atomic, like the `RwLock` guards); `next_id()` for the connection counter; `find_service` introspection (no upstream counterpart — the map is otherwise private) |
| `CHILD_PROCESS` + `check_zombie` | pid list + `track_child_pid` seam (platform pushes here) + detached 100ms `waitpid/WNOHANG` reaper; Windows branch deferred |

## Bug found by the new test (real defect class, fixed in-step)

`~ServiceSwap` unconditionally promoted `new_subscribes` — but the C++
callback chain **moves** the swap object several times (function argument,
lambda parameter), and every moved-from temporary's destructor ran the
promotion on null state (segfault in `wrlock`). Rust moves have a single
`Drop` owner, so upstream needs no guard. Fix: the destructor skips
null-state holders. Found via gdb backtrace after bisecting with minimal
probes (shared_mutex, -O3, virtual bases all exonerated first).

## Tests

`tests/test_server.cpp` (hermetic): queue order/identity/drain/close,
framework fan-out identity + `send_without` exclusion + snapshot stage →
promote → `join`, cursor cache (full then id-only then passthrough),
all five service names, registry add/noperms/remove/subscribe-toggle,
`next_id`, and a real fork+`_exit` zombie reaped through the sweeper.

## Smoke test

```console
$ cmake -B /tmp/b-13 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-13 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-13/cppdesk
cppdesk 1.1.2 step 13 (d1013487 part 10): + server foundation — connection/ui/platform pending.
$ ctest --test-dir /tmp/b-13 --output-on-failure
100% tests passed, 14 tests passed out of 14
```

Next in `d1013487` parts: `Connection` event loop (needs `ipc.rs`), `ipc.rs`
+ `port_forward.rs`, `platform/*`, service capture/playback bodies, `ui`,
entry (`main.rs` + `cli.rs`).
