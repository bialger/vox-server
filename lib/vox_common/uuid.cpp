#include "lib/vox_common/uuid.hpp"

#include <array>
#include <random>
#include <sstream>

#include <fmt/format.h>

namespace vox::common {

std::string GenerateUuid() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> dist;

  std::uint64_t high = dist(rng);
  std::uint64_t low = dist(rng);

  // UUID v4: set version (4) and variant (10xx)
  high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  return fmt::format(
      "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
      static_cast<std::uint32_t>(high >> 32),
      static_cast<std::uint16_t>((high >> 16) & 0xFFFF),
      static_cast<std::uint16_t>(high & 0xFFFF),
      static_cast<std::uint16_t>(low >> 48),
      low & 0x0000FFFFFFFFFFFFULL);
}

}  // namespace vox::common
