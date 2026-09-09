# Step 12 — "source code", part 9 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 9 of upstream commit d1013487**: the client connection layer
(`Client::start/connect/secure_connection/request_relay/create_relay`) over a
**real libsodium crypto seam**. The seam also unblocks `get_key_pair` (real
lazy keygen now) and `TcpFramedStream` encryption (seal/open per frame).

## What changed vs step 11

| Upstream item | C++ translation |
|---|---|
| `sodiumoxide::{sign, box_, secretbox}` uses | `hbb_common::crypto` (`crypto.hpp`/`crypto.cpp`): every primitive upstream touches — `sign_gen_keypair/sign_sign/sign_verify`, `box_gen_keypair/box_seal/box_open`, `secretbox_gen_key/seal/open`, `seq_nonce` — calling libsodium directly (system package, offline-safe lookup). Errors throw (`Err(())` parity) |
| `TcpFramedStream` crypto paths | `send_raw` seals with incremented seq, `next` opens (failure throws `"decryption error"`, loud and distinct from EOF/timeout); `set_key` enforces 32 bytes; new public `set_timeout_ms` for handshake bounding |
| `Config::get_key_pair` | real lazy generation + store (the crypto-step throw is gone) |
| `Client::start` (punch loop, relay arm) | 1:1 incl. `i * 3000` attempt timeouts, failure arms (`ID not exist` / `Remote desktop is offline` / ignore-unknown), `drop(socket)` via explicit move-drop |
| `Client::connect` (timeout tree, direct→relay fallback, `direct_failures` persist) | 1:1; `request_relay` errors map to the outer relay-failure bail exactly like upstream's `is_err` check |
| `Client::secure_connection` | 1:1: empty-pk keep-alive, `next_timeout(CONNECT_TIMEOUT)`, format/type/key-length bails, verify-failure **fallback** (not throw — the subtle arm), id-mismatch bail, box+seal reply, `set_key` |
| `request_relay` / `create_relay` | 1:1 (fresh socket per relay attempt, `uuid_v4`, `check_port(RELAY_PORT)`) |
| `TcpListener` / `UdpFramedSocket` | new `local_addr()` on both (getsockname; TCP also gained move ops in step 11) |

## Tests

- `tests/test_crypto.cpp`: sign/box/secretbox round-trips + tamper/wrong-key
  failures + `seq_nonce` layout bytes.
- `tests/test_client.cpp` += duplex secure-handshake over loopback: a server
  thread performs the server.rs half manually, `Client::secure_connection`
  runs the client half, then both directions exchange secretbox frames
  through the stream layer (would fail on any nonce/seq/key mismatch).
- `tests/test_mediator.cpp` += register loopback against a stub hbbs:
  `register_pk` wire fields (id, pk, pk-fallback uuid) and the confirmed-key
  `register_peer` path (hermetic HOME redirect added).

## Smoke test

```console
$ cmake -B /tmp/b-12 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-12 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-12/cppdesk
cppdesk 1.1.2 step 12 (d1013487 part 9): + client connection and crypto — server pending.
$ ctest --test-dir /tmp/b-12 --output-on-failure
100% tests passed, 13 tests passed out of 13
```

Next in `d1013487` parts: `server/*` (part 10: `start_all`, handoffs,
connection services), then `platform/*`, `ui`, entry (`main.rs`+`cli.rs`+
`port_forward.rs`+`ipc.rs`), vendored-lib strategy.
