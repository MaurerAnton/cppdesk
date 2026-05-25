#include "common/config.hpp"
#include <stdexcept>
namespace cppdesk::common {
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
} // namespace
