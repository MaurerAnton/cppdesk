// common.hpp — translation of src/common.rs (entry-layer portion, step 08).
//
// What's here: pure helpers (key predicates, time, version, port strings),
// process-global CONTENT/SOFTWARE_UPDATE_URL, username/run_me, and the
// rendezvous/NAT/software-update background tasks (std::thread + blocking
// streams instead of #[tokio::main]).
//
// Deferred with their owner steps (declared nowhere until then):
// - check_clipboard/update_clipboard → clipboard step (needs ClipboardContext)
// - resample_channels → audio step (needs dasp resampling)
// - get_rendezvous_server(ms)/get_nat_type(ms) IPC override → ipc step (the
//   desktop variants delegate to the IPC daemon; here they read Config
//   directly, exactly like the mobile variants upstream)
// - cli.rs Session/start_one_port_forward → client step (needs client::Interface)
// - main.rs argv dispatch → entry step (needs server/ui/platform)

#pragma once

#include <hbb_common/config.hpp>
#include <hbb_common/util.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Generated protobuf (message.proto): KeyEvent oneof predicates below.
#include "message.pb.h"

namespace rustdesk {

inline constexpr char kClipboardName[] = "clipboard";
inline constexpr uint64_t kClipboardIntervalMs = 333;
inline constexpr char kPostfixService[] = "_service";

// CONTENT / SOFTWARE_UPDATE_URL parity (Arc<Mutex<String>> globals).
std::mutex& content_lock();
std::string& shared_content();
std::mutex& update_url_lock();
std::string& software_update_url();

// KeyEvent predicates (control_key()/chr() + union_case() parity).
bool valid_for_numlock(const hbb::KeyEvent& evt);
bool valid_for_capslock(const hbb::KeyEvent& evt);
bool is_control_key(const hbb::KeyEvent& evt, hbb::ControlKey key);
bool is_modifier(const hbb::KeyEvent& evt);

// Misc helpers.
int64_t get_time();  // ms since UNIX epoch
std::string check_port(const std::string& host, int32_t port);
std::string test_if_valid_server(std::string host);
int64_t get_version_number(const std::string& v);
std::string username();  // whoami parity (getlogin/getenv fallback)
// run_me parity: spawn current exe detached; throws std::system_error on failure
// (upstream returns io::Result<Child>; callers here detach immediately).
void run_me(const std::vector<std::string>& args);

// Rendezvous / NAT / update tasks (each spawns a detached worker thread).
// hbb_common timeouts in ms flow straight through.
void test_nat_type();
void test_rendezvous_server();
void check_software_update();

// Direct-Config variants (mobile parity; the desktop IPC override lands
// with the ipc step).
hbb_common::ResolvedAddr get_rendezvous_server(uint64_t timeout_ms);
int32_t get_nat_type(uint64_t timeout_ms);

}  // namespace rustdesk
