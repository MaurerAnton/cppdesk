#pragma once

#include "common/protocol.hpp"
#include "common/config.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <queue>
#include <thread>

namespace cppdesk::rendezvous {

using namespace common;

// Rendezvous message types
enum class RendezvousMessageType : uint32_t {
    REGISTER_PEER = 1,
    PUNCH_HOLE_REQUEST = 2,
    PUNCH_HOLE_RESPONSE = 3,
    REQUEST_RELAY = 4,
    TEST_NAT = 5,
    QUERY_ONLINE = 6,
    HEARTBEAT = 7,
    REGISTER_PK = 8,
    PK_RESPONSE = 9,
    CONFIG_REQUEST = 10,
    CONFIG_RESPONSE = 11,
    SOFTWARE_UPDATE = 12,
    ALIAS_UPDATE = 13,
    ADDRESS_BOOK = 14,
};

// Rendezvous mediator — handles all communication with rendezvous server
class RendezvousMediator {
public:
    RendezvousMediator(const std::string& server_addr, const std::string& host,
        const std::string& host_prefix, int32_t keep_alive_ms);
    ~RendezvousMediator();
    
    // Server registration
    void register_peer(const std::string& id, const std::string& password);
    void register_pk(const std::string& pk, const std::string& token);
    void unregister_peer();
    void update_alias(const std::string& alias);
    
    // Client operations
    void punch_hole(const std::string& peer_id, const std::string& key,
        const std::string& token, ConnType conn_type, bool force_relay);
    void request_relay(const std::string& uuid, const std::string& licence_key);
    void query_online(const std::string& peer_id);
    
    // NAT handling
    int32_t get_nat_type() const;
    void test_nat();
    uint16_t get_udp_port() const;
    
    // Status
    bool is_registered() const;
    bool is_connected() const;
    void restart();
    void stop();
    
    // Callbacks
    using OnMessage = std::function<void(RendezvousMessageType type,
        const std::vector<uint8_t>& data)>;
    void set_on_message(OnMessage cb);
    
    using OnPeerOnline = std::function<void(const std::string& id, bool online)>;
    void set_on_peer_online(OnPeerOnline cb);
    
    using OnSoftwareUpdate = std::function<void(const std::string& version,
        const std::string& url)>;
    void set_on_software_update(OnSoftwareUpdate cb);
    
    static constexpr int64_t DEFAULT_KEEP_ALIVE = 60000;
    static constexpr int64_t DEPLOY_RETRY_INTERVAL = 30000;
    static constexpr int64_t PEER_REG_INTERVAL = 2000;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Rendezvous server (hbbs) equivalent — manages peer registration and hole punching
class RendezvousServer {
public:
    RendezvousServer(uint16_t port = RENDEZVOUS_PORT);
    ~RendezvousServer();
    
    void start();
    void stop();
    bool is_running() const;
    
    // Peer management
    void register_peer(const std::string& id, const std::string& pk,
        const std::string& ip, uint16_t port);
    void unregister_peer(const std::string& id);
    bool is_peer_online(const std::string& id) const;
    std::vector<std::string> get_online_peers() const;
    
    // NAT info
    void update_nat_type(const std::string& id, NatType nat_type);
    NatType get_nat_type(const std::string& id) const;
    
    using OnRegister = std::function<void(const std::string& id, bool success)>;
    void set_on_register(OnRegister cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Relay server (hbbr) equivalent
class RelayServer {
public:
    RelayServer(uint16_t port = RELAY_PORT);
    ~RelayServer();
    
    void start();
    void stop();
    bool is_running() const;
    
    // Relay a connection between two peers
    void relay_connection(const std::string& uuid, StreamPtr stream_a,
        StreamPtr stream_b);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppdesk::rendezvous
