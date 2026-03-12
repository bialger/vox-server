#include "lib/vox_common/logging.hpp"

#include <spdlog/spdlog.h>

namespace vox::common {

void InitLogging(const std::string& level) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
  spdlog::set_level(spdlog::level::from_str(level));
}

} // namespace vox::common
