#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <spdlog/spdlog.h>
namespace cppdesk::common {
std::string trim(const std::string& s) { auto a=s.find_first_not_of(" \t\n\r"); if(a==std::string::npos) return ""; auto b=s.find_last_not_of(" \t\n\r"); return s.substr(a,b-a+1); }
std::vector<std::string> split(const std::string& s, char d) { std::vector<std::string> r; std::istringstream is(s); std::string p; while(std::getline(is,p,d)) if(!p.empty()) r.push_back(p); return r; }
std::string join(const std::vector<std::string>& v, const std::string& d) { if(v.empty()) return ""; std::ostringstream o; o<<v[0]; for(size_t i=1;i<v.size();i++) o<<d<<v[i]; return o.str(); }
bool starts_with(const std::string& s, const std::string& p) { return s.size()>=p.size() && s.compare(0,p.size(),p)==0; }
bool ends_with(const std::string& s, const std::string& sf) { return s.size()>=sf.size() && s.compare(s.size()-sf.size(),sf.size(),sf)==0; }
std::string to_lower(const std::string& s) { std::string r=s; std::transform(r.begin(),r.end(),r.begin(),::tolower); return r; }
std::string to_upper(const std::string& s) { std::string r=s; std::transform(r.begin(),r.end(),r.begin(),::toupper); return r; }
std::string replace_all(const std::string& s, const std::string& f, const std::string& t) { if(f.empty()) return s; std::string r=s; size_t p=0; while((p=r.find(f,p))!=std::string::npos){r.replace(p,f.size(),t);p+=t.size();} return r; }
bool contains(const std::string& s, const std::string& sub) { return s.find(sub)!=std::string::npos; }
int count_substr(const std::string& s, const std::string& sub) { int c=0; size_t p=0; while((p=s.find(sub,p))!=std::string::npos){c++;p+=sub.size();} return c; }
std::string pad_left(const std::string& s, size_t n, char c) { return s.size()>=n?s:std::string(n-s.size(),c)+s; }
std::string pad_right(const std::string& s, size_t n, char c) { return s.size()>=n?s:s+std::string(n-s.size(),c); }
std::string repeat(const std::string& s, int n) { std::string r; for(int i=0;i<n;i++) r+=s; return r; }
std::string reverse(const std::string& s) { return std::string(s.rbegin(),s.rend()); }
bool is_alpha(const std::string& s) { return !s.empty()&&std::all_of(s.begin(),s.end(),::isalpha); }
bool is_digit(const std::string& s) { return !s.empty()&&std::all_of(s.begin(),s.end(),::isdigit); }
bool is_alnum(const std::string& s) { return !s.empty()&&std::all_of(s.begin(),s.end(),::isalnum); }
} // namespace