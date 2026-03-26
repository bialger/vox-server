#include "lib/vox_common/hmac_sha256.hpp"

#include <array>
#include <cstring>

#include "lib/vox_common/picosha2.h"

namespace vox::common {

namespace {

constexpr std::size_t kBlock = 64;
// HMAC-SHA256 inner/outer pad XOR bytes (FIPS 198-1).
constexpr unsigned char kHmacIpadXor = 0x36;
constexpr unsigned char kHmacOpadXor = 0x5c;

void Sha256Raw(std::string_view data, std::array<picosha2::byte_t, picosha2::k_digest_size>& out) {
  picosha2::hash256(data.begin(), data.end(), out.begin(), out.end());
}

} // namespace

std::string HmacSha256Hex(std::string_view key, std::string_view message) {
  std::array<picosha2::byte_t, kBlock> key_block{};
  if (key.size() > kBlock) {
    std::array<picosha2::byte_t, picosha2::k_digest_size> kh{};
    Sha256Raw(key, kh);
    std::memcpy(key_block.data(), kh.data(), kh.size());
  } else {
    std::memcpy(key_block.data(), key.data(), key.size());
  }

  std::array<picosha2::byte_t, kBlock> ipad{};
  std::array<picosha2::byte_t, kBlock> opad{};
  const auto* const kbytes = key_block.data();
  auto* const ibytes = ipad.data();
  auto* const obytes = opad.data();
  for (std::size_t i = 0; i < kBlock; ++i) {
    const auto kb = static_cast<unsigned char>(*(kbytes + i));
    *(ibytes + i) = static_cast<picosha2::byte_t>(kb ^ kHmacIpadXor);
    *(obytes + i) = static_cast<picosha2::byte_t>(kb ^ kHmacOpadXor);
  }

  std::string inner;
  inner.assign(reinterpret_cast<const char*>(ipad.data()), kBlock);
  inner.append(message);

  std::array<picosha2::byte_t, picosha2::k_digest_size> inner_hash{};
  Sha256Raw(inner, inner_hash);

  std::string outer;
  outer.assign(reinterpret_cast<const char*>(opad.data()), kBlock);
  outer.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size());

  std::array<picosha2::byte_t, picosha2::k_digest_size> outer_hash{};
  Sha256Raw(outer, outer_hash);

  return picosha2::bytes_to_hex_string(outer_hash.begin(), outer_hash.end());
}

} // namespace vox::common
