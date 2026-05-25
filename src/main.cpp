#include <iostream>
#include <csignal>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include "common/config.hpp"
#include "platform/platform.hpp"
#include "client/client.hpp"
#include "rendezvous/rendezvous.hpp"

using namespace cppdesk;

static std::atomic<bool> running{true};

void signal_handler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    running = false;
}

int main(int argc, char* argv[]) {
    // Setup logging
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("cppdesk.log", true);
    spdlog::logger logger("cppdesk", {console, file});
    spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
    spdlog::set_level(spdlog::level::info);
    
    spdlog::info("cppdesk v{} starting...", common::get_version_number());
    spdlog::info("Platform: {}", common::get_platform_name());
    spdlog::info("Hostname: {}", common::get_hostname());
    
    // Signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Initialize platform
    platform::init();
    
    // Load configuration
    auto& config = common::Config::instance();
    config.load("");
    
    // Generate/load device ID
    std::string device_id = common::Config::get_id();
    spdlog::info("Device ID: {}", device_id);
    
    // Generate key pair
    auto [sk, pk] = common::Config::get_key_pair();
    spdlog::info("Key pair generated (PK size: {} bytes)", pk.size());
    
    // Start rendezvous mediator
    std::string rendezvous_server = common::Config::get_rendezvous_server();
    spdlog::info("Rendezvous server: {}", rendezvous_server);
    
    auto rdv = std::make_unique<rendezvous::RendezvousMediator>(
        rendezvous_server, common::get_hostname(), "cppdesk", 60000);
    
    // Register with rendezvous server
    rdv->register_peer(device_id, common::Config::get_password());
    rdv->test_nat();
    
    spdlog::info("NAT type: {}", rdv->get_nat_type());
    spdlog::info("cppdesk client ready. ID: {}", device_id);
    
    // Main loop
    while (running) {
        // Process rendezvous messages
        // Handle incoming connections
        // Update UI state
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Periodic tasks
        static auto last_save = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now - last_save > std::chrono::seconds(30)) {
            config.save("");
            last_save = now;
        }
    }
    
    // Cleanup
    rdv->unregister_peer();
    config.save("");
    platform::cleanup();
    
    spdlog::info("cppdesk shutdown complete");
    return 0;
}
