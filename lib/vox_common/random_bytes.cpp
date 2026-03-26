#include "lib/vox_common/random_bytes.hpp"

#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// NOLINTNEXTLINE(misc-include-cleaner): Windows SDK
#include <bcrypt.h>
#endif

namespace vox::common {

bool FillRandomBytes(std::span<std::uint8_t> out) {
  if (out.empty()) {
    return true;
  }
#ifdef _WIN32
  const auto status =
      BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return BCRYPT_SUCCESS(status);
#else
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (!urandom) {
    return false;
  }
  urandom.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  return static_cast<std::size_t>(urandom.gcount()) == out.size();
#endif
}

} // namespace vox::common
