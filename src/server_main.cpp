#include <iostream>
#include <csignal>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "common/config.hpp"
#include "common/protocol.hpp"
#include "server/server.hpp"
#include "rendezvous/rendezvous.hpp"
#include "platform/platform.hpp"
#include <asio.hpp>

using namespace cppdesk;

static std::atomic<bool> running{true};

void signal_handler(int sig) {
    spdlog::info("Server received signal {}, shutting down...", sig);
    running = false;
}

class ServerApp {
public:
    ServerApp(uint16_t port = 21117)
        : port_(port), acceptor_(io_ctx_) {}
    
    void run() {
        spdlog::info("cppdesk server v{} starting on port {}",
            common::get_version_number(), port_);
        
        // Create server instance
        server_ = server::Server::create();
        
        // Create rendezvous server
        rdv_server_ = std::make_unique<rendezvous::RendezvousServer>(21116);
        rdv_server_->start();
        
        // Create relay server
        relay_server_ = std::make_unique<rendezvous::RelayServer>(21117);
        relay_server_->start();
        
        // Start TCP acceptor
        start_accept();
        
        // Start IO
        while (running) {
            io_ctx_.run();
            io_ctx_.restart();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        shutdown();
    }
    
    void start_accept() {
        asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), port_);
        acceptor_.open(ep.protocol());
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(ep);
        acceptor_.listen();
        
        spdlog::info("Listening on port {}", port_);
        
        do_accept();
    }
    
    void do_accept() {
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_ctx_);
        acceptor_.async_accept(*socket, [this, socket](std::error_code ec) {
            if (!ec) {
                handle_connection(std::move(*socket));
            }
            if (running) {
                do_accept();
            }
        });
    }
    
    void handle_connection(asio::ip::tcp::socket socket) {
        auto conn = server_->next_connection_id();
        spdlog::info("New connection #{} from {}:{}", conn,
            socket.remote_endpoint().address().to_string(),
            socket.remote_endpoint().port());
        
        // Handle in background
        std::thread([this, sock = std::move(socket), conn]() mutable {
            try {
                sock.set_option(asio::ip::tcp::no_delay(true));
                // Read/write loop
                while (sock.is_open() && running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } catch (...) {}
            server_->remove_connection(conn);
        }).detach();
    }
    
    void shutdown() {
        running = false;
        acceptor_.close();
        if (rdv_server_) rdv_server_->stop();
        if (relay_server_) relay_server_->stop();
        if (server_) server_->close_all_connections();
        spdlog::info("Server shutdown complete");
    }
    
private:
    uint16_t port_;
    asio::io_context io_ctx_;
    asio::ip::tcp::acceptor acceptor_;
    server::ServerPtr server_;
    std::unique_ptr<rendezvous::RendezvousServer> rdv_server_;
    std::unique_ptr<rendezvous::RelayServer> relay_server_;
};

int main(int argc, char* argv[]) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    spdlog::logger logger("cppdesk-server", {console});
    spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
    spdlog::set_level(spdlog::level::info);
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    platform::init();
    
    uint16_t port = common::RELAY_PORT;
    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));
    
    ServerApp app(port);
    app.run();
    
    platform::cleanup();
    return 0;
}
