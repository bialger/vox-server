#include "lib/vox_common/uuid.hpp"

#include <array>
#include <random>
#include <sstream>

#include <fmt/format.h>

namespace vox::common {

namespace {

// UUID v4 bit masks: clear version nibble, set version=4, clear variant bits, set variant=10xx
constexpr std::uint64_t kHighVersionMask = 0xFFFFFFFFFFFF0FFFULL;
constexpr std::uint64_t kHighVersionValue = 0x0000000000004000ULL;
constexpr std::uint64_t kLowVariantMask = 0x3FFFFFFFFFFFFFFFULL;
constexpr std::uint64_t kLowVariantValue = 0x8000000000000000ULL;
constexpr std::uint64_t kLowNodeMask = 0x0000FFFFFFFFFFFFULL;
constexpr unsigned int kHighMsbShift = 32;
constexpr unsigned int kHighMidShift = 16;
constexpr unsigned int kLowMsbShift = 48;
constexpr unsigned int kNibbleMask = 0xFFFF;

} // namespace

std::string GenerateUuid() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> dist;

  std::uint64_t high = dist(rng);
  std::uint64_t low = dist(rng);

  // UUID v4: set version (4) and variant (10xx)
  high = (high & kHighVersionMask) | kHighVersionValue;
  low = (low & kLowVariantMask) | kLowVariantValue;

  return fmt::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
                     static_cast<std::uint32_t>(high >> kHighMsbShift),
                     static_cast<std::uint16_t>((high >> kHighMidShift) & kNibbleMask),
                     static_cast<std::uint16_t>(high & kNibbleMask),
                     static_cast<std::uint16_t>(low >> kLowMsbShift),
                     low & kLowNodeMask);
}

} // namespace vox::common
