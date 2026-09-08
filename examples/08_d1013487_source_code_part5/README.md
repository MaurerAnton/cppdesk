# Step 8 — "source code", part 5 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 5 of upstream commit d1013487**. So far: workspace + core
(04), config/fs-TOML (05), transport (06), protobuf structs (07). This part
ports the **app entry layer**: `src/common.rs` (365L) + `src/lib.rs` (module
map) + the portable core of `src/main.rs`/`src/cli.rs` analysis. `cli.rs` and
`main.rs` themselves stay deferred (they need `client.rs` first).

## What changed vs step 07

| Upstream file | C++ translation |
|---|---|
| `src/common.rs` (365L) | `include/rustdesk/common.hpp` + `src/common.cpp` (`rustdesk::` namespace, compiled into the `cppdesk` binary): key predicates, time/version/port helpers, username/run_me, CONTENT/SOFTWARE_UPDATE_URL globals, NAT/rendezvous/update background tasks |
| `src/lib.rs` (29L, module map) | No code file — the map lives in CMake: `hbb_common` lib + app sources in the `cppdesk` target. Per-module rows are in this table |
| `src/cli.rs` (94L) | Deferred → client step: `Session` needs `client::Interface`/`Data`/`LoginConfigHandler`; `start_one_port_forward` needs `port_forward::listen` |
| `src/main.rs` (148L) | Deferred → entry step: argv dispatch needs `server`/`ui`/`platform`. Root binary stays a placeholder (now linking `rustdesk::common`) |
| `libs/hbb_common/src/config.rs` (remainder) | **Completed here**: all ~40 `impl Config`/`Config2`/`PeerConfig` method bodies (paths, TOML load/store, every getter/setter, latency map, peers(), import/save_tmp). Step 05 had only structs + converters |
| `libs/hbb_common/src/{tcp,udp}.hpp` | Corrective fix: `send()` templates now use `SerializeAsString()` (protobuf C++ API; `write_to_bytes()` doesn't exist). Plus new `TcpFramedStream::local_addr()` (getsockname) for NAT-test port re-binding |

## Implementation details

- **Async → threads**: `#[tokio::main]` entry points become detached
  `std::thread` workers over the blocking streams from step 06
  (`test_nat_type`, `test_rendezvous_server`, `check_software_update`).
  `join_all` → `std::thread::join` loop.
- **Desktop IPC override deferred**: upstream desktop `get_rendezvous_server(ms)`/
  `get_nat_type(ms)` delegate to the IPC daemon; here they read `Config`
  directly (mobile parity). The override lands with the ipc step.
- **Lock discipline**: `shared_mutex` is not recursive — `get_rendezvous_servers()`
  snapshots `serial` under lock, releases, then reads options (a nested
  `shared_lock` would self-deadlock; caught during this step).
- **Config globals** (`config.hpp`-declared locks + `rng`/`app_dir_opt`) had
  duplicate anonymous-namespace definitions from step 05 shadowing the header
  declarations — consolidated to single namespace-scope definitions (build
  break caught during this step).
- **Path scheme** (`ProjectDirs` parity): `$XDG_CONFIG_HOME/RustDesk`,
  macOS `~/Library/Application Support/com.carriez/RustDesk` + `patch()` to
  `Preferences`, Windows `%APPDATA%/RustDesk` + service-profile `patch()`.
  Open fidelity item: exact directories-next nesting/case unverified against
  a real install — each branch is isolated for a one-line fix.
- **Deferred with owners**: `check_clipboard`/`update_clipboard` → clipboard
  step (`ClipboardContext`); `resample_channels` → audio step (dasp);
  `get_key_pair` lazy-gen throws until crypto step; `get_auto_id` empty until
  platform step (MAC lookup); `run_me` uses `/proc/self/exe` (Linux; platform
  step refines).

## Tests

`tests/test_common.cpp` (hermetic, no network/files): all four key predicates
(incl. empty-union and boundary values), `get_time` monotonicity,
`check_port`, `get_version_number` (incl. garbage → 0), `test_if_valid_server`
(localhost resolves without connecting; `.invalid` never resolves).

## Smoke test

```console
$ cmake -B /tmp/b-08 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-08 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-08/cppdesk
cppdesk 1.1.2 step 08 (d1013487 part 5): + app common layer — client/server/ui pending.
$ ctest --test-dir /tmp/b-08 --output-on-failure
100% tests passed, 9 tests passed out of 9
```

Next in `d1013487` parts: `fs.rs` file-transfer jobs (part 6), then
`rendezvous_mediator.rs` / `client.rs` / `server/*` / `platform/*` / `ui`.
