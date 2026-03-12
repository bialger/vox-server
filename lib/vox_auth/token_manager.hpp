#ifndef VOX_AUTH_TOKEN_MANAGER_HPP
#define VOX_AUTH_TOKEN_MANAGER_HPP

#include <string>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/session_repository.hpp"

namespace vox::auth {

struct TokenPair {
  std::string access_token;
  std::string refresh_token;
};

class TokenManager {
public:
  TokenManager(store::SessionRepository& sessions,
               common::Timestamp access_lifetime_seconds,
               common::Timestamp refresh_lifetime_seconds);

  common::Result<TokenPair> IssueTokens(const common::UserId& user_id,
                                        const common::DeviceId& device_id,
                                        common::Timestamp now);

  common::Result<store::SessionRecord> ValidateAccessToken(const std::string& access_token, common::Timestamp now);

  common::Result<TokenPair> RefreshTokens(const std::string& refresh_token,
                                          const common::DeviceId& device_id,
                                          common::Timestamp now);

  common::VoidResult RevokeSession(const std::string& session_id, common::Timestamp now);
  common::VoidResult RevokeAllForUser(const common::UserId& user_id, common::Timestamp now);

  static std::string HashToken(const std::string& token);

private:
  static std::string GenerateToken();

  store::SessionRepository& sessions_;
  common::Timestamp access_lifetime_seconds_;
  common::Timestamp refresh_lifetime_seconds_;
};

} // namespace vox::auth

#endif // VOX_AUTH_TOKEN_MANAGER_HPP
