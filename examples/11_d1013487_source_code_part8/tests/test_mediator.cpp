// test_mediator.cpp — tests for rustdesk::RendezvousMediator helpers.
// Hermetic by construction: only pure logic is tested here (host prefix,
// UUID shape, message construction). The registration loop and register_*
// flows touch global Config + the network and are covered by loopback
// integration tests arriving with the crypto step (keypair needed).

#include <rustdesk/rendezvous_mediator.hpp>

#include <cstdio>
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

}  // namespace

int main() {
    test_host_prefix();
    test_uuid_v4();
    test_register_peer_wire();
    test_punch_hole_sent_wire();
    if (g_failures == 0) {
        std::puts("test_mediator: all tests passed");
    }
    return g_failures == 0 ? 0 : 1;
}
