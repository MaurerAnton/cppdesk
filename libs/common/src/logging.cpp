#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
namespace cppdesk::common {
void init_logging() {
  auto logger = spdlog::stdout_color_mt("cppdesk");
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::info);
}
} // namespace
