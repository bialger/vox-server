#ifndef VOX_STORE_SESSION_REPOSITORY_HPP
#define VOX_STORE_SESSION_REPOSITORY_HPP

#include <optional>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct SessionRecord {
  std::string session_id;
  common::UserId user_id;
  common::DeviceId device_id;
  std::string access_token_hash;
  std::string refresh_token_hash;
  common::Timestamp access_expires_at;
  common::Timestamp refresh_expires_at;
  common::Timestamp created_at;
  std::optional<common::Timestamp> revoked_at;
};

class ISessionRepository {
public:
  virtual ~ISessionRepository() = default;
  virtual common::VoidResult CreateSession(const SessionRecord& session) = 0;
  virtual std::optional<SessionRecord> FindByAccessToken(const std::string& access_token_hash) = 0;
  virtual std::optional<SessionRecord> FindByRefreshToken(const std::string& refresh_token_hash) = 0;
  virtual common::VoidResult RevokeSession(const std::string& session_id, common::Timestamp now) = 0;
  virtual common::VoidResult RevokeAllForUser(const common::UserId& user_id, common::Timestamp now) = 0;
  virtual common::VoidResult RevokeAllForDevice(const common::UserId& user_id,
                                                const common::DeviceId& device_id,
                                                common::Timestamp now) = 0;
  virtual int CleanExpired(common::Timestamp now) = 0;
  virtual std::size_t CountActiveForUser(const common::UserId& user_id, common::Timestamp now) = 0;
};

class SessionRepository : public ISessionRepository {
public:
  explicit SessionRepository(IDatabase& db);

  common::VoidResult CreateSession(const SessionRecord& session) override;
  std::optional<SessionRecord> FindByAccessToken(const std::string& access_token_hash) override;
  std::optional<SessionRecord> FindByRefreshToken(const std::string& refresh_token_hash) override;
  common::VoidResult RevokeSession(const std::string& session_id, common::Timestamp now) override;
  common::VoidResult RevokeAllForUser(const common::UserId& user_id, common::Timestamp now) override;
  common::VoidResult RevokeAllForDevice(const common::UserId& user_id,
                                        const common::DeviceId& device_id,
                                        common::Timestamp now) override;
  int CleanExpired(common::Timestamp now) override;
  std::size_t CountActiveForUser(const common::UserId& user_id, common::Timestamp now) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_SESSION_REPOSITORY_HPP
