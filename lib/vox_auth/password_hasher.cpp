#include "lib/vox_auth/password_hasher.hpp"

#include <span>
#include <vector>

#include <argon2.h>
#include <fmt/format.h>

#include "lib/vox_common/random_bytes.hpp"

namespace vox::auth {

namespace {

constexpr std::size_t kSaltLength = 16;
constexpr std::size_t kHashLength = 32;
constexpr int kHexRadix = 16;

std::string BytesToHex(const std::vector<std::uint8_t>& bytes) {
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (auto b : bytes) {
    hex += fmt::format("{:02x}", b);
  }
  return hex;
}

std::vector<std::uint8_t> HexToBytes(const std::string& hex) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, kHexRadix)));
  }
  return bytes;
}

common::Result<std::vector<std::uint8_t>> GenerateRandomSalt() {
  std::vector<std::uint8_t> salt(kSaltLength);
  if (!vox::common::FillRandomBytes(std::span(salt.data(), salt.size()))) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInternal, .message = "Failed to generate random salt"});
  }
  return salt;
}

} // namespace

PasswordHasher::PasswordHasher(std::uint32_t time_cost, std::uint32_t memory_cost, std::uint32_t parallelism) :
    time_cost_(time_cost), memory_cost_(memory_cost), parallelism_(parallelism) {
}

common::Result<HashResult> PasswordHasher::Hash(const std::string& password_derived_value) {
  auto salt_result = GenerateRandomSalt();
  if (!salt_result) {
    return std::unexpected(salt_result.error());
  }
  auto salt_bytes = std::move(*salt_result);
  std::vector<std::uint8_t> hash_bytes(kHashLength);

  int result = argon2id_hash_raw(time_cost_,
                                 memory_cost_,
                                 parallelism_,
                                 password_derived_value.data(),
                                 password_derived_value.size(),
                                 salt_bytes.data(),
                                 salt_bytes.size(),
                                 hash_bytes.data(),
                                 hash_bytes.size());

  if (result != ARGON2_OK) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInternal,
                      .message = fmt::format("Argon2 hash failed: {}", argon2_error_message(result))});
  }

  return HashResult{.salt = BytesToHex(salt_bytes), .verifier = BytesToHex(hash_bytes)};
}

bool PasswordHasher::Verify(const std::string& password_derived_value,
                            const std::string& salt,
                            const std::string& verifier) {
  auto salt_bytes = HexToBytes(salt);
  std::vector<std::uint8_t> hash_bytes(kHashLength);

  int result = argon2id_hash_raw(time_cost_,
                                 memory_cost_,
                                 parallelism_,
                                 password_derived_value.data(),
                                 password_derived_value.size(),
                                 salt_bytes.data(),
                                 salt_bytes.size(),
                                 hash_bytes.data(),
                                 hash_bytes.size());

  if (result != ARGON2_OK) {
    return false;
  }

  return BytesToHex(hash_bytes) == verifier;
}

} // namespace vox::auth
