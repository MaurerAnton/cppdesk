// client.hpp — translation of src/client.rs, part 1 (step 11).
//
// Ported here: SEC30, LoginConfigHandler (+ load_config), Interface,
// Data/Key (+ KEY_MAP), handle_test_delay/handle_hash/handle_login_from_ui.
// Each maps 1:1 (see step README for the async/lock adaptations).
//
// Deferred with owners:
// - Client::start/connect/secure_connection/request_relay/create_relay
//   → client-connection step (needs the crypto seam: box_/secretbox/sign)
// - AudioHandler (+ AUDIO_HOST/OboePlayer) → audio step (cpal/opus/oboe)
// - VideoHandler → video step (scrap VP9 decoder)

#pragma once

#include <hbb_common/config.hpp>
#include <hbb_common/tcp.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "message.pb.h"
#include "rendezvous.pb.h"

namespace rustdesk {

// SEC30 parity (defined in client.rs; first used by server/connection.rs).
inline constexpr std::chrono::seconds kSec30(30);

// --- Key (Key::ControlKey/Chr/_Raw parity) -----------------------------------

struct Key {
    enum class Kind { ControlKey, Chr, Raw } kind = Kind::Raw;
    hbb::ControlKey control = hbb::Unknown;
    uint32_t code = 0;

    static Key control_key(hbb::ControlKey c) {
        Key k;
        k.kind = Kind::ControlKey;
        k.control = c;
        return k;
    }
    static Key chr(uint32_t c) {
        Key k;
        k.kind = Kind::Chr;
        k.code = c;
        return k;
    }
    static Key raw(uint32_t c) {
        Key k;
        k.kind = Kind::Raw;
        k.code = c;
        return k;
    }
    bool operator==(const Key& o) const {
        return kind == o.kind && control == o.control && code == o.code;
    }
};

// KEY_MAP parity (generated table, see src/key_map.cpp).
const std::unordered_map<std::string, Key>& key_map();

// --- Data (pub enum Data parity: 13 variants, payloads field-for-field) ------

struct DataLogin {
    std::string password;
    bool remember = false;
};
struct DataMessage {
    hbb::Message message;
};
struct DataSendFiles {
    int32_t id = 0;
    std::string a, b;
    bool c = false, d = false;
};
struct DataRemoveDirAll {
    int32_t id = 0;
    std::string path;
    bool flag = false;
};
struct DataConfirmDeleteFiles {
    int32_t a = 0, b = 0;
};
struct DataSetNoConfirm {
    int32_t id = 0;
};
struct DataRemoveDir {
    int32_t id = 0;
    std::string path;
};
struct DataRemoveFile {
    int32_t id = 0;
    std::string path;
    int32_t num = 0;
    bool flag = false;
};
struct DataCreateDir {
    int32_t id = 0;
    std::string path;
    bool flag = false;
};
struct DataCancelJob {
    int32_t id = 0;
};
struct DataRemovePortForward {
    int32_t id = 0;
};
struct DataAddPortForward {
    int32_t id = 0;
    std::string a;
    int32_t b = 0;
};
struct DataClose {};
struct DataNewRdp {};

using Data = std::variant<DataClose, DataLogin, DataMessage, DataSendFiles, DataRemoveDirAll,
                           DataConfirmDeleteFiles, DataSetNoConfirm, DataRemoveDir, DataRemoveFile,
                           DataCreateDir, DataCancelJob, DataRemovePortForward, DataAddPortForward,
                           DataNewRdp>;

// --- Interface (pub trait Interface parity) ----------------------------------
// Upstream is `Send + Clone + Sized` with async fns; here an abstract base
// (async → sync over the blocking stream; cloning left to implementors).

class Interface {
public:
    virtual ~Interface() = default;
    virtual void msgbox(const std::string& msgtype, const std::string& title,
                        const std::string& text) = 0;
    virtual bool handle_login_error(const std::string& err) = 0;
    virtual void handle_peer_info(const hbb::PeerInfo& pi) = 0;
    virtual void handle_hash(const hbb::Hash& hash, hbb_common::TcpFramedStream& peer) = 0;
    virtual void handle_login_from_ui(const std::string& password, bool remember,
                                      hbb_common::TcpFramedStream& peer) = 0;
    virtual void handle_test_delay(const hbb::TestDelay& t, hbb_common::TcpFramedStream& peer) = 0;
};

// --- Client connection (start/connect/secure_connection/relays) --------------
// Step 12: full port of Client::start/connect/secure_connection/
// request_relay/create_relay using the crypto seam + blocking streams.
// Upstream returns anyhow::Result (async); here failures throw
// std::runtime_error/system_error and timeouts are ms integers.

// Connected session: (Stream, direct) tuple parity.
struct ClientSession {
    hbb_common::TcpFramedStream stream;
    bool direct = false;
};

struct Client {
    Client() = delete;
    // Punch through the rendezvous server (up to 3 attempts), then connect
    // directly or via relay, then run the secure handshake.
    static ClientSession start(const std::string& peer);
    // Encrypted handshake (no-op keep-alive when pk is not a sign public key).
    // Public so tests (and later the relay paths) can drive it directly.
    static void secure_connection(const std::string& peer_id, const std::vector<uint8_t>& pk,
                                  hbb_common::TcpFramedStream& conn);

private:
    static ClientSession connect(hbb_common::ResolvedAddr local_addr, hbb_common::ResolvedAddr peer,
                                 const std::string& peer_id, const std::vector<uint8_t>& pk,
                                 const std::string& relay_server, hbb_common::ResolvedAddr rendezvous,
                                 uint64_t punch_time_used_ms, hbb::NatType peer_nat_type,
                                 int32_t my_nat_type, bool is_local);
    static hbb_common::TcpFramedStream request_relay(const std::string& peer,
                                                     const std::string& relay_server,
                                                     hbb_common::ResolvedAddr rendezvous,
                                                     bool secure);
    static hbb_common::TcpFramedStream create_relay(const std::string& peer,
                                                    const std::string& uuid,
                                                    const std::string& relay_server);
};

// --- LoginConfigHandler ------------------------------------------------------

// Arc<RwLock<LoginConfigHandler>> parity (forward-declared for friends).
struct LoginShared;
using LoginPtr = std::shared_ptr<LoginShared>;

class LoginConfigHandler {
public:
    // Upstream pub fields stay public; the rest are private like upstream.
    bool remember = false;
    std::pair<std::string, int32_t> port_forward;
    bool support_press = false;
    bool support_refresh = false;

    void initialize(const std::string& id, bool is_file_transfer, bool is_port_forward);
    hbb_common::PeerConfig load_config() const;
    void save_config(hbb_common::PeerConfig config);
    void save_view_style(const std::string& value);
    // Returns the Misc-carrying Message, or nullopt for plain option toggles.
    std::optional<hbb::Message> toggle_option(const std::string& name);
    bool get_toggle_option(const std::string& name) const;
    static hbb::Message refresh();
    hbb::Message save_custom_image_quality(int32_t bitrate, int32_t quantizer);
    std::optional<hbb::Message> save_image_quality(const std::string& value);
    std::string get_option(const std::string& k) const;
    bool handle_login_error(const std::string& err, Interface& interface);
    std::string get_username(const hbb::PeerInfo& pi) const;
    void handle_peer_info(const std::string& username, const hbb::PeerInfo& pi);
    hbb::Message create_login_msg(const std::vector<uint8_t>& password) const;

    const std::string& id() const { return id_; }
    const hbb_common::PeerConfig& config() const { return config_; }

private:
    std::optional<hbb::OptionMessage> get_option_message(bool ignore_default) const;
    static std::optional<hbb::ImageQuality> get_image_quality_enum(const std::string& q,
                                                                   bool ignore_default);

    std::string id_;
    bool is_file_transfer_ = false;
    bool is_port_forward_ = false;
    hbb::Hash hash_;
    std::vector<uint8_t> password_;  // remember password for reconnect
    hbb_common::PeerConfig config_;

    // Free handshake helpers below touch session state under the caller's
    // LoginShared lock (mirrors methods running under RwLock guards upstream).
    friend void handle_hash(LoginPtr, const hbb::Hash&, Interface&,
                            hbb_common::TcpFramedStream&);
    friend void handle_login_from_ui(LoginPtr, const std::string&, bool,
                                     hbb_common::TcpFramedStream&);

    friend struct LoginShared;
};

// Arc<RwLock<LoginConfigHandler>> parity.
struct LoginShared {
    std::mutex mutex;
    LoginConfigHandler handler;
};

// Free functions (handle_test_delay/handle_hash/handle_login_from_ui parity).
// Hash helpers use SHA-256 (OpenSSL); throws only on stream failure, which
// callers report via allow_err-style swallowing.
void handle_test_delay(const hbb::TestDelay& t, hbb_common::TcpFramedStream& peer);
void handle_hash(LoginPtr lc, const hbb::Hash& hash, Interface& interface,
                 hbb_common::TcpFramedStream& peer);
void handle_login_from_ui(LoginPtr lc, const std::string& password, bool remember,
                          hbb_common::TcpFramedStream& peer);

// Login-hash helper (the repeated sha256(password+salt)+challenge chain).
// Exposed for tests; upstream inlines Sha256 each time.
std::vector<uint8_t> sha256_two(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len);

}  // namespace rustdesk
