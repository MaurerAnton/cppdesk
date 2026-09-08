# Step 4 — "source code", part 1 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

Upstream drops 175 files / ~35K lines — the first real code. One turn cannot
port that accurately, so this commit is translated in ordered parts; this step
is **part 1: workspace + `hbb_common` core**. Later steps continue the same
upstream commit (config/fs, transport, protos, app modules, platform, UI)
before the next upstream commit (`f43f5df9`) begins.

## What changed vs step 03 (full manifest)

| Upstream group | Files | C++ translation in this step |
|---|---|---|
| `Cargo.toml` [package]/[workspace]/[profile] | 1 | Root `CMakeLists.txt`: `project(cppdesk VERSION 1.1.2)`, workspace section notes scrap/enigo as future native modules. Full `[dependencies]` mapping below; `[profile.release]` strip note carried into README building section later with packaging |
| `Cargo.lock` (generated) | 1 | Skipped like any lockfile: no C++ equivalent at this stage (later steps vendor via system packages) |
| `LICENSE` (GPL-3.0 text) | 1 | `LICENSE`, byte-identical copy |
| `README.md` (+30 lines: tagline, sciter deps, vcpkg build, file structure) | 1 | Quoted below for reference; product README grows once app modules land |
| `build.rs` | 1 | Mapped into CMake: `build_windows()` → `if(WIN32) target_sources(... windows.cc)` (WtsApi32 stays commented out, as upstream); `gen_version()` → `configure_file(cmake/version.hpp.in)` producing `cppdesk::kVersion`; `build_manifest` (inline feature) + `install_oboe` (Android) → deferred with their feature steps; macOS `ApplicationServices` link → deferred with the platform step |
| `.gitignore` (8 lines) | 1 | Merged into root `.gitignore`, each line mapped in a comment (`/target` → `build/`, `src/version.rs` → binary-dir generated header, `src/ui/inline.rs` → UI step) |
| `src/windows.cc` (Win32 service helpers, already C++) | 1 | `src/platform/windows.cc`, byte-identical, compiled only under `if(WIN32)` |
| `src/tray-icon.ico` | 1 (bin) | `src/tray-icon.ico`, byte-identical |
| `libs/hbb_common`: `lib.rs` (AddrMangle, version/url/socket helpers, macros) | 1 | `addr_mangle.{hpp,cpp}` + `util.{hpp,cpp}`: full port except async (`sleep`/`timeout`/`Stream`) and re-exports, which move with the transport step |
| `libs/hbb_common`: `bytes_codec.rs` (+ 6 tests) | 1 | `bytes_codec.{hpp,cpp}` full port; all 6 tests ported assertion-for-assertion to `tests/test_bytes_codec.cpp` |
| `libs/hbb_common`: `compress.rs` (zstd, no tests) | 1 | `compress.{hpp,cpp}` full port against system libzstd (offline-safe `find_path`/`find_library`, no FetchContent); new round-trip test guards the wiring |
| `libs/hbb_common`: `config.rs`, `fs.rs` | 2 | Deferred → step 05 |
| `libs/hbb_common`: `tcp.rs`, `udp.rs` (+ `new_socket` use) | 2 | Deferred → step 06 (`new_socket` itself is ported now in `util.cpp` so the transport step has its dependency) |
| `libs/hbb_common`: `quic.rs` (feature-gated) | 1 | Deferred with the transport step (notes the `quic` feature flag) |
| `libs/hbb_common`: `build.rs` (protobuf codegen) + `protos/*.proto` | 3 | `.proto` files carried over byte-identical under `libs/hbb_common/protos/`; codegen + C++ struct mapping → protos step |
| `src/*.rs` app modules (client, server/*, common, cli, ipc, rendezvous_mediator, port_forward, platform/*, ui/*, lib/main) | ~20 | Deferred → steps 07+ in dependency order |
| Vendored third-party forks: enigo (26), magnum-opus (13), parity-tokio-ipc (13), pulsectl (11), scrap (31), systray-rs (15) | 122 | NOT carried over (same rule as progressive-server: no external Rust deps in translation). Each becomes a native C++ module in a later step (capture, input, audio, IPC, tray), mirroring the old `libs/` layout |

New upstream `README.md` section for reference (guides later steps, not built yet):

```md
## Dependence
Desktop versions use sciter for GUI, please download sciter dynamic library yourself.
## How To Build
* Prepare your Rust development env and C++ build env
* Install vcpkg, and set VCPKG_ROOT env variable correctly
   - Windows: vcpkg install libvpx:x64-windows-static libyuv:x64-windows-static opus:x64-windows-static
   - Linux/Osx: vcpkg install libvpx libyuv opus
* cargo run
```

## Implementation details

- Error-model mapping (used throughout): `anyhow::Result<T>` → return-T-or-throw
  (`bail!` → `throw std::runtime_error`); `allow_err!` → `HBB_ALLOW_ERR` (silent,
  matching `log::debug!` with no initialized logger).
- `AddrMangle` integer layout is 1:1 (`unsigned __int128`, native-endian
  memcpy, trailing-zero trim); needs GCC/Clang until the Windows step adds an
  MSVC path.
- `get_version_from_url` is byte-wise (delimiters are ASCII; upstream only feeds
  ASCII release URLs) with a manual `parse::<i32>` equivalent.
- `compress` keeps upstream's thread-local reused zstd contexts and the
  `30*len` clamp heuristic for the decompression bound.
- `src/main.rs` stays unported (needs client/server/ui) — the binary remains a
  step placeholder, now linking `hbb_common` and printing `kVersion`.

## Smoke test

```console
$ cmake -B /tmp/b-04 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-04 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-04/cppdesk
cppdesk 1.1.2 step 04 (d1013487 part 1): workspace + hbb_common core — app modules pending.
$ ctest --test-dir /tmp/b-04 --output-on-failure
100% tests passed, 3 tests passed out of 3
```
