// network_utils.cpp — Comprehensive network utility implementations
// Part of cppdesk::common networking layer
//
// Provides:
//  1. HTTP/HTTPS client with async requests
//  2. STUN client for NAT type detection
//  3. UPnP/NAT-PMP port forwarding
//  4. Network interface enumeration (IPs, MACs)
//  5. Bandwidth test (speed test client)
//  6. DNS resolution with caching
//  7. Proxy support (HTTP, SOCKS5)
//  8. Ping / ICMP echo
//  9. Traceroute
// 10. Network change detection
// 11. Connection quality scoring

#include "common/config.hpp"
#include "common/protocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <spdlog/spdlog.h>

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/if_packet.h>
#include <net/ethernet.h>
#endif
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <mstcpip.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace cppdesk::common {

// ============================================================================
// Forward declarations and type aliases
// ============================================================================

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using icmp = asio::ip::icmp;
using error_code = asio::error_code;
using steady_clock = std::chrono::steady_clock;
using system_clock = std::chrono::system_clock;

// ============================================================================
// Utility: CRC32 for checksums
// ============================================================================

namespace {
constexpr uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB30A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t compute_crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFF) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}
} // anonymous namespace

// ============================================================================
// 1. HTTP/HTTPS Client with async requests via asio
// ============================================================================

struct HttpClientConfig {
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    std::string user_agent = "cppdesk/2.0";
    size_t max_redirects = 10;
    bool verify_ssl = false;
    std::string proxy_host;
    uint16_t proxy_port = 0;
    std::string proxy_user;
    std::string proxy_pass;
};

enum class HttpMethod { GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH };

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    HttpMethod method = HttpMethod::GET;
    std::string url;
    std::string path;
    std::string host;
    uint16_t port = 80;
    bool use_ssl = false;
    std::vector<HttpHeader> headers;
    std::string body;
    std::chrono::milliseconds timeout{30000};
};

struct HttpResponse {
    int status_code = 0;
    std::string status_message;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string effective_url;
    double elapsed_ms = 0.0;
    error_code error;
};

namespace {

std::string method_string(HttpMethod m) {
    switch (m) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::POST:    return "POST";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE:  return "DELETE";
        case HttpMethod::HEAD:    return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::PATCH:   return "PATCH";
    }
    return "GET";
}

bool parse_url(const std::string& url, std::string& host, uint16_t& port,
               bool& ssl, std::string& path) {
    std::string_view sv(url);
    if (sv.starts_with("https://")) {
        ssl = true;
        port = 443;
        sv.remove_prefix(8);
    } else if (sv.starts_with("http://")) {
        ssl = false;
        port = 80;
        sv.remove_prefix(7);
    } else {
        ssl = false;
        port = 80;
    }

    auto slash = sv.find('/');
    std::string_view host_port;
    if (slash != std::string_view::npos) {
        host_port = sv.substr(0, slash);
        path = std::string(sv.substr(slash));
    } else {
        host_port = sv;
        path = "/";
    }

    auto colon = host_port.rfind(':');
    if (colon != std::string_view::npos) {
        host = std::string(host_port.substr(0, colon));
        auto port_str = host_port.substr(colon + 1);
        auto res = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
        if (res.ec != std::errc{}) return false;
    } else {
        host = std::string(host_port);
    }
    return !host.empty();
}

} // anonymous namespace

class HttpClient {
    asio::io_context& io_;
    tcp::resolver resolver_;
    HttpClientConfig config_;

public:
    explicit HttpClient(asio::io_context& io, HttpClientConfig cfg = {})
        : io_(io), resolver_(io), config_(std::move(cfg)) {}

    HttpResponse request(const HttpRequest& req) {
        return do_request(req, 0);
    }

    void async_request(HttpRequest req,
                       std::function<void(HttpResponse)> callback) {
        asio::post(io_, [this, req = std::move(req), cb = std::move(callback)]() mutable {
            auto resp = do_request(req, 0);
            cb(std::move(resp));
        });
    }

    // Convenience: GET
    HttpResponse get(const std::string& url,
                     std::vector<HttpHeader> extra_headers = {}) {
        HttpRequest req;
        req.url = url;
        req.method = HttpMethod::GET;
        req.headers = std::move(extra_headers);
        req.timeout = config_.request_timeout;
        return request(req);
    }

    // Convenience: POST JSON
    HttpResponse post_json(const std::string& url, const std::string& json_body,
                           std::vector<HttpHeader> extra_headers = {}) {
        HttpRequest req;
        req.url = url;
        req.method = HttpMethod::POST;
        req.body = json_body;
        req.headers = std::move(extra_headers);
        req.headers.push_back({"Content-Type", "application/json"});
        req.headers.push_back({"Content-Length", std::to_string(json_body.size())});
        req.timeout = config_.request_timeout;
        return request(req);
    }

private:
    HttpResponse do_request(HttpRequest req, int redirect_count) {
        HttpResponse resp;
        auto start = steady_clock::now();

        if (!parse_url(req.url, req.host, req.port, req.use_ssl, req.path)) {
            resp.error = asio::error::invalid_argument;
            resp.status_code = -1;
            return resp;
        }

        try {
            // Resolve
            auto endpoints = resolver_.resolve(req.host, std::to_string(req.port));

            if (req.use_ssl) {
                resp = do_https_request(std::move(req), endpoints);
            } else {
                resp = do_http_request(std::move(req), endpoints);
            }

            // Handle redirect
            if ((resp.status_code == 301 || resp.status_code == 302 ||
                 resp.status_code == 307 || resp.status_code == 308) &&
                redirect_count < static_cast<int>(config_.max_redirects)) {
                auto loc = resp.headers.find("Location");
                auto loc_lower = resp.headers.find("location");
                std::string new_url;
                if (loc != resp.headers.end()) new_url = loc->second;
                else if (loc_lower != resp.headers.end()) new_url = loc_lower->second;

                if (!new_url.empty()) {
                    // Handle relative redirects
                    if (new_url.starts_with("/")) {
                        new_url = (req.use_ssl ? "https://" : "http://") + req.host + new_url;
                    }
                    HttpRequest redirect_req;
                    redirect_req.url = new_url;
                    redirect_req.method = (resp.status_code == 307 || resp.status_code == 308)
                        ? req.method : HttpMethod::GET;
                    redirect_req.timeout = req.timeout;
                    redirect_req.headers = req.headers;
                    return do_request(std::move(redirect_req), redirect_count + 1);
                }
            }
        } catch (std::exception& e) {
            resp.error = asio::error::operation_aborted;
            resp.status_code = -1;
            resp.status_message = e.what();
            spdlog::debug("HTTP request failed: {} {}", req.url, e.what());
        }

        auto end = steady_clock::now();
        resp.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return resp;
    }

    HttpResponse do_http_request(HttpRequest req, tcp::resolver::results_type endpoints) {
        HttpResponse resp;
        tcp::socket socket(io_);

        error_code ec;
        asio::connect(socket, endpoints, ec);
        if (ec) { resp.error = ec; return resp; }

        socket.set_option(tcp::no_delay(true));

        // Build request
        std::ostringstream request_stream;
        request_stream << method_string(req.method) << " " << req.path << " HTTP/1.1\r\n";
        request_stream << "Host: " << req.host << "\r\n";
        request_stream << "User-Agent: " << config_.user_agent << "\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Connection: close\r\n";

        for (auto& h : req.headers) {
            request_stream << h.name << ": " << h.value << "\r\n";
        }
        if (!req.body.empty() &&
            std::find_if(req.headers.begin(), req.headers.end(),
                [](auto& h) { return h.name == "Content-Length"; }) == req.headers.end()) {
            request_stream << "Content-Length: " << req.body.size() << "\r\n";
        }
        request_stream << "\r\n";
        request_stream << req.body;

        std::string req_str = request_stream.str();
        asio::write(socket, asio::buffer(req_str), ec);
        if (ec) { resp.error = ec; return resp; }

        // Read response
        asio::streambuf response_buf;
        asio::read_until(socket, response_buf, "\r\n\r\n", ec);
        if (ec) { resp.error = ec; return resp; }

        // Parse status line
        std::istream response_stream(&response_buf);
        std::string http_version;
        response_stream >> http_version;
        response_stream >> resp.status_code;
        std::getline(response_stream, resp.status_message);
        if (!resp.status_message.empty() && resp.status_message[0] == ' ') {
            resp.status_message.erase(0, 1);
        }
        if (!resp.status_message.empty() && resp.status_message.back() == '\r') {
            resp.status_message.pop_back();
        }

        // Parse headers
        std::string header_line;
        while (std::getline(response_stream, header_line) && header_line != "\r") {
            if (header_line.back() == '\r') header_line.pop_back();
            if (header_line.empty()) break;
            auto colon = header_line.find(':');
            if (colon != std::string::npos) {
                std::string name = header_line.substr(0, colon);
                std::string value = header_line.substr(colon + 1);
                // Trim leading space
                if (!value.empty() && value[0] == ' ') value.erase(0, 1);
                resp.headers[name] = value;
            }
        }

        // Read body
        auto content_length_it = resp.headers.find("Content-Length");
        auto chunked_it = resp.headers.find("Transfer-Encoding");
        bool is_chunked = (chunked_it != resp.headers.end() && chunked_it->second == "chunked");

        if (content_length_it != resp.headers.end()) {
            size_t content_length = std::stoull(content_length_it->second);
            if (response_buf.size() < content_length) {
                asio::read(socket, response_buf,
                    asio::transfer_exactly(content_length - response_buf.size()), ec);
            }
            std::ostringstream body_ss;
            body_ss << &response_buf;
            resp.body = body_ss.str();
        } else if (is_chunked) {
            resp.body = read_chunked_body(socket, response_buf);
        } else {
            // Read until EOF
            std::ostringstream body_ss;
            body_ss << &response_buf;
            while (true) {
                asio::read(socket, response_buf, asio::transfer_at_least(1), ec);
                if (ec == asio::error::eof) break;
                if (ec) { resp.error = ec; return resp; }
                body_ss << &response_buf;
            }
            resp.body = body_ss.str();
        }

        resp.effective_url = (req.use_ssl ? "https://" : "http://") + req.host + req.path;
        return resp;
    }

    HttpResponse do_https_request(HttpRequest req, tcp::resolver::results_type endpoints) {
        HttpResponse resp;
        asio::ssl::context ssl_ctx(asio::ssl::context::tlsv12_client);

        if (!config_.verify_ssl) {
            ssl_ctx.set_verify_mode(asio::ssl::verify_none);
        }

        using ssl_socket = asio::ssl::stream<tcp::socket>;
        ssl_socket socket(io_, ssl_ctx);

        error_code ec;
        asio::connect(socket.lowest_layer(), endpoints, ec);
        if (ec) { resp.error = ec; return resp; }

        socket.lowest_layer().set_option(tcp::no_delay(true));

        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(socket.native_handle(), req.host.c_str())) {
            resp.error = asio::error::invalid_argument;
            return resp;
        }

        socket.handshake(ssl_socket::client, ec);
        if (ec) { resp.error = ec; spdlog::debug("SSL handshake failed: {}", ec.message()); return resp; }

        // Build request
        std::ostringstream request_stream;
        request_stream << method_string(req.method) << " " << req.path << " HTTP/1.1\r\n";
        request_stream << "Host: " << req.host << "\r\n";
        request_stream << "User-Agent: " << config_.user_agent << "\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Connection: close\r\n";

        for (auto& h : req.headers) {
            request_stream << h.name << ": " << h.value << "\r\n";
        }
        if (!req.body.empty() &&
            std::find_if(req.headers.begin(), req.headers.end(),
                [](auto& h) { return h.name == "Content-Length"; }) == req.headers.end()) {
            request_stream << "Content-Length: " << req.body.size() << "\r\n";
        }
        request_stream << "\r\n";
        request_stream << req.body;

        std::string req_str = request_stream.str();
        asio::write(socket, asio::buffer(req_str), ec);
        if (ec) { resp.error = ec; return resp; }

        // Read response
        asio::streambuf response_buf;
        asio::read_until(socket, response_buf, "\r\n\r\n", ec);
        if (ec) { resp.error = ec; return resp; }

        std::istream response_stream(&response_buf);
        std::string http_version;
        response_stream >> http_version;
        response_stream >> resp.status_code;
        std::getline(response_stream, resp.status_message);
        trim_status_message(resp.status_message);

        std::string header_line;
        while (std::getline(response_stream, header_line) && header_line != "\r") {
            if (header_line.back() == '\r') header_line.pop_back();
            if (header_line.empty()) break;
            auto colon = header_line.find(':');
            if (colon != std::string::npos) {
                std::string name = header_line.substr(0, colon);
                std::string value = header_line.substr(colon + 1);
                if (!value.empty() && value[0] == ' ') value.erase(0, 1);
                resp.headers[name] = value;
            }
        }

        auto content_length_it = resp.headers.find("Content-Length");
        auto chunked_it = resp.headers.find("Transfer-Encoding");
        bool is_chunked = (chunked_it != resp.headers.end() && chunked_it->second == "chunked");

        if (content_length_it != resp.headers.end()) {
            size_t content_length = std::stoull(content_length_it->second);
            if (response_buf.size() < content_length) {
                asio::read(socket, response_buf,
                    asio::transfer_exactly(content_length - response_buf.size()), ec);
            }
            std::ostringstream body_ss;
            body_ss << &response_buf;
            resp.body = body_ss.str();
        } else if (is_chunked) {
            resp.body = read_chunked_body_ssl(socket, response_buf);
        } else {
            std::ostringstream body_ss;
            body_ss << &response_buf;
            while (true) {
                asio::read(socket, response_buf, asio::transfer_at_least(1), ec);
                if (ec == asio::error::eof) break;
                if (ec) { resp.error = ec; return resp; }
                body_ss << &response_buf;
            }
            resp.body = body_ss.str();
        }

        resp.effective_url = "https://" + req.host + req.path;
        return resp;
    }

    static std::string read_chunked_body(tcp::socket& socket, asio::streambuf& buf) {
        std::string body;
        error_code ec;
        while (true) {
            asio::read_until(socket, buf, "\r\n", ec);
            if (ec) break;
            std::istream is(&buf);
            std::string chunk_size_line;
            std::getline(is, chunk_size_line);
            if (!chunk_size_line.empty() && chunk_size_line.back() == '\r')
                chunk_size_line.pop_back();
            size_t chunk_size = std::stoul(chunk_size_line, nullptr, 16);
            if (chunk_size == 0) {
                // Read trailing \r\n
                asio::read_until(socket, buf, "\r\n", ec);
                break;
            }
            if (buf.size() < chunk_size + 2) {
                asio::read(socket, buf,
                    asio::transfer_exactly(chunk_size + 2 - buf.size()), ec);
            }
            std::vector<char> chunk(chunk_size);
            is.read(chunk.data(), chunk_size);
            body.append(chunk.data(), chunk_size);
            // Consume trailing \r\n
            char crlf[2];
            is.read(crlf, 2);
        }
        return body;
    }

    static std::string read_chunked_body_ssl(asio::ssl::stream<tcp::socket>& socket,
                                              asio::streambuf& buf) {
        std::string body;
        error_code ec;
        while (true) {
            asio::read_until(socket, buf, "\r\n", ec);
            if (ec) break;
            std::istream is(&buf);
            std::string chunk_size_line;
            std::getline(is, chunk_size_line);
            if (!chunk_size_line.empty() && chunk_size_line.back() == '\r')
                chunk_size_line.pop_back();
            size_t chunk_size = std::stoul(chunk_size_line, nullptr, 16);
            if (chunk_size == 0) {
                asio::read_until(socket, buf, "\r\n", ec);
                break;
            }
            if (buf.size() < chunk_size + 2) {
                asio::read(socket, buf,
                    asio::transfer_exactly(chunk_size + 2 - buf.size()), ec);
            }
            std::vector<char> chunk(chunk_size);
            is.read(chunk.data(), chunk_size);
            body.append(chunk.data(), chunk_size);
            char crlf[2];
            is.read(crlf, 2);
        }
        return body;
    }

    static void trim_status_message(std::string& msg) {
        if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
        if (!msg.empty() && msg.back() == '\r') msg.pop_back();
    }
};

// ============================================================================
// 2. STUN Client for NAT type detection (RFC 5389 / RFC 3489)
// ============================================================================

enum class NatType {
    Unknown,
    Open,
    FullCone,
    RestrictedCone,
    PortRestrictedCone,
    Symmetric,
    SymmetricUdpFirewall,
    Blocked
};

struct StunResult {
    NatType nat_type = NatType::Unknown;
    std::string public_ip;
    uint16_t public_port = 0;
    double rtt_ms = 0.0;
    std::string stun_server;
    error_code error;
};

namespace {

constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;
constexpr uint16_t STUN_BINDING_REQUEST = 0x0001;
constexpr uint16_t STUN_BINDING_RESPONSE = 0x0101;
constexpr uint16_t STUN_ATTR_MAPPED_ADDRESS = 0x0001;
constexpr uint16_t STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020;
constexpr uint16_t STUN_ATTR_CHANGE_REQUEST = 0x0003;
constexpr uint16_t STUN_ATTR_RESPONSE_ORIGIN = 0x802b;
constexpr uint16_t STUN_ATTR_OTHER_ADDRESS = 0x802c;

struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint32_t magic_cookie;
    uint8_t transaction_id[12];
};

uint16_t stun_read16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

uint32_t stun_read32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

void stun_write16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

void stun_write32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

void generate_transaction_id(uint8_t* tid) {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;
    uint64_t r1 = dist(gen);
    uint64_t r2 = dist(gen);
    std::memcpy(tid, &r1, 8);
    std::memcpy(tid + 4, &r2, 4); // overlap to fill 12 bytes
}

} // anonymous namespace

class StunClient {
    asio::io_context& io_;
    std::string primary_server_ = "stun.l.google.com";
    uint16_t primary_port_ = 19302;
    std::chrono::milliseconds timeout_{3000};

public:
    explicit StunClient(asio::io_context& io) : io_(io) {}

    void set_server(const std::string& host, uint16_t port = 19302) {
        primary_server_ = host;
        primary_port_ = port;
    }

    void set_timeout(std::chrono::milliseconds t) { timeout_ = t; }

    StunResult detect_nat() {
        StunResult result;
        result.stun_server = primary_server_;

        try {
            udp::resolver resolver(io_);
            auto endpoints = resolver.resolve(udp::v4(), primary_server_,
                                               std::to_string(primary_port_));
            udp::socket socket(io_);
            socket.open(udp::v4());

            // Test 1: Basic binding request
            auto [mapped_ip, mapped_port, rtt] = send_binding_request(socket, *endpoints.begin());
            result.public_ip = mapped_ip;
            result.public_port = mapped_port;
            result.rtt_ms = rtt;

            if (mapped_ip.empty()) {
                result.nat_type = NatType::Blocked;
                return result;
            }

            // Test 2: Change IP/port request to detect NAT behavior
            // Send request with CHANGE-REQUEST attribute asking server to
            // respond from a different IP and port
            auto [mapped_ip2, mapped_port2, rtt2] =
                send_change_request(socket, *endpoints.begin(), true, true);
            bool got_different_ip = !mapped_ip2.empty();

            // Determine NAT type
            if (got_different_ip) {
                // Server responded from different IP -> no NAT or full cone
                result.nat_type = NatType::Open;
            } else {
                // Test if we can receive from same IP different port
                auto [mapped_ip3, mapped_port3, rtt3] =
                    send_change_request(socket, *endpoints.begin(), false, true);

                if (!mapped_ip3.empty()) {
                    // Can receive from different port on same IP
                    result.nat_type = NatType::RestrictedCone;
                } else {
                    // Try sending to different port to see if port-restricted
                    udp::socket socket2(io_);
                    socket2.open(udp::v4());
                    auto [mapped_ip4, mapped_port4, rtt4] =
                        send_binding_request(socket2, *endpoints.begin());

                    if (!mapped_ip4.empty() && mapped_port4 != mapped_port) {
                        result.nat_type = NatType::Symmetric;
                    } else {
                        result.nat_type = NatType::PortRestrictedCone;
                    }
                }
            }

            socket.close();
        } catch (std::exception& e) {
            result.error = asio::error::host_unreachable;
            spdlog::debug("STUN detection failed: {}", e.what());
        }

        return result;
    }

    static const char* nat_type_name(NatType t) {
        switch (t) {
            case NatType::Unknown:            return "Unknown";
            case NatType::Open:               return "Open";
            case NatType::FullCone:           return "Full Cone";
            case NatType::RestrictedCone:     return "Restricted Cone";
            case NatType::PortRestrictedCone: return "Port Restricted Cone";
            case NatType::Symmetric:          return "Symmetric";
            case NatType::SymmetricUdpFirewall: return "Symmetric UDP Firewall";
            case NatType::Blocked:            return "Blocked";
        }
        return "Unknown";
    }

private:
    std::tuple<std::string, uint16_t, double>
    send_binding_request(udp::socket& socket, const udp::endpoint& server) {
        uint8_t request[20];
        auto* hdr = reinterpret_cast<StunHeader*>(request);
        hdr->type = htons(STUN_BINDING_REQUEST);
        hdr->length = htons(0);
        hdr->magic_cookie = htonl(STUN_MAGIC_COOKIE);
        generate_transaction_id(hdr->transaction_id);

        auto start = steady_clock::now();
        error_code ec;
        socket.send_to(asio::buffer(request, sizeof(request)), server, 0, ec);
        if (ec) return {{}, 0, 0.0};

        uint8_t response[1024];
        udp::endpoint sender;
        size_t len = socket.receive_from(asio::buffer(response), sender, 0, ec);
        auto elapsed = std::chrono::duration<double, std::milli>(
            steady_clock::now() - start).count();

        if (ec || len < 20) return {{}, 0, elapsed};

        auto* rhdr = reinterpret_cast<StunHeader*>(response);
        if (ntohs(rhdr->type) != STUN_BINDING_RESPONSE) return {{}, 0, elapsed};

        // Parse attributes for XOR-MAPPED-ADDRESS or MAPPED-ADDRESS
        return parse_mapped_address(response + 20,
                                     ntohs(rhdr->length), rhdr->magic_cookie);
    }

    std::tuple<std::string, uint16_t, double>
    send_change_request(udp::socket& socket, const udp::endpoint& server,
                         bool change_ip, bool change_port) {
        uint8_t request[28];
        auto* hdr = reinterpret_cast<StunHeader*>(request);
        hdr->type = htons(STUN_BINDING_REQUEST);
        hdr->length = htons(8); // 4 bytes type + 4 bytes value
        hdr->magic_cookie = htonl(STUN_MAGIC_COOKIE);
        generate_transaction_id(hdr->transaction_id);

        // CHANGE-REQUEST attribute
        stun_write16(request + 20, STUN_ATTR_CHANGE_REQUEST);
        stun_write16(request + 22, 4); // length
        uint32_t flags = 0;
        if (change_ip) flags |= 0x04;
        if (change_port) flags |= 0x02;
        stun_write32(request + 24, flags);

        auto start = steady_clock::now();
        error_code ec;
        socket.send_to(asio::buffer(request, sizeof(request)), server, 0, ec);
        if (ec) return {{}, 0, 0.0};

        uint8_t response[1024];
        udp::endpoint sender;
        size_t len = socket.receive_from(asio::buffer(response), sender, 0, ec);
        auto elapsed = std::chrono::duration<double, std::milli>(
            steady_clock::now() - start).count();

        if (ec || len < 20) return {{}, 0, elapsed};

        auto* rhdr = reinterpret_cast<StunHeader*>(response);
        if (ntohs(rhdr->type) != STUN_BINDING_RESPONSE) return {{}, 0, elapsed};

        return parse_mapped_address(response + 20,
                                     ntohs(rhdr->length), rhdr->magic_cookie);
    }

    static std::tuple<std::string, uint16_t, double>
    parse_mapped_address(const uint8_t* attrs, uint16_t length, uint32_t magic) {
        const uint8_t* end = attrs + length;
        while (attrs + 4 <= end) {
            uint16_t type = stun_read16(attrs);
            uint16_t attr_len = stun_read16(attrs + 2);
            const uint8_t* val = attrs + 4;

            if (attrs + 4 + attr_len > end) break;

            if (type == STUN_ATTR_XOR_MAPPED_ADDRESS && attr_len >= 8) {
                // XOR-MAPPED-ADDRESS: family (2), port (2 XOR), addr (4 XOR)
                uint16_t family = stun_read16(val + 1);
                if (family == 0x01) { // IPv4
                    uint16_t port = stun_read16(val + 2) ^ (magic >> 16);
                    uint32_t addr = stun_read32(val + 4) ^ magic;
                    char ip_str[INET_ADDRSTRLEN];
                    in_addr ia;
                    ia.s_addr = htonl(addr);
                    inet_ntop(AF_INET, &ia, ip_str, sizeof(ip_str));
                    return {std::string(ip_str), port, 0.0};
                }
            } else if (type == STUN_ATTR_MAPPED_ADDRESS && attr_len >= 8) {
                uint16_t family = stun_read16(val + 1);
                if (family == 0x01) { // IPv4
                    uint16_t port = stun_read16(val + 2);
                    uint32_t addr = stun_read32(val + 4);
                    char ip_str[INET_ADDRSTRLEN];
                    in_addr ia;
                    ia.s_addr = htonl(addr);
                    inet_ntop(AF_INET, &ia, ip_str, sizeof(ip_str));
                    return {std::string(ip_str), port, 0.0};
                }
            }

            attrs += 4 + attr_len;
            // Align to 4 bytes
            if (attr_len % 4 != 0) attrs += (4 - attr_len % 4);
        }
        return {{}, 0, 0.0};
    }
};

// ============================================================================
// 3. UPnP / NAT-PMP Port Forwarding
// ============================================================================

enum class PortMapProtocol { TCP, UDP };

struct PortMapping {
    std::string internal_host;
    uint16_t internal_port = 0;
    uint16_t external_port = 0;
    PortMapProtocol protocol = PortMapProtocol::TCP;
    std::string description;
    uint32_t lease_seconds = 3600;
    bool enabled = false;
    std::string gateway;
};

class UpnpClient {
    asio::io_context& io_;
    std::string gateway_ip_;
    std::string control_url_;
    std::string service_type_;
    std::chrono::milliseconds timeout_{5000};

public:
    explicit UpnpClient(asio::io_context& io) : io_(io) {}

    bool discover() {
        try {
            // SSDP M-SEARCH
            udp::socket socket(io_, udp::endpoint(udp::v4(), 0));
            socket.set_option(udp::socket::reuse_address(true));
            socket.set_option(asio::socket_base::broadcast(true));

            std::string ms = "M-SEARCH * HTTP/1.1\r\n"
                             "HOST: 239.255.255.250:1900\r\n"
                             "MAN: \"ssdp:discover\"\r\n"
                             "MX: 3\r\n"
                             "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                             "\r\n";

            udp::endpoint multicast(asio::ip::make_address("239.255.255.250"), 1900);
            socket.send_to(asio::buffer(ms), multicast);

            char response[4096];
            udp::endpoint sender;
            error_code ec;
            size_t len = socket.receive_from(asio::buffer(response, sizeof(response) - 1), sender, 0, ec);

            if (ec || len == 0) {
                // Try alternative: NAT-PMP multicast discovery
                return discover_nat_pmp();
            }

            response[len] = '\0';
            std::string resp_str(response, len);

            // Parse LOCATION header
            std::string location;
            auto pos = resp_str.find("LOCATION:");
            if (pos == std::string::npos) pos = resp_str.find("Location:");
            if (pos != std::string::npos) {
                auto end = resp_str.find("\r\n", pos);
                location = resp_str.substr(pos + 9, end - pos - 9);
                // Trim spaces
                while (!location.empty() && location[0] == ' ') location.erase(0, 1);
                while (!location.empty() && location.back() == '\r') location.pop_back();
            }

            if (location.empty()) {
                return discover_nat_pmp();
            }

            // Fetch device description XML
            gateway_ip_ = sender.address().to_string();
            spdlog::info("UPnP gateway discovered: {} at {}", gateway_ip_, location);
            return true;

        } catch (std::exception& e) {
            spdlog::debug("UPnP discovery failed: {}", e.what());
        }
        return false;
    }

    bool add_port_mapping(const PortMapping& mapping) {
        if (gateway_ip_.empty()) {
            if (!discover()) return false;
        }

        try {
            tcp::socket socket(io_);
            tcp::resolver resolver(io_);
            auto endpoints = resolver.resolve(gateway_ip_, "80");
            asio::connect(socket, endpoints);

            std::string soap_action = "urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping";
            std::string soap_body = build_add_port_mapping_soap(mapping);

            std::ostringstream req;
            req << "POST /upnp/control/WANIPConn1 HTTP/1.1\r\n";
            req << "Host: " << gateway_ip_ << "\r\n";
            req << "Content-Type: text/xml; charset=\"utf-8\"\r\n";
            req << "SOAPAction: \"" << soap_action << "\"\r\n";
            req << "Content-Length: " << soap_body.size() << "\r\n";
            req << "Connection: close\r\n";
            req << "\r\n";
            req << soap_body;

            asio::write(socket, asio::buffer(req.str()));

            asio::streambuf resp_buf;
            error_code ec;
            asio::read(socket, resp_buf, ec);

            std::string resp_str(asio::buffers_begin(resp_buf.data()),
                                 asio::buffers_end(resp_buf.data()));

            bool success = resp_str.find("200 OK") != std::string::npos ||
                          resp_str.find("500 Internal") == std::string::npos; // some UPnP returns 500 on conflict
            if (success) {
                spdlog::info("UPnP port mapped: {} -> {}:{}",
                    mapping.internal_port, mapping.external_port,
                    mapping.protocol == PortMapProtocol::TCP ? "TCP" : "UDP");
            }
            return success;

        } catch (std::exception& e) {
            spdlog::error("UPnP add mapping failed: {}", e.what());
            return false;
        }
    }

    bool delete_port_mapping(const PortMapping& mapping) {
        if (gateway_ip_.empty()) return false;

        try {
            tcp::socket socket(io_);
            tcp::resolver resolver(io_);
            auto endpoints = resolver.resolve(gateway_ip_, "80");
            asio::connect(socket, endpoints);

            std::string soap_body = build_delete_port_mapping_soap(mapping);
            std::string soap_action = "urn:schemas-upnp-org:service:WANIPConnection:1#DeletePortMapping";

            std::ostringstream req;
            req << "POST /upnp/control/WANIPConn1 HTTP/1.1\r\n";
            req << "Host: " << gateway_ip_ << "\r\n";
            req << "Content-Type: text/xml; charset=\"utf-8\"\r\n";
            req << "SOAPAction: \"" << soap_action << "\"\r\n";
            req << "Content-Length: " << soap_body.size() << "\r\n";
            req << "Connection: close\r\n";
            req << "\r\n";
            req << soap_body;

            asio::write(socket, asio::buffer(req.str()));

            asio::streambuf resp_buf;
            error_code ec;
            asio::read(socket, resp_buf, ec);
            spdlog::info("UPnP port mapping deleted: {} {}", mapping.external_port,
                         mapping.protocol == PortMapProtocol::TCP ? "TCP" : "UDP");
            return true;
        } catch (std::exception& e) {
            spdlog::error("UPnP delete failed: {}", e.what());
            return false;
        }
    }

    std::string external_ip() {
        if (gateway_ip_.empty()) discover();

        try {
            tcp::socket socket(io_);
            tcp::resolver resolver(io_);
            auto endpoints = resolver.resolve(gateway_ip_, "80");
            asio::connect(socket, endpoints);

            std::string soap_body =
                "<?xml version=\"1.0\"?>"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                "<s:Body><u:GetExternalIPAddress "
                "xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
                "</u:GetExternalIPAddress></s:Body></s:Envelope>";

            std::string soap_action = "urn:schemas-upnp-org:service:WANIPConnection:1#GetExternalIPAddress";

            std::ostringstream req;
            req << "POST /upnp/control/WANIPConn1 HTTP/1.1\r\n";
            req << "Host: " << gateway_ip_ << "\r\n";
            req << "Content-Type: text/xml; charset=\"utf-8\"\r\n";
            req << "SOAPAction: \"" << soap_action << "\"\r\n";
            req << "Content-Length: " << soap_body.size() << "\r\n";
            req << "\r\n" << soap_body;

            asio::write(socket, asio::buffer(req.str()));
            asio::streambuf resp_buf;
            error_code ec;
            asio::read(socket, resp_buf, ec);

            std::string resp(asio::buffers_begin(resp_buf.data()),
                            asio::buffers_end(resp_buf.data()));
            auto start = resp.find("<NewExternalIPAddress>");
            if (start != std::string::npos) {
                start += 22;
                auto end = resp.find("</NewExternalIPAddress>", start);
                if (end != std::string::npos) {
                    return resp.substr(start, end - start);
                }
            }
        } catch (std::exception& e) {
            spdlog::debug("UPnP GetExternalIP failed: {}", e.what());
        }
        return {};
    }

private:
    bool discover_nat_pmp() {
        // NAT-PMP multicast on 224.0.0.1:5350
        try {
            udp::socket socket(io_, udp::endpoint(udp::v4(), 0));
            udp::endpoint gw(asio::ip::make_address("224.0.0.1"), 5350);

            // Send external address request
            uint8_t req[2] = {0, 0}; // version=0, opcode=0
            socket.send_to(asio::buffer(req), gw);

            uint8_t resp[12];
            udp::endpoint sender;
            error_code ec;
            size_t len = socket.receive_from(asio::buffer(resp), sender, 0, ec);

            if (!ec && len >= 12 && resp[1] == 128) { // response opcode 128
                uint32_t ip;
                std::memcpy(&ip, resp + 8, 4);
                gateway_ip_ = sender.address().to_string();
                char ip_str[INET_ADDRSTRLEN];
                in_addr ia;
                ia.s_addr = ip; // already in network byte order
                inet_ntop(AF_INET, &ia, ip_str, sizeof(ip_str));
                spdlog::info("NAT-PMP gateway: {} external IP: {}", gateway_ip_, ip_str);
                return true;
            }
        } catch (std::exception& e) {
            spdlog::debug("NAT-PMP discovery failed: {}", e.what());
        }
        return false;
    }

    static std::string build_add_port_mapping_soap(const PortMapping& m) {
        std::string proto = (m.protocol == PortMapProtocol::TCP) ? "TCP" : "UDP";
        return "<?xml version=\"1.0\"?>"
               "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
               "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
               "<s:Body><u:AddPortMapping "
               "xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
               "<NewRemoteHost></NewRemoteHost>"
               "<NewExternalPort>" + std::to_string(m.external_port) + "</NewExternalPort>"
               "<NewProtocol>" + proto + "</NewProtocol>"
               "<NewInternalPort>" + std::to_string(m.internal_port) + "</NewInternalPort>"
               "<NewInternalClient>" + m.internal_host + "</NewInternalClient>"
               "<NewEnabled>1</NewEnabled>"
               "<NewPortMappingDescription>" + m.description + "</NewPortMappingDescription>"
               "<NewLeaseDuration>" + std::to_string(m.lease_seconds) + "</NewLeaseDuration>"
               "</u:AddPortMapping></s:Body></s:Envelope>";
    }

    static std::string build_delete_port_mapping_soap(const PortMapping& m) {
        std::string proto = (m.protocol == PortMapProtocol::TCP) ? "TCP" : "UDP";
        return "<?xml version=\"1.0\"?>"
               "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
               "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
               "<s:Body><u:DeletePortMapping "
               "xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
               "<NewRemoteHost></NewRemoteHost>"
               "<NewExternalPort>" + std::to_string(m.external_port) + "</NewExternalPort>"
               "<NewProtocol>" + proto + "</NewProtocol>"
               "</u:DeletePortMapping></s:Body></s:Envelope>";
    }
};

// ============================================================================
// 4. Network Interface Enumeration (all IPs, MAC addresses)
// ============================================================================

struct NetworkInterface {
    std::string name;
    std::string friendly_name;
    std::string mac_address;
    std::vector<std::string> ipv4_addresses;
    std::vector<std::string> ipv6_addresses;
    std::vector<std::string> netmasks;
    bool is_loopback = false;
    bool is_up = false;
    bool is_wireless = false;
    uint32_t mtu = 1500;
    uint64_t speed_bps = 0;
    std::string gateway;
};

class NetworkEnumerator {
public:
    static std::vector<NetworkInterface> enumerate() {
        std::vector<NetworkInterface> result;
#if defined(__linux__) || defined(__APPLE__)
        enumerate_posix(result);
#elif defined(_WIN32)
        enumerate_windows(result);
#endif
        return result;
    }

    static std::optional<NetworkInterface> primary_interface() {
        auto ifaces = enumerate();
        for (auto& iface : ifaces) {
            if (!iface.is_loopback && iface.is_up && !iface.ipv4_addresses.empty()) {
                return iface;
            }
        }
        return std::nullopt;
    }

    static std::vector<std::string> all_local_addresses() {
        std::vector<std::string> addrs;
        for (auto& iface : enumerate()) {
            for (auto& ip : iface.ipv4_addresses) addrs.push_back(ip);
            for (auto& ip : iface.ipv6_addresses) addrs.push_back(ip);
        }
        return addrs;
    }

#if defined(__linux__) || defined(__APPLE__)
private:
    static void enumerate_posix(std::vector<NetworkInterface>& result) {
        struct ifaddrs *ifaddr = nullptr;
        if (getifaddrs(&ifaddr) != 0) {
            spdlog::error("getifaddrs failed: {}", strerror(errno));
            return;
        }

        std::map<std::string, NetworkInterface> iface_map;

        for (auto* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;

            std::string name(ifa->ifa_name);
            auto& iface = iface_map[name];
            iface.name = name;
            iface.is_up = (ifa->ifa_flags & IFF_UP) != 0;
            iface.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

            if (ifa->ifa_addr->sa_family == AF_INET) {
                char ip[INET_ADDRSTRLEN];
                auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
                inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                iface.ipv4_addresses.push_back(ip);

                if (ifa->ifa_netmask) {
                    auto* nm = reinterpret_cast<sockaddr_in*>(ifa->ifa_netmask);
                    char mask[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &nm->sin_addr, mask, sizeof(mask));
                    iface.netmasks.push_back(mask);
                }
            } else if (ifa->ifa_addr->sa_family == AF_INET6) {
                char ip[INET6_ADDRSTRLEN];
                auto* sin6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
                inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
                iface.ipv6_addresses.push_back(ip);
            }

#ifdef __linux__
            // Get MAC address via AF_PACKET
            if (ifa->ifa_addr->sa_family == AF_PACKET) {
                auto* sll = reinterpret_cast<sockaddr_ll*>(ifa->ifa_addr);
                char mac[18];
                snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                         sll->sll_addr[0], sll->sll_addr[1], sll->sll_addr[2],
                         sll->sll_addr[3], sll->sll_addr[4], sll->sll_addr[5]);
                iface.mac_address = mac;
            }
#elif defined(__APPLE__)
            // On macOS, get MAC via AF_LINK
            if (ifa->ifa_addr->sa_family == AF_LINK) {
                auto* sdl = reinterpret_cast<sockaddr_dl*>(ifa->ifa_addr);
                if (sdl->sdl_alen == 6) {
                    unsigned char* mac_bytes = reinterpret_cast<unsigned char*>(LLADDR(sdl));
                    char mac[18];
                    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                             mac_bytes[0], mac_bytes[1], mac_bytes[2],
                             mac_bytes[3], mac_bytes[4], mac_bytes[5]);
                    iface.mac_address = mac;
                }
            }
#endif
        }

        freeifaddrs(ifaddr);

        // Get gateway via /proc/net/route (Linux) or netstat (macOS)
#ifdef __linux__
        std::ifstream route("/proc/net/route");
        if (route.is_open()) {
            std::string line;
            std::getline(route, line); // skip header
            while (std::getline(route, line)) {
                std::istringstream iss(line);
                std::string ifname, dest, gw, flags;
                iss >> ifname >> dest >> gw >> flags;
                if (dest == "00000000" && gw != "00000000") {
                    uint32_t gw_int;
                    std::sscanf(gw.c_str(), "%x", &gw_int);
                    gw_int = ntohl(gw_int); // route file has hex in little-endian
                    // Actually /proc/net/route is raw hex, need to reverse
                    uint32_t gw_be = ((gw_int & 0xFF) << 24) | ((gw_int & 0xFF00) << 8) |
                                     ((gw_int & 0xFF0000) >> 8) | ((gw_int >> 24) & 0xFF);
                    struct in_addr addr;
                    addr.s_addr = htonl(gw_be);
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
                    iface_map[ifname].gateway = buf;
                }
            }
        }
#endif

        for (auto& [_, iface] : iface_map) {
            result.push_back(std::move(iface));
        }
    }

#elif defined(_WIN32)
    static void enumerate_windows(std::vector<NetworkInterface>& result) {
        // Windows enumeration using GetAdaptersAddresses
        ULONG buf_size = 15000;
        std::vector<uint8_t> buf(buf_size);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());

        ULONG ret = GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &buf_size);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.resize(buf_size);
            adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
            ret = GetAdaptersAddresses(AF_UNSPEC,
                GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS, nullptr, adapters, &buf_size);
        }

        if (ret != NO_ERROR) {
            spdlog::error("GetAdaptersAddresses failed: {}", ret);
            return;
        }

        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            NetworkInterface iface;
            iface.name = adapter->AdapterName;
            iface.friendly_name = wchar_to_utf8(adapter->FriendlyName);
            iface.is_up = adapter->OperStatus == IfOperStatusUp;
            iface.is_loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
            iface.is_wireless = adapter->IfType == IF_TYPE_IEEE80211;
            iface.mtu = adapter->Mtu;

            // MAC
            if (adapter->PhysicalAddressLength >= 6) {
                char mac[18];
                snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                         adapter->PhysicalAddress[0], adapter->PhysicalAddress[1],
                         adapter->PhysicalAddress[2], adapter->PhysicalAddress[3],
                         adapter->PhysicalAddress[4], adapter->PhysicalAddress[5]);
                iface.mac_address = mac;
            }

            // IP addresses
            for (auto* addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
                char ip_str[INET6_ADDRSTRLEN];
                if (addr->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = reinterpret_cast<sockaddr_in*>(addr->Address.lpSockaddr);
                    inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
                    iface.ipv4_addresses.push_back(ip_str);
                } else if (addr->Address.lpSockaddr->sa_family == AF_INET6) {
                    auto* sin6 = reinterpret_cast<sockaddr_in6*>(addr->Address.lpSockaddr);
                    inet_ntop(AF_INET6, &sin6->sin6_addr, ip_str, sizeof(ip_str));
                    iface.ipv6_addresses.push_back(ip_str);
                }
            }

            // Gateway
            for (auto* gw = adapter->FirstGatewayAddress; gw; gw = gw->Next) {
                char ip_str[INET6_ADDRSTRLEN];
                if (gw->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = reinterpret_cast<sockaddr_in*>(gw->Address.lpSockaddr);
                    inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
                    iface.gateway = ip_str;
                    break;
                }
            }

            result.push_back(std::move(iface));
        }
    }

    static std::string wchar_to_utf8(const wchar_t* wstr) {
        if (!wstr) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
        return result;
    }
#endif
};

// ============================================================================
// 5. Bandwidth Test (Speed Test Client)
// ============================================================================

struct BandwidthResult {
    double download_mbps = 0.0;
    double upload_mbps = 0.0;
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double packet_loss_pct = 0.0;
    std::string server_used;
    error_code error;
};

class BandwidthTester {
    asio::io_context& io_;
    std::string test_server_ = "speedtest.net";
    size_t download_size_ = 10 * 1024 * 1024; // 10 MB
    size_t upload_size_ = 5 * 1024 * 1024;    // 5 MB
    std::chrono::milliseconds timeout_{30000};

public:
    explicit BandwidthTester(asio::io_context& io) : io_(io) {}

    void set_test_server(const std::string& host) { test_server_ = host; }
    void set_download_size(size_t bytes) { download_size_ = bytes; }
    void set_upload_size(size_t bytes) { upload_size_ = bytes; }

    BandwidthResult run_test() {
        BandwidthResult result;
        result.server_used = test_server_;

        // Measure latency via TCP connect
        result.latency_ms = measure_latency();
        if (result.latency_ms < 0) {
            result.error = asio::error::host_unreachable;
            return result;
        }

        // Jitter test
        result.jitter_ms = measure_jitter();

        // Download test
        result.download_mbps = test_download();

        // Upload test
        result.upload_mbps = test_upload();

        spdlog::info("Bandwidth: {:.1f} Mbps down, {:.1f} Mbps up, {:.1f} ms latency",
                     result.download_mbps, result.upload_mbps, result.latency_ms);
        return result;
    }

private:
    double measure_latency() {
        try {
            tcp::resolver resolver(io_);
            tcp::socket socket(io_);
            auto endpoints = resolver.resolve(test_server_, "80");

            auto start = steady_clock::now();
            error_code ec;
            asio::connect(socket, endpoints, ec);
            auto elapsed = std::chrono::duration<double, std::milli>(
                steady_clock::now() - start).count();

            return ec ? -1.0 : elapsed;
        } catch (...) {
            return -1.0;
        }
    }

    double measure_jitter() {
        std::vector<double> samples;
        for (int i = 0; i < 10; ++i) {
            double rtt = measure_latency();
            if (rtt > 0) samples.push_back(rtt);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (samples.size() < 2) return 0.0;

        double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        double variance = 0.0;
        for (double s : samples) variance += (s - mean) * (s - mean);
        variance /= samples.size();
        return std::sqrt(variance);
    }

    double test_download() {
        try {
            tcp::resolver resolver(io_);
            tcp::socket socket(io_);
            auto endpoints = resolver.resolve(test_server_, "80");
            error_code ec;
            asio::connect(socket, endpoints, ec);
            if (ec) return 0.0;

            socket.set_option(tcp::no_delay(true));
            // Use socket buffer tuning for speed
            socket.set_option(asio::socket_base::receive_buffer_size(256 * 1024));

            // Send simple HTTP request for a large resource or use plain TCP throughput
            std::string req = "GET /speedtest/10MB.bin HTTP/1.1\r\n"
                              "Host: " + test_server_ + "\r\n"
                              "Connection: close\r\n\r\n";
            asio::write(socket, asio::buffer(req), ec);

            // Read response and measure throughput
            auto start = steady_clock::now();
            size_t total_read = 0;
            std::array<char, 65536> buf;

            while (total_read < download_size_) {
                size_t n = socket.read_some(asio::buffer(buf), ec);
                if (ec == asio::error::eof) break;
                if (ec) return 0.0;
                total_read += n;
            }

            auto elapsed = std::chrono::duration<double>(steady_clock::now() - start).count();
            if (elapsed <= 0) return 0.0;

            double bits = static_cast<double>(total_read) * 8.0;
            return bits / elapsed / 1'000'000.0; // Mbps
        } catch (...) {
            return 0.0;
        }
    }

    double test_upload() {
        try {
            tcp::resolver resolver(io_);
            tcp::socket socket(io_);
            auto endpoints = resolver.resolve(test_server_, "80");
            error_code ec;
            asio::connect(socket, endpoints, ec);
            if (ec) return 0.0;

            socket.set_option(tcp::no_delay(true));
            socket.set_option(asio::socket_base::send_buffer_size(256 * 1024));

            std::string req = "POST /speedtest/upload HTTP/1.1\r\n"
                              "Host: " + test_server_ + "\r\n"
                              "Content-Length: " + std::to_string(upload_size_) + "\r\n"
                              "Connection: close\r\n\r\n";
            asio::write(socket, asio::buffer(req), ec);

            // Send payload
            std::vector<char> payload(65536, 'A');
            auto start = steady_clock::now();
            size_t total_sent = 0;

            while (total_sent < upload_size_) {
                size_t chunk = std::min(payload.size(), upload_size_ - total_sent);
                size_t n = asio::write(socket, asio::buffer(payload.data(), chunk), ec);
                if (ec) return 0.0;
                total_sent += n;
            }

            auto elapsed = std::chrono::duration<double>(steady_clock::now() - start).count();
            if (elapsed <= 0) return 0.0;

            double bits = static_cast<double>(total_sent) * 8.0;
            return bits / elapsed / 1'000'000.0; // Mbps
        } catch (...) {
            return 0.0;
        }
    }
};

// ============================================================================
// 6. DNS Resolution with Caching
// ============================================================================

struct DnsEntry {
    std::vector<std::string> addresses;
    std::chrono::steady_clock::time_point expiry;
    std::string hostname;
};

class DnsCache {
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, DnsEntry> cache_;
    std::chrono::seconds default_ttl_{300};
    asio::io_context& io_;
    size_t max_entries_{10000};

public:
    explicit DnsCache(asio::io_context& io, std::chrono::seconds ttl = std::chrono::seconds(300))
        : io_(io), default_ttl_(ttl) {}

    std::vector<std::string> resolve(const std::string& hostname) {
        // Check cache
        {
            std::shared_lock lock(mutex_);
            auto it = cache_.find(hostname);
            if (it != cache_.end() && steady_clock::now() < it->second.expiry) {
                spdlog::trace("DNS cache hit: {} -> {} addresses", hostname, it->second.addresses.size());
                return it->second.addresses;
            }
        }

        // Resolve
        auto addrs = do_resolve(hostname);

        // Store in cache
        {
            std::unique_lock lock(mutex_);
            if (cache_.size() >= max_entries_) {
                evict_oldest();
            }
            DnsEntry entry;
            entry.addresses = addrs;
            entry.expiry = steady_clock::now() + default_ttl_;
            entry.hostname = hostname;
            cache_[hostname] = std::move(entry);
        }

        return addrs;
    }

    void async_resolve(const std::string& hostname,
                       std::function<void(std::vector<std::string>)> callback) {
        // Try cache first
        {
            std::shared_lock lock(mutex_);
            auto it = cache_.find(hostname);
            if (it != cache_.end() && steady_clock::now() < it->second.expiry) {
                callback(it->second.addresses);
                return;
            }
        }

        // Async resolve
        auto resolver = std::make_shared<tcp::resolver>(io_);
        resolver->async_resolve(hostname, "",
            [this, resolver, hostname, cb = std::move(callback)](error_code ec, tcp::resolver::results_type results) {
                std::vector<std::string> addrs;
                if (!ec) {
                    for (auto& ep : results) {
                        addrs.push_back(ep.endpoint().address().to_string());
                    }
                } else {
                    spdlog::debug("DNS async resolve failed for {}: {}", hostname, ec.message());
                }

                // Cache the result
                {
                    std::unique_lock lock(mutex_);
                    DnsEntry entry;
                    entry.addresses = addrs;
                    entry.expiry = steady_clock::now() + default_ttl_;
                    entry.hostname = hostname;
                    cache_[hostname] = std::move(entry);
                }

                cb(addrs);
            });
    }

    void flush() {
        std::unique_lock lock(mutex_);
        cache_.clear();
        spdlog::debug("DNS cache flushed");
    }

    size_t size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }

    size_t cleanup_expired() {
        std::unique_lock lock(mutex_);
        size_t removed = 0;
        auto now = steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (now >= it->second.expiry) {
                it = cache_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        return removed;
    }

    void set_ttl(std::chrono::seconds ttl) { default_ttl_ = ttl; }

private:
    std::vector<std::string> do_resolve(const std::string& hostname) {
        std::vector<std::string> addrs;
        try {
            tcp::resolver resolver(io_);
            auto results = resolver.resolve(hostname, "");
            for (auto& ep : results) {
                addrs.push_back(ep.endpoint().address().to_string());
            }
            spdlog::trace("DNS resolved: {} -> {} addresses", hostname, addrs.size());
        } catch (std::exception& e) {
            spdlog::debug("DNS resolve failed for {}: {}", hostname, e.what());
        }
        return addrs;
    }

    void evict_oldest() {
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.expiry < oldest->second.expiry) {
                oldest = it;
            }
        }
        cache_.erase(oldest);
    }
};

// ============================================================================
// 7. Proxy Support (HTTP, SOCKS5)
// ============================================================================

enum class ProxyType { None, HTTP, SOCKS4, SOCKS5 };

struct ProxyConfig {
    ProxyType type = ProxyType::None;
    std::string host;
    uint16_t port = 1080;
    std::string username;
    std::string password;
    bool enabled = false;
};

class ProxyClient {
    asio::io_context& io_;
    ProxyConfig config_;

public:
    ProxyClient(asio::io_context& io, ProxyConfig cfg) : io_(io), config_(std::move(cfg)) {}

    bool connect_through_proxy(tcp::socket& socket,
                                const std::string& target_host, uint16_t target_port) {
        if (config_.type == ProxyType::HTTP) {
            return http_proxy_connect(socket, target_host, target_port);
        } else if (config_.type == ProxyType::SOCKS5) {
            return socks5_connect(socket, target_host, target_port);
        } else if (config_.type == ProxyType::SOCKS4) {
            return socks4_connect(socket, target_host, target_port);
        }
        return false;
    }

    static std::optional<ProxyConfig> detect_system_proxy() {
        ProxyConfig cfg;
        const char* http_proxy = std::getenv("HTTP_PROXY");
        const char* https_proxy = std::getenv("HTTPS_PROXY");
        const char* all_proxy = std::getenv("ALL_PROXY");

        std::string proxy_env;
        if (http_proxy) proxy_env = http_proxy;
        else if (https_proxy) proxy_env = https_proxy;
        else if (all_proxy) proxy_env = all_proxy;
        else return std::nullopt;

        if (parse_proxy_env(proxy_env, cfg)) {
            cfg.enabled = true;
            return cfg;
        }

        // Check SOCKS env vars
        const char* socks_proxy = std::getenv("SOCKS_PROXY");
        const char* socks5_proxy = std::getenv("SOCKS5_PROXY");
        if (socks_proxy) proxy_env = socks_proxy;
        else if (socks5_proxy) proxy_env = socks5_proxy;
        else return std::nullopt;

        if (parse_proxy_env(proxy_env, cfg)) {
            cfg.enabled = true;
            return cfg;
        }

        return std::nullopt;
    }

private:
    bool http_proxy_connect(tcp::socket& socket,
                            const std::string& target_host, uint16_t target_port) {
        error_code ec;

        // HTTP CONNECT tunnel
        std::ostringstream req;
        req << "CONNECT " << target_host << ":" << target_port << " HTTP/1.1\r\n";
        req << "Host: " << target_host << "\r\n";

        if (!config_.username.empty()) {
            std::string auth = config_.username + ":" + config_.password;
            std::string encoded = base64_encode(auth);
            req << "Proxy-Authorization: Basic " << encoded << "\r\n";
        }
        req << "\r\n";

        asio::write(socket, asio::buffer(req.str()), ec);
        if (ec) return false;

        // Read response
        asio::streambuf resp;
        asio::read_until(socket, resp, "\r\n\r\n", ec);
        if (ec) return false;

        std::istream is(&resp);
        std::string status_line;
        std::getline(is, status_line);

        // Expect "HTTP/1.x 200"
        return status_line.find("200") != std::string::npos;
    }

    bool socks5_connect(tcp::socket& socket,
                         const std::string& target_host, uint16_t target_port) {
        error_code ec;

        // Handshake
        // +----+----------+----------+
        // |VER | NMETHODS | METHODS  |
        // +----+----------+----------+
        // | 1  |    1     | 1 to 255 |
        uint8_t handshake[3] = {0x05, 0x01, 0x00}; // VER=5, 1 method, NO AUTH
        size_t hs_len = 3;

        // If auth needed
        if (!config_.username.empty()) {
            handshake[1] = 0x02;
            handshake[2] = 0x00; // NO AUTH
            // We'll need a longer buffer
            uint8_t hs_auth[4] = {0x05, 0x02, 0x00, 0x02}; // VER=5, 2 methods, NO AUTH, USER/PASS
            asio::write(socket, asio::buffer(hs_auth, 4), ec);
            hs_len = 0; // signal we already sent
        }

        if (hs_len > 0) {
            asio::write(socket, asio::buffer(handshake, hs_len), ec);
        }
        if (ec) return false;

        // Read server method selection
        uint8_t method_resp[2];
        asio::read(socket, asio::buffer(method_resp, 2), ec);
        if (ec || method_resp[0] != 0x05) return false;

        if (method_resp[1] == 0x02) {
            // Username/password auth required
            if (!socks5_auth(socket)) return false;
        } else if (method_resp[1] != 0x00) {
            return false;
        }

        // Send request
        // +----+-----+-------+------+----------+----------+
        // |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
        // +----+-----+-------+------+----------+----------+
        std::vector<uint8_t> req;
        req.push_back(0x05); // VER
        req.push_back(0x01); // CMD = CONNECT
        req.push_back(0x00); // RSV

        // Try to parse target as IP first to use ATYP=1 (IPv4)
        error_code addr_ec;
        auto addr = asio::ip::make_address(target_host, addr_ec);
        if (!addr_ec && addr.is_v4()) {
            req.push_back(0x01); // ATYP = IPv4
            auto bytes = addr.to_v4().to_bytes();
            req.insert(req.end(), bytes.begin(), bytes.end());
        } else if (!addr_ec && addr.is_v6()) {
            req.push_back(0x04); // ATYP = IPv6
            auto bytes = addr.to_v6().to_bytes();
            req.insert(req.end(), bytes.begin(), bytes.end());
        } else {
            req.push_back(0x03); // ATYP = DOMAINNAME
            req.push_back(static_cast<uint8_t>(target_host.size()));
            req.insert(req.end(), target_host.begin(), target_host.end());
        }

        uint16_t port_n = htons(target_port);
        req.push_back(static_cast<uint8_t>(port_n >> 8));
        req.push_back(static_cast<uint8_t>(port_n & 0xFF));

        asio::write(socket, asio::buffer(req), ec);
        if (ec) return false;

        // Read response (first 4 bytes, then address)
        uint8_t resp_hdr[4];
        asio::read(socket, asio::buffer(resp_hdr, 4), ec);
        if (ec || resp_hdr[0] != 0x05) return false;
        if (resp_hdr[1] != 0x00) return false; // REP != 0 (success)

        // Read remaining address bytes
        uint8_t atype = resp_hdr[3];
        size_t addr_len = 0;
        switch (atype) {
            case 0x01: addr_len = 4; break; // IPv4
            case 0x03: addr_len = 0; /*dynamic, read first byte*/ break;
            case 0x04: addr_len = 16; break; // IPv6
        }

        if (atype == 0x03) {
            uint8_t len_byte;
            asio::read(socket, asio::buffer(&len_byte, 1), ec);
            addr_len = len_byte;
        }

        std::vector<uint8_t> remainder(addr_len + 2); // address + port
        asio::read(socket, asio::buffer(remainder), ec);

        return !ec;
    }

    bool socks5_auth(tcp::socket& socket) {
        error_code ec;
        // +----+------+----------+------+----------+
        // |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
        // +----+------+----------+------+----------+
        std::vector<uint8_t> auth;
        auth.push_back(0x01); // AUTH version
        auth.push_back(static_cast<uint8_t>(config_.username.size()));
        auth.insert(auth.end(), config_.username.begin(), config_.username.end());
        auth.push_back(static_cast<uint8_t>(config_.password.size()));
        auth.insert(auth.end(), config_.password.begin(), config_.password.end());

        asio::write(socket, asio::buffer(auth), ec);
        if (ec) return false;

        uint8_t resp[2];
        asio::read(socket, asio::buffer(resp, 2), ec);
        return !ec && resp[0] == 0x01 && resp[1] == 0x00;
    }

    bool socks4_connect(tcp::socket& socket,
                         const std::string& target_host, uint16_t target_port) {
        error_code ec;
        auto addr = asio::ip::make_address(target_host, ec);
        if (ec) {
            spdlog::debug("SOCKS4 requires IPv4 address, resolving {}...", target_host);
            tcp::resolver resolver(io_);
            auto results = resolver.resolve(target_host, "");
            if (results.empty()) return false;
            addr = results.begin()->endpoint().address();
        }

        // SOCKS4 request
        // +----+----+----+----+----+----+----+----+----+----+...+----+
        // | VN | CD | DSTPORT |      DSTIP        | USERID       |NULL|
        // +----+----+----+----+----+----+----+----+----+----+...+----+
        auto bytes = addr.to_v4().to_bytes();
        uint16_t port_n = htons(target_port);

        uint8_t req[9] = {
            0x04, // VN = SOCKS4
            0x01, // CD = CONNECT
            static_cast<uint8_t>(port_n >> 8),
            static_cast<uint8_t>(port_n & 0xFF),
            bytes[0], bytes[1], bytes[2], bytes[3],
            0x00  // NULL terminator for empty userid
        };

        asio::write(socket, asio::buffer(req, sizeof(req)), ec);
        if (ec) return false;

        // Response
        uint8_t resp[8];
        asio::read(socket, asio::buffer(resp, 8), ec);
        if (ec) return false;

        // resp[0] should be 0 (VN), resp[1] should be 90 (granted)
        return resp[1] == 90;
    }

    static bool parse_proxy_env(const std::string& env, ProxyConfig& cfg) {
        std::string_view sv(env);
        if (sv.starts_with("http://")) {
            cfg.type = ProxyType::HTTP;
            sv.remove_prefix(7);
        } else if (sv.starts_with("socks5://")) {
            cfg.type = ProxyType::SOCKS5;
            sv.remove_prefix(9);
        } else if (sv.starts_with("socks4://")) {
            cfg.type = ProxyType::SOCKS4;
            sv.remove_prefix(9);
        } else if (sv.starts_with("socks://")) {
            cfg.type = ProxyType::SOCKS5;
            sv.remove_prefix(7);
        } else {
            cfg.type = ProxyType::HTTP;
        }

        // Parse user:pass@host:port
        auto at = sv.find('@');
        if (at != std::string_view::npos) {
            auto userinfo = sv.substr(0, at);
            auto colon = userinfo.find(':');
            if (colon != std::string_view::npos) {
                cfg.username = std::string(userinfo.substr(0, colon));
                cfg.password = std::string(userinfo.substr(colon + 1));
            } else {
                cfg.username = std::string(userinfo);
            }
            sv.remove_prefix(at + 1);
        }

        auto port_colon = sv.rfind(':');
        if (port_colon != std::string_view::npos) {
            cfg.host = std::string(sv.substr(0, port_colon));
            auto port_str = sv.substr(port_colon + 1);
            auto res = std::from_chars(port_str.data(), port_str.data() + port_str.size(), cfg.port);
            if (res.ec != std::errc{}) cfg.port = 1080;
        } else {
            cfg.host = std::string(sv);
            cfg.port = 1080;
        }

        return !cfg.host.empty();
    }

    static std::string base64_encode(const std::string& input) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve(((input.size() + 2) / 3) * 4);

        for (size_t i = 0; i < input.size(); i += 3) {
            uint32_t n = static_cast<unsigned char>(input[i]) << 16;
            if (i + 1 < input.size()) n |= static_cast<unsigned char>(input[i + 1]) << 8;
            if (i + 2 < input.size()) n |= static_cast<unsigned char>(input[i + 2]);

            result.push_back(chars[(n >> 18) & 0x3F]);
            result.push_back(chars[(n >> 12) & 0x3F]);
            result.push_back((i + 1 < input.size()) ? chars[(n >> 6) & 0x3F] : '=');
            result.push_back((i + 2 < input.size()) ? chars[n & 0x3F] : '=');
        }
        return result;
    }
};

// ============================================================================
// 8. Ping / ICMP Echo
// ============================================================================

struct PingResult {
    std::string target;
    double rtt_ms = 0.0;
    bool success = false;
    int ttl = 0;
    int sequence = 0;
    size_t bytes = 0;
    error_code error;
};

class PingClient {
    asio::io_context& io_;
    std::chrono::milliseconds timeout_{3000};
    int ttl_{64};
    size_t packet_size_{56};
    uint16_t identifier_;

public:
    explicit PingClient(asio::io_context& io) : io_(io) {
        static std::random_device rd;
        identifier_ = static_cast<uint16_t>(rd() & 0xFFFF);
    }

    void set_timeout(std::chrono::milliseconds t) { timeout_ = t; }
    void set_ttl(int ttl) { ttl_ = ttl; }
    void set_packet_size(size_t s) { packet_size_ = std::max(s, size_t(16)); }

    PingResult ping(const std::string& host, int sequence = 0) {
        PingResult result;
        result.target = host;
        result.sequence = sequence;

        try {
            error_code ec;
            auto addr = resolve_host(host, ec);
            if (ec) { result.error = ec; return result; }

#ifdef _WIN32
            return ping_windows(host, sequence);
#else
            return ping_posix(addr, sequence);
#endif
        } catch (std::exception& e) {
            result.error = asio::error::host_unreachable;
            spdlog::debug("Ping failed: {}", e.what());
            return result;
        }
    }

    std::vector<PingResult> ping_multi(const std::string& host, int count = 4,
                                        std::chrono::milliseconds interval = std::chrono::milliseconds(1000)) {
        std::vector<PingResult> results;
        for (int i = 0; i < count; ++i) {
            results.push_back(ping(host, i));
            if (i < count - 1) {
                std::this_thread::sleep_for(interval);
            }
        }
        return results;
    }

    static double average_rtt(const std::vector<PingResult>& results) {
        double sum = 0.0;
        int count = 0;
        for (auto& r : results) {
            if (r.success) { sum += r.rtt_ms; ++count; }
        }
        return count > 0 ? sum / count : 0.0;
    }

    static double packet_loss(const std::vector<PingResult>& results) {
        if (results.empty()) return 100.0;
        size_t lost = 0;
        for (auto& r : results) { if (!r.success) ++lost; }
        return 100.0 * static_cast<double>(lost) / results.size();
    }

    static double jitter(const std::vector<PingResult>& results) {
        std::vector<double> rtts;
        for (auto& r : results) { if (r.success) rtts.push_back(r.rtt_ms); }
        if (rtts.size() < 2) return 0.0;
        double variance = 0.0;
        for (size_t i = 1; i < rtts.size(); ++i) {
            double diff = std::abs(rtts[i] - rtts[i-1]);
            variance += diff;
        }
        variance /= (rtts.size() - 1);
        return variance; // in ms
    }

private:
    asio::ip::address resolve_host(const std::string& host, error_code& ec) {
        // Try as IP first
        ec.clear();
        auto addr = asio::ip::make_address(host, ec);
        if (!ec) return addr;

        // Resolve
        tcp::resolver resolver(io_);
        auto results = resolver.resolve(host, "", ec);
        if (ec) return {};
        return results.begin()->endpoint().address();
    }

#ifndef _WIN32
    PingResult ping_posix(const asio::ip::address& addr, int sequence) {
        PingResult result;

        // ICMP: need raw socket (requires root or CAP_NET_RAW)
        int sock = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) {
            // Fallback: try with SOCK_DGRAM (some Linux allows this for non-root ping)
            sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
            if (sock < 0) {
                result.error = asio::error::access_denied;
                spdlog::warn("Raw socket unavailable (need root/CAP_NET_RAW). Using TCP fallback.");
                return tcp_fallback_ping(addr.to_string(), sequence);
            }
        }

        // Set TTL
        if (::setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl_, sizeof(ttl_)) < 0) {
            ::close(sock);
            result.error = asio::error::access_denied;
            return result;
        }

        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = timeout_.count() / 1000;
        tv.tv_usec = (timeout_.count() % 1000) * 1000;
        ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Build ICMP echo request
        size_t total_len = sizeof(struct icmphdr) + packet_size_;
        std::vector<uint8_t> packet(total_len);
        auto* icmp_hdr = reinterpret_cast<struct icmphdr*>(packet.data());

        icmp_hdr->type = ICMP_ECHO;
        icmp_hdr->code = 0;
        icmp_hdr->un.echo.id = htons(identifier_);
        icmp_hdr->un.echo.sequence = htons(static_cast<uint16_t>(sequence));
        icmp_hdr->checksum = 0;

        // Fill payload with timestamp + pattern
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            steady_clock::now().time_since_epoch()).count();
        std::memcpy(packet.data() + sizeof(struct icmphdr), &now_us, sizeof(now_us));
        for (size_t i = sizeof(struct icmphdr) + sizeof(now_us); i < total_len; ++i) {
            packet[i] = static_cast<uint8_t>(i & 0xFF);
        }

        // Compute checksum
        icmp_hdr->checksum = internet_checksum(packet.data(), total_len);

        // Send
        sockaddr_in target{};
        target.sin_family = AF_INET;
        if (addr.is_v4()) {
            auto bytes = addr.to_v4().to_bytes();
            std::memcpy(&target.sin_addr, bytes.data(), 4);
        }
        if (::sendto(sock, packet.data(), total_len, 0,
                     reinterpret_cast<sockaddr*>(&target), sizeof(target)) < 0) {
            ::close(sock);
            result.error = asio::error::network_unreachable;
            return result;
        }

        auto send_time = steady_clock::now();

        // Receive reply
        std::vector<uint8_t> recv_buf(1500);
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);

        ssize_t recv_len = ::recvfrom(sock, recv_buf.data(), recv_buf.size(), 0,
                                       reinterpret_cast<sockaddr*>(&from), &from_len);
        ::close(sock);

        if (recv_len < 0) {
            result.error = asio::error::timed_out;
            return result;
        }

        auto recv_time = steady_clock::now();
        result.rtt_ms = std::chrono::duration<double, std::milli>(
            recv_time - send_time).count();

        // Parse IP header to find ICMP
        size_t ip_hdr_len = (recv_buf[0] & 0x0F) * 4;
        if (static_cast<size_t>(recv_len) < ip_hdr_len + sizeof(struct icmphdr)) {
            result.error = asio::error::message_size;
            return result;
        }

        auto* recv_icmp = reinterpret_cast<struct icmphdr*>(recv_buf.data() + ip_hdr_len);
        if (recv_icmp->type == ICMP_ECHOREPLY) {
            result.success = true;
            result.ttl = recv_buf[8]; // TTL from IP header
            result.bytes = recv_len - ip_hdr_len - sizeof(struct icmphdr);
        } else {
            result.success = false;
            result.error = asio::error::no_reply;
        }

        return result;
    }
#else
    PingResult ping_windows(const std::string& host, int sequence) {
        PingResult result;
        // On Windows use TCP connect fallback — raw sockets require admin
        return tcp_fallback_ping(host, sequence);
    }
#endif

    PingResult tcp_fallback_ping(const std::string& host, int sequence) {
        PingResult result;
        try {
            tcp::socket socket(io_);
            tcp::resolver resolver(io_);
            auto endpoints = resolver.resolve(host, "80");

            auto start = steady_clock::now();
            error_code ec;
            asio::connect(socket, endpoints, ec);
            auto elapsed = std::chrono::duration<double, std::milli>(
                steady_clock::now() - start).count();

            result.success = !ec;
            result.rtt_ms = elapsed;
            result.sequence = sequence;
            if (ec) result.error = ec;
        } catch (...) {
            result.error = asio::error::host_unreachable;
        }
        return result;
    }

    static uint16_t internet_checksum(const void* data, size_t len) {
        uint32_t sum = 0;
        auto* p = static_cast<const uint16_t*>(data);
        for (size_t i = 0; i < len / 2; ++i) {
            sum += p[i];
        }
        if (len % 2) {
            sum += static_cast<const uint8_t*>(data)[len - 1];
        }
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<uint16_t>(~sum);
    }
};

// ============================================================================
// 9. Traceroute Implementation
// ============================================================================

struct TracerouteHop {
    int hop_number = 0;
    std::string address;
    std::string hostname;
    double rtt1_ms = 0.0;
    double rtt2_ms = 0.0;
    double rtt3_ms = 0.0;
    double avg_rtt_ms = 0.0;
    bool reached_destination = false;
    bool timed_out = false;
};

class Traceroute {
    asio::io_context& io_;
    int max_hops_ = 30;
    int probes_per_hop_ = 3;
    std::chrono::milliseconds timeout_{3000};
    int start_ttl_ = 1;
    uint16_t base_port_ = 33434;

public:
    explicit Traceroute(asio::io_context& io) : io_(io) {}

    void set_max_hops(int n) { max_hops_ = n; }
    void set_probes_per_hop(int n) { probes_per_hop_ = n; }
    void set_timeout(std::chrono::milliseconds t) { timeout_ = t; }

    std::vector<TracerouteHop> trace(const std::string& target) {
        std::vector<TracerouteHop> hops;

        try {
            error_code ec;
            auto dest_addr = asio::ip::make_address(target, ec);
            if (ec) {
                tcp::resolver resolver(io_);
                auto results = resolver.resolve(target, "", ec);
                if (ec) {
                    spdlog::error("Traceroute: cannot resolve {}", target);
                    return hops;
                }
                dest_addr = results.begin()->endpoint().address();
            }

            // Use UDP traceroute technique (more portable than raw ICMP)
            for (int ttl = start_ttl_; ttl <= max_hops_; ++ttl) {
                TracerouteHop hop;
                hop.hop_number = ttl;

                std::vector<double> rtts;
                bool got_response = false;

                for (int probe = 0; probe < probes_per_hop_; ++probe) {
                    auto result = probe_hop_udp(dest_addr, ttl, base_port_ + ttl);
                    if (result.reached) {
                        hop.address = result.addr;
                        hop.reached_destination = result.is_dest;
                        rtts.push_back(result.rtt_ms);
                        got_response = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (!got_response) {
                    hop.address = "*";
                    hop.timed_out = true;
                } else {
                    // Compute stats
                    if (rtts.size() >= 1) hop.rtt1_ms = rtts[0];
                    if (rtts.size() >= 2) hop.rtt2_ms = rtts[1];
                    if (rtts.size() >= 3) hop.rtt3_ms = rtts[2];
                    hop.avg_rtt_ms = std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();

                    // Try reverse DNS
                    if (!hop.address.empty() && hop.address != "*") {
                        try {
                            tcp::resolver resolver(io_);
                            auto addr = asio::ip::make_address(hop.address);
                            tcp::endpoint ep(addr, 0);
                            // asio doesn't have reverse resolution, do a quick lookup
                            // Using blocking getnameinfo
                            sockaddr_in sa{};
                            sa.sin_family = AF_INET;
                            auto bytes = addr.to_v4().to_bytes();
                            std::memcpy(&sa.sin_addr, bytes.data(), 4);
                            char hostname[NI_MAXHOST];
                            if (getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof(sa),
                                            hostname, sizeof(hostname), nullptr, 0, 0) == 0) {
                                hop.hostname = hostname;
                            }
                        } catch (...) {}
                    }
                }

                hops.push_back(hop);
                if (hop.reached_destination) break;
            }
        } catch (std::exception& e) {
            spdlog::error("Traceroute error: {}", e.what());
        }

        return hops;
    }

private:
    struct ProbeResult {
        bool reached = false;
        bool is_dest = false;
        std::string addr;
        double rtt_ms = 0.0;
    };

    ProbeResult probe_hop_udp(const asio::ip::address& dest, int ttl, uint16_t port) {
        ProbeResult result;

        try {
            // Open raw socket or use UDP with TTL
#ifdef _WIN32
            // Windows: use ICMP via IcmpSendEcho
            return tcp_probe_hop(dest, ttl);
#else
            // Unix: send UDP with specific TTL, receive ICMP Time Exceeded or Port Unreachable
            int send_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (send_sock < 0) return tcp_probe_hop(dest, ttl);

            int recv_sock = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
            if (recv_sock < 0) {
                ::close(send_sock);
                recv_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
                if (recv_sock < 0) {
                    ::close(send_sock);
                    return tcp_probe_hop(dest, ttl);
                }
            }

            ::setsockopt(send_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

            struct timeval tv;
            tv.tv_sec = timeout_.count() / 1000;
            tv.tv_usec = (timeout_.count() % 1000) * 1000;
            ::setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_port = htons(port);
            auto bytes = dest.to_v4().to_bytes();
            std::memcpy(&target.sin_addr, bytes.data(), 4);

            char payload = 'X';
            auto start = steady_clock::now();
            ::sendto(send_sock, &payload, 1, 0,
                     reinterpret_cast<sockaddr*>(&target), sizeof(target));

            // Receive ICMP response
            std::vector<uint8_t> buf(512);
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            ssize_t len = ::recvfrom(recv_sock, buf.data(), buf.size(), 0,
                                      reinterpret_cast<sockaddr*>(&from), &from_len);
            auto elapsed = std::chrono::duration<double, std::milli>(
                steady_clock::now() - start).count();

            ::close(send_sock);
            ::close(recv_sock);

            if (len < 0) return result;

            result.reached = true;
            result.rtt_ms = elapsed;

            // Parse ICMP
            size_t ip_hdr_len = (buf[0] & 0x0F) * 4;
            if (static_cast<size_t>(len) < ip_hdr_len + 8) return result;

            auto* icmp_hdr = reinterpret_cast<struct icmphdr*>(buf.data() + ip_hdr_len);
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, addr_str, sizeof(addr_str));
            result.addr = addr_str;

            if (icmp_hdr->type == ICMP_TIME_EXCEEDED) {
                result.is_dest = false;
            } else if (icmp_hdr->type == ICMP_DEST_UNREACH &&
                       icmp_hdr->code == ICMP_PORT_UNREACH) {
                result.is_dest = true;
            }
            return result;
#endif
        } catch (...) {
            return tcp_probe_hop(dest, ttl);
        }
    }

    ProbeResult tcp_probe_hop(const asio::ip::address& dest, int ttl) {
        // Fallback: TCP connect with increasing TTL (less accurate but portable)
        ProbeResult result;
        try {
            tcp::socket socket(io_);
            socket.open(tcp::v4());

            // Set TTL
#ifdef __linux__
            int fd = socket.native_handle();
            ::setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
#elif defined(_WIN32)
            int fd = socket.native_handle();
            ::setsockopt(fd, IPPROTO_IP, IP_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));
#endif

            tcp::endpoint ep(dest, 80);
            auto start = steady_clock::now();
            error_code ec;
            socket.connect(ep, ec);
            auto elapsed = std::chrono::duration<double, std::milli>(
                steady_clock::now() - start).count();

            if (!ec) {
                // Connected = reached destination
                result.reached = true;
                result.is_dest = true;
                result.addr = dest.to_string();
                result.rtt_ms = elapsed;
            } else if (ec == asio::error::connection_refused ||
                       ec == asio::error::timed_out) {
                // Connection refused by intermediate (but we got a response)
                // We can't easily get the intermediate IP via TCP fallback though
                result.reached = true;
                result.is_dest = false;
                // Try to resolve this hop - not precise in TCP mode
                result.addr = "?";
                result.rtt_ms = elapsed;
            }
        } catch (...) {}
        return result;
    }
};

// ============================================================================
// 10. Network Change Detection
// ============================================================================

struct NetworkChangeEvent {
    enum class Type {
        InterfaceUp,
        InterfaceDown,
        AddressChanged,
        AddressAdded,
        AddressRemoved,
        GatewayChanged,
        DnsChanged,
        ConnectivityChanged
    };

    Type type;
    std::string interface_name;
    std::string detail;
    std::chrono::system_clock::time_point timestamp;
};

class NetworkChangeDetector {
    asio::io_context& io_;
    std::chrono::milliseconds poll_interval_{2000};
    std::vector<NetworkInterface> last_state_;
    std::vector<std::string> last_dns_;
    bool running_ = false;
    std::thread worker_;
    std::mutex callback_mutex_;
    std::vector<std::function<void(const NetworkChangeEvent&)>> callbacks_;

public:
    explicit NetworkChangeDetector(asio::io_context& io) : io_(io) {}

    ~NetworkChangeDetector() { stop(); }

    void set_poll_interval(std::chrono::milliseconds interval) {
        poll_interval_ = interval;
    }

    void on_change(std::function<void(const NetworkChangeEvent&)> cb) {
        std::lock_guard lock(callback_mutex_);
        callbacks_.push_back(std::move(cb));
    }

    void start() {
        if (running_) return;
        running_ = true;
        last_state_ = NetworkEnumerator::enumerate();
        last_dns_ = get_dns_servers();

        worker_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(poll_interval_);
                poll_changes();
            }
        });
        spdlog::info("Network change detector started ({}ms interval)",
                     poll_interval_.count());
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

    // Run a single poll check
    std::vector<NetworkChangeEvent> check_now() {
        std::vector<NetworkChangeEvent> events;
        auto current = NetworkEnumerator::enumerate();
        auto current_dns = get_dns_servers();

        // Compare interfaces
        std::set<std::string> old_names, new_names;
        for (auto& i : last_state_) old_names.insert(i.name);
        for (auto& i : current) new_names.insert(i.name);

        // New interfaces
        for (auto& name : new_names) {
            if (!old_names.count(name)) {
                auto it = std::find_if(current.begin(), current.end(),
                    [&](auto& i) { return i.name == name; });
                NetworkChangeEvent ev;
                ev.type = NetworkChangeEvent::Type::InterfaceUp;
                ev.interface_name = name;
                ev.detail = it != current.end() && !it->ipv4_addresses.empty()
                    ? it->ipv4_addresses[0] : "up";
                ev.timestamp = system_clock::now();
                events.push_back(ev);
            }
        }

        // Removed interfaces
        for (auto& name : old_names) {
            if (!new_names.count(name)) {
                NetworkChangeEvent ev;
                ev.type = NetworkChangeEvent::Type::InterfaceDown;
                ev.interface_name = name;
                ev.timestamp = system_clock::now();
                events.push_back(ev);
            }
        }

        // Address changes on existing interfaces
        for (auto& current_iface : current) {
            auto old_it = std::find_if(last_state_.begin(), last_state_.end(),
                [&](auto& i) { return i.name == current_iface.name; });
            if (old_it == last_state_.end()) continue;

            std::set<std::string> old_ips(old_it->ipv4_addresses.begin(),
                                          old_it->ipv4_addresses.end());
            std::set<std::string> new_ips(current_iface.ipv4_addresses.begin(),
                                          current_iface.ipv4_addresses.end());

            for (auto& ip : new_ips) {
                if (!old_ips.count(ip)) {
                    NetworkChangeEvent ev;
                    ev.type = NetworkChangeEvent::Type::AddressAdded;
                    ev.interface_name = current_iface.name;
                    ev.detail = ip;
                    ev.timestamp = system_clock::now();
                    events.push_back(ev);
                }
            }
            for (auto& ip : old_ips) {
                if (!new_ips.count(ip)) {
                    NetworkChangeEvent ev;
                    ev.type = NetworkChangeEvent::Type::AddressRemoved;
                    ev.interface_name = current_iface.name;
                    ev.detail = ip;
                    ev.timestamp = system_clock::now();
                    events.push_back(ev);
                }
            }

            // Gateway change
            if (old_it->gateway != current_iface.gateway) {
                NetworkChangeEvent ev;
                ev.type = NetworkChangeEvent::Type::GatewayChanged;
                ev.interface_name = current_iface.name;
                ev.detail = old_it->gateway + " -> " + current_iface.gateway;
                ev.timestamp = system_clock::now();
                events.push_back(ev);
            }
        }

        // DNS changes
        std::set<std::string> old_dns_set(last_dns_.begin(), last_dns_.end());
        std::set<std::string> new_dns_set(current_dns.begin(), current_dns.end());
        if (old_dns_set != new_dns_set) {
            NetworkChangeEvent ev;
            ev.type = NetworkChangeEvent::Type::DnsChanged;
            ev.timestamp = system_clock::now();
            events.push_back(ev);
        }

        last_state_ = std::move(current);
        last_dns_ = std::move(current_dns);

        // Notify callbacks
        for (auto& ev : events) {
            std::lock_guard lock(callback_mutex_);
            for (auto& cb : callbacks_) {
                cb(ev);
            }
        }

        if (!events.empty()) {
            spdlog::debug("Network change detected: {} event(s)", events.size());
        }
        return events;
    }

private:
    void poll_changes() { check_now(); }

    static std::vector<std::string> get_dns_servers() {
        std::vector<std::string> servers;
#ifdef __linux__
        std::ifstream resolv("/etc/resolv.conf");
        if (resolv.is_open()) {
            std::string line;
            while (std::getline(resolv, line)) {
                if (line.starts_with("nameserver ")) {
                    servers.push_back(line.substr(11));
                }
            }
        }
#elif defined(__APPLE__)
        // On macOS, parse scutil output or just check resolv.conf
        std::ifstream resolv("/etc/resolv.conf");
        if (resolv.is_open()) {
            std::string line;
            while (std::getline(resolv, line)) {
                if (line.starts_with("nameserver ")) {
                    servers.push_back(line.substr(11));
                }
            }
        }
#elif defined(_WIN32)
        // Get DNS via GetNetworkParams
        FIXED_INFO* info = nullptr;
        ULONG buf_len = sizeof(FIXED_INFO);
        info = reinterpret_cast<FIXED_INFO*>(malloc(buf_len));
        if (GetNetworkParams(info, &buf_len) == ERROR_BUFFER_OVERFLOW) {
            free(info);
            info = reinterpret_cast<FIXED_INFO*>(malloc(buf_len));
        }
        if (GetNetworkParams(info, &buf_len) == NO_ERROR && info) {
            auto* dns = &info->DnsServerList;
            while (dns) {
                servers.push_back(dns->IpAddress.String);
                dns = dns->Next;
            }
        }
        free(info);
#endif
        return servers;
    }
};

// ============================================================================
// 11. Connection Quality Scoring
// ============================================================================

struct ConnectionQuality {
    // Scores 0-100 (higher = better)
    double overall_score = 0.0;
    double latency_score = 0.0;
    double bandwidth_score = 0.0;
    double jitter_score = 0.0;
    double packet_loss_score = 0.0;
    double stability_score = 0.0;

    double avg_latency_ms = 0.0;
    double jitter_ms = 0.0;
    double packet_loss_pct = 0.0;
    double bandwidth_mbps = 0.0;

    enum class Grade {
        Excellent, // 90-100
        Good,      // 75-89
        Fair,      // 50-74
        Poor,      // 25-49
        Bad        // 0-24
    };

    Grade grade() const {
        if (overall_score >= 90) return Grade::Excellent;
        if (overall_score >= 75) return Grade::Good;
        if (overall_score >= 50) return Grade::Fair;
        if (overall_score >= 25) return Grade::Poor;
        return Grade::Bad;
    }

    static const char* grade_name(Grade g) {
        switch (g) {
            case Grade::Excellent: return "Excellent";
            case Grade::Good:      return "Good";
            case Grade::Fair:      return "Fair";
            case Grade::Poor:      return "Poor";
            case Grade::Bad:       return "Bad";
        }
        return "Unknown";
    }

    bool suitable_for_video() const { return overall_score >= 60; }
    bool suitable_for_voice() const { return overall_score >= 40; }
    bool suitable_for_remote_desktop() const { return overall_score >= 50; }
};

class ConnectionQualityScorer {
public:
    struct Config {
        // Thresholds for scoring
        double excellent_latency_ms = 20.0;
        double good_latency_ms = 60.0;
        double fair_latency_ms = 150.0;

        double excellent_jitter_ms = 5.0;
        double good_jitter_ms = 15.0;
        double fair_jitter_ms = 30.0;

        double excellent_loss_pct = 0.1;
        double good_loss_pct = 1.0;
        double fair_loss_pct = 5.0;

        double excellent_bandwidth_mbps = 50.0;
        double good_bandwidth_mbps = 10.0;
        double fair_bandwidth_mbps = 2.0;

        // Weights for overall score (sum should be 1.0)
        double weight_latency = 0.30;
        double weight_bandwidth = 0.25;
        double weight_jitter = 0.20;
        double weight_loss = 0.15;
        double weight_stability = 0.10;
    };

    explicit ConnectionQualityScorer(Config cfg = {}) : config_(cfg) {}

    ConnectionQuality score(const std::vector<PingResult>& ping_results,
                            const BandwidthResult& bw_result) {
        ConnectionQuality q;

        // Calculate ping stats
        q.avg_latency_ms = PingClient::average_rtt(ping_results);
        q.jitter_ms = PingClient::jitter(ping_results);
        q.packet_loss_pct = PingClient::packet_loss(ping_results);
        q.bandwidth_mbps = bw_result.download_mbps;

        // Score each component (0-100)
        q.latency_score = score_latency(q.avg_latency_ms);
        q.bandwidth_score = score_bandwidth(q.bandwidth_mbps);
        q.jitter_score = score_jitter(q.jitter_ms);
        q.packet_loss_score = score_packet_loss(q.packet_loss_pct);
        q.stability_score = compute_stability(ping_results);

        // Weighted overall
        q.overall_score =
            config_.weight_latency * q.latency_score +
            config_.weight_bandwidth * q.bandwidth_score +
            config_.weight_jitter * q.jitter_score +
            config_.weight_loss * q.packet_loss_score +
            config_.weight_stability * q.stability_score;

        q.overall_score = std::clamp(q.overall_score, 0.0, 100.0);

        spdlog::debug("Connection quality: {:.1f}/100 ({})",
                      q.overall_score, ConnectionQuality::grade_name(q.grade()));
        return q;
    }

    // Score from live measurements
    ConnectionQuality score_live(asio::io_context& io, const std::string& target = "8.8.8.8") {
        PingClient ping(io);
        auto ping_results = ping.ping_multi(target, 10, std::chrono::milliseconds(200));

        // Quick bandwidth estimate
        BandwidthTester bw(io);
        bw.set_download_size(1 * 1024 * 1024); // Small test for speed
        bw.set_upload_size(256 * 1024);
        auto bw_result = bw.run_test();

        return score(ping_results, bw_result);
    }

private:
    Config config_;

    double score_latency(double ms) const {
        if (ms <= config_.excellent_latency_ms) return 100.0;
        if (ms <= config_.good_latency_ms) {
            return 100.0 - 25.0 * (ms - config_.excellent_latency_ms) /
                (config_.good_latency_ms - config_.excellent_latency_ms);
        }
        if (ms <= config_.fair_latency_ms) {
            return 75.0 - 25.0 * (ms - config_.good_latency_ms) /
                (config_.fair_latency_ms - config_.good_latency_ms);
        }
        return std::max(0.0, 50.0 - 50.0 * (ms - config_.fair_latency_ms) / config_.fair_latency_ms);
    }

    double score_bandwidth(double mbps) const {
        if (mbps >= config_.excellent_bandwidth_mbps) return 100.0;
        if (mbps >= config_.good_bandwidth_mbps) {
            return 100.0 - 25.0 * (config_.excellent_bandwidth_mbps - mbps) /
                (config_.excellent_bandwidth_mbps - config_.good_bandwidth_mbps);
        }
        if (mbps >= config_.fair_bandwidth_mbps) {
            return 75.0 - 25.0 * (config_.good_bandwidth_mbps - mbps) /
                (config_.good_bandwidth_mbps - config_.fair_bandwidth_mbps);
        }
        return std::max(0.0, 50.0 * mbps / config_.fair_bandwidth_mbps);
    }

    double score_jitter(double ms) const {
        if (ms <= config_.excellent_jitter_ms) return 100.0;
        if (ms <= config_.good_jitter_ms) {
            return 100.0 - 25.0 * (ms - config_.excellent_jitter_ms) /
                (config_.good_jitter_ms - config_.excellent_jitter_ms);
        }
        if (ms <= config_.fair_jitter_ms) {
            return 75.0 - 25.0 * (ms - config_.good_jitter_ms) /
                (config_.fair_jitter_ms - config_.good_jitter_ms);
        }
        return std::max(0.0, 50.0 - 50.0 * (ms - config_.fair_jitter_ms) / config_.fair_jitter_ms);
    }

    double score_packet_loss(double pct) const {
        if (pct <= config_.excellent_loss_pct) return 100.0;
        if (pct <= config_.good_loss_pct) {
            return 100.0 - 25.0 * (pct - config_.excellent_loss_pct) /
                (config_.good_loss_pct - config_.excellent_loss_pct);
        }
        if (pct <= config_.fair_loss_pct) {
            return 75.0 - 25.0 * (pct - config_.good_loss_pct) /
                (config_.fair_loss_pct - config_.good_loss_pct);
        }
        return std::max(0.0, 50.0 - 50.0 * (pct - config_.fair_loss_pct) / (100.0 - config_.fair_loss_pct));
    }

    double compute_stability(const std::vector<PingResult>& results) const {
        if (results.size() < 2) return 50.0;

        // Check for consecutive successes and consistent RTT
        int consecutive_ok = 0;
        int max_consecutive = 0;
        double prev = -1;
        double variance_sum = 0;
        int variance_count = 0;

        for (auto& r : results) {
            if (r.success) {
                ++consecutive_ok;
                max_consecutive = std::max(max_consecutive, consecutive_ok);
                if (prev >= 0) {
                    variance_sum += std::abs(r.rtt_ms - prev);
                    ++variance_count;
                }
                prev = r.rtt_ms;
            } else {
                consecutive_ok = 0;
            }
        }

        double consistency = static_cast<double>(max_consecutive) / results.size() * 100.0;
        double avg_variance = variance_count > 0 ? variance_sum / variance_count : 0;
        double variance_score = std::max(0.0, 100.0 - avg_variance * 2.0);

        return (consistency * 0.6 + variance_score * 0.4);
    }
};

// ============================================================================
// Network Diagnostic Utility
// ============================================================================

struct NetworkDiagnostic {
    bool internet_reachable = false;
    bool dns_working = false;
    std::string public_ip;
    NatType nat_type = NatType::Unknown;
    ConnectionQuality quality;
    std::vector<NetworkInterface> interfaces;
    std::chrono::system_clock::time_point timestamp;
    double elapsed_ms = 0.0;
};

class NetworkDiagnostics {
    asio::io_context& io_;

public:
    explicit NetworkDiagnostics(asio::io_context& io) : io_(io) {}

    NetworkDiagnostic run_full_diagnostic() {
        NetworkDiagnostic diag;
        diag.timestamp = system_clock::now();
        auto start = steady_clock::now();

        spdlog::info("Starting full network diagnostic...");

        // 1. Enumerate interfaces
        diag.interfaces = NetworkEnumerator::enumerate();
        spdlog::info("  Interfaces: {} found", diag.interfaces.size());

        // 2. Check DNS
        diag.dns_working = check_dns();
        spdlog::info("  DNS: {}", diag.dns_working ? "OK" : "FAILED");

        // 3. Check internet connectivity
        diag.internet_reachable = check_internet();
        spdlog::info("  Internet: {}", diag.internet_reachable ? "Reachable" : "Unreachable");

        // 4. Detect NAT type
        if (diag.internet_reachable) {
            StunClient stun(io_);
            auto stun_result = stun.detect_nat();
            diag.nat_type = stun_result.nat_type;
            diag.public_ip = stun_result.public_ip;
            spdlog::info("  NAT: {} (public IP: {})",
                         StunClient::nat_type_name(diag.nat_type), diag.public_ip);
        }

        // 5. Quality score
        if (diag.internet_reachable) {
            ConnectionQualityScorer scorer;
            diag.quality = scorer.score_live(io_);
            spdlog::info("  Quality: {:.1f}/100 - {}",
                         diag.quality.overall_score,
                         ConnectionQuality::grade_name(diag.quality.grade()));
        }

        auto end = steady_clock::now();
        diag.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        spdlog::info("Network diagnostic complete in {:.0f}ms", diag.elapsed_ms);

        return diag;
    }

private:
    bool check_dns() {
        try {
            tcp::resolver resolver(io_);
            auto results = resolver.resolve("google.com", "");
            return !results.empty();
        } catch (...) {
            return false;
        }
    }

    bool check_internet() {
        try {
            tcp::socket socket(io_);
            tcp::resolver resolver(io_);
            auto endpoints = resolver.resolve("8.8.8.8", "53");
            error_code ec;
            asio::connect(socket, endpoints, ec);
            return !ec;
        } catch (...) {
            return false;
        }
    }
};

// ============================================================================
// Network Utilities: Helper Functions
// ============================================================================

class NetworkUtils {
public:
    // Get a free local TCP port
    static uint16_t get_free_port() {
        try {
            asio::io_context io;
            tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
            return acceptor.local_endpoint().port();
        } catch (...) {
            return 0;
        }
    }

    // Get a free local UDP port
    static uint16_t get_free_udp_port() {
        try {
            asio::io_context io;
            udp::socket socket(io, udp::endpoint(udp::v4(), 0));
            return socket.local_endpoint().port();
        } catch (...) {
            return 0;
        }
    }

    // Check if a port is available
    static bool is_port_available(uint16_t port, bool tcp = true) {
        try {
            asio::io_context io;
            if (tcp) {
                tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), port));
            } else {
                udp::socket socket(io, udp::endpoint(udp::v4(), port));
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    // Check if a host:port is reachable
    static bool is_reachable(const std::string& host, uint16_t port,
                             std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        try {
            asio::io_context io;
            tcp::socket socket(io);
            tcp::resolver resolver(io);
            auto endpoints = resolver.resolve(host, std::to_string(port));
            error_code ec;
            asio::connect(socket, endpoints, ec);
            return !ec;
        } catch (...) {
            return false;
        }
    }

    // Get local hostname
    static std::string hostname() {
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0) {
            return std::string(buf);
        }
        return {};
    }

    // Get fully qualified domain name
    static std::string fqdn() {
        std::string h = hostname();
        if (h.empty()) return {};

        struct addrinfo hints{}, *info;
        hints.ai_family = AF_INET;
        hints.ai_flags = AI_CANONNAME;

        if (getaddrinfo(h.c_str(), nullptr, &hints, &info) == 0) {
            std::string result(info->ai_canonname ? info->ai_canonname : h);
            freeaddrinfo(info);
            return result;
        }
        return h;
    }

    // Encode URL (basic)
    static std::string url_encode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        for (char c : value) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return escaped.str();
    }

    // Decode URL
    static std::string url_decode(const std::string& value) {
        std::string result;
        result.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '%' && i + 2 < value.size()) {
                int code;
                std::stringstream ss;
                ss << std::hex << value.substr(i + 1, 2);
                ss >> code;
                result.push_back(static_cast<char>(code));
                i += 2;
            } else if (value[i] == '+') {
                result.push_back(' ');
            } else {
                result.push_back(value[i]);
            }
        }
        return result;
    }

    // Parse query string
    static std::map<std::string, std::string> parse_query_string(const std::string& query) {
        std::map<std::string, std::string> result;
        std::string_view sv(query);
        if (sv.starts_with("?")) sv.remove_prefix(1);

        size_t pos = 0;
        while (pos < sv.size()) {
            auto amp = sv.find('&', pos);
            auto pair = (amp == std::string_view::npos) ? sv.substr(pos) : sv.substr(pos, amp - pos);

            auto eq = pair.find('=');
            if (eq != std::string_view::npos) {
                result[url_decode(std::string(pair.substr(0, eq)))] =
                    url_decode(std::string(pair.substr(eq + 1)));
            } else {
                result[url_decode(std::string(pair))] = "";
            }

            if (amp == std::string_view::npos) break;
            pos = amp + 1;
        }
        return result;
    }

    // Parse MIME types
    static std::string mime_type_from_extension(const std::string& ext) {
        static const std::map<std::string, std::string> mime_map = {
            {"html", "text/html"}, {"htm", "text/html"}, {"css", "text/css"},
            {"js", "application/javascript"}, {"json", "application/json"},
            {"xml", "application/xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"},
            {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"svg", "image/svg+xml"},
            {"ico", "image/x-icon"}, {"pdf", "application/pdf"},
            {"zip", "application/zip"}, {"gz", "application/gzip"},
            {"tar", "application/x-tar"}, {"txt", "text/plain"},
            {"mp3", "audio/mpeg"}, {"mp4", "video/mp4"}, {"webm", "video/webm"},
            {"ogg", "audio/ogg"}, {"wav", "audio/wav"},
            {"woff", "font/woff"}, {"woff2", "font/woff2"},
            {"ttf", "font/ttf"}, {"otf", "font/otf"},
            {"wasm", "application/wasm"}
        };
        auto it = mime_map.find(ext);
        return it != mime_map.end() ? it->second : "application/octet-stream";
    }

    // Split host:port string
    static std::pair<std::string, uint16_t> split_host_port(const std::string& hostport,
                                                              uint16_t default_port = 0) {
        auto colon = hostport.rfind(':');
        if (colon == std::string::npos) return {hostport, default_port};

        // Check if colon is part of IPv6 address
        auto bracket = hostport.rfind(']');
        if (bracket != std::string::npos && colon < bracket) {
            return {hostport, default_port};
        }

        std::string host = hostport.substr(0, colon);
        uint16_t port = default_port;
        auto port_result = std::from_chars(
            hostport.data() + colon + 1,
            hostport.data() + hostport.size(), port);
        if (port_result.ec != std::errc{}) port = default_port;

        return {host, port};
    }

    // Check if string is a valid IPv4
    static bool is_valid_ipv4(const std::string& s) {
        error_code ec;
        auto addr = asio::ip::make_address(s, ec);
        return !ec && addr.is_v4();
    }

    // Check if string is a valid IPv6
    static bool is_valid_ipv6(const std::string& s) {
        error_code ec;
        auto addr = asio::ip::make_address(s, ec);
        return !ec && addr.is_v6();
    }

    // Compute subnet broadcast address
    static std::string broadcast_address(const std::string& ip, const std::string& netmask) {
        error_code ec;
        auto ip_addr = asio::ip::make_address(ip, ec);
        auto nm_addr = asio::ip::make_address(netmask, ec);
        if (ec || !ip_addr.is_v4() || !nm_addr.is_v4()) return {};

        auto ip_bytes = ip_addr.to_v4().to_bytes();
        auto nm_bytes = nm_addr.to_v4().to_bytes();

        std::array<uint8_t, 4> bc{};
        for (int i = 0; i < 4; ++i) {
            bc[i] = ip_bytes[i] | ~nm_bytes[i];
        }

        return asio::ip::make_address_v4(bc).to_string();
    }

    // IP address to integer conversion
    static uint32_t ipv4_to_int(const std::string& ip) {
        error_code ec;
        auto addr = asio::ip::make_address(ip, ec);
        if (ec || !addr.is_v4()) return 0;
        return htonl(addr.to_v4().to_uint());
    }

    static std::string int_to_ipv4(uint32_t val) {
        return asio::ip::make_address_v4(ntohl(val)).to_string();
    }
};

} // namespace cppdesk::common
