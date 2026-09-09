// rendezvous_mediator.hpp — translation of src/rendezvous_mediator.rs (step 10).
//
// Keeps the rendezvous-server registration loop, PK registration / UUID
// mismatch recovery, NAT hole-punching and relay/intranet connection setup.
//
// Async mapping: the `select!` loop becomes poll-with-timeout
// (`UdpFramedSocket::next_timeout(1000)` + 1s tick processing); per-message
// `tokio::spawn` becomes detached `std::thread`s; the tokio 1s-timer
// workaround is dropped (no tokio timer here).
//
// Server seam: upstream threads `ServerPtr` through every handler for the
// final `accept_connection` / `create_relay_connection` handoff (both live in
// server.rs — the NEXT part). Here that handoff is a `ServerHooks` struct of
// `std::function`s that the server step will wire to the real server; until
// then the hooks are empty and reaching one throws (loud, not silent).
// `start_all` (spawns the server itself) is deferred to the server step.

#pragma once

#include <hbb_common/config.hpp>
#include <hbb_common/tcp.hpp>
#include <hbb_common/udp.hpp>
#include <hbb_common/util.hpp>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// Generated protobuf (rendezvous.proto): handler signatures name these types.
#include "rendezvous.pb.h"

namespace rustdesk {

// Server handoff seam (see header comment). Signatures mirror
// server.rs `accept_connection(server, socket, peer_addr, secure)` and
// `create_relay_connection(server, relay_server, uuid, peer_addr, secure)`
// minus the ServerPtr, which the server step closes over.
struct ServerHooks {
    std::function<void(hbb_common::TcpFramedStream, hbb_common::ResolvedAddr, bool)>
        accept_connection;
    std::function<void(std::string, std::string, hbb_common::ResolvedAddr, bool)>
        create_relay_connection;
};

// SOLVING_PK_MISMATCH parity (process-global "host currently being fixed").
std::mutex& pk_mismatch_lock();
std::string& solving_pk_mismatch();

class RendezvousMediator {
public:
    RendezvousMediator(std::string host, hbb_common::ResolvedAddr addr,
                       std::vector<std::string> rendezvous_servers);

    // host_prefix() helper factored for tests: first DNS label, or the whole
    // host when that label parses as i32 (upstream inline logic, unchanged).
    static std::string make_host_prefix(const std::string& host);

    // Registration loop for one rendezvous server (start() parity). Returns
    // when the server list changes or stop-service is set. IoErrors throw
    // (upstream `?` on socket creation); per-message failures are swallowed
    // (allow_err! parity).
    void start(const ServerHooks& hooks);

    void dns_check();  // resolves host:RENDEZVOUS_PORT into addr_

    // PK / registration flows (register_pk needs a keypair: throws until the
    // crypto step lands, exactly where upstream would lazy-generate keys).
    void register_peer(hbb_common::UdpFramedSocket& socket);
    void register_pk(hbb_common::UdpFramedSocket& socket);
    void handle_uuid_mismatch(hbb_common::UdpFramedSocket& socket);

    // Connection-setup flows (network halves ported; final server handoff
    // goes through hooks and throws while the seam is unwired).
    void create_relay(const std::vector<uint8_t>& socket_addr, const std::string& relay_server,
                      const std::string& uuid, const ServerHooks& hooks, bool secure, bool initiate);
    void handle_request_relay(const hbb::RequestRelay& rr, const ServerHooks& hooks);
    void handle_intranet(const hbb::FetchLocalAddr& fla, const ServerHooks& hooks);
    void handle_punch_hole(const hbb::PunchHole& ph, const ServerHooks& hooks);

    const std::string& host() const { return host_; }

private:
    std::string host_;
    std::string host_prefix_;
    hbb_common::ResolvedAddr addr_;
    std::vector<std::string> rendezvous_servers_;
    std::string last_id_pk_registry_;
};

// Uuid::new_v4 parity (RFC 4122, random). RNG differs from the uuid crate
// (ChaCha) — both are valid v4 sources; noted for the record.
std::string uuid_v4();

}  // namespace rustdesk
