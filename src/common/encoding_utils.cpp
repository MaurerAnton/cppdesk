#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <spdlog/spdlog.h>
namespace cppdesk::common {
std::string url_encode(const std::string& s) { std::ostringstream o; for(unsigned char c:s){ if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') o<<c; else o<<'%'<<std::hex<<std::uppercase<<std::setw(2)<<std::setfill('0')<<(int)c; } return o.str(); }
std::string url_decode(const std::string& s) { std::string r; for(size_t i=0;i<s.size();i++){ if(s[i]=='%'&&i+2<s.size()){ int v; std::istringstream(s.substr(i+1,2))>>std::hex>>v; r+=(char)v; i+=2; } else if(s[i]=='+') r+=' '; else r+=s[i]; } return r; }
std::string hex_encode(const uint8_t* d, size_t n) { static const char h[]="0123456789abcdef"; std::string r; r.reserve(n*2); for(size_t i=0;i<n;i++){r+=h[d[i]>>4];r+=h[d[i]&0xF];} return r; }
std::vector<uint8_t> hex_decode(const std::string& s) { std::vector<uint8_t> r; for(size_t i=0;i+1<s.size();i+=2){ char hi=s[i],lo=s[i+1]; uint8_t v=0; if(hi>='0'&&hi<='9') v=(hi-'0')<<4; else if(hi>='a'&&hi<='f') v=(hi-'a'+10)<<4; else if(hi>='A'&&hi<='F') v=(hi-'A'+10)<<4; if(lo>='0'&&lo<='9') v|=(lo-'0'); else if(lo>='a'&&lo<='f') v|=(lo-'a'+10); else if(lo>='A'&&lo<='F') v|=(lo-'A'+10); r.push_back(v); } return r; }
std::string html_encode(const std::string& s) { std::string r; for(char c:s){ switch(c){ case '&':r+="&amp;";break; case '<':r+="&lt;";break; case '>':r+="&gt;";break; case '"':r+="&quot;";break; case '\'':r+="&#39;";break; default:r+=c; } } return r; }
std::string html_decode(const std::string& s) { std::string r=s; size_t p; while((p=r.find("&amp;"))!=std::string::npos) r.replace(p,5,"&"); while((p=r.find("&lt;"))!=std::string::npos) r.replace(p,4,"<"); while((p=r.find("&gt;"))!=std::string::npos) r.replace(p,4,">"); while((p=r.find("&quot;"))!=std::string::npos) r.replace(p,6,"\""); while((p=r.find("&#39;"))!=std::string::npos) r.replace(p,5,"'"); return r; }
std::string json_escape(const std::string& s) { std::string r; for(char c:s){ switch(c){ case '"':r+="\\\"";break; case '\\':r+="\\\\";break; case '\b':r+="\\b";break; case '\f':r+="\\f";break; case '\n':r+="\\n";break; case '\r':r+="\\r";break; case '\t':r+="\\t";break; default:r+=c; } } return r; }
} // namespace