# Step 10 — "source code", part 7 (RustDesk `d1013487`)

Source: [`rustdesk/rustdesk@d1013487`](https://github.com/rustdesk/rustdesk/commit/d1013487e2f3862e2f801ef1a704bb61fdff8bbb)
(AuthorDate 2021-03-29; parent `002fce13`.)

This is **part 7 of upstream commit d1013487**. So far: workspace + core
(04), config/TOML (05), transport (06), protobuf structs (07), app common +
Config methods (08), fs jobs (09). This part ports
**`src/rendezvous_mediator.rs`** (416L): the rendezvous-server registration
loop, PK registration / UUID-mismatch recovery, and NAT hole-punching /
relay / intranet connection setup.

## What changed vs step 09

| Upstream item | C++ translation |
|---|---|
| `RendezvousMediator` struct + `SOLVING_PK_MISMATCH` | `include/rustdesk/rendezvous_mediator.hpp` + `src/rendezvous_mediator.cpp` (`rustdesk::` namespace); mismatch flag as mutex+string globals |
| `start()` select!-loop (registration state machine) | poll-with-timeout loop (`next_timeout(1000)` doubles as the 1s tick); per-message `tokio::spawn` → detached `std::thread`s; tokio-timer workaround dropped (no tokio timer) |
| Latency EMA, `MAX_FAILS1/2`, `REG_INTERVAL/TIMEOUT`, `DNS_INTERVAL` | 1:1, **including the startup quirk**: both stamps start at EPOCH so every tick registers (and counts fails) until the first response lands — reproduced exactly, not "fixed" |
| `register_peer` / `register_pk` / `handle_uuid_mismatch` / `dns_check` | 1:1 (PK bytes fall back to machine-uid, whose lookup is deferred to the platform step — the upstream `else { pk.clone() }` arm, taken unconditionally) |
| `create_relay` / `handle_request_relay` / `handle_intranet` / `handle_punch_hole` | network halves ported 1:1 (connect, local-port re-binding via the step-08 `local_addr()`, message builds+sends); the final `accept_connection` / `create_relay_connection` handoff goes through a documented `ServerHooks` seam (`std::function`s, empty until the server step wires them — reaching one throws, never silently wrong). Upstream's duplicated `rr.uuid` assignment is ported once, noted |
| `start_all` (spawns the server itself) | Deferred → server step (needs `check_zombie` + `new_server` from server.rs) |
| `Uuid::new_v4` | `rustdesk::uuid_v4()` (RFC 4122, mt19937_64; RNG differs from the uuid crate — both valid v4) |
| `NatType::from_i32(..).unwrap_or(UNKNOWN_NAT)` | explicit int→enum map with the same default |

## Wire/compat notes

- `register_peer_response` with unknown `Result` (incl. the new `ResultUnknown`
  sentinel from step 07) is **ignored**; upstream's exhaustive `match` would
  panic on it. Deliberate, documented divergence (fail-open beats crash).
- `register_pk` throws until the crypto step lands (`get_key_pair` has no keys
  to return) — so live registration flows are structural until then; the
  loopback integration tests arrive with the crypto step.
- `make_host_prefix` is factored out of `start()` as a static (upstream inlines
  it) purely for testability; logic byte-identical.

## Tests

`tests/test_mediator.cpp` (hermetic): `make_host_prefix` (dotted, numeric-IP,
bare, empty), `uuid_v4` shape (length, dashes, version/variant nibbles,
uniqueness), `RegisterPeer` + `PunchHoleSent` wire round-trips.

## Smoke test

```console
$ cmake -B /tmp/b-10 -S . -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/b-10 --parallel "$JOBS"  # $JOBS per AGENTS.md RAM rule
$ /tmp/b-10/cppdesk
cppdesk 1.1.2 step 10 (d1013487 part 7): + rendezvous mediator — client/server pending.
$ ctest --test-dir /tmp/b-10 --output-on-failure
100% tests passed, 11 tests passed out of 11
```

Next in `d1013487` parts: `client.rs` (part 8), then `server/*` (+ `start_all`,
`accept_connection`, `create_relay_connection`), `platform/*`, `ui`, entry
(`main.rs` + `cli.rs` + `port_forward.rs` + `ipc.rs`).
