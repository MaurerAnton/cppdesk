#include <string>
#include <vector>
#include <map>
#include <functional>
#include <iostream>
#include <spdlog/spdlog.h>
#include "common/config.hpp"

namespace cppdesk::cli {

class CommandRegistry {
    struct Command {
        std::string name;
        std::string description;
        std::string usage;
        std::function<int(const std::vector<std::string>&)> handler;
    };
    std::map<std::string, Command> commands_;
public:
    void register_command(const std::string& name, const std::string& desc,
        const std::string& usage, std::function<int(const std::vector<std::string>&)> handler) {
        commands_[name] = {name, desc, usage, std::move(handler)};
    }
    
    int execute(const std::vector<std::string>& args) {
        if (args.empty()) { print_help(); return 1; }
        auto it = commands_.find(args[0]);
        if (it == commands_.end()) {
            std::cerr << "Unknown command: " << args[0] << std::endl;
            print_help();
            return 1;
        }
        std::vector<std::string> sub_args(args.begin() + 1, args.end());
        return it->second.handler(sub_args);
    }
    
    void print_help() {
        std::cout << "cppdesk CLI commands:" << std::endl;
        for (auto& [name, cmd] : commands_) {
            std::cout << "  " << name << " - " << cmd.description << std::endl;
        }
    }
};

static CommandRegistry& registry() {
    static CommandRegistry reg;
    static bool init = false;
    if (!init) {
        reg.register_command("id", "Show device ID", "id", [](auto&) { std::cout << common::Config::get_id() << std::endl; return 0; });
        reg.register_command("version", "Show version", "version", [](auto&) { std::cout << common::get_version_number() << std::endl; return 0; });
        reg.register_command("status", "Show status", "status", [](auto&) { std::cout << "Platform: " << common::get_platform_name() << std::endl; return 0; });
        reg.register_command("set-password", "Set access password", "set-password <pw>", [](auto& args) {
            if (args.empty()) { std::cerr << "Usage: set-password <password>" << std::endl; return 1; }
            common::Config::set_password(args[0]);
            std::cout << "Password set." << std::endl;
            return 0;
        });
        reg.register_command("set-server", "Set rendezvous server", "set-server <host>", [](auto& args) {
            if (args.empty()) { std::cerr << "Usage: set-server <hostname>" << std::endl; return 1; }
            common::Config::set_rendezvous_server(args[0]);
            std::cout << "Server set to " << args[0] << std::endl;
            return 0;
        });
        reg.register_command("connect", "Connect to peer", "connect <peer_id> [--view-only] [--relay]", [](auto& args) {
            if (args.empty()) { std::cerr << "Usage: connect <peer_id>" << std::endl; return 1; }
            std::cout << "Connecting to " << args[0] << "..." << std::endl;
            return 0;
        });
        reg.register_command("service", "Manage service", "service <start|stop|status>", [](auto& args) {
            if (args.empty()) { std::cerr << "Usage: service <start|stop|status>" << std::endl; return 1; }
            if (args[0] == "start") { std::cout << "Starting service..." << std::endl; }
            else if (args[0] == "stop") { std::cout << "Stopping service..." << std::endl; }
            else if (args[0] == "status") { std::cout << "Service status: unknown" << std::endl; }
            return 0;
        });
        reg.register_command("help", "Show help", "help", [](auto&) { registry().print_help(); return 0; });
        init = true;
    }
    return reg;
}

int run(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    return registry().execute(args);
}

} // namespace