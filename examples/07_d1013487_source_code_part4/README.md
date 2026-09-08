# Step 7 — "source code", part 4 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 4 of upstream commit d1013487**. So far: workspace + core
(04), config/fs (05), transport tcp/udp (06). This part adds the **protobuf
structs**: `message.proto` (401 lines) + `rendezvous.proto` (133 lines) compiled
with `protoc` into `hbb::` C++ classes.

## What changed vs step 06

| Upstream file | C++ translation |
|---|---|
| `libs/hbb_common/build.rs` (protobuf-codegen-pure invocation) | `protoc --proto_path=protos --cpp_out=protos/gen protos/*.proto`, output checked in under `libs/hbb_common/protos/gen/` (4 files, ~57K lines). Regen command in `libs/hbb_common/CMakeLists.txt`. No protoc needed at build time |
| `libs/hbb_common/protos/message.proto` | Verbatim since step 04, except **C++ scoping fixes** (see below): `Permission.Clipboard` → `PermissionClipboard`, `ImageQuality.NotSet` → `ImageQualityNotSet`, `BoolOption.NotSet` → `BoolOptionNotSet`; added `Unknown`/`FileTypeUnknown`/`PermissionUnknown` zero sentinels (`ControlKey`, `FileType`, `Permission` started at 1, rejected by protoc) |
| `libs/hbb_common/protos/rendezvous.proto` | Verbatim since step 04, except added `ResultUnknown`/`FailureUnknown` zero sentinels (nested `Result`/`Failure` started at 1) |
| (new) generated `hbb::Message`, `hbb::RendezvousMessage` + ~60 sub-messages | Wired into `libhbb_common` (system libprotobuf, offline-safe `find_library`; `protos/gen` on the public include path). `tcp.hpp send()`/`udp.hpp send()` templates can now serialize real messages via `write_to_bytes()`/`SerializeToString()` |

## Wire-compatibility notes (important)

- **Field numbers are untouched** — the wire format is byte-identical to upstream.
- **Only enum *names* changed**, and only where protoc requires uniqueness
  (`PermissionClipboard`, `ImageQualityNotSet`, `BoolOptionNotSet`). The Rust
  side is unaffected (it uses its own codegen from the same numbers).
- **New zero sentinels** (`Unknown`, `FileTypeUnknown`, `PermissionUnknown`,
  `ResultUnknown`, `FailureUnknown`) shift the *default* value: an absent enum
  field now decodes as the sentinel instead of the old first value (e.g.
  `Dir`). Upstream's rust-protobuf accepted non-zero-first enums; standard
  protoc does not. Open fidelity item: confirm no upstream logic depends on
  the old implicit defaults (`FileType::Dir`, `ControlKey::Alt`,
  `Permission::Keyboard`).
- Upstream pins `protobuf 3.0.0-pre` (rust-protobuf fork); here system
  libprotobuf 35 serializes the same proto3 wire format. `sint32 file_num`
  zigzag encoding is covered by `test_block_zigzag` (value `-1`).

## Tests

`tests/test_protos.cpp` (upstream has no proto tests; these lock the contract
later steps rely on): `FileEntry` full-field round-trip, `FileTransferBlock`
zigzag `-1`, `Message` envelope (`file_response` → `error`) incl. oneof
exclusivity, renamed-enum symbols, `RendezvousMessage` → `register_peer`.

## Housekeeping correction

Steps 05/06 updated only their snapshots' binaries; the root `src/main.cpp`
placeholder was stale at step 04. Fixed here — root now prints step 07 and
the step rule is re-stated in the root README layout section.

## Smoke test

```console
$ cmake -B /tmp/b-07 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-07 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-07/cppdesk
cppdesk 1.1.2 step 07 (d1013487 part 4): + protobuf structs — app modules pending.
$ ctest --test-dir /tmp/b-07 --output-on-failure
100% tests passed, 8 tests passed out of 8
```

Next in `d1013487` parts: app modules (`src/common.rs`, `cli.rs`, `lib.rs`,
`main.rs`, then client/server/ipc/rendezvous), then vendored-lib strategy.
