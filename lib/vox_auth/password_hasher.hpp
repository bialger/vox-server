#ifndef VOX_AUTH_PASSWORD_HASHER_HPP
#define VOX_AUTH_PASSWORD_HASHER_HPP

#include <string>

#include "lib/vox_common/types.hpp"

namespace vox::auth {

struct HashResult {
  std::string salt;
  std::string verifier;
};

class PasswordHasher {
 public:
  PasswordHasher(std::uint32_t time_cost = 3, std::uint32_t memory_cost = 65536, std::uint32_t parallelism = 1);

  common::Result<HashResult> Hash(const std::string& password_derived_value);
  bool Verify(const std::string& password_derived_value, const std::string& salt, const std::string& verifier);

 private:
  std::uint32_t time_cost_;
  std::uint32_t memory_cost_;
  std::uint32_t parallelism_;
};

}  // namespace vox::auth

#endif  // VOX_AUTH_PASSWORD_HASHER_HPP
