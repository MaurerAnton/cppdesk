// client.cpp — see client.hpp (part 1: login config + handshake helpers,
// part 2 (step 12): Client connection + secure handshake).
#include <rustdesk/client.hpp>

#include <rustdesk/common.hpp>
#include <rustdesk/rendezvous_mediator.hpp>

#include <hbb_common/addr_mangle.hpp>
#include <hbb_common/crypto.hpp>

#include <openssl/sha.h>

#include <chrono>
#include <stdexcept>

#include <sys/socket.h>

namespace rustdesk {
namespace {

hbb::BoolOption yes_no(bool v) {
    return v ? hbb::Yes : hbb::No;  // `.into()` parity
}

hbb::Message wrap_option(hbb::OptionMessage option) {
    hbb::Misc misc;
    *misc.mutable_option() = option;
    hbb::Message msg;
    *msg.mutable_misc() = misc;
    return msg;
}

}  // namespace

std::vector<uint8_t> sha256_two(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len) {
    std::vector<uint8_t> out(SHA256_DIGEST_LENGTH);
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, a, a_len);
    SHA256_Update(&ctx, b, b_len);
    SHA256_Final(out.data(), &ctx);
    return out;
}

void LoginConfigHandler::initialize(const std::string& id, bool is_file_transfer,
                                    bool is_port_forward) {
    id_ = id;
    is_file_transfer_ = is_file_transfer;
    is_port_forward_ = is_port_forward;
    config_ = load_config();
    remember = !config_.password.empty();
}

hbb_common::PeerConfig LoginConfigHandler::load_config() const {
    return hbb_common::peer_load(id_);  // load_config(id) parity
}

void LoginConfigHandler::save_config(hbb_common::PeerConfig config) {
    hbb_common::peer_store(config, id_);
    config_ = std::move(config);
}

void LoginConfigHandler::save_view_style(const std::string& value) {
    hbb_common::PeerConfig config = load_config();
    config.view_style = value;
    save_config(std::move(config));
}

std::optional<hbb::Message> LoginConfigHandler::toggle_option(const std::string& name) {
    hbb::OptionMessage option;
    hbb_common::PeerConfig config = load_config();
    if (name == "show-remote-cursor") {
        config.show_remote_cursor = !config.show_remote_cursor;
        option.set_show_remote_cursor(yes_no(config.show_remote_cursor));
    } else if (name == "disable-audio") {
        config.disable_audio = !config.disable_audio;
        option.set_disable_audio(yes_no(config.disable_audio));
    } else if (name == "disable-clipboard") {
        config.disable_clipboard = !config.disable_clipboard;
        option.set_disable_clipboard(yes_no(config.disable_clipboard));
    } else if (name == "lock-after-session-end") {
        config.lock_after_session_end = !config.lock_after_session_end;
        option.set_lock_after_session_end(yes_no(config.lock_after_session_end));
    } else if (name == "privacy-mode") {
        config.privacy_mode = !config.privacy_mode;
        option.set_privacy_mode(yes_no(config.privacy_mode));
    } else if (name == "block-input") {
        option.set_block_input(hbb::Yes);
    } else if (name == "unblock-input") {
        option.set_block_input(hbb::No);
    } else {
        // Plain option toggle: stored in-memory AND on disk, no message.
        // NOTE: upstream reads self.options (in-memory Deref) here, while the
        // named branches above reload from disk — quirk preserved exactly.
        if (config_.options.count(name)) {
            config_.options.erase(name);
        } else {
            config_.options[name] = "Y";
        }
        hbb_common::peer_store(config_, id_);
        return std::nullopt;
    }
    save_config(std::move(config));
    return wrap_option(option);
}

std::optional<hbb::OptionMessage> LoginConfigHandler::get_option_message(bool ignore_default) const {
    if (is_port_forward_ || is_file_transfer_) {
        return std::nullopt;
    }
    int n = 0;
    hbb::OptionMessage msg;
    const std::string q = config_.image_quality;
    if (const auto e = get_image_quality_enum(q, ignore_default)) {
        msg.set_image_quality(*e);
        ++n;
    } else if (q == "custom") {
        const hbb_common::PeerConfig config = hbb_common::peer_load(id_);
        if (config.custom_image_quality.size() >= 2) {
            const int32_t bitrate = config.custom_image_quality[0];
            const int32_t quantizer = config.custom_image_quality[1];
            msg.set_custom_image_quality(bitrate << 8 | quantizer);
            ++n;
        }
    }
    if (get_toggle_option("show-remote-cursor")) {
        msg.set_show_remote_cursor(hbb::Yes);
        ++n;
    }
    if (get_toggle_option("lock-after-session-end")) {
        msg.set_lock_after_session_end(hbb::Yes);
        ++n;
    }
    if (get_toggle_option("privacy_mode")) {  // underscore verbatim (upstream quirk)
        msg.set_privacy_mode(hbb::Yes);
        ++n;
    }
    if (n > 0) {
        return msg;
    }
    return std::nullopt;
}

std::optional<hbb::ImageQuality> LoginConfigHandler::get_image_quality_enum(const std::string& q,
                                                                            bool ignore_default) {
    if (q == "low") {
        return hbb::Low;
    }
    if (q == "best") {
        return hbb::Best;
    }
    if (q == "balanced") {
        if (ignore_default) {
            return std::nullopt;
        }
        return hbb::Balanced;
    }
    return std::nullopt;
}

bool LoginConfigHandler::get_toggle_option(const std::string& name) const {
    if (name == "show-remote-cursor") {
        return config_.show_remote_cursor;
    }
    if (name == "lock-after-session-end") {
        return config_.lock_after_session_end;
    }
    if (name == "privacy-mode") {
        return config_.privacy_mode;
    }
    if (name == "disable-audio") {
        return config_.disable_audio;
    }
    if (name == "disable-clipboard") {
        return config_.disable_clipboard;
    }
    return !get_option(name).empty();
}

hbb::Message LoginConfigHandler::refresh() {
    hbb::Misc misc;
    misc.set_refresh_video(true);
    hbb::Message msg;
    *msg.mutable_misc() = misc;
    return msg;
}

hbb::Message LoginConfigHandler::save_custom_image_quality(int32_t bitrate, int32_t quantizer) {
    hbb::OptionMessage option;
    option.set_custom_image_quality(bitrate << 8 | quantizer);
    hbb::Message msg = wrap_option(option);
    hbb_common::PeerConfig config = load_config();
    config.image_quality = "custom";
    config.custom_image_quality = {bitrate, quantizer};
    save_config(std::move(config));
    return msg;
}

std::optional<hbb::Message> LoginConfigHandler::save_image_quality(const std::string& value) {
    std::optional<hbb::Message> res;
    if (const auto q = get_image_quality_enum(value, false)) {
        hbb::OptionMessage option;
        option.set_image_quality(*q);
        res = wrap_option(option);
    }
    hbb_common::PeerConfig config = load_config();
    config.image_quality = value;
    save_config(std::move(config));
    return res;
}

std::string LoginConfigHandler::get_option(const std::string& k) const {
    const auto it = config_.options.find(k);
    return it != config_.options.end() ? it->second : "";
}

bool LoginConfigHandler::handle_login_error(const std::string& err, Interface& interface) {
    if (err == "Wrong Password") {
        password_.clear();
        interface.msgbox("re-input-password", err, "Do you want to enter again?");
        return true;
    }
    interface.msgbox("error", "Login Error", err);
    return false;
}

std::string LoginConfigHandler::get_username(const hbb::PeerInfo& pi) const {
    if (pi.username().empty()) {
        return config_.info.username;  // self.info via Deref parity
    }
    return pi.username();
}

void LoginConfigHandler::handle_peer_info(const std::string& username, const hbb::PeerInfo& pi) {
    if (!pi.version().empty()) {
        support_press = true;
        support_refresh = true;
    }
    hbb_common::PeerInfoSerde serde{username, pi.hostname(), pi.platform()};
    hbb_common::PeerConfig config = load_config();
    config.info = serde;
    const std::vector<uint8_t> password = password_;
    const std::vector<uint8_t> password0 = config.password;
    if (remember) {
        if (!password.empty() && password != password0) {
            config.password = password;
        }
    } else if (!password0.empty()) {
        config.password.clear();
    }
    // Saved unconditionally (refreshes file mtime — upstream comment).
    save_config(std::move(config));
}

hbb::Message LoginConfigHandler::create_login_msg(const std::vector<uint8_t>& password) const {
    hbb::LoginRequest lr;
    lr.set_username(id_);
    lr.set_password(password.data(), static_cast<int>(password.size()));
    lr.set_my_id(hbb_common::get_id());
    lr.set_my_name(username());
    if (const auto option = get_option_message(true)) {
        *lr.mutable_option() = *option;  // Optionxxxx.into() parity (Some sets)
    }
    if (is_file_transfer_) {
        hbb::FileTransfer ft;
        ft.set_dir(get_option("remote_dir"));
        ft.set_show_hidden(!get_option("remote_show_hidden").empty());
        *lr.mutable_file_transfer() = ft;
    } else if (is_port_forward_) {
        hbb::PortForward pf;
        pf.set_host(port_forward.first);
        pf.set_port(port_forward.second);
        *lr.mutable_port_forward() = pf;
    }
    hbb::Message msg;
    *msg.mutable_login_request() = lr;
    return msg;
}

void handle_test_delay(const hbb::TestDelay& t, hbb_common::TcpFramedStream& peer) {
    if (!t.from_client()) {
        hbb::Message msg;
        *msg.mutable_test_delay() = t;
        if (!peer.send(msg)) {
            // allow_err! parity
        }
    }
}

namespace {

void send_login(LoginPtr lc, const std::vector<uint8_t>& password,
                hbb_common::TcpFramedStream& peer) {
    hbb::Message msg;
    {
        std::unique_lock l(lc->mutex);
        msg = lc->handler.create_login_msg(password);
    }
    if (!peer.send(msg)) {
        // allow_err! parity
    }
}

}  // namespace

void handle_hash(LoginPtr lc, const hbb::Hash& hash, Interface& interface,
                 hbb_common::TcpFramedStream& peer) {
    std::vector<uint8_t> password;
    {
        std::unique_lock l(lc->mutex);
        password = lc->handler.password_;
        if (password.empty()) {
            password = lc->handler.config_.password;
        }
    }
    if (password.empty()) {
        // Login without password; the remote side clicks accept.
        send_login(lc, {}, peer);
        interface.msgbox("input-password", "Password Required", "");
    } else {
        const std::vector<uint8_t> digest =
            sha256_two(password.data(), password.size(),
                       reinterpret_cast<const uint8_t*>(hash.challenge().data()),
                       hash.challenge().size());
        send_login(lc, digest, peer);
    }
    std::unique_lock l(lc->mutex);
    lc->handler.hash_ = hash;
}

void handle_login_from_ui(LoginPtr lc, const std::string& password, bool remember,
                          hbb_common::TcpFramedStream& peer) {
    std::vector<uint8_t> salted;
    {
        std::unique_lock l(lc->mutex);
        hbb::Hash hash = lc->handler.hash_;
        l.unlock();
        salted = sha256_two(reinterpret_cast<const uint8_t*>(password.data()), password.size(),
                            reinterpret_cast<const uint8_t*>(hash.salt().data()),
                            hash.salt().size());
    }
    std::vector<uint8_t> challenge;
    {
        std::unique_lock l(lc->mutex);
        lc->handler.remember = remember;
        lc->handler.password_ = salted;
        hbb::Hash hash = lc->handler.hash_;
        l.unlock();
        challenge = sha256_two(salted.data(), salted.size(),
                               reinterpret_cast<const uint8_t*>(hash.challenge().data()),
                               hash.challenge().size());
    }
    send_login(lc, challenge, peer);
}

// --- Client connection -------------------------------------------------------

namespace {

hbb_common::TcpFramedStream must_connect(const hbb_common::ResolvedAddr& remote,
                                         const hbb_common::ResolvedAddr& local, uint64_t timeout_ms,
                                         const std::string& ctx) {
    auto stream = hbb_common::TcpFramedStream::connect(remote, local, timeout_ms);
    if (!stream) {
        throw std::runtime_error(ctx);  // `.await?` / with_context parity
    }
    return std::move(*stream);
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

[[noreturn]] void fail_handshake(hbb_common::TcpFramedStream& conn, const std::string& what) {
    conn.set_timeout_ms(0);
    throw std::runtime_error(what);
}

}  // namespace

ClientSession Client::start(const std::string& peer) {
    using namespace hbb_common;
    // to-do (upstream): remember the port for each peer, to retry easier.
    const ResolvedAddr any = get_any_listen_addr();
    const ResolvedAddr rendezvous_server = rustdesk::get_rendezvous_server(1000);
    TcpFramedStream socket = must_connect(rendezvous_server, any, kRendezvousTimeoutMs,
                                          "Failed to connect to rendezvous server");
    const auto my_addr = socket.local_addr();
    if (!my_addr) {
        throw std::runtime_error("start: local_addr failed");  // `?` parity
    }
    std::vector<uint8_t> pk;
    std::string relay_server;
    const auto start = std::chrono::steady_clock::now();

    ResolvedAddr peer_addr{AF_INET, "0.0.0.0", 0};
    hbb::NatType peer_nat_type = hbb::UNKNOWN_NAT;
    const int32_t my_nat_type = rustdesk::get_nat_type(100);
    bool is_local = false;
    bool punched = false;
    for (int i = 1; i <= 3; ++i) {
        hbb::RendezvousMessage msg_out;
        hbb::PunchHoleRequest* req = msg_out.mutable_punch_hole_request();
        req->set_id(peer);
        req->set_nat_type(nat_type_from_i32(my_nat_type));
        if (!socket.send(msg_out)) {
            throw std::runtime_error("start: punch request send failed");  // `?` parity
        }
        const auto reply = socket.next_timeout(static_cast<uint64_t>(i) * 3000);
        if (!reply) {
            continue;
        }
        hbb::RendezvousMessage msg_in;
        if (!msg_in.ParseFromArray(reply->data(), static_cast<int>(reply->size()))) {
            continue;  // non-protobuf bytes: log-error parity (silent)
        }
        switch (msg_in.union_case()) {
            case hbb::RendezvousMessage::kPunchHoleResponse: {
                const hbb::PunchHoleResponse& ph = msg_in.punch_hole_response();
                if (ph.socket_addr().empty()) {
                    if (ph.failure() == hbb::PunchHoleResponse::ID_NOT_EXIST) {
                        throw std::runtime_error("ID not exist");
                    }
                    if (ph.failure() == hbb::PunchHoleResponse::OFFLINE) {
                        throw std::runtime_error("Remote desktop is offline");
                    }
                    // Unknown failure: `_ => {}` parity (keep punching).
                } else {
                    peer_nat_type = ph.nat_type();
                    is_local = ph.is_local();
                    pk.assign(ph.pk().begin(), ph.pk().end());
                    relay_server = ph.relay_server();
                    const auto decoded = AddrMangle::decode(
                        reinterpret_cast<const uint8_t*>(ph.socket_addr().data()),
                        ph.socket_addr().size());
                    peer_addr = ResolvedAddr{AF_INET, decoded.first, decoded.second};
                    punched = true;
                    break;  // Hole punched — leave the attempt loop
                }
                break;
            }
            case hbb::RendezvousMessage::kRelayResponse: {
                const hbb::RelayResponse& rr = msg_in.relay_response();
                pk.assign(rr.pk().begin(), rr.pk().end());
                TcpFramedStream conn = create_relay(peer, rr.uuid(), rr.relay_server());
                secure_connection(peer, pk, conn);
                return ClientSession{std::move(conn), false};
            }
            default:
                break;  // unexpected message: log-error parity (silent)
        }
        if (punched) {
            break;
        }
    }
    { TcpFramedStream dropped = std::move(socket); }  // drop(socket) parity
    if (peer_addr.port == 0) {
        throw std::runtime_error("Failed to connect via rendezvous server");
    }
    const auto time_used = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count());
    return connect(*my_addr, peer_addr, peer, pk, relay_server, rendezvous_server, time_used,
                   peer_nat_type, my_nat_type, is_local);
}

ClientSession Client::connect(hbb_common::ResolvedAddr local_addr, hbb_common::ResolvedAddr peer,
                              const std::string& peer_id,
                              const std::vector<uint8_t>& pk, const std::string& relay_server,
                              hbb_common::ResolvedAddr rendezvous_server, uint64_t punch_time_used_ms,
                              hbb::NatType peer_nat_type, int32_t my_nat_type, bool is_local) {
    using namespace hbb_common;
    uint64_t connect_timeout = 0;
    int32_t direct_failures = 0;
    constexpr uint64_t kMin = 1000;
    if (is_local || peer_nat_type == hbb::SYMMETRIC) {
        connect_timeout = kMin;
    } else {
        if (relay_server.empty()) {
            connect_timeout = kConnectTimeoutMs;
        } else {
            if (peer_nat_type == hbb::ASYMMETRIC) {
                if (my_nat_type == static_cast<int32_t>(hbb::UNKNOWN_NAT)) {
                    my_nat_type = rustdesk::get_nat_type(100);
                }
                if (my_nat_type == static_cast<int32_t>(hbb::ASYMMETRIC)) {
                    connect_timeout = kConnectTimeoutMs;
                } else if (my_nat_type == static_cast<int32_t>(hbb::SYMMETRIC)) {
                    connect_timeout = kMin;
                }
            }
            if (connect_timeout == 0) {
                const PeerConfig config = peer_load(peer_id);
                direct_failures = config.direct_failures;
                const uint64_t n = direct_failures > 0 ? 3 : 6;
                connect_timeout = punch_time_used_ms * n;
            }
        }
        if (connect_timeout < kMin) {
            connect_timeout = kMin;
        }
    }
    std::optional<TcpFramedStream> conn = TcpFramedStream::connect(peer, local_addr, connect_timeout);
    const bool direct = conn.has_value();
    if (!conn) {
        if (!relay_server.empty()) {
            try {
                conn = request_relay(peer_id, relay_server, rendezvous_server,
                                     pk.size() == crypto::kSignPublicKeyBytes);
            } catch (...) {
                conn.reset();
            }
            if (!conn) {
                throw std::runtime_error("Failed to connect via relay server");
            }
        } else {
            throw std::runtime_error("Failed to make direct connection to remote desktop");
        }
    }
    if (!relay_server.empty() && (direct_failures == 0) != direct) {
        PeerConfig config = peer_load(peer_id);
        config.direct_failures = direct ? 0 : 1;
        peer_store(config, peer_id);
    }
    secure_connection(peer_id, pk, *conn);
    return ClientSession{std::move(*conn), direct};
}

void Client::secure_connection(const std::string& peer_id, const std::vector<uint8_t>& pk,
                               hbb_common::TcpFramedStream& conn) {
    using namespace hbb_common;
    conn.set_timeout_ms(static_cast<int>(kConnectTimeoutMs));  // timeout() parity
    if (pk.size() != crypto::kSignPublicKeyBytes) {
        // Send an empty message in case the server waits for the first one.
        hbb::Message empty;
        if (!conn.send(empty)) {
            conn.set_timeout_ms(0);
            throw std::runtime_error("secure_connection: keep-alive send failed");
        }
        conn.set_timeout_ms(0);
        return;
    }
    const auto frame = conn.next_timeout(kConnectTimeoutMs);  // timeout() parity
    if (!frame) {
        fail_handshake(conn, "Reset by the peer");  // None parity
    }
    hbb::Message msg_in;
    if (!msg_in.ParseFromArray(frame->data(), static_cast<int>(frame->size()))) {
        fail_handshake(conn, "Handshake failed: invalid message format");
    }
    if (!msg_in.has_signed_id()) {
        fail_handshake(conn, "Handshake failed: invalid message type");
    }
    const hbb::SignedId& si = msg_in.signed_id();
    if (si.pk().size() != crypto::kBoxPublicKeyBytes) {
        fail_handshake(conn, "Handshake failed: invalid public box key length from peer");
    }
    std::vector<uint8_t> id;
    try {
        id = crypto::sign_verify(reinterpret_cast<const uint8_t*>(si.id().data()), si.id().size(),
                                 std::vector<uint8_t>(pk.begin(), pk.end()));
    } catch (...) {
        // Fall back to non-secure connection on pk mismatch (upstream parity).
        hbb::Message msg_out;
        msg_out.mutable_public_key();  // empty PublicKey::new() parity
        if (!conn.send(msg_out)) {
            fail_handshake(conn, "Handshake failed: fallback send failed");
        }
        conn.set_timeout_ms(0);
        return;
    }
    if (id != std::vector<uint8_t>(peer_id.begin(), peer_id.end())) {
        fail_handshake(conn, "Handshake failed: sign failure");
    }
    const auto [our_pk_b, our_sk_b] = crypto::box_gen_keypair();
    const std::vector<uint8_t> key = crypto::secretbox_gen_key();
    const crypto::Nonce24 nonce{};  // box_::Nonce([0u8; NONCEBYTES]) parity
    const std::vector<uint8_t> sealed = crypto::box_seal(
        key.data(), key.size(), nonce,
        std::vector<uint8_t>(si.pk().begin(), si.pk().end()), our_sk_b);
    hbb::Message msg_out;
    hbb::PublicKey* pub_key = msg_out.mutable_public_key();
    pub_key->set_asymmetric_value(our_pk_b.data(), static_cast<int>(our_pk_b.size()));
    pub_key->set_symmetric_value(sealed.data(), static_cast<int>(sealed.size()));
    if (!conn.send(msg_out)) {
        fail_handshake(conn, "Handshake failed: public-key send failed");  // `.await??` parity
    }
    conn.set_key(key);
    conn.set_timeout_ms(0);
}

hbb_common::TcpFramedStream Client::request_relay(const std::string& peer,
                                                  const std::string& relay_server,
                                                  hbb_common::ResolvedAddr rendezvous, bool secure) {
    using namespace hbb_common;
    const ResolvedAddr any = get_any_listen_addr();
    bool succeed = false;
    std::string uuid;
    for (int i = 1; i <= 3; ++i) {
        // Fresh socket per attempt (hbbs wants a different NAT address each time).
        TcpFramedStream socket =
            must_connect(rendezvous, any, kRendezvousTimeoutMs, "Failed to connect to rendezvous server");
        hbb::RendezvousMessage msg_out;
        uuid = rustdesk::uuid_v4();
        hbb::RequestRelay* req = msg_out.mutable_request_relay();
        req->set_id(peer);
        req->set_uuid(uuid);
        req->set_relay_server(relay_server);
        req->set_secure(secure);
        if (!socket.send(msg_out)) {
            throw std::runtime_error("request_relay: send failed");  // `?` parity
        }
        const auto reply = socket.next_timeout(static_cast<uint64_t>(i) * 3000);
        if (!reply) {
            continue;
        }
        hbb::RendezvousMessage msg_in;
        if (!msg_in.ParseFromArray(reply->data(), static_cast<int>(reply->size()))) {
            continue;
        }
        if (msg_in.has_relay_response()) {
            succeed = true;
            break;
        }
    }
    if (!succeed) {
        throw std::runtime_error("request_relay: no relay response");  // bail!("") parity
    }
    return create_relay(peer, uuid, relay_server);
}

hbb_common::TcpFramedStream Client::create_relay(const std::string& peer, const std::string& uuid,
                                                 const std::string& relay_server) {
    using namespace hbb_common;
    TcpFramedStream conn =
        must_connect(to_socket_addr(check_port(relay_server, kRelayPort)), get_any_listen_addr(),
                     kConnectTimeoutMs, "Failed to connect to relay server");
    hbb::RendezvousMessage msg_out;
    hbb::RequestRelay* req = msg_out.mutable_request_relay();
    req->set_id(peer);
    req->set_uuid(uuid);
    if (!conn.send(msg_out)) {
        throw std::runtime_error("create_relay: send failed");
    }
    return conn;
}

}  // namespace rustdesk
