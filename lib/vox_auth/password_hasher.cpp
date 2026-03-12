#include "lib/vox_auth/password_hasher.hpp"

#include <array>
#include <random>
#include <vector>

#include <argon2.h>
#include <fmt/format.h>

namespace vox::auth {

namespace {

constexpr std::size_t kSaltLength = 16;
constexpr std::size_t kHashLength = 32;
constexpr int kHexRadix = 16;
constexpr unsigned int kMaxUint8 = 255;

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

std::vector<std::uint8_t> GenerateRandomSalt() {
  std::vector<std::uint8_t> salt(kSaltLength);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<unsigned int> dist(0, kMaxUint8);
  for (auto& b : salt) {
    b = static_cast<std::uint8_t>(dist(gen));
  }
  return salt;
}

} // namespace

PasswordHasher::PasswordHasher(std::uint32_t time_cost, std::uint32_t memory_cost, std::uint32_t parallelism) :
    time_cost_(time_cost), memory_cost_(memory_cost), parallelism_(parallelism) {
}

common::Result<HashResult> PasswordHasher::Hash(const std::string& password_derived_value) {
  auto salt_bytes = GenerateRandomSalt();
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
    return std::unexpected(common::Error{common::ErrorCode::kInternal,
                                         fmt::format("Argon2 hash failed: {}", argon2_error_message(result))});
  }

  return HashResult{BytesToHex(salt_bytes), BytesToHex(hash_bytes)};
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
