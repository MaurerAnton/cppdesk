#include <string>
#include <map>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace cppdesk::hbbs_http::api {

using json = nlohmann::json;

class ApiHandler {
    std::map<std::string, std::string> peers_;
    std::map<std::string, json> configs_;
public:
    json handle_request(const std::string& method, const std::string& path, const json& body) {
        if (path == "/api/status") return {{"status","ok"},{"peers",peers_.size()}};
        if (path == "/api/peers" && method == "GET") return {{"peers",peers_}};
        if (path == "/api/register" && method == "POST") {
            std::string id = body.value("id","");
            std::string pk = body.value("pk","");
            if (!id.empty()) peers_[id] = pk;
            return {{"success",true}};
        }
        if (path == "/api/config" && method == "GET") return configs_;
        if (path == "/api/version") return {{"version","1.3.0-cpp"}};
        return {{"error","not_found"}};
    }
};

} // namespace