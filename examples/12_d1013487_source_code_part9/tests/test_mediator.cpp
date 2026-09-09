// test_mediator.cpp — tests for rustdesk::RendezvousMediator helpers.
// Hermetic: XDG_CONFIG_HOME/HOME point at a temp dir (register flows touch
// global Config), network stays on loopback. Pure-logic tests need neither.

#include <rustdesk/rendezvous_mediator.hpp>

#include <hbb_common/crypto.hpp>

#include <sys/socket.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                      \
    } while (0)

using rustdesk::RendezvousMediator;

void test_host_prefix() {
    using M = RendezvousMediator;
    CHECK(M::make_host_prefix("rs-sg.rustdesk.com") == "rs-sg");
    CHECK(M::make_host_prefix("rs-cn.rustdesk.com") == "rs-cn");
    // First label parses as i32 → whole host is kept.
    CHECK(M::make_host_prefix("1.2.3.4") == "1.2.3.4");
    CHECK(M::make_host_prefix("12345.example.com") == "12345.example.com");
    // No dot at all.
    CHECK(M::make_host_prefix("localhost") == "localhost");
    CHECK(M::make_host_prefix("") == "");
}

bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

void test_uuid_v4() {
    const std::string u = rustdesk::uuid_v4();
    CHECK(u.size() == 36);
    CHECK(u[8] == '-' && u[13] == '-' && u[18] == '-' && u[23] == '-');
    CHECK(u[14] == '4');  // version nibble
    CHECK(u[19] == '8' || u[19] == '9' || u[19] == 'a' || u[19] == 'b');  // variant
    for (size_t i = 0; i < u.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        if (i == 14 || i == 19) {
            continue;  // checked above
        }
        CHECK(is_hex(u[i]));
    }
    CHECK(rustdesk::uuid_v4() != u);  // random, not constant
}

void test_register_peer_wire() {
    // The exact bytes register_peer() emits (handshake contract with hbbs).
    hbb::RendezvousMessage msg;
    hbb::RegisterPeer* req = msg.mutable_register_peer();
    req->set_id("123456");
    req->set_serial(9);
    std::string bytes;
    CHECK(msg.SerializeToString(&bytes));
    hbb::RendezvousMessage back;
    CHECK(back.ParseFromString(bytes));
    CHECK(back.has_register_peer());
    CHECK(back.register_peer().id() == "123456");
    CHECK(back.register_peer().serial() == 9);
}

void test_punch_hole_sent_wire() {
    // PunchHoleSent incl. NatType mapping used by handle_punch_hole.
    hbb::RendezvousMessage msg;
    hbb::PunchHoleSent* sent = msg.mutable_punch_hole_sent();
    sent->set_socket_addr("\x01\x02", 2);
    sent->set_id("999");
    sent->set_relay_server("relay.example.com");
    sent->set_nat_type(hbb::SYMMETRIC);
    CHECK(msg.has_punch_hole_sent());
    CHECK(msg.punch_hole_sent().nat_type() == hbb::SYMMETRIC);
    CHECK(msg.punch_hole_sent().id() == "999");
}

hbb::RendezvousMessage recv_one(hbb_common::UdpFramedSocket& stub) {
    auto frame = stub.next_timeout(5000);
    CHECK(frame.has_value());
    hbb::RendezvousMessage msg;
    if (frame) {
        CHECK(msg.ParseFromArray(frame->first.data(), static_cast<int>(frame->first.size())));
    }
    return msg;
}

void test_register_pk_loopback() {
    // Stub hbbs on loopback: bind :0, learn the port, point a mediator at it.
    auto stub = hbb_common::UdpFramedSocket::bind(hbb_common::to_socket_addr("127.0.0.1:0"));
    CHECK(stub.has_value() && stub->local_addr().has_value());
    if (!stub || !stub->local_addr()) {
        return;
    }
    // Seed a real keypair (hermetic: config dir is the temp HOME).
    const auto [pk, sk] = hbb_common::crypto::sign_gen_keypair();
    hbb_common::set_key_pair({sk, pk});  // (sk, pk) tuple parity
    hbb_common::set_id("medtest");

    auto sock = hbb_common::UdpFramedSocket::bind(hbb_common::get_any_listen_addr());
    CHECK(sock.has_value());
    if (!sock) {
        return;
    }
    const hbb_common::ResolvedAddr stub_addr{AF_INET, "127.0.0.1", stub->local_addr()->port};
    RendezvousMediator mediator("127.0.0.1", stub_addr, {"127.0.0.1"});

    // Key not confirmed yet → register_pk path: RegisterPk on the wire.
    hbb_common::set_key_confirmed(false);
    mediator.register_pk(*sock);
    const hbb::RendezvousMessage got = recv_one(*stub);
    CHECK(got.has_register_pk());
    CHECK(got.register_pk().id() == "medtest");
    CHECK(got.register_pk().pk() == std::string(pk.begin(), pk.end()));
    // machine_uid stub → uuid falls back to pk bytes (upstream else-arm).
    CHECK(got.register_pk().uuid() == std::string(pk.begin(), pk.end()));

    // Keys confirmed → register_peer path instead.
    hbb_common::set_key_confirmed(true);
    hbb_common::set_host_key_confirmed("127.0.0.1", true);
    mediator.register_peer(*sock);
    const hbb::RendezvousMessage got2 = recv_one(*stub);
    CHECK(got2.has_register_peer());
    CHECK(got2.register_peer().id() == "medtest");
}

}  // namespace

int main() {
    // Hermetic config dir BEFORE any Config global can load (register flows
    // read/write global Config).
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cppdesk_test_mediator_home";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);
    ::setenv("HOME", tmp.c_str(), 1);

    test_host_prefix();
    test_uuid_v4();
    test_register_peer_wire();
    test_punch_hole_sent_wire();
    test_register_pk_loopback();
    if (g_failures == 0) {
        std::puts("test_mediator: all tests passed");
    }
    std::filesystem::remove_all(tmp, ec);
    return g_failures == 0 ? 0 : 1;
}
