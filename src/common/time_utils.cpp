#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <spdlog/spdlog.h>
namespace cppdesk::common {
std::string format_timestamp(int64_t us) { auto ms=us/1000; auto s=ms/1000; auto m=s/60; auto h=m/60; std::ostringstream o; o<<std::setfill('0')<<std::setw(2)<<(h%24)<<":"<<std::setw(2)<<(m%60)<<":"<<std::setw(2)<<(s%60)<<"."<<std::setw(3)<<(ms%1000); return o.str(); }
int64_t now_us() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int64_t now_ms() { return now_us()/1000; }
std::string iso8601_now() { auto t=std::chrono::system_clock::now(); auto tt=std::chrono::system_clock::to_time_t(t); std::ostringstream o; o<<std::put_time(std::gmtime(&tt),"%Y-%m-%dT%H:%M:%SZ"); return o.str(); }
std::string format_duration(int64_t us) { auto ms=us/1000; auto s=ms/1000; auto m=s/60; auto h=m/60; std::ostringstream o; if(h>0) o<<h<<"h "; if(m>0) o<<(m%60)<<"m "; o<<(s%60)<<"s"; return o.str(); }
double elapsed_seconds(int64_t start_us) { return (now_us()-start_us)/1e6; }
int64_t parse_duration(const std::string& s) { try{ return std::stoll(s); }catch(...){ return 0; } }
std::string weekday_name(int w) { static const char* d[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}; return d[w%7]; }
std::string month_name(int m) { static const char* n[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; return n[(m-1)%12]; }
} // namespace