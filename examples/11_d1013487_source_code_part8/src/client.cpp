// client.cpp — see client.hpp (part 1: login config + handshake helpers).
#include <rustdesk/client.hpp>

#include <rustdesk/common.hpp>

#include <openssl/sha.h>

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

}  // namespace rustdesk
