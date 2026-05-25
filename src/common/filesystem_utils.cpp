#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
namespace cppdesk::common {
namespace fs = std::filesystem;
bool file_exists(const std::string& p) { return fs::exists(p); }
std::string file_extension(const std::string& p) { auto d=p.rfind('.'); return d==std::string::npos?"":p.substr(d); }
std::string file_name(const std::string& p) { return fs::path(p).filename().string(); }
std::string dir_name(const std::string& p) { return fs::path(p).parent_path().string(); }
std::string temp_file_path(const std::string& pref, const std::string& ext) { auto t=fs::temp_directory_path(); return (t/(pref+"_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+ext)).string(); }
std::vector<uint8_t> read_file_bytes(const std::string& p) { std::ifstream f(p,std::ios::binary|std::ios::ate); if(!f) return {}; auto s=f.tellg(); f.seekg(0); std::vector<uint8_t> d(static_cast<size_t>(s)); f.read(reinterpret_cast<char*>(d.data()),s); return d; }
bool write_file_bytes(const std::string& p, const std::vector<uint8_t>& d) { std::ofstream f(p,std::ios::binary|std::ios::trunc); if(!f) return false; f.write(reinterpret_cast<const char*>(d.data()),d.size()); return f.good(); }
std::string read_file_text(const std::string& p) { std::ifstream f(p); if(!f) return ""; std::ostringstream o; o<<f.rdbuf(); return o.str(); }
bool write_file_text(const std::string& p, const std::string& t) { std::ofstream f(p); if(!f) return false; f<<t; return f.good(); }
bool create_directory(const std::string& p) { return fs::create_directories(p); }
bool delete_file(const std::string& p) { return fs::remove(p); }
bool copy_file(const std::string& src, const std::string& dst) { return fs::copy_file(src,dst); }
bool move_file(const std::string& src, const std::string& dst) { std::error_code ec; fs::rename(src,dst,ec); return !ec; }
uint64_t file_size(const std::string& p) { return fs::file_size(p); }
std::vector<std::string> list_directory(const std::string& p) { std::vector<std::string> r; for(auto& e:fs::directory_iterator(p)) r.push_back(e.path().string()); return r; }
std::string canonical_path(const std::string& p) { return fs::canonical(p).string(); }
std::string home_directory() { const char* h=getenv("HOME"); if(!h) h=getenv("USERPROFILE"); return h?h:"/tmp"; }
std::string config_directory() { return home_directory()+"/.config/cppdesk"; }
std::string cache_directory() { return home_directory()+"/.cache/cppdesk"; }
std::string data_directory() { return home_directory()+"/.local/share/cppdesk"; }
std::string log_directory() { return cache_directory()+"/logs"; }
} // namespace