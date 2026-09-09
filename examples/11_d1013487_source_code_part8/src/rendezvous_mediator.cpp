// rendezvous_mediator.cpp — see rendezvous_mediator.hpp.
#include <rustdesk/rendezvous_mediator.hpp>

#include <rustdesk/common.hpp>

#include <hbb_common/addr_mangle.hpp>

#include <sys/socket.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <thread>

namespace rustdesk {
namespace {

using hbb_common::ResolvedAddr;
using hbb_common::TcpFramedStream;
using hbb_common::UdpFramedSocket;

constexpr int64_t kRegIntervalMs = 12'000;  // REG_INTERVAL parity
constexpr int64_t kRegTimeoutMs = 3'000;    // REG_TIMEOUT parity
constexpr int64_t kMaxFails1 = 3;
constexpr int64_t kMaxFails2 = 6;
constexpr int64_t kDnsIntervalMs = 60'000;  // DNS_INTERVAL parity
constexpr uint64_t kTickMs = 1000;          // TIMER_OUT parity

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t now_micros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool parses_as_i32(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i == s.size()) {
        return false;
    }
    int64_t v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (s[i] - '0');
        if (v > INT32_MAX + 1LL) {
            return false;
        }
    }
    return true;
}

std::string bytes_to_string(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// AddrMangle::decode yields (ip, port); the hooks take ResolvedAddr.
// decode() is IPv4-only upstream, so AF_INET here is exact parity.
ResolvedAddr to_resolved(const std::pair<std::string, uint16_t>& addr) {
    return ResolvedAddr{AF_INET, addr.first, addr.second};
}

hbb::NatType nat_type_from_i32(int32_t v) {
    if (v == hbb::SYMMETRIC) {
        return hbb::SYMMETRIC;
    }
    if (v == hbb::ASYMMETRIC) {
        return hbb::ASYMMETRIC;
    }
    return hbb::UNKNOWN_NAT;  // from_i32().unwrap_or(UNKNOWN_NAT) parity
}

void require_hooks(const ServerHooks& hooks) {
    if (!hooks.accept_connection || !hooks.create_relay_connection) {
        // Loud until the server step wires the seam (never silent-wrong).
        throw std::runtime_error("RendezvousMediator: server seam unwired (server step)");
    }
}

}  // namespace

std::mutex& pk_mismatch_lock() {
    static std::mutex m;
    return m;
}

std::string& solving_pk_mismatch() {
    static std::string s;
    return s;
}

std::string uuid_v4() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    const uint64_t hi = dist(rng);
    const uint64_t lo = dist(rng);
    // Version (4) + variant (10xx) bits per RFC 4122.
    char buf[37];
    std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%012llx",
                  static_cast<uint32_t>(hi >> 32), static_cast<uint32_t>((hi >> 16) & 0xFFFF),
                  static_cast<uint32_t>(hi & 0x0FFF),
                  static_cast<uint32_t>(((lo >> 60) & 0x3) | 0x8) << 12 | (lo >> 48 & 0x0FFF),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
    return std::string(buf);
}

RendezvousMediator::RendezvousMediator(std::string host, ResolvedAddr addr,
                                       std::vector<std::string> rendezvous_servers)
    : host_(std::move(host)),
      host_prefix_(make_host_prefix(host_)),
      addr_(addr),
      rendezvous_servers_(std::move(rendezvous_servers)),
      last_id_pk_registry_() {}

std::string RendezvousMediator::make_host_prefix(const std::string& host) {
    const size_t dot = host.find('.');
    const std::string first = (dot == std::string::npos) ? host : host.substr(0, dot);
    if (parses_as_i32(first)) {
        return host;
    }
    return first;
}

void RendezvousMediator::dns_check() {
    addr_ = hbb_common::to_socket_addr(check_port(host_, hbb_common::kRendezvousPort));
}

void RendezvousMediator::register_pk(UdpFramedSocket& socket) {
    const std::vector<uint8_t> pk = hbb_common::get_key_pair()[1];  // throws pre-crypto
    // machine_uid::get() deferred to the platform step: fall back to pk bytes
    // (exactly the upstream `else { pk.clone() }` arm, taken unconditionally).
    const std::string uuid(bytes_to_string(pk));
    const std::string id = hbb_common::get_id();
    last_id_pk_registry_ = id;
    hbb::RendezvousMessage msg_out;
    hbb::RegisterPk* req = msg_out.mutable_register_pk();
    req->set_id(id);
    req->set_uuid(uuid);
    req->set_pk(bytes_to_string(pk));
    if (!socket.send(msg_out, addr_)) {
        throw std::runtime_error("register_pk: send failed");
    }
}

void RendezvousMediator::handle_uuid_mismatch(UdpFramedSocket& socket) {
    if (last_id_pk_registry_ != hbb_common::get_id()) {
        return;
    }
    {
        std::unique_lock l(pk_mismatch_lock());
        std::string& solving = solving_pk_mismatch();
        if (!solving.empty() && solving != host_) {
            return;
        }
        hbb_common::set_key_confirmed(false);
        hbb_common::update_id();
        solving = host_;
    }
    register_pk(socket);
}

void RendezvousMediator::register_peer(UdpFramedSocket& socket) {
    {
        std::unique_lock l(pk_mismatch_lock());
        if (!solving_pk_mismatch().empty()) {
            return;
        }
    }
    if (!hbb_common::get_key_confirmed() ||
        !hbb_common::get_host_key_confirmed(host_prefix_)) {
        return register_pk(socket);  // "register_pk ... due to key not confirmed"
    }
    const std::string id = hbb_common::get_id();
    hbb::RendezvousMessage msg_out;
    hbb::RegisterPeer* req = msg_out.mutable_register_peer();
    req->set_id(id);
    req->set_serial(hbb_common::get_serial());
    if (!socket.send(msg_out, addr_)) {
        throw std::runtime_error("register_peer: send failed");
    }
}

void RendezvousMediator::create_relay(const std::vector<uint8_t>& socket_addr,
                                      const std::string& relay_server, const std::string& uuid,
                                      const ServerHooks& hooks, bool secure, bool initiate) {
    require_hooks(hooks);
    const auto peer_addr = hbb_common::AddrMangle::decode(socket_addr);
    auto socket = TcpFramedStream::connect(addr_, hbb_common::get_any_listen_addr(),
                                           hbb_common::kRendezvousTimeoutMs);
    if (!socket) {
        throw std::runtime_error("create_relay: connect failed");  // `?` parity
    }
    hbb::RendezvousMessage msg_out;
    hbb::RelayResponse* rr = msg_out.mutable_relay_response();
    rr->set_socket_addr(bytes_to_string(socket_addr));
    if (initiate) {
        // NOTE: upstream assigns rr.uuid twice (lines 272+274); ported once.
        rr->set_uuid(uuid);
        rr->set_relay_server(relay_server);
        rr->set_id(hbb_common::get_id());
    }
    if (!socket->send(msg_out)) {
        throw std::runtime_error("create_relay: send failed");
    }
    hooks.create_relay_connection(relay_server, uuid, to_resolved(peer_addr), secure);
}

void RendezvousMediator::handle_request_relay(const hbb::RequestRelay& rr,
                                              const ServerHooks& hooks) {
    create_relay(std::vector<uint8_t>(rr.socket_addr().begin(), rr.socket_addr().end()),
                 rr.relay_server(), rr.uuid(), hooks, rr.secure(), false);
}

void RendezvousMediator::handle_intranet(const hbb::FetchLocalAddr& fla,
                                         const ServerHooks& hooks) {
    require_hooks(hooks);
    const auto peer_addr = hbb_common::AddrMangle::decode(
        reinterpret_cast<const uint8_t*>(fla.socket_addr().data()), fla.socket_addr().size());
    auto socket = TcpFramedStream::connect(addr_, hbb_common::get_any_listen_addr(),
                                           hbb_common::kRendezvousTimeoutMs);
    if (!socket) {
        throw std::runtime_error("handle_intranet: connect failed");
    }
    const auto local = socket->local_addr();
    if (!local) {
        throw std::runtime_error("handle_intranet: local_addr failed");
    }
    // Keep our IP, advertise the fresh local port (upstream parity).
    const ResolvedAddr advertised{local->family, local->ip, local->port};
    std::string relay_server = hbb_common::get_option("relay-server");
    if (relay_server.empty()) {
        relay_server = fla.relay_server();
    }
    hbb::RendezvousMessage msg_out;
    hbb::LocalAddr* la = msg_out.mutable_local_addr();
    const auto peer_enc = hbb_common::AddrMangle::encode(peer_addr.first, peer_addr.second);
    const auto local_enc = hbb_common::AddrMangle::encode(advertised.ip, advertised.port);
    la->set_socket_addr(bytes_to_string(peer_enc));
    la->set_local_addr(bytes_to_string(local_enc));
    la->set_relay_server(relay_server);
    const std::string bytes = msg_out.SerializeAsString();  // write_to_bytes parity
    std::vector<uint8_t> raw(bytes.begin(), bytes.end());
    if (!socket->send_raw(raw)) {
        throw std::runtime_error("handle_intranet: send failed");
    }
    hooks.accept_connection(std::move(*socket), to_resolved(peer_addr), false);
}

void RendezvousMediator::handle_punch_hole(const hbb::PunchHole& ph, const ServerHooks& hooks) {
    require_hooks(hooks);
    std::string relay_server = hbb_common::get_option("relay-server");
    if (relay_server.empty()) {
        relay_server = ph.relay_server();
    }
    if (ph.nat_type() == hbb::SYMMETRIC ||
        hbb_common::get_nat_type() == hbb::SYMMETRIC) {
        return create_relay(std::vector<uint8_t>(ph.socket_addr().begin(), ph.socket_addr().end()),
                            relay_server, uuid_v4(), hooks, true, true);
    }
    const auto peer_addr = hbb_common::AddrMangle::decode(
        reinterpret_cast<const uint8_t*>(ph.socket_addr().data()), ph.socket_addr().size());
    auto socket = TcpFramedStream::connect(addr_, hbb_common::get_any_listen_addr(),
                                           hbb_common::kRendezvousTimeoutMs);
    if (!socket) {
        throw std::runtime_error("handle_punch_hole: connect failed");
    }
    // Punch from the same local port, then keep the rendezvous socket.
    const auto local = socket->local_addr();
    if (!local) {
        throw std::runtime_error("handle_punch_hole: local_addr failed");
    }
    auto punched = TcpFramedStream::connect(to_resolved(peer_addr), *local, 300);
    (void)punched;  // allow_err! parity: result intentionally ignored
    hbb::RendezvousMessage msg_out;
    hbb::PunchHoleSent* sent = msg_out.mutable_punch_hole_sent();
    sent->set_socket_addr(ph.socket_addr());
    sent->set_id(hbb_common::get_id());
    sent->set_relay_server(relay_server);
    sent->set_nat_type(nat_type_from_i32(hbb_common::get_nat_type()));
    const std::string bytes = msg_out.SerializeAsString();
    if (!socket->send_raw(std::vector<uint8_t>(bytes.begin(), bytes.end()))) {
        throw std::runtime_error("handle_punch_hole: send failed");
    }
    hooks.accept_connection(std::move(*socket), to_resolved(peer_addr), true);
}

void RendezvousMediator::start(const ServerHooks& hooks) {
    auto socket = UdpFramedSocket::bind(hbb_common::get_any_listen_addr());
    if (!socket) {
        throw std::runtime_error("mediator start: bind failed");  // `?` parity
    }
    // HBB_ALLOW_ERR(dns_check) parity: failure just leaves port 0, retried below.
    try {
        dns_check();
    } catch (...) {
    }

    int64_t fails = 0;
    int64_t last_register_resp = 0;  // SystemTime::UNIX_EPOCH parity (ms)
    int64_t last_register_sent = 0;  // same clock base (ms; micros twin below)
    int64_t last_register_sent_us = 0;
    int64_t last_dns_check = 0;
    int64_t old_latency = 0;
    int64_t ema_latency = 0;

    while (true) {
        // select!: socket readability arm (1s tick doubles as the timer arm).
        auto incoming = socket->next_timeout(kTickMs);
        const int64_t now = now_ms();
        if (incoming) {
            hbb::RendezvousMessage msg_in;
            if (!msg_in.ParseFromArray(incoming->first.data(),
                                       static_cast<int>(incoming->first.size()))) {
                continue;  // non-protobuf bytes: debug-log parity (silent)
            }
            switch (msg_in.union_case()) {
                case hbb::RendezvousMessage::kRegisterPeerResponse: {
                    const hbb::RegisterPeerResponse& rpr = msg_in.register_peer_response();
                    if (rpr.request_pk()) {
                        try {
                            register_pk(*socket);
                        } catch (...) {
                        }
                        continue;
                    }
                    last_register_resp = now;
                    // Latency since the register was SENT, in micros
                    // (duration_since().as_micros() parity).
                    int64_t latency = now_micros() - last_register_sent_us;
                    if (ema_latency == 0) {
                        ema_latency = latency;
                    } else {
                        ema_latency = latency / 30 + (ema_latency * 29 / 30);
                        latency = ema_latency;
                    }
                    int64_t n = latency / 5;
                    if (n < 3000) {
                        n = 3000;
                    }
                    if ((latency >= old_latency ? latency - old_latency : old_latency - latency) >
                            n ||
                        old_latency <= 0) {
                        hbb_common::update_latency(host_, latency);
                        old_latency = latency;
                    }
                    fails = 0;
                    break;
                }
                case hbb::RendezvousMessage::kRegisterPkResponse: {
                    const hbb::RegisterPkResponse& rpr = msg_in.register_pk_response();
                    if (rpr.result() == hbb::RegisterPkResponse::OK) {
                        hbb_common::set_key_confirmed(true);
                        hbb_common::set_host_key_confirmed(host_prefix_, true);
                        {
                            std::unique_lock l(pk_mismatch_lock());
                            solving_pk_mismatch().clear();
                        }
                        last_register_resp = now;
                        const int64_t latency = now_micros() - last_register_sent_us;
                        hbb_common::update_latency(host_, latency);
                        fails = 0;
                    } else if (rpr.result() == hbb::RegisterPkResponse::UUID_MISMATCH) {
                        try {
                            handle_uuid_mismatch(*socket);
                        } catch (...) {
                        }
                    }
                    // Unknown Result (incl. the new ResultUnknown sentinel):
                    // ignored. (Upstream match would panic; see step README.)
                    break;
                }
                case hbb::RendezvousMessage::kPunchHole: {
                    RendezvousMediator self = *this;
                    std::thread([self, hooks, ph = msg_in.punch_hole()]() mutable {
                        try {
                            self.handle_punch_hole(ph, hooks);
                        } catch (...) {
                        }
                    }).detach();
                    break;
                }
                case hbb::RendezvousMessage::kRequestRelay: {
                    RendezvousMediator self = *this;
                    std::thread([self, hooks, rr = msg_in.request_relay()]() mutable {
                        try {
                            self.handle_request_relay(rr, hooks);
                        } catch (...) {
                        }
                    }).detach();
                    break;
                }
                case hbb::RendezvousMessage::kFetchLocalAddr: {
                    RendezvousMediator self = *this;
                    std::thread([self, hooks, fla = msg_in.fetch_local_addr()]() mutable {
                        try {
                            self.handle_intranet(fla, hooks);
                        } catch (...) {
                        }
                    }).detach();
                    break;
                }
                case hbb::RendezvousMessage::kConfigureUpdate: {
                    const hbb::ConfigUpdate& cu = msg_in.configure_update();
                    std::string servers;
                    for (int i = 0; i < cu.rendezvous_servers_size(); ++i) {
                        if (i > 0) {
                            servers += ',';
                        }
                        servers += cu.rendezvous_servers(i);
                    }
                    hbb_common::set_option("rendezvous-servers", servers);
                    hbb_common::set_serial(cu.serial());
                    break;
                }
                default:
                    break;  // `_ => {}` parity
            }
            continue;
        }

        // Timer arm (1s tick). The tokio-timer-bug workaround has no
        // counterpart here (no tokio timer); the tick itself is the guard.
        if (hbb_common::get_rendezvous_servers() != rendezvous_servers_) {
            break;
        }
        if (!hbb_common::get_option("stop-service").empty()) {
            break;
        }
        if (addr_.port == 0) {
            try {
                dns_check();
            } catch (...) {
            }
            if (addr_.port == 0) {
                continue;
            }
            // OSX workaround parity: re-create the socket once DNS resolves
            // (avoids "Can't assign requested address" at early boot).
            auto fresh = UdpFramedSocket::bind(hbb_common::get_any_listen_addr());
            if (!fresh) {
                continue;  // `?` inside allow_err!: skip tick, retry later
            }
            socket = std::move(fresh);
        }
        // Faithful quirk parity: both stamps start at UNIX_EPOCH, so before
        // the first response `sent - resp` is huge and every tick registers
        // (and counts fails) until a response lands — exactly like upstream.
        const int64_t elapsed_resp = now - last_register_resp;
        const bool timed_out = (last_register_sent - last_register_resp) >= kRegTimeoutMs;
        if (timed_out || elapsed_resp >= kRegIntervalMs) {
            try {
                register_peer(*socket);
            } catch (...) {
            }
            last_register_sent = now;
            last_register_sent_us = now_micros();
            if (timed_out) {
                ++fails;
                if (fails > kMaxFails2) {
                    hbb_common::update_latency(host_, -1);
                    old_latency = 0;
                    if (now - last_dns_check > kDnsIntervalMs) {
                        try {
                            dns_check();
                        } catch (...) {
                        }
                        last_dns_check = now;
                    }
                } else if (fails > kMaxFails1) {
                    hbb_common::update_latency(host_, 0);
                    old_latency = 0;
                }
            }
        }
    }
}

}  // namespace rustdesk
