# Step 5 — "source code", part 2 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 2 of upstream commit d1013487** (part 1 was step 04). Part 1
ported the workspace skeleton + `hbb_common` pure-logic core (AddrMangle,
util, BytesCodec, compress). This part adds the **config/fs layer**:

## What changed vs step 04 (files in this part)

| Upstream file | C++ translation |
|---|---|
| `libs/hbb_common/src/config.rs` (688 lines) | `libs/hbb_common/include/hbb_common/config.hpp` + `libs/hbb_common/src/config.cpp`: full port of `Config` / `Config2` / `PeerConfig` TOML persistence (confy → custom minimal TOML parser), global state (`Arc<RwLock>`/`Arc<Mutex>` → `shared_mutex`/`mutex`), auto-id/password/salt/key_pair generators, path helpers (`patch`, `ProjectDirs`, `log_path`, `ipc_path`, `icon_path`, rendezvous server resolution). |
| `libs/hbb_common/src/fs.rs` (554 lines) | **Deferred to step 06+** — `read_dir`, `TransferJob`, async file transfer (`tokio::fs`), and message builders depend on the transport layer (`Stream`, `tcp`/`udp`) and protobuf message structs (`FileDirectory`, `FileEntry`, etc.). The synchronous path helpers (`get_file_name`, `get_string`, `get_path`, `get_home_as_string`, `get_recursive_files`) are simple `std::filesystem` wrappers and could be ported now, but they are only used by the deferred async code so they move with it. |
| `libs/hbb_common/protos/*.proto` | Already copied verbatim in step 04 (message.proto, rendezvous.proto). C++ codegen from `.proto` + struct mapping → protos step. |
| ICON constants (3 platform variants) | Already handled in step 04 via generated `icon.hpp` (byte-identical). |

## Implementation details

- **Minimal TOML parser/emitter** (`toml.hpp`/`toml.cpp`): implements exactly the subset confy uses for these structs — basic strings, integers, bools, arrays, single-level `[table]` sections. No floats, datetimes, dotted keys, or `[[array-of-tables]]`. Parser throws `TomlError` (`std::runtime_error`) on any unsupported construct so format changes fail loudly.
- **Error-model mapping**: `anyhow::Result` → return-by-value or throw (`std::runtime_error`); `allow_err!` → `HBB_ALLOW_ERR` (silent, matching `log::debug!` without initialized logger).
- **Global state**: `lazy_static Arc<RwLock<Config>>`/`Config2` → `std::shared_mutex` + function-local statics (thread-safe init). `Arc<Mutex<HashMap>> ONLINE` → `std::mutex` + `unordered_map`.
- **Crypto/platform stubs**: `sodiumoxide::sign::gen_keypair` → throws until crypto step; `mac_address::get_mac_address` → returns empty; `filetime::set_file_mtime` → no-op.
- **Android/iOS `APP_DIR`**: process-global `std::optional<std::string>`.
- **Random**: `rand::thread_rng` → `std::mt19937` seeded from `random_device`.
- **Rendezvous server logic** (`get_rendezvous_server` / `get_rendezvous_servers`): 1:1 port of upstream fallback chain (custom option → Config2 value → hardcoded `RENDEZVOUS_SERVERS`).
- **Tests**: added `test_toml.cpp` (parser/emitter round-trip) and `test_config.cpp` (Config/Config2/PeerConfig TOML round-trip, parity with upstream `test_serialize`).

## Smoke test

```console
$ cmake -B /tmp/b-05 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-05 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-05/cppdesk
cppdesk 1.1.2 step 05 (d1013487 part 2): workspace + hbb_common + config/fs — app modules pending.
$ ctest --test-dir /tmp/b-05 --output-on-failure
100% tests passed, 5 tests passed out of 5
```

## ICON regeneration (if upstream changes `ICON` constants)

1. Extract the three `pub const ICON` string literals from `config.rs` in source order (macOS, Windows, Linux — same order as `#[cfg]` attributes).
2. Run the generator script (embedded in step 04 README / `examples/04_*/README.md`) or manually update `libs/hbb_common/include/hbb_common/icon.hpp`:
   - Escape each string for C++ (`"` → `\"`, `\` → `\\`, newline → `\n`, etc.).
   - Place under `#if defined(__APPLE__)`, `#elif defined(_WIN32)`, `#elif defined(__linux__)`.
   - Verify byte-identical with `diff <(python3 -c "import re; ...") icon.hpp`.
3. Commit the updated `icon.hpp`; no other changes needed.