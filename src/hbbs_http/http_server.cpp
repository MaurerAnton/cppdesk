// hbbs_http/http_server.cpp expanded
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <spdlog/spdlog.h>
#include <asio.hpp>

namespace cppdesk::hbbs_http {

using tcp = asio::ip::tcp;

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
};

class HttpServer {
    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    int port_;

public:
    HttpServer(int port = 21118) : acceptor_(io_, tcp::endpoint(tcp::v4(), port)), port_(port) {}

    void start() {
        running_ = true;
        worker_ = std::thread([this]() {
            while (running_) {
                try {
                    tcp::socket socket(io_);
                    acceptor_.accept(socket);
                    handle_client(std::move(socket));
                } catch (std::exception& e) {
                    if (running_) spdlog::error("HTTP accept error: {}", e.what());
                }
            }
        });
        spdlog::info("HBBS HTTP server started on port {}", port_);
    }

    void stop() {
        running_ = false;
        acceptor_.close();
        if (worker_.joinable()) worker_.join();
    }

    using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;
    void add_route(const std::string& path, RouteHandler handler) {
        routes_[path] = std::move(handler);
    }

private:
    std::map<std::string, RouteHandler> routes_;

    void handle_client(tcp::socket socket) {
        try {
            asio::streambuf buf;
            asio::read_until(socket, buf, "\r\n\r\n");
            std::istream is(&buf);
            std::string line;

            HttpRequest req;
            std::getline(is, line);
            line.erase(line.find_last_not_of("\r") + 1);

            size_t sp1 = line.find(' ');
            size_t sp2 = line.find(' ', sp1 + 1);
            req.method = line.substr(0, sp1);
            req.path = line.substr(sp1 + 1, sp2 - sp1 - 1);
            req.version = line.substr(sp2 + 1);

            while (std::getline(is, line) && line != "\r") {
                line.erase(line.find_last_not_of("\r") + 1);
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string val = line.substr(colon + 1);
                    if (!val.empty() && val[0] == ' ') val = val.substr(1);
                    req.headers[key] = val;
                }
            }

            HttpResponse resp;
            auto it = routes_.find(req.path);
            if (it != routes_.end()) {
                resp = it->second(req);
            } else {
                resp.status = 404;
                resp.status_text = "Not Found";
                resp.body = "{"error":"not found"}";
            }

            std::ostringstream oss;
            oss << "HTTP/1.1 " << resp.status << " " << resp.status_text << "\r\n";
            resp.headers["Content-Length"] = std::to_string(resp.body.size());
            resp.headers["Content-Type"] = "application/json";
            resp.headers["Server"] = "cppdesk-hbbs/1.0";
            for (auto& [k, v] : resp.headers) {
                oss << k << ": " << v << "\r\n";
            }
            oss << "\r\n" << resp.body;

            std::string response = oss.str();
            asio::write(socket, asio::buffer(response));
        } catch (std::exception& e) {
            spdlog::debug("HTTP client error: {}", e.what());
        }
    }
};

// API endpoints
class HbbsApi {
    HttpServer server_;
    std::map<std::string, std::string> peers_;

public:
    HbbsApi(int port = 21118) : server_(port) {
        server_.add_route("/api/status", [this](auto& req) {
            return json_response({{"status", "ok"}, {"peers", static_cast<int>(peers_.size())}});
        });
        server_.add_route("/api/peers", [this](auto& req) {
            std::vector<json> list;
            for (auto& [id, pk] : peers_) list.push_back({{"id", id}});
            return json_response({{"peers", list}});
        });
        server_.add_route("/api/version", [](auto& req) {
            return json_response({{"version", "1.3.0-cpp"}});
        });
    }

    void start() { server_.start(); }
    void stop() { server_.stop(); }

    void register_peer(const std::string& id, const std::string& pk) {
        peers_[id] = pk;
    }

private:
    using json = nlohmann::json;
    HttpResponse json_response(const json& j) {
        HttpResponse resp;
        resp.headers["Content-Type"] = "application/json";
        resp.body = j.dump();
        return resp;
    }
};

namespace sync {
    void start() { spdlog::info("HBBS sync started"); }
    void stop() { spdlog::info("HBBS sync stopped"); }
}

namespace api {
    HbbsApi* g_instance = nullptr;
    void init(int port) {
        if (!g_instance) { g_instance = new HbbsApi(port); g_instance->start(); }
    }
    void shutdown() { if (g_instance) { g_instance->stop(); delete g_instance; g_instance = nullptr; } }
    void register_peer(const std::string& id, const std::string& pk) {
        if (g_instance) g_instance->register_peer(id, pk);
    }
}

} // namespace cppdesk::hbbs_http
