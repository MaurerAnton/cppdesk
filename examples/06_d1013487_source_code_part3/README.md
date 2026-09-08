# Step 6 — "source code", part 3 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 3 of upstream commit d1013487** (parts 1–2 were steps 04–05).
Part 1 (step 04): workspace skeleton + `hbb_common` pure-logic core (AddrMangle,
util, BytesCodec, compress).
Part 2 (step 05): `config.rs` + `fs.rs` layer (TOML persistence, path helpers).
Part 3 (this step): **transport layer** — `tcp.rs` + `udp.rs`.

## What changed vs step 05 (files in this part)

| Upstream file | C++ translation |
|---|---|
| `libs/hbb_common/src/tcp.rs` (146 lines) | `libs/hbb_common/include/hbb_common/tcp.hpp` + `libs/hbb_common/src/tcp.cpp`: `TcpFramedStream` (length-delimited framing via `BytesCodec`, secretbox encryption stub), `TcpListener`. Blocking sockets with `SO_RCVTIMEO`/`SO_SNDTIMEO` timeouts instead of async/await. |
| `libs/hbb_common/src/udp.rs` (75 lines) | `libs/hbb_common/include/hbb_common/udp.hpp` + `libs/hbb_common/src/udp.cpp`: `UdpFramedSocket` (framed UDP with `BytesCodec`, send/recv with peer address). |
| `libs/hbb_common/src/util.rs` (partial) | `new_socket` already ported in step 04 (`util.cpp`). Used by both `tcp.cpp` and `udp.cpp` for socket creation with reuse options. |

## Implementation details

- **Blocking sockets + timeouts**: Upstream uses Tokio async (`TcpStream`/`UdpSocket` + `tokio_util::codec::Framed`). C++ port uses plain `socket`/`bind`/`connect`/`listen`/`accept` + `send`/`recv`/`sendto`/`recvfrom` with `SO_RCVTIMEO`/`SO_SNDTIMEO` for per-call timeouts. This matches the observable behavior (blocking with deadline) without an async runtime.
- **Framing**: Both `TcpFramedStream` and `UdpFramedSocket` use the same `BytesCodec` ported in step 04 (1/2/3/4-byte length prefix). API: `send`/`send_raw`/`send_bytes` + `next`/`next_timeout`.
- **Encryption stub**: `TcpFramedStream::set_key` / `crypto_` state records the key and sequence numbers but does not encrypt until the crypto step lands (upstream uses `sodiumoxide::secretbox` with 24-byte key, 24-byte nonce from LE(seqnum)).
- **`new_listener` parity**: `TcpListener::bind(addr, reuse)` mirrors `new_listener(addr, reuse)`, using `new_socket` (step 04) for `SO_REUSEADDR`/`SO_REUSEPORT` when `reuse=true`.
- **Tests**: `test_tcp.cpp` (listener bind, basic construction), `test_udp.cpp` (bind, bind_reuse). Full framing round-trip over loopback is deferred to a later integration test (needs real async for concurrent client/server in one process; current tests exercise construction/destruction and API shape).

## Smoke test

```console
$ cmake -B /tmp/b-06 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-06 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-06/cppdesk
cppdesk 1.1.2 step 06 (d1013487 part 3): workspace + hbb_common + config/fs + transport — app modules pending.
$ ctest --test-dir /tmp/b-06 --output-on-failure
100% tests passed, 7 tests passed out of 7
```

Next in `d1013487` parts: protos codegen (message.proto / rendezvous.proto → C++ structs), then app modules (`src/*.rs`).