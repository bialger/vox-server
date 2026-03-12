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

class SessionRepository {
 public:
  explicit SessionRepository(Database& db);

  common::VoidResult CreateSession(const SessionRecord& session);
  std::optional<SessionRecord> FindByAccessToken(const std::string& access_token_hash);
  std::optional<SessionRecord> FindByRefreshToken(const std::string& refresh_token_hash);
  common::VoidResult RevokeSession(const std::string& session_id, common::Timestamp now);
  common::VoidResult RevokeAllForUser(const common::UserId& user_id, common::Timestamp now);
  int CleanExpired(common::Timestamp now);
  std::size_t CountActiveForUser(const common::UserId& user_id, common::Timestamp now);

 private:
  Database& db_;
};

}  // namespace vox::store

#endif  // VOX_STORE_SESSION_REPOSITORY_HPP
