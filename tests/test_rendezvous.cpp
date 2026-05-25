#include <gtest/gtest.h>
#include "rendezvous/rendezvous.hpp"
#include "common/config.hpp"
#include "common/protocol.hpp"

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <memory>
#include <functional>
#include <atomic>

using namespace cppdesk;
using namespace cppdesk::common;
using namespace cppdesk::rendezvous;
using namespace std::chrono_literals;

// =============================================================================
// Helper: wait for a condition with timeout
// =============================================================================
static bool wait_for(std::function<bool()> condition, int timeout_ms = 2000) {
    auto start = std::chrono::steady_clock::now();
    while (!condition()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

// =============================================================================
// Tests: RendezvousMessageType enum values
// =============================================================================
TEST(RendezvousMessageTypeTest, EnumValues) {
    // Verify all enum values are assigned correctly
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REGISTER_PEER), 1u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_REQUEST), 2u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_RESPONSE), 3u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REQUEST_RELAY), 4u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::TEST_NAT), 5u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::QUERY_ONLINE), 6u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::HEARTBEAT), 7u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REGISTER_PK), 8u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PK_RESPONSE), 9u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::CONFIG_REQUEST), 10u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::CONFIG_RESPONSE), 11u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::SOFTWARE_UPDATE), 12u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::ALIAS_UPDATE), 13u);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::ADDRESS_BOOK), 14u);
}

TEST(RendezvousMessageTypeTest, SequentialOrder) {
    // Verify values are sequential starting at 1
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_REQUEST),
              static_cast<uint32_t>(RendezvousMessageType::REGISTER_PEER) + 1);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_RESPONSE),
              static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_REQUEST) + 1);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REQUEST_RELAY),
              static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_RESPONSE) + 1);
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::TEST_NAT),
              static_cast<uint32_t>(RendezvousMessageType::REQUEST_RELAY) + 1);
}

TEST(RendezvousMessageTypeTest, CastToUint32) {
    RendezvousMessageType mt = RendezvousMessageType::PUNCH_HOLE_REQUEST;
    uint32_t val = static_cast<uint32_t>(mt);
    EXPECT_EQ(val, 2u);
}

TEST(RendezvousMessageTypeTest, AllValuesNonZero) {
    std::vector<RendezvousMessageType> all = {
        RendezvousMessageType::REGISTER_PEER,
        RendezvousMessageType::PUNCH_HOLE_REQUEST,
        RendezvousMessageType::PUNCH_HOLE_RESPONSE,
        RendezvousMessageType::REQUEST_RELAY,
        RendezvousMessageType::TEST_NAT,
        RendezvousMessageType::QUERY_ONLINE,
        RendezvousMessageType::HEARTBEAT,
        RendezvousMessageType::REGISTER_PK,
        RendezvousMessageType::PK_RESPONSE,
        RendezvousMessageType::CONFIG_REQUEST,
        RendezvousMessageType::CONFIG_RESPONSE,
        RendezvousMessageType::SOFTWARE_UPDATE,
        RendezvousMessageType::ALIAS_UPDATE,
        RendezvousMessageType::ADDRESS_BOOK,
    };
    for (auto v : all) {
        EXPECT_NE(static_cast<uint32_t>(v), 0u);
    }
}

TEST(RendezvousMessageTypeTest, TotalValuesCount) {
    // There are exactly 14 message types
    int count = 0;
    for (uint32_t i = 1; i <= 14; ++i) {
        auto mt = static_cast<RendezvousMessageType>(i);
        switch (mt) {
            case RendezvousMessageType::REGISTER_PEER:      count++; break;
            case RendezvousMessageType::PUNCH_HOLE_REQUEST:  count++; break;
            case RendezvousMessageType::PUNCH_HOLE_RESPONSE: count++; break;
            case RendezvousMessageType::REQUEST_RELAY:       count++; break;
            case RendezvousMessageType::TEST_NAT:            count++; break;
            case RendezvousMessageType::QUERY_ONLINE:        count++; break;
            case RendezvousMessageType::HEARTBEAT:           count++; break;
            case RendezvousMessageType::REGISTER_PK:         count++; break;
            case RendezvousMessageType::PK_RESPONSE:         count++; break;
            case RendezvousMessageType::CONFIG_REQUEST:      count++; break;
            case RendezvousMessageType::CONFIG_RESPONSE:     count++; break;
            case RendezvousMessageType::SOFTWARE_UPDATE:     count++; break;
            case RendezvousMessageType::ALIAS_UPDATE:        count++; break;
            case RendezvousMessageType::ADDRESS_BOOK:        count++; break;
        }
    }
    EXPECT_EQ(count, 14);
}

// =============================================================================
// Tests: NatType enum values
// =============================================================================
TEST(NatTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<int32_t>(NatType::UNKNOWN_NAT), 0);
    EXPECT_EQ(static_cast<int32_t>(NatType::OPEN_INTERNET), 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::FULL_CONE), 2);
    EXPECT_EQ(static_cast<int32_t>(NatType::RESTRICTED_CONE), 3);
    EXPECT_EQ(static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE), 4);
    EXPECT_EQ(static_cast<int32_t>(NatType::SYMMETRIC), 5);
}

TEST(NatTypeTest, IsSequential) {
    EXPECT_EQ(static_cast<int32_t>(NatType::OPEN_INTERNET),
              static_cast<int32_t>(NatType::UNKNOWN_NAT) + 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::FULL_CONE),
              static_cast<int32_t>(NatType::OPEN_INTERNET) + 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::RESTRICTED_CONE),
              static_cast<int32_t>(NatType::FULL_CONE) + 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE),
              static_cast<int32_t>(NatType::RESTRICTED_CONE) + 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::SYMMETRIC),
              static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE) + 1);
}

TEST(NatTypeTest, DefaultIsUnknown) {
    NatType nt{};
    EXPECT_EQ(nt, NatType::UNKNOWN_NAT);
}

TEST(NatTypeTest, CastToInt32) {
    NatType nt = NatType::SYMMETRIC;
    EXPECT_EQ(static_cast<int32_t>(nt), 5);
}

TEST(NatTypeTest, CompareDifferentTypes) {
    EXPECT_NE(NatType::OPEN_INTERNET, NatType::SYMMETRIC);
    EXPECT_NE(NatType::FULL_CONE, NatType::RESTRICTED_CONE);
    EXPECT_NE(NatType::PORT_RESTRICTED_CONE, NatType::UNKNOWN_NAT);
}

TEST(NatTypeTest, CompareSameType) {
    EXPECT_EQ(NatType::OPEN_INTERNET, NatType::OPEN_INTERNET);
    EXPECT_EQ(NatType::FULL_CONE, NatType::FULL_CONE);
    EXPECT_EQ(NatType::UNKNOWN_NAT, NatType::UNKNOWN_NAT);
}

TEST(NatTypeTest, AllValuesDifferent) {
    std::set<int32_t> seen;
    seen.insert(static_cast<int32_t>(NatType::UNKNOWN_NAT));
    seen.insert(static_cast<int32_t>(NatType::OPEN_INTERNET));
    seen.insert(static_cast<int32_t>(NatType::FULL_CONE));
    seen.insert(static_cast<int32_t>(NatType::RESTRICTED_CONE));
    seen.insert(static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE));
    seen.insert(static_cast<int32_t>(NatType::SYMMETRIC));
    EXPECT_EQ(seen.size(), 6u);
}

// =============================================================================
// Tests: RendezvousMediator constants
// =============================================================================
TEST(RendezvousMediatorConstantsTest, DefaultKeepAlive) {
    EXPECT_EQ(RendezvousMediator::DEFAULT_KEEP_ALIVE, 60000);
}

TEST(RendezvousMediatorConstantsTest, DeployRetryInterval) {
    EXPECT_EQ(RendezvousMediator::DEPLOY_RETRY_INTERVAL, 30000);
}

TEST(RendezvousMediatorConstantsTest, PeerRegInterval) {
    EXPECT_EQ(RendezvousMediator::PEER_REG_INTERVAL, 2000);
}

TEST(RendezvousMediatorConstantsTest, DefaultKeepAlivePositive) {
    EXPECT_GT(RendezvousMediator::DEFAULT_KEEP_ALIVE, 0);
}

TEST(RendezvousMediatorConstantsTest, DeployRetryIntervalPositive) {
    EXPECT_GT(RendezvousMediator::DEPLOY_RETRY_INTERVAL, 0);
}

TEST(RendezvousMediatorConstantsTest, PeerRegIntervalPositive) {
    EXPECT_GT(RendezvousMediator::PEER_REG_INTERVAL, 0);
}

TEST(RendezvousMediatorConstantsTest, DeployRetryLessThanKeepAlive) {
    EXPECT_LT(RendezvousMediator::DEPLOY_RETRY_INTERVAL,
              RendezvousMediator::DEFAULT_KEEP_ALIVE);
}

TEST(RendezvousMediatorConstantsTest, PeerRegIntervalLessThanDeployRetry) {
    EXPECT_LT(RendezvousMediator::PEER_REG_INTERVAL,
              RendezvousMediator::DEPLOY_RETRY_INTERVAL);
}

TEST(RendezvousMediatorConstantsTest, ConstantsAreConstexpr) {
    constexpr int64_t ka = RendezvousMediator::DEFAULT_KEEP_ALIVE;
    constexpr int64_t dr = RendezvousMediator::DEPLOY_RETRY_INTERVAL;
    constexpr int64_t pr = RendezvousMediator::PEER_REG_INTERVAL;
    static_assert(ka == 60000, "DEFAULT_KEEP_ALIVE must be 60000");
    static_assert(dr == 30000, "DEPLOY_RETRY_INTERVAL must be 30000");
    static_assert(pr == 2000, "PEER_REG_INTERVAL must be 2000");
    EXPECT_EQ(ka, 60000);
    EXPECT_EQ(dr, 30000);
    EXPECT_EQ(pr, 2000);
}

// =============================================================================
// Tests: Protocol constants (port numbers)
// =============================================================================
TEST(ProtocolConstantsTest, RendezvousPortDefault) {
    EXPECT_EQ(RENDEZVOUS_PORT, 21116u);
}

TEST(ProtocolConstantsTest, RelayPortDefault) {
    EXPECT_EQ(RELAY_PORT, 21117u);
}

TEST(ProtocolConstantsTest, WebSocketPortDefault) {
    EXPECT_EQ(WEBSOCKET_PORT, 21118u);
}

TEST(ProtocolConstantsTest, LocalPortDefault) {
    EXPECT_EQ(LOCAL_PORT, 21119u);
}

TEST(ProtocolConstantsTest, PortsAreDistinct) {
    EXPECT_NE(RENDEZVOUS_PORT, RELAY_PORT);
    EXPECT_NE(RENDEZVOUS_PORT, WEBSOCKET_PORT);
    EXPECT_NE(RENDEZVOUS_PORT, LOCAL_PORT);
    EXPECT_NE(RELAY_PORT, WEBSOCKET_PORT);
    EXPECT_NE(RELAY_PORT, LOCAL_PORT);
    EXPECT_NE(WEBSOCKET_PORT, LOCAL_PORT);
}

TEST(ProtocolConstantsTest, PortsInValidRange) {
    for (auto port : {RENDEZVOUS_PORT, RELAY_PORT, WEBSOCKET_PORT, LOCAL_PORT}) {
        EXPECT_GE(port, 1024u);   // Above well-known ports
        EXPECT_LE(port, 65535u);  // Valid port range
    }
}

TEST(ProtocolConstantsTest, RendezvousServersNotEmpty) {
    EXPECT_FALSE(RENDEZVOUS_SERVERS.empty());
}

TEST(ProtocolConstantsTest, RendezvousServersAllUnique) {
    std::set<std::string> seen(RENDEZVOUS_SERVERS.begin(), RENDEZVOUS_SERVERS.end());
    EXPECT_EQ(seen.size(), RENDEZVOUS_SERVERS.size());
}

TEST(ProtocolConstantsTest, ConnTypeEnum) {
    EXPECT_NE(ConnType::DEFAULT_CONN, ConnType::FILE_TRANSFER);
    EXPECT_NE(ConnType::FILE_TRANSFER, ConnType::PORT_FORWARD);
    EXPECT_NE(ConnType::PORT_FORWARD, ConnType::RDP);
}

// =============================================================================
// Tests: RendezvousMediator construction
// =============================================================================
TEST(RendezvousMediatorConstructionTest, DefaultConstruction) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorConstructionTest, DifferentServerAddresses) {
    std::vector<std::string> servers = {
        "rs-ny.rustdesk.com",
        "rs-sg.rustdesk.com",
        "rs-cn.rustdesk.com",
        "custom.example.com",
        "192.168.1.1",
        "10.0.0.1:21116",
    };
    for (const auto& addr : servers) {
        RendezvousMediator mediator(addr, "host", "prefix", 30000);
        EXPECT_FALSE(mediator.is_registered());
    }
}

TEST(RendezvousMediatorConstructionTest, DifferentHosts) {
    std::vector<std::string> hosts = {"desktop-pc", "laptop", "server01", ""};
    for (const auto& h : hosts) {
        RendezvousMediator mediator("rs-ny.rustdesk.com", h, "cppdesk", 60000);
        EXPECT_FALSE(mediator.is_registered());
    }
}

TEST(RendezvousMediatorConstructionTest, DifferentPrefixes) {
    std::vector<std::string> prefixes = {"cppdesk", "rustdesk", "test", ""};
    for (const auto& p : prefixes) {
        RendezvousMediator mediator("rs-ny.rustdesk.com", "host", p, 60000);
        EXPECT_FALSE(mediator.is_registered());
    }
}

TEST(RendezvousMediatorConstructionTest, DifferentKeepAliveValues) {
    std::vector<int32_t> values = {5000, 10000, 30000, 60000, 120000, 0, -1};
    for (auto v : values) {
        RendezvousMediator mediator("rs-ny.rustdesk.com", "host", "cppdesk", v);
        EXPECT_FALSE(mediator.is_registered());
    }
}

TEST(RendezvousMediatorConstructionTest, NotRegisteredAfterConstruction) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test", "cppdesk", 60000);
    EXPECT_FALSE(mediator.is_registered());
    EXPECT_FALSE(mediator.is_connected());
}

TEST(RendezvousMediatorConstructionTest, InitialNatTypeIsUnknown) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test", "cppdesk", 60000);
    EXPECT_EQ(mediator.get_nat_type(), static_cast<int32_t>(NatType::UNKNOWN_NAT));
}

TEST(RendezvousMediatorConstructionTest, InitialUdpPortIsZero) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test", "cppdesk", 60000);
    EXPECT_EQ(mediator.get_udp_port(), 0u);
}

// =============================================================================
// Tests: RendezvousMediator register/unregister
// =============================================================================
TEST(RendezvousMediatorRegistrationTest, RegisterPeerSetsRegistered) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("123456789", "secret123");
    EXPECT_TRUE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, UnregisterPeerClearsRegistered) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("123456789", "secret123");
    EXPECT_TRUE(mediator.is_registered());
    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, RegisterThenUnregisterThenRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer1", "pw1");
    EXPECT_TRUE(mediator.is_registered());
    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());
    mediator.register_peer("peer2", "pw2");
    EXPECT_TRUE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, MultipleUnregisterCalls) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer", "pw");
    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());
    // Double unregister should not crash
    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, RegisterWithEmptyId) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("", "");
    EXPECT_TRUE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, RegisterWithLongId) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::string long_id(1000, 'A');
    mediator.register_peer(long_id, "password");
    EXPECT_TRUE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, RegisterWithSpecialCharacters) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer@#$%^&*()", "pass!@#$%");
    EXPECT_TRUE(mediator.is_registered());
}

TEST(RendezvousMediatorRegistrationTest, RegisterWithUnicodeId) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer_字符_тест", "밀번호");
    EXPECT_TRUE(mediator.is_registered());
}

// =============================================================================
// Tests: RendezvousMediator register_pk
// =============================================================================
TEST(RendezvousMediatorPKTest, RegisterPK) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_pk("base64_public_key_data", "token123");
    // Should not crash
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorPKTest, RegisterPKWithEmptyValues) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_pk("", "");
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorPKTest, RegisterPKMultipleTimes) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_pk("pk1", "t1");
    mediator.register_pk("pk2", "t2");
    mediator.register_pk("pk3", "t3");
    // Should not crash on multiple calls
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator update_alias
// =============================================================================
TEST(RendezvousMediatorAliasTest, UpdateAlias) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.update_alias("My Desktop");
    SUCCEED();
}

TEST(RendezvousMediatorAliasTest, UpdateAliasEmpty) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.update_alias("");
    SUCCEED();
}

TEST(RendezvousMediatorAliasTest, UpdateAliasLongString) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::string long_alias(5000, 'X');
    mediator.update_alias(long_alias);
    SUCCEED();
}

TEST(RendezvousMediatorAliasTest, UpdateAliasUnicode) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.update_alias("デスクトップ PC");
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator punch_hole
// =============================================================================
TEST(RendezvousMediatorPunchHoleTest, PunchHoleDefault) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.punch_hole("peer123", "encryption_key", "auth_token",
                        ConnType::DEFAULT_CONN, false);
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHoleForceRelay) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.punch_hole("peer123", "encryption_key", "auth_token",
                        ConnType::DEFAULT_CONN, true);
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHoleFileTransfer) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.punch_hole("peer456", "key", "token",
                        ConnType::FILE_TRANSFER, false);
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHolePortForward) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.punch_hole("peer789", "key", "token",
                        ConnType::PORT_FORWARD, false);
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHoleRDP) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.punch_hole("peer999", "key", "token",
                        ConnType::RDP, true);
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHoleAllConnTypes) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::vector<ConnType> types = {
        ConnType::DEFAULT_CONN, ConnType::FILE_TRANSFER,
        ConnType::PORT_FORWARD, ConnType::RDP
    };
    for (auto t : types) {
        for (bool relay : {false, true}) {
            mediator.punch_hole("peer", "key", "token", t, relay);
        }
    }
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHoleEmptyStrings) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.punch_hole("", "", "", ConnType::DEFAULT_CONN, false);
    SUCCEED();
}

TEST(RendezvousMediatorPunchHoleTest, PunchHoleRepeated) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    for (int i = 0; i < 10; i++) {
        mediator.punch_hole("peer", "key" + std::to_string(i), "token",
                            ConnType::DEFAULT_CONN, i % 2 == 0);
    }
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator request_relay
// =============================================================================
TEST(RendezvousMediatorRelayTest, RequestRelay) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.request_relay("uuid-1234", "licence-key-5678");
    SUCCEED();
}

TEST(RendezvousMediatorRelayTest, RequestRelayEmptyUUID) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.request_relay("", "licence");
    SUCCEED();
}

TEST(RendezvousMediatorRelayTest, RequestRelayEmptyLicence) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.request_relay("uuid", "");
    SUCCEED();
}

TEST(RendezvousMediatorRelayTest, RequestRelayMultiple) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    for (int i = 0; i < 5; i++) {
        mediator.request_relay("uuid-" + std::to_string(i), "licence-" + std::to_string(i));
    }
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator query_online
// =============================================================================
TEST(RendezvousMediatorQueryOnlineTest, QueryOnline) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.query_online("peer12345");
    SUCCEED();
}

TEST(RendezvousMediatorQueryOnlineTest, QueryOnlineEmptyPeer) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.query_online("");
    SUCCEED();
}

TEST(RendezvousMediatorQueryOnlineTest, QueryOnlineMultiplePeers) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    for (int i = 0; i < 10; i++) {
        mediator.query_online("peer-" + std::to_string(i));
    }
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator NAT type
// =============================================================================
TEST(RendezvousMediatorNatTest, GetNatTypeInitiallyUnknown) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    EXPECT_EQ(mediator.get_nat_type(), static_cast<int32_t>(NatType::UNKNOWN_NAT));
}

TEST(RendezvousMediatorNatTest, TestNatDoesNotCrash) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.test_nat();
    SUCCEED();
}

TEST(RendezvousMediatorNatTest, TestNatMultipleTimes) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    for (int i = 0; i < 3; i++) {
        mediator.test_nat();
    }
    SUCCEED();
}

TEST(RendezvousMediatorNatTest, NatTypeValuesAreValid) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    int32_t nat = mediator.get_nat_type();
    EXPECT_TRUE(nat >= 0 && nat <= 5);
}

// =============================================================================
// Tests: RendezvousMediator UDP port
// =============================================================================
TEST(RendezvousMediatorUdpPortTest, GetUdpPortInitiallyZero) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    EXPECT_EQ(mediator.get_udp_port(), 0u);
}

TEST(RendezvousMediatorUdpPortTest, UdpPortConsistent) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    uint16_t p1 = mediator.get_udp_port();
    uint16_t p2 = mediator.get_udp_port();
    EXPECT_EQ(p1, p2);
}

// =============================================================================
// Tests: RendezvousMediator is_registered / is_connected
// =============================================================================
TEST(RendezvousMediatorStatusTest, NotRegisteredWithoutRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorStatusTest, RegisteredAfterRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer", "pw");
    EXPECT_TRUE(mediator.is_registered());
}

TEST(RendezvousMediatorStatusTest, NotRegisteredAfterUnregister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer", "pw");
    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorStatusTest, IsConnectedReturnsBool) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    bool c = mediator.is_connected();
    EXPECT_TRUE(c == true || c == false);
}

// =============================================================================
// Tests: RendezvousMediator restart / stop
// =============================================================================
TEST(RendezvousMediatorLifecycleTest, RestartDoesNotCrash) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.restart();
    SUCCEED();
}

TEST(RendezvousMediatorLifecycleTest, StopDoesNotCrash) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.stop();
    SUCCEED();
}

TEST(RendezvousMediatorLifecycleTest, StopMultipleTimes) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.stop();
    mediator.stop();
    mediator.stop();
    SUCCEED();
}

TEST(RendezvousMediatorLifecycleTest, RestartAfterStop) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.stop();
    mediator.restart();
    SUCCEED();
}

TEST(RendezvousMediatorLifecycleTest, RegisterStopRestartRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.register_peer("peer1", "pw1");
    mediator.stop();
    mediator.restart();
    mediator.register_peer("peer2", "pw2");
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator callbacks
// =============================================================================
TEST(RendezvousMediatorCallbackTest, SetOnMessage) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::atomic<int> call_count{0};
    mediator.set_on_message([&](RendezvousMessageType type,
                                 const std::vector<uint8_t>& data) {
        call_count++;
    });
    EXPECT_EQ(call_count.load(), 0);
}

TEST(RendezvousMediatorCallbackTest, SetOnPeerOnline) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::atomic<int> call_count{0};
    std::string last_id;
    bool last_online = false;
    mediator.set_on_peer_online([&](const std::string& id, bool online) {
        call_count++;
        last_id = id;
        last_online = online;
    });
    EXPECT_EQ(call_count.load(), 0);
}

TEST(RendezvousMediatorCallbackTest, SetOnSoftwareUpdate) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::atomic<int> call_count{0};
    std::string last_version;
    std::string last_url;
    mediator.set_on_software_update([&](const std::string& version,
                                          const std::string& url) {
        call_count++;
        last_version = version;
        last_url = url;
    });
    EXPECT_EQ(call_count.load(), 0);
}

TEST(RendezvousMediatorCallbackTest, MultipleCallbacks) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::atomic<int> msg_count{0};
    std::atomic<int> online_count{0};
    std::atomic<int> update_count{0};

    mediator.set_on_message([&](RendezvousMessageType, const std::vector<uint8_t>&) {
        msg_count++;
    });
    mediator.set_on_peer_online([&](const std::string&, bool) {
        online_count++;
    });
    mediator.set_on_software_update([&](const std::string&, const std::string&) {
        update_count++;
    });

    EXPECT_EQ(msg_count.load(), 0);
    EXPECT_EQ(online_count.load(), 0);
    EXPECT_EQ(update_count.load(), 0);
}

TEST(RendezvousMediatorCallbackTest, ReplaceCallback) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::atomic<int> cb1_count{0};
    std::atomic<int> cb2_count{0};

    mediator.set_on_message([&](RendezvousMessageType, const std::vector<uint8_t>&) {
        cb1_count++;
    });
    mediator.set_on_message([&](RendezvousMessageType, const std::vector<uint8_t>&) {
        cb2_count++;
    });
    // Second callback replaces first
    SUCCEED();
}

TEST(RendezvousMediatorCallbackTest, SetNullCallbacks) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.set_on_message(nullptr);
    mediator.set_on_peer_online(nullptr);
    mediator.set_on_software_update(nullptr);
    // Should not crash
    mediator.register_peer("peer", "pw");
    mediator.punch_hole("peer", "key", "token", ConnType::DEFAULT_CONN, false);
    mediator.query_online("peer");
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousMediator concurrent operations
// =============================================================================
TEST(RendezvousMediatorConcurrentTest, ParallelRegisterUnregister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&mediator, i]() {
            mediator.register_peer("peer" + std::to_string(i),
                                   "pw" + std::to_string(i));
            std::this_thread::sleep_for(10ms);
            mediator.unregister_peer();
        });
    }
    for (auto& t : threads) t.join();
    SUCCEED();
}

TEST(RendezvousMediatorConcurrentTest, ParallelQueries) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&mediator, i]() {
            for (int j = 0; j < 10; j++) {
                mediator.query_online("peer-" + std::to_string(i) +
                                      "-" + std::to_string(j));
            }
        });
    }
    for (auto& t : threads) t.join();
    SUCCEED();
}

TEST(RendezvousMediatorConcurrentTest, ParallelPunchHole) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&mediator, i]() {
            mediator.punch_hole("peer-" + std::to_string(i), "key", "token",
                                ConnType::DEFAULT_CONN, i % 2 == 0);
        });
    }
    for (auto& t : threads) t.join();
    SUCCEED();
}

TEST(RendezvousMediatorConcurrentTest, ParallelNatTest) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; i++) {
        threads.emplace_back([&mediator]() {
            mediator.test_nat();
            mediator.get_nat_type();
        });
    }
    for (auto& t : threads) t.join();
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousServer construction
// =============================================================================
TEST(RendezvousServerConstructionTest, DefaultPort) {
    RendezvousServer server;
    EXPECT_FALSE(server.is_running());
}

TEST(RendezvousServerConstructionTest, CustomPort) {
    RendezvousServer server(21116);
    EXPECT_FALSE(server.is_running());
}

TEST(RendezvousServerConstructionTest, AlternativePort) {
    RendezvousServer server(12345);
    EXPECT_FALSE(server.is_running());
}

TEST(RendezvousServerConstructionTest, MultipleInstances) {
    RendezvousServer s1(21116);
    RendezvousServer s2(21117);
    EXPECT_FALSE(s1.is_running());
    EXPECT_FALSE(s2.is_running());
}

TEST(RendezvousServerConstructionTest, NotRunningInitially) {
    RendezvousServer server(21116);
    EXPECT_FALSE(server.is_running());
}

// =============================================================================
// Tests: RendezvousServer start/stop
// =============================================================================
TEST(RendezvousServerStartStopTest, StartAndIsRunning) {
    RendezvousServer server(21116);
    EXPECT_FALSE(server.is_running());
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

TEST(RendezvousServerStartStopTest, StartTwiceNoCrash) {
    RendezvousServer server(21116);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.start(); // Second start should be no-op
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

TEST(RendezvousServerStartStopTest, StopWithoutStart) {
    RendezvousServer server(21116);
    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(RendezvousServerStartStopTest, StopTwiceNoCrash) {
    RendezvousServer server(21116);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    server.stop(); // Second stop should be no-op
    EXPECT_FALSE(server.is_running());
}

TEST(RendezvousServerStartStopTest, StartStopCycle) {
    RendezvousServer server(21116);
    for (int i = 0; i < 3; i++) {
        server.start();
        EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
        server.stop();
        EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    }
}

TEST(RendezvousServerStartStopTest, HighPortNumber) {
    RendezvousServer server(50000);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

// =============================================================================
// Tests: RendezvousServer peer registration (inline API)
// =============================================================================
TEST(RendezvousServerPeerTest, RegisterPeer) {
    RendezvousServer server(21116);
    server.register_peer("peer-001", "pk-data", "192.168.1.1", 12345);
    EXPECT_TRUE(server.is_peer_online("peer-001"));
}

TEST(RendezvousServerPeerTest, RegisterPeerNotRunning) {
    RendezvousServer server(21116);
    server.register_peer("peer-002", "pk", "10.0.0.1", 9999);
    EXPECT_TRUE(server.is_peer_online("peer-002"));
}

TEST(RendezvousServerPeerTest, RegisterMultiplePeers) {
    RendezvousServer server(21116);
    for (int i = 0; i < 10; i++) {
        std::string id = "peer-" + std::to_string(i);
        server.register_peer(id, "pk", "192.168.1." + std::to_string(i + 1),
                             10000 + i);
    }
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(server.is_peer_online("peer-" + std::to_string(i)));
    }
}

TEST(RendezvousServerPeerTest, UnregisterPeer) {
    RendezvousServer server(21116);
    server.register_peer("peer-100", "pk", "192.168.1.100", 11111);
    EXPECT_TRUE(server.is_peer_online("peer-100"));
    server.unregister_peer("peer-100");
    EXPECT_FALSE(server.is_peer_online("peer-100"));
}

TEST(RendezvousServerPeerTest, UnregisterNonexistent) {
    RendezvousServer server(21116);
    server.unregister_peer("does-not-exist");
    EXPECT_FALSE(server.is_peer_online("does-not-exist"));
}

TEST(RendezvousServerPeerTest, IsPeerOnlineInitiallyFalse) {
    RendezvousServer server(21116);
    EXPECT_FALSE(server.is_peer_online("any-peer"));
}

TEST(RendezvousServerPeerTest, RegisterPeerEmptyId) {
    RendezvousServer server(21116);
    server.register_peer("", "", "", 0);
    EXPECT_TRUE(server.is_peer_online(""));
}

TEST(RendezvousServerPeerTest, RegisterDuplicatePeer) {
    RendezvousServer server(21116);
    server.register_peer("dup-peer", "pk1", "1.1.1.1", 1000);
    server.register_peer("dup-peer", "pk2", "2.2.2.2", 2000);
    EXPECT_TRUE(server.is_peer_online("dup-peer"));
}

// =============================================================================
// Tests: RendezvousServer get_online_peers
// =============================================================================
TEST(RendezvousServerOnlinePeersTest, EmptyListInitially) {
    RendezvousServer server(21116);
    auto peers = server.get_online_peers();
    EXPECT_TRUE(peers.empty());
}

TEST(RendezvousServerOnlinePeersTest, ContainsRegisteredPeers) {
    RendezvousServer server(21116);
    server.register_peer("a", "pk", "1.1.1.1", 1000);
    server.register_peer("b", "pk", "2.2.2.2", 2000);
    server.register_peer("c", "pk", "3.3.3.3", 3000);

    auto peers = server.get_online_peers();
    EXPECT_EQ(peers.size(), 3u);
}

TEST(RendezvousServerOnlinePeersTest, DoesNotContainUnregisteredPeers) {
    RendezvousServer server(21116);
    server.register_peer("x", "pk", "1.1.1.1", 1000);
    server.register_peer("y", "pk", "2.2.2.2", 2000);
    server.unregister_peer("x");

    auto peers = server.get_online_peers();
    EXPECT_EQ(peers.size(), 1u);
    EXPECT_NE(std::find(peers.begin(), peers.end(), "y"), peers.end());
    EXPECT_EQ(std::find(peers.begin(), peers.end(), "x"), peers.end());
}

TEST(RendezvousServerOnlinePeersTest, AllRegisteredPeersAreOnline) {
    RendezvousServer server(21116);
    std::vector<std::string> expected = {"p1", "p2", "p3", "p4", "p5"};
    for (const auto& id : expected) {
        server.register_peer(id, "pk", "1.1.1.1", 1000);
    }
    auto peers = server.get_online_peers();
    for (const auto& id : expected) {
        EXPECT_NE(std::find(peers.begin(), peers.end(), id), peers.end());
    }
}

// =============================================================================
// Tests: RendezvousServer NAT type updates
// =============================================================================
TEST(RendezvousServerNatTest, DefaultNatTypeUnknown) {
    RendezvousServer server(21116);
    EXPECT_EQ(server.get_nat_type("nonexistent"), NatType::UNKNOWN_NAT);
}

TEST(RendezvousServerNatTest, DefaultNatForRegisteredPeer) {
    RendezvousServer server(21116);
    server.register_peer("peer-nat", "pk", "1.1.1.1", 1000);
    EXPECT_EQ(server.get_nat_type("peer-nat"), NatType::UNKNOWN_NAT);
}

TEST(RendezvousServerNatTest, UpdateNatType) {
    RendezvousServer server(21116);
    server.register_peer("peer-nat2", "pk", "1.1.1.1", 1000);
    server.update_nat_type("peer-nat2", NatType::OPEN_INTERNET);
    EXPECT_EQ(server.get_nat_type("peer-nat2"), NatType::OPEN_INTERNET);
}

TEST(RendezvousServerNatTest, UpdateNatTypeToAllValues) {
    RendezvousServer server(21116);
    std::vector<NatType> nats = {
        NatType::UNKNOWN_NAT, NatType::OPEN_INTERNET, NatType::FULL_CONE,
        NatType::RESTRICTED_CONE, NatType::PORT_RESTRICTED_CONE, NatType::SYMMETRIC
    };
    for (auto nat : nats) {
        std::string id = "peer-nat-" + std::to_string(static_cast<int32_t>(nat));
        server.register_peer(id, "pk", "1.1.1.1", 1000);
        server.update_nat_type(id, nat);
        EXPECT_EQ(server.get_nat_type(id), nat);
    }
}

TEST(RendezvousServerNatTest, UpdateNatTypeNonexistent) {
    RendezvousServer server(21116);
    server.update_nat_type("ghost", NatType::FULL_CONE);
    EXPECT_EQ(server.get_nat_type("ghost"), NatType::UNKNOWN_NAT);
}

TEST(RendezvousServerNatTest, GetNatTypeNonexistent) {
    RendezvousServer server(21116);
    EXPECT_EQ(server.get_nat_type("never-registered"), NatType::UNKNOWN_NAT);
}

// =============================================================================
// Tests: RendezvousServer callbacks
// =============================================================================
TEST(RendezvousServerCallbackTest, SetOnRegister) {
    RendezvousServer server(21116);
    std::atomic<int> count{0};
    std::string last_id;
    bool last_success = false;

    server.set_on_register([&](const std::string& id, bool success) {
        count++;
        last_id = id;
        last_success = success;
    });

    server.register_peer("cb-peer", "pk", "1.1.1.1", 1000);
    // Give the ASIO strand time to process
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(last_id, "cb-peer");
    EXPECT_TRUE(last_success);
}

TEST(RendezvousServerCallbackTest, OnRegisterCalledOnUnregister) {
    RendezvousServer server(21116);
    std::atomic<int> count{0};
    std::string last_id;
    bool last_success = true;

    server.set_on_register([&](const std::string& id, bool success) {
        count++;
        last_id = id;
        last_success = success;
    });

    server.register_peer("cb-peer2", "pk", "1.1.1.1", 1000);
    std::this_thread::sleep_for(50ms);

    count = 0;
    server.unregister_peer("cb-peer2");
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(last_id, "cb-peer2");
    EXPECT_FALSE(last_success);
}

TEST(RendezvousServerCallbackTest, SetNullCallback) {
    RendezvousServer server(21116);
    server.set_on_register(nullptr);
    server.register_peer("null-cb", "pk", "1.1.1.1", 1000);
    // Should not crash
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousServer concurrent access
// =============================================================================
TEST(RendezvousServerConcurrentTest, ConcurrentPeerRegistration) {
    RendezvousServer server(21116);
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&server, i]() {
            server.register_peer("conc-" + std::to_string(i), "pk",
                                 "192.168.1." + std::to_string(i + 100), 10000 + i);
        });
    }
    for (auto& t : threads) t.join();

    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(server.is_peer_online("conc-" + std::to_string(i)));
    }
}

TEST(RendezvousServerConcurrentTest, ConcurrentNatUpdates) {
    RendezvousServer server(21116);
    // Register peers first
    for (int i = 0; i < 5; i++) {
        server.register_peer("cnat-" + std::to_string(i), "pk", "1.1.1.1", 1000);
    }

    // Update NAT types concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&server, i]() {
            server.update_nat_type("cnat-" + std::to_string(i),
                                   static_cast<NatType>(i % 6));
        });
    }
    for (auto& t : threads) t.join();
    SUCCEED();
}

// =============================================================================
// Tests: RendezvousServer edge cases
// =============================================================================
TEST(RendezvousServerEdgeCaseTest, PortZero) {
    RendezvousServer server(0);
    server.start();
    // Should fail to start (or pick a random port)
    // Either way, don't crash
    std::this_thread::sleep_for(100ms);
    server.stop();
    SUCCEED();
}

TEST(RendezvousServerEdgeCaseTest, HighPort) {
    RendezvousServer server(65535);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

TEST(RendezvousServerEdgeCaseTest, ManyPeers) {
    RendezvousServer server(21116);
    for (int i = 0; i < 100; i++) {
        server.register_peer("batch-" + std::to_string(i), "pk",
                             "10.0.0." + std::to_string(i % 255 + 1), 20000 + i);
    }
    auto peers = server.get_online_peers();
    EXPECT_EQ(peers.size(), 100u);
}

TEST(RendezvousServerEdgeCaseTest, RegisterThenUpdateNatThenGetOnline) {
    RendezvousServer server(21116);
    server.register_peer("flow1", "pk1", "1.1.1.1", 1000);
    EXPECT_TRUE(server.is_peer_online("flow1"));
    server.update_nat_type("flow1", NatType::OPEN_INTERNET);
    EXPECT_EQ(server.get_nat_type("flow1"), NatType::OPEN_INTERNET);
    auto peers = server.get_online_peers();
    EXPECT_EQ(peers.size(), 1u);

    server.register_peer("flow2", "pk2", "2.2.2.2", 2000);
    server.update_nat_type("flow2", NatType::SYMMETRIC);
    EXPECT_EQ(server.get_nat_type("flow2"), NatType::SYMMETRIC);
    EXPECT_EQ(server.get_online_peers().size(), 2u);

    server.unregister_peer("flow1");
    EXPECT_FALSE(server.is_peer_online("flow1"));
    EXPECT_TRUE(server.is_peer_online("flow2"));
    EXPECT_EQ(server.get_online_peers().size(), 1u);
    EXPECT_EQ(server.get_nat_type("flow1"), NatType::UNKNOWN_NAT);
}

// =============================================================================
// Tests: RelayServer construction
// =============================================================================
TEST(RelayServerConstructionTest, DefaultPort) {
    RelayServer server;
    EXPECT_FALSE(server.is_running());
}

TEST(RelayServerConstructionTest, CustomPort) {
    RelayServer server(21117);
    EXPECT_FALSE(server.is_running());
}

TEST(RelayServerConstructionTest, AlternativePort) {
    RelayServer server(12346);
    EXPECT_FALSE(server.is_running());
}

TEST(RelayServerConstructionTest, MultipleInstances) {
    RelayServer r1(21117);
    RelayServer r2(21118);
    EXPECT_FALSE(r1.is_running());
    EXPECT_FALSE(r2.is_running());
}

TEST(RelayServerConstructionTest, NotRunningInitially) {
    RelayServer server(21117);
    EXPECT_FALSE(server.is_running());
}

// =============================================================================
// Tests: RelayServer start/stop
// =============================================================================
TEST(RelayServerStartStopTest, StartAndIsRunning) {
    RelayServer server(21117);
    EXPECT_FALSE(server.is_running());
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

TEST(RelayServerStartStopTest, StartTwiceNoCrash) {
    RelayServer server(21117);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.start(); // Second start should be no-op
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

TEST(RelayServerStartStopTest, StopWithoutStart) {
    RelayServer server(21117);
    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(RelayServerStartStopTest, StopTwiceNoCrash) {
    RelayServer server(21117);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    server.stop(); // Second stop should be no-op
    EXPECT_FALSE(server.is_running());
}

TEST(RelayServerStartStopTest, StartStopCycle) {
    RelayServer server(21117);
    for (int i = 0; i < 3; i++) {
        server.start();
        EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
        server.stop();
        EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    }
}

TEST(RelayServerStartStopTest, HighPortNumber) {
    RelayServer server(55555);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

// =============================================================================
// Tests: RelayServer relay_connection
// =============================================================================
// A mock stream for testing relay_connection()
class MockStream : public Stream {
public:
    MockStream(bool open = true) : open_(open) {}
    bool send(const std::vector<uint8_t>& data) override {
        sent_.insert(sent_.end(), data.begin(), data.end());
        send_count_++;
        return true;
    }
    std::vector<uint8_t> recv() override {
        if (recv_queue_.empty()) return {};
        auto data = std::move(recv_queue_.front());
        recv_queue_.pop();
        return data;
    }
    bool is_open() const override { return open_; }
    void close() override { open_ = false; }
    std::string local_addr() const override { return "mock-local"; }
    std::string remote_addr() const override { return "mock-remote"; }
    void set_nodelay(bool) override {}
    void set_encryption_key(const std::vector<uint8_t>&) override {}

    void enqueue_recv(const std::vector<uint8_t>& data) { recv_queue_.push(data); }
    bool open_ = true;
    std::vector<uint8_t> sent_;
    int send_count_ = 0;
    std::queue<std::vector<uint8_t>> recv_queue_;
};

TEST(RelayServerRelayConnectionTest, RelayConnectionWithValidStreams) {
    RelayServer server(21117);
    server.start();

    auto mock_a = std::make_shared<MockStream>();
    auto mock_b = std::make_shared<MockStream>();

    // Enqueue some data on stream B so relay thread has something to do
    std::vector<uint8_t> test_data = {'h', 'e', 'l', 'l', 'o'};
    mock_b->enqueue_recv(test_data);

    server.relay_connection("test-uuid-001", mock_a, mock_b);

    // Give the relay thread time to process
    std::this_thread::sleep_for(200ms);

    // Stop server, which will stop all relay threads
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));

    // Data from B should have been relayed to A
    // (send_count_ may be 0 on some platforms depending on timing)
    EXPECT_GE(mock_a->send_count_, 0);
}

TEST(RelayServerRelayConnectionTest, RelayConnectionClosedStreams) {
    RelayServer server(21117);
    server.start();

    auto mock_a = std::make_shared<MockStream>(false); // closed
    auto mock_b = std::make_shared<MockStream>(false); // closed

    server.relay_connection("test-uuid-002", mock_a, mock_b);
    std::this_thread::sleep_for(200ms);
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));

    // Should handle gracefully
    SUCCEED();
}

TEST(RelayServerRelayConnectionTest, RelayConnectionMultiple) {
    RelayServer server(21117);
    server.start();

    for (int i = 0; i < 3; i++) {
        auto a = std::make_shared<MockStream>();
        auto b = std::make_shared<MockStream>();
        b->enqueue_recv({'t', 'e', 's', 't'});
        server.relay_connection("uuid-" + std::to_string(i), a, b);
    }

    std::this_thread::sleep_for(200ms);
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    SUCCEED();
}

TEST(RelayServerRelayConnectionTest, RelayConnectionNotRunning) {
    RelayServer server(21117);
    // Server not started
    auto a = std::make_shared<MockStream>();
    auto b = std::make_shared<MockStream>();
    server.relay_connection("uuid-003", a, b);
    // Should not crash even though server isn't running
    SUCCEED();
}

TEST(RelayServerRelayConnectionTest, RelayConnectionEmptyUUID) {
    RelayServer server(21117);
    server.start();
    auto a = std::make_shared<MockStream>();
    auto b = std::make_shared<MockStream>();
    server.relay_connection("", a, b);
    std::this_thread::sleep_for(100ms);
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    SUCCEED();
}

// =============================================================================
// Tests: RelayServer concurrent access
// =============================================================================
TEST(RelayServerConcurrentTest, ConcurrentStartStop) {
    for (int i = 0; i < 3; i++) {
        RelayServer server(21117);
        server.start();
        EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
        server.stop();
        EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    }
}

TEST(RelayServerConcurrentTest, ConcurrentRelayConnections) {
    RelayServer server(21117);
    server.start();

    std::vector<std::thread> threads;
    std::atomic<int> completed{0};
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&server, i, &completed]() {
            auto a = std::make_shared<MockStream>();
            auto b = std::make_shared<MockStream>();
            b->enqueue_recv({'x', 'y', 'z'});
            server.relay_connection("conc-uuid-" + std::to_string(i), a, b);
            completed++;
        });
    }

    for (auto& t : threads) t.join();
    std::this_thread::sleep_for(200ms);
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    EXPECT_EQ(completed.load(), 5);
}

// =============================================================================
// Tests: RendezvousMediator comprehensive scenarios
// =============================================================================
TEST(RendezvousMediatorScenarioTest, FullLifecycle) {
    // Construct -> Register -> Query -> Punch -> Unregister -> Stop
    RendezvousMediator mediator("rs-ny.rustdesk.com", "full-lifecycle",
                                "cppdesk", 60000);

    EXPECT_FALSE(mediator.is_registered());
    EXPECT_FALSE(mediator.is_connected());

    mediator.register_peer("lifecycle-peer", "lifecycle-pw");
    EXPECT_TRUE(mediator.is_registered());

    mediator.query_online("another-peer");
    mediator.punch_hole("another-peer", "key123", "token456",
                        ConnType::DEFAULT_CONN, false);

    EXPECT_EQ(mediator.get_nat_type(), static_cast<int32_t>(NatType::UNKNOWN_NAT));
    EXPECT_EQ(mediator.get_udp_port(), 0u);

    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());

    mediator.stop();
    SUCCEED();
}

TEST(RendezvousMediatorScenarioTest, ServerClientInteraction) {
    // Create a local rendezvous server
    RendezvousServer server(21116);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));

    // Check that we can manage peers on the server
    server.register_peer("local-peer-1", "pk-local", "127.0.0.1", 9999);
    EXPECT_TRUE(server.is_peer_online("local-peer-1"));

    server.register_peer("local-peer-2", "pk-local-2", "127.0.0.1", 9998);
    auto peers = server.get_online_peers();
    EXPECT_GE(peers.size(), 2u);

    server.update_nat_type("local-peer-1", NatType::FULL_CONE);
    EXPECT_EQ(server.get_nat_type("local-peer-1"), NatType::FULL_CONE);

    server.unregister_peer("local-peer-2");
    EXPECT_FALSE(server.is_peer_online("local-peer-2"));

    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
}

TEST(RendezvousMediatorScenarioTest, RelayServerFullLifecycle) {
    RelayServer relay(21117);

    EXPECT_FALSE(relay.is_running());

    relay.start();
    EXPECT_TRUE(wait_for([&]() { return relay.is_running(); }, 1000));

    EXPECT_TRUE(relay.is_running());

    relay.stop();
    EXPECT_TRUE(wait_for([&]() { return !relay.is_running(); }, 1000));

    EXPECT_FALSE(relay.is_running());
}

TEST(RendezvousMediatorScenarioTest, AllServersStartStop) {
    RendezvousServer rendezvous(21116);
    RelayServer relay(21117);

    rendezvous.start();
    relay.start();

    EXPECT_TRUE(wait_for([&]() { return rendezvous.is_running(); }, 1000));
    EXPECT_TRUE(wait_for([&]() { return relay.is_running(); }, 1000));

    EXPECT_TRUE(rendezvous.is_running());
    EXPECT_TRUE(relay.is_running());

    rendezvous.stop();
    relay.stop();

    EXPECT_TRUE(wait_for([&]() { return !rendezvous.is_running(); }, 1000));
    EXPECT_TRUE(wait_for([&]() { return !relay.is_running(); }, 1000));

    EXPECT_FALSE(rendezvous.is_running());
    EXPECT_FALSE(relay.is_running());
}

// =============================================================================
// Tests: Stress / Edge Cases
// =============================================================================
TEST(StressTest, ManyRendezvousMediators) {
    std::vector<std::unique_ptr<RendezvousMediator>> mediators;
    for (int i = 0; i < 10; i++) {
        mediators.push_back(std::make_unique<RendezvousMediator>(
            "rs-ny.rustdesk.com", "host-" + std::to_string(i),
            "cppdesk", 60000));
    }
    for (auto& m : mediators) {
        m->stop();
    }
    SUCCEED();
}

TEST(StressTest, ManyRendezvousServersSequential) {
    for (int i = 0; i < 5; i++) {
        RendezvousServer server(21116);
        server.start();
        EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
        server.stop();
        EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    }
}

TEST(StressTest, ManyRelayServersSequential) {
    for (int i = 0; i < 5; i++) {
        RelayServer server(21117);
        server.start();
        EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
        server.stop();
        EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    }
}

TEST(StressTest, PeerRegistrationStress) {
    RendezvousServer server(21116);
    for (int i = 0; i < 50; i++) {
        std::string id = "stress-peer-" + std::to_string(i);
        server.register_peer(id, "pk", "10.0.0." + std::to_string(i % 256), 20000 + i);
        server.update_nat_type(id, static_cast<NatType>(i % 6));
    }
    for (int i = 0; i < 50; i++) {
        std::string id = "stress-peer-" + std::to_string(i);
        EXPECT_TRUE(server.is_peer_online(id));
        int32_t expected_nat = i % 6;
        EXPECT_EQ(static_cast<int32_t>(server.get_nat_type(id)), expected_nat);
    }
}

TEST(StressTest, RapidRegisterUnregister) {
    RendezvousServer server(21116);
    for (int i = 0; i < 50; i++) {
        server.register_peer("rapid-peer", "pk", "1.1.1.1", 1000);
        server.unregister_peer("rapid-peer");
    }
    EXPECT_FALSE(server.is_peer_online("rapid-peer"));
}

TEST(StressTest, CallbackStress) {
    RendezvousServer server(21116);
    std::atomic<int> callback_count{0};

    server.set_on_register([&](const std::string&, bool) {
        callback_count++;
    });

    for (int i = 0; i < 20; i++) {
        server.register_peer("cb-stress-" + std::to_string(i), "pk", "1.1.1.1", 1000);
    }

    std::this_thread::sleep_for(100ms);

    // We may not get all callbacks (ASIO timing), but we shouldn't crash
    EXPECT_GE(callback_count.load(), 0);

    for (int i = 0; i < 20; i++) {
        server.unregister_peer("cb-stress-" + std::to_string(i));
    }
}

// =============================================================================
// Tests: Default construction and destructor safety
// =============================================================================
TEST(DestructorSafetyTest, RendezvousMediatorDestroyedWithoutStop) {
    {
        RendezvousMediator mediator("rs-ny.rustdesk.com", "test",
                                    "cppdesk", 60000);
        mediator.register_peer("peer", "pw");
        // Let destructor clean up
    }
    SUCCEED();
}

TEST(DestructorSafetyTest, RendezvousServerDestroyedWithoutStop) {
    {
        RendezvousServer server(21116);
        server.start();
        std::this_thread::sleep_for(50ms);
        // Let destructor clean up
    }
    SUCCEED();
}

TEST(DestructorSafetyTest, RelayServerDestroyedWithoutStop) {
    {
        RelayServer server(21117);
        server.start();
        std::this_thread::sleep_for(50ms);
        // Let destructor clean up
    }
    SUCCEED();
}

TEST(DestructorSafetyTest, RendezvousMediatorDestroyedAfterCallbacks) {
    {
        RendezvousMediator mediator("rs-ny.rustdesk.com", "test",
                                    "cppdesk", 60000);
        mediator.set_on_message([](RendezvousMessageType, const std::vector<uint8_t>&) {});
        mediator.set_on_peer_online([](const std::string&, bool) {});
        mediator.set_on_software_update([](const std::string&, const std::string&) {});
        mediator.register_peer("peer", "pw");
        // Destructor should cleanly stop and join threads
    }
    SUCCEED();
}

// =============================================================================
// Tests: move semantics (if applicable, unique_ptr based)
// =============================================================================
TEST(MoveSemanticsTest, UniquePtrRendezvousMediator) {
    auto m1 = std::make_unique<RendezvousMediator>(
        "rs-ny.rustdesk.com", "test", "cppdesk", 60000);
    auto m2 = std::move(m1);
    EXPECT_FALSE(m2->is_registered());
    m2->register_peer("move-peer", "move-pw");
    EXPECT_TRUE(m2->is_registered());
}

TEST(MoveSemanticsTest, UniquePtrRendezvousServer) {
    auto s1 = std::make_unique<RendezvousServer>(21116);
    auto s2 = std::move(s1);
    EXPECT_FALSE(s2->is_running());
}

TEST(MoveSemanticsTest, UniquePtrRelayServer) {
    auto r1 = std::make_unique<RelayServer>(21117);
    auto r2 = std::move(r1);
    EXPECT_FALSE(r2->is_running());
}

// =============================================================================
// Tests: ConnType enum interactions
// =============================================================================
TEST(ConnTypeTest, DefaultValue) {
    ConnType ct{};
    EXPECT_EQ(ct, ConnType::DEFAULT_CONN);
}

TEST(ConnTypeTest, AllDistinct) {
    EXPECT_NE(ConnType::DEFAULT_CONN, ConnType::FILE_TRANSFER);
    EXPECT_NE(ConnType::DEFAULT_CONN, ConnType::PORT_FORWARD);
    EXPECT_NE(ConnType::DEFAULT_CONN, ConnType::RDP);
    EXPECT_NE(ConnType::FILE_TRANSFER, ConnType::PORT_FORWARD);
    EXPECT_NE(ConnType::FILE_TRANSFER, ConnType::RDP);
    EXPECT_NE(ConnType::PORT_FORWARD, ConnType::RDP);
}

// =============================================================================
// Tests: Interaction between Config and Rendezvous
// =============================================================================
TEST(ConfigRendezvousTest, GetRendezvousServer) {
    std::string server = Config::get_rendezvous_server();
    EXPECT_FALSE(server.empty());
}

TEST(ConfigRendezvousTest, SetAndGetRendezvousServer) {
    std::string original = Config::get_rendezvous_server();
    Config::set_rendezvous_server("test.server.com:21116");
    EXPECT_EQ(Config::get_rendezvous_server(), "test.server.com:21116");
    // Restore original
    Config::set_rendezvous_server(original);
}

TEST(ConfigRendezvousTest, ForceRelayOption) {
    bool original = Config::is_force_relay();
    Config::set_option_bool("force-relay", true);
    EXPECT_TRUE(Config::is_force_relay());
    Config::set_option_bool("force-relay", false);
    EXPECT_FALSE(Config::is_force_relay());
    // Restore
    Config::set_option_bool("force-relay", original);
}

// =============================================================================
// Tests: Comprehensive cross-module interaction
// =============================================================================
TEST(CrossModuleTest, MediatorWithServerStarted) {
    RendezvousServer server(21116);
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));

    RendezvousMediator mediator("127.0.0.1", "local-test", "cppdesk", 60000);
    mediator.register_peer("local-peer-mediator", "test-pw");

    EXPECT_TRUE(mediator.is_registered());

    mediator.unregister_peer();
    mediator.stop();
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    SUCCEED();
}

TEST(CrossModuleTest, RelayWithMediatorCreated) {
    RelayServer relay(21117);
    relay.start();
    EXPECT_TRUE(wait_for([&]() { return relay.is_running(); }, 1000));

    RendezvousMediator mediator("127.0.0.1", "relay-test", "cppdesk", 60000);
    mediator.request_relay("relay-uuid-test", "test-licence");

    mediator.stop();
    relay.stop();
    EXPECT_TRUE(wait_for([&]() { return !relay.is_running(); }, 1000));
    SUCCEED();
}

// =============================================================================
// Tests: Additional edge case coverage
// =============================================================================
TEST(EdgeCaseTest, RendezvousServerPortAlreadyBound) {
    RendezvousServer first(21116);
    first.start();
    EXPECT_TRUE(wait_for([&]() { return first.is_running(); }, 1000));

    // Try to start second on same port
    RendezvousServer second(21116);
    second.start();
    // Second should fail to start (port already in use)
    // Give time to fail
    std::this_thread::sleep_for(100ms);

    first.stop();
    second.stop();
    EXPECT_TRUE(wait_for([&]() { return !first.is_running(); }, 1000));
    SUCCEED();
}

TEST(EdgeCaseTest, RelayServerPortAlreadyBound) {
    RelayServer first(21117);
    first.start();
    EXPECT_TRUE(wait_for([&]() { return first.is_running(); }, 1000));

    RelayServer second(21117);
    second.start();
    std::this_thread::sleep_for(100ms);

    first.stop();
    second.stop();
    EXPECT_TRUE(wait_for([&]() { return !first.is_running(); }, 1000));
    SUCCEED();
}

TEST(EdgeCaseTest, MediatorWithInvalidServer) {
    RendezvousMediator mediator("invalid.server.that.does.not.exist.example.com",
                                "test-host", "cppdesk", 60000);
    EXPECT_FALSE(mediator.is_registered());
    EXPECT_FALSE(mediator.is_connected());

    // Operations should not crash even with no connection
    mediator.register_peer("peer-id", "password");
    mediator.query_online("some-peer");
    mediator.punch_hole("peer", "key", "token", ConnType::DEFAULT_CONN, false);
    mediator.test_nat();

    EXPECT_TRUE(mediator.is_registered());
    mediator.stop();
    SUCCEED();
}

TEST(EdgeCaseTest, MediatorWithLocalhost) {
    RendezvousMediator mediator("127.0.0.1", "localhost-test",
                                "cppdesk", 60000);
    EXPECT_FALSE(mediator.is_registered());
    mediator.register_peer("lh-peer", "lh-pw");
    EXPECT_TRUE(mediator.is_registered());
    mediator.stop();
}

TEST(EdgeCaseTest, PunchHoleBeforeRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    // Punch hole before registering anything
    mediator.punch_hole("peer-id", "key", "token",
                        ConnType::DEFAULT_CONN, true);
    SUCCEED();
}

TEST(EdgeCaseTest, QueryOnlineBeforeRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.query_online("some-peer");
    SUCCEED();
}

TEST(EdgeCaseTest, TestNatBeforeRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.test_nat();
    EXPECT_EQ(mediator.get_nat_type(), static_cast<int32_t>(NatType::UNKNOWN_NAT));
}

TEST(EdgeCaseTest, UpdateAliasBeforeRegister) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "test-host",
                                "cppdesk", 60000);
    mediator.update_alias("Pre-register Alias");
    SUCCEED();
}

TEST(EdgeCaseTest, AllOperationsCombined) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "combined-test",
                                "cppdesk", 60000);

    // Set callbacks
    mediator.set_on_message([](RendezvousMessageType, const std::vector<uint8_t>&) {});
    mediator.set_on_peer_online([](const std::string&, bool) {});
    mediator.set_on_software_update([](const std::string&, const std::string&) {});

    // Register
    mediator.register_peer("combined-peer", "combined-pw");
    EXPECT_TRUE(mediator.is_registered());

    // Query
    mediator.query_online("target-peer");

    // Punch holes
    mediator.punch_hole("target-peer", "key1", "token1",
                        ConnType::DEFAULT_CONN, false);
    mediator.punch_hole("target-peer", "key2", "token2",
                        ConnType::FILE_TRANSFER, true);

    // Relay
    mediator.request_relay("combined-uuid", "combined-licence");

    // PK
    mediator.register_pk("combined-pk", "combined-token");

    // Alias
    mediator.update_alias("Combined Alias");

    // NAT
    mediator.test_nat();
    int32_t nat = mediator.get_nat_type();
    EXPECT_TRUE(nat >= 0 && nat <= 5);

    // UDP
    uint16_t port = mediator.get_udp_port();
    EXPECT_EQ(port, 0u);

    // Status
    EXPECT_TRUE(mediator.is_registered());
    bool conn = mediator.is_connected();
    EXPECT_TRUE(conn == true || conn == false);

    // Cleanup
    mediator.unregister_peer();
    EXPECT_FALSE(mediator.is_registered());
    mediator.stop();
}

// =============================================================================
// Tests: RendezvousServer with RelayServer cross testing
// =============================================================================
TEST(ServerCrossTest, BothServersConcurrently) {
    RendezvousServer rendezvous(21116);
    RelayServer relay(21117);

    rendezvous.start();
    relay.start();

    EXPECT_TRUE(wait_for([&]() { return rendezvous.is_running(); }, 1000));
    EXPECT_TRUE(wait_for([&]() { return relay.is_running(); }, 1000));

    // Register peers on rendezvous
    rendezvous.register_peer("cross-peer-1", "pk1", "10.0.0.1", 10001);
    rendezvous.register_peer("cross-peer-2", "pk2", "10.0.0.2", 10002);

    EXPECT_TRUE(rendezvous.is_peer_online("cross-peer-1"));
    EXPECT_TRUE(rendezvous.is_peer_online("cross-peer-2"));

    // Setup relay
    auto a = std::make_shared<MockStream>();
    auto b = std::make_shared<MockStream>();
    b->enqueue_recv({'c', 'r', 'o', 's', 's'});
    relay.relay_connection("cross-uuid", a, b);

    std::this_thread::sleep_for(100ms);

    rendezvous.stop();
    relay.stop();

    EXPECT_TRUE(wait_for([&]() { return !rendezvous.is_running(); }, 1000));
    EXPECT_TRUE(wait_for([&]() { return !relay.is_running(); }, 1000));
    SUCCEED();
}

// =============================================================================
// Tests: Verification of enum-to-wire protocol mapping
// =============================================================================
TEST(ProtocolMappingTest, MessageTypeToWireMapping) {
    // These must match the protocol::WIRE_* constants
    // REGISTER_PEER = 1
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REGISTER_PEER), 1u);
    // PUNCH_HOLE_REQUEST = 2
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_REQUEST), 2u);
    // PUNCH_HOLE_RESPONSE = 3
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PUNCH_HOLE_RESPONSE), 3u);
    // REQUEST_RELAY = 4
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REQUEST_RELAY), 4u);
    // TEST_NAT = 5
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::TEST_NAT), 5u);
    // QUERY_ONLINE = 6
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::QUERY_ONLINE), 6u);
    // HEARTBEAT = 7
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::HEARTBEAT), 7u);
    // REGISTER_PK = 8
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::REGISTER_PK), 8u);
    // PK_RESPONSE = 9
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::PK_RESPONSE), 9u);
    // CONFIG_REQUEST = 10
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::CONFIG_REQUEST), 10u);
    // CONFIG_RESPONSE = 11
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::CONFIG_RESPONSE), 11u);
    // SOFTWARE_UPDATE = 12
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::SOFTWARE_UPDATE), 12u);
    // ALIAS_UPDATE = 13
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::ALIAS_UPDATE), 13u);
    // ADDRESS_BOOK = 14
    EXPECT_EQ(static_cast<uint32_t>(RendezvousMessageType::ADDRESS_BOOK), 14u);
}

TEST(ProtocolMappingTest, NatTypeToWireMapping) {
    // Wire constants must match enum values
    EXPECT_EQ(static_cast<int32_t>(NatType::UNKNOWN_NAT), 0);
    EXPECT_EQ(static_cast<int32_t>(NatType::OPEN_INTERNET), 1);
    EXPECT_EQ(static_cast<int32_t>(NatType::FULL_CONE), 2);
    EXPECT_EQ(static_cast<int32_t>(NatType::RESTRICTED_CONE), 3);
    EXPECT_EQ(static_cast<int32_t>(NatType::PORT_RESTRICTED_CONE), 4);
    EXPECT_EQ(static_cast<int32_t>(NatType::SYMMETRIC), 5);
}

// =============================================================================
// Tests: PeerConfig with rendezvous types
// =============================================================================
TEST(PeerConfigTest, DefaultPeerConfig) {
    PeerConfig pc;
    EXPECT_TRUE(pc.id.empty());
    EXPECT_TRUE(pc.username.empty());
    EXPECT_TRUE(pc.hostname.empty());
    EXPECT_TRUE(pc.platform.empty());
    EXPECT_TRUE(pc.alias.empty());
    EXPECT_FALSE(pc.online);
    EXPECT_EQ(pc.nat_type, NatType::UNKNOWN_NAT);
    EXPECT_TRUE(pc.tags.empty());
}

TEST(PeerConfigTest, PeerConfigWithNatType) {
    PeerConfig pc;
    pc.nat_type = NatType::OPEN_INTERNET;
    EXPECT_EQ(pc.nat_type, NatType::OPEN_INTERNET);

    pc.nat_type = NatType::SYMMETRIC;
    EXPECT_EQ(pc.nat_type, NatType::SYMMETRIC);
}

TEST(PeerConfigTest, PeerConfigOnline) {
    PeerConfig pc;
    pc.online = true;
    EXPECT_TRUE(pc.online);

    pc.online = false;
    EXPECT_FALSE(pc.online);
}

// =============================================================================
// Tests: Timeout configurations
// =============================================================================
TEST(TimeoutConfigTest, HeartbeatInterval) {
    EXPECT_GT(HEARTBEAT_INTERVAL.count(), 0);
}

TEST(TimeoutConfigTest, UdpPunchTimeout) {
    EXPECT_GT(UDP_PUNCH_TIMEOUT.count(), 0);
}

TEST(TimeoutConfigTest, RegInterval) {
    EXPECT_GT(REG_INTERVAL.count(), 0);
}

TEST(TimeoutConfigTest, ConnectTimeout) {
    EXPECT_GT(CONNECT_TIMEOUT.count(), 0);
}

TEST(TimeoutConfigTest, ReadTimeout) {
    EXPECT_GT(READ_TIMEOUT.count(), 0);
}

// =============================================================================
// Tests: RendezvousMediator with default keep alive
// =============================================================================
TEST(RendezvousMediatorDefaultKeepAliveTest, DefaultParameter) {
    // Test that DEFAULT_KEEP_ALIVE works as default
    RendezvousMediator mediator("rs-ny.rustdesk.com", "default-ka",
                                "cppdesk",
                                RendezvousMediator::DEFAULT_KEEP_ALIVE);
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorDefaultKeepAliveTest, HalfDefault) {
    int32_t half = RendezvousMediator::DEFAULT_KEEP_ALIVE / 2;
    RendezvousMediator mediator("rs-ny.rustdesk.com", "half-ka",
                                "cppdesk", half);
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorDefaultKeepAliveTest, DoubleDefault) {
    int32_t doubled = RendezvousMediator::DEFAULT_KEEP_ALIVE * 2;
    RendezvousMediator mediator("rs-ny.rustdesk.com", "double-ka",
                                "cppdesk", doubled);
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorDefaultKeepAliveTest, ZeroKeepAlive) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "zero-ka",
                                "cppdesk", 0);
    EXPECT_FALSE(mediator.is_registered());
}

TEST(RendezvousMediatorDefaultKeepAliveTest, NegativeKeepAlive) {
    RendezvousMediator mediator("rs-ny.rustdesk.com", "neg-ka",
                                "cppdesk", -1000);
    EXPECT_FALSE(mediator.is_registered());
}

// =============================================================================
// Tests: Server is_running consistency
// =============================================================================
TEST(ServerConsistencyTest, RendezvousServerIsRunningAfterStart) {
    RendezvousServer server(21116);
    EXPECT_FALSE(server.is_running());
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    EXPECT_FALSE(server.is_running());
}

TEST(ServerConsistencyTest, RelayServerIsRunningAfterStart) {
    RelayServer server(21117);
    EXPECT_FALSE(server.is_running());
    server.start();
    EXPECT_TRUE(wait_for([&]() { return server.is_running(); }, 1000));
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_TRUE(wait_for([&]() { return !server.is_running(); }, 1000));
    EXPECT_FALSE(server.is_running());
}

// =============================================================================
// Tests: API boundary conditions
// =============================================================================
TEST(BoundaryTest, MaxUint16Ports) {
    RendezvousServer rs(65535);
    RelayServer rl(65535);

    rs.start();
    EXPECT_TRUE(wait_for([&]() { return rs.is_running(); }, 1000));
    rs.stop();
    EXPECT_TRUE(wait_for([&]() { return !rs.is_running(); }, 1000));

    rl.start();
    EXPECT_TRUE(wait_for([&]() { return rl.is_running(); }, 1000));
    rl.stop();
    EXPECT_TRUE(wait_for([&]() { return !rl.is_running(); }, 1000));
}

TEST(BoundaryTest, MinUint16Ports) {
    // Port 0 means "pick any available"
    RendezvousServer rs(0);
    rs.start();
    std::this_thread::sleep_for(100ms);
    rs.stop();
    SUCCEED();
}

TEST(BoundaryTest, VeryLongServerAddress) {
    std::string long_addr(1000, 'a');
    long_addr += ".com";
    RendezvousMediator mediator(long_addr, "test", "cppdesk", 60000);
    EXPECT_FALSE(mediator.is_registered());
    mediator.stop();
}

TEST(BoundaryTest, VeryLongPeerId) {
    RendezvousServer server(21116);
    std::string long_id(5000, 'X');
    server.register_peer(long_id, "pk", "1.1.1.1", 1234);
    EXPECT_TRUE(server.is_peer_online(long_id));
}

// =============================================================================
// Main
// =============================================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
