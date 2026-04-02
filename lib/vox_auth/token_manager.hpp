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

struct RefreshResult {
  TokenPair tokens;
  common::UserId user_id;
};

class ITokenManager {
public:
  virtual ~ITokenManager() = default;
  virtual common::Result<TokenPair> IssueTokens(const common::UserId& user_id,
                                                const common::DeviceId& device_id,
                                                common::Timestamp now) = 0;

  virtual common::Result<store::SessionRecord> ValidateAccessToken(const std::string& access_token,
                                                                   common::Timestamp now) = 0;

  virtual common::Result<RefreshResult> RefreshTokens(const std::string& refresh_token,
                                                      const common::DeviceId& device_id,
                                                      common::Timestamp now) = 0;

  virtual common::VoidResult RevokeSession(const std::string& session_id, common::Timestamp now) = 0;
  virtual common::VoidResult RevokeByAccessToken(const std::string& access_token, common::Timestamp now) = 0;
  virtual common::VoidResult RevokeAllForUser(const common::UserId& user_id, common::Timestamp now) = 0;
  virtual common::VoidResult RevokeAllForDevice(const common::UserId& user_id,
                                                const common::DeviceId& device_id,
                                                common::Timestamp now) = 0;
};

class TokenManager : public ITokenManager {
public:
  TokenManager(store::ISessionRepository& sessions,
               common::Timestamp access_lifetime_seconds,
               common::Timestamp refresh_lifetime_seconds,
               std::string session_token_pepper);

  common::Result<TokenPair> IssueTokens(const common::UserId& user_id,
                                        const common::DeviceId& device_id,
                                        common::Timestamp now) override;

  common::Result<store::SessionRecord> ValidateAccessToken(const std::string& access_token,
                                                           common::Timestamp now) override;

  common::Result<RefreshResult> RefreshTokens(const std::string& refresh_token,
                                              const common::DeviceId& device_id,
                                              common::Timestamp now) override;

  common::VoidResult RevokeSession(const std::string& session_id, common::Timestamp now) override;
  common::VoidResult RevokeByAccessToken(const std::string& access_token, common::Timestamp now) override;
  common::VoidResult RevokeAllForUser(const common::UserId& user_id, common::Timestamp now) override;
  common::VoidResult RevokeAllForDevice(const common::UserId& user_id,
                                        const common::DeviceId& device_id,
                                        common::Timestamp now) override;

  [[nodiscard]] std::string HashToken(const std::string& token) const;

private:
  static std::string GenerateToken();

  store::ISessionRepository& sessions_;
  common::Timestamp access_lifetime_seconds_;
  common::Timestamp refresh_lifetime_seconds_;
  std::string session_token_pepper_;
};

} // namespace vox::auth

#endif // VOX_AUTH_TOKEN_MANAGER_HPP
