#include <iostream>
#include <spdlog/spdlog.h>
#include <CLI/CLI.hpp>
#include "common/config.hpp"
#include "common/crypto.hpp"

using namespace cppdesk;

int main(int argc, char* argv[]) {
    CLI::App app{"cppdesk CLI - Remote Desktop Command Line Interface"};
    app.set_version_flag("--version,-v", common::get_version_number());
    
    std::string peer_id;
    std::string password;
    bool view_only = false;
    bool force_relay = false;
    
    // Connect subcommand
    auto* connect = app.add_subcommand("connect", "Connect to a remote peer");
    connect->add_option("peer", peer_id, "Peer ID to connect to")->required();
    connect->add_option("--password,-p", password, "Password for authentication");
    connect->add_flag("--view-only", view_only, "View only mode");
    connect->add_flag("--force-relay", force_relay, "Force relay connection");
    
    // ID subcommand
    auto* id = app.add_subcommand("id", "Show this device ID");
    
    // Server subcommand
    auto* server = app.add_subcommand("server", "Start in server mode");
    
    // Password subcommand
    auto* pw_cmd = app.add_subcommand("password", "Set or show password");
    std::string new_pw;
    pw_cmd->add_option("set", new_pw, "New password to set");
    
    CLI11_PARSE(app, argc, argv);
    
    if (connect->parsed()) {
        spdlog::info("Connecting to peer: {}", peer_id);
        if (view_only) spdlog::info("View-only mode");
        if (force_relay) spdlog::info("Force relay mode");
        
        // Connect logic
        spdlog::info("Connection requested to {}", peer_id);
        spdlog::info("Note: GUI features require the graphical client");
        return 0;
    }
    
    if (id->parsed()) {
        std::cout << "Device ID: " << common::Config::get_id() << std::endl;
        std::cout << "Platform: " << common::get_platform_name() << std::endl;
        std::cout << "Hostname: " << common::get_hostname() << std::endl;
        std::cout << "Version: " << common::get_version_number() << std::endl;
        return 0;
    }
    
    if (server->parsed()) {
        spdlog::info("Starting in server mode...");
        spdlog::info("Server mode requires the cppdesk_server binary");
        return 0;
    }
    
    if (pw_cmd->parsed()) {
        if (!new_pw.empty()) {
            common::Config::set_password(new_pw);
            common::Config::instance().save("");
            spdlog::info("Password set successfully");
        } else {
            auto pw = common::Config::get_password();
            if (pw.empty()) {
                spdlog::info("No password set");
            } else {
                spdlog::info("Password is set ({} characters)", pw.size());
            }
        }
        return 0;
    }
    
    // Default: show ID
    std::cout << "cppdesk CLI v" << common::get_version_number() << std::endl;
    std::cout << "Device ID: " << common::Config::get_id() << std::endl;
    std::cout << "Use --help for commands" << std::endl;
    return 0;
}
