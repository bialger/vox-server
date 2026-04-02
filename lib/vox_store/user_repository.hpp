#ifndef VOX_STORE_USER_REPOSITORY_HPP
#define VOX_STORE_USER_REPOSITORY_HPP

#include <optional>
#include <string>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct UserRecord {
  common::UserId user_id;
  std::string username;
  std::string password_salt;
  std::string password_verifier;
  common::Timestamp created_at;
  std::optional<common::Timestamp> disabled_at;
  int sync_key_version = 0;
  std::string wrapped_sync_key;
  std::string sync_wrap_salt;
  std::string sync_wrap_params;
};

struct SyncKeyBundleRecord {
  int sync_key_version = 0;
  std::string wrapped_sync_key;
  std::string sync_wrap_salt;
  std::string sync_wrap_params;
};

class IUserRepository {
public:
  virtual ~IUserRepository() = default;
  virtual common::VoidResult CreateUser(const UserRecord& user) = 0;
  virtual std::optional<UserRecord> FindByUsername(const std::string& username) = 0;
  virtual std::optional<UserRecord> FindById(const common::UserId& user_id) = 0;
  virtual common::VoidResult DisableUser(const common::UserId& user_id, common::Timestamp now) = 0;
  virtual std::vector<UserRecord> ListUsers(std::size_t limit = 100, std::size_t offset = 0) = 0;

  /// Prefix search on username (`query%`), case-insensitive. Excludes disabled users.
  virtual std::vector<UserRecord> SearchByUsernamePrefix(const std::string& query, std::size_t limit) = 0;

  virtual std::optional<SyncKeyBundleRecord> GetSyncKeyBundle(const common::UserId& user_id) = 0;
  virtual common::VoidResult SetSyncKeyBundle(const common::UserId& user_id, const SyncKeyBundleRecord& bundle) = 0;
  virtual common::VoidResult UpdatePasswordCredentials(const common::UserId& user_id,
                                                       const std::string& password_salt,
                                                       const std::string& password_verifier) = 0;
};

class UserRepository : public IUserRepository {
public:
  explicit UserRepository(IDatabase& db);

  common::VoidResult CreateUser(const UserRecord& user) override;
  std::optional<UserRecord> FindByUsername(const std::string& username) override;
  std::optional<UserRecord> FindById(const common::UserId& user_id) override;
  common::VoidResult DisableUser(const common::UserId& user_id, common::Timestamp now) override;
  std::vector<UserRecord> ListUsers(std::size_t limit = 100, std::size_t offset = 0) override;

  std::vector<UserRecord> SearchByUsernamePrefix(const std::string& query, std::size_t limit) override;

  std::optional<SyncKeyBundleRecord> GetSyncKeyBundle(const common::UserId& user_id) override;
  common::VoidResult SetSyncKeyBundle(const common::UserId& user_id, const SyncKeyBundleRecord& bundle) override;
  common::VoidResult UpdatePasswordCredentials(const common::UserId& user_id,
                                               const std::string& password_salt,
                                               const std::string& password_verifier) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_USER_REPOSITORY_HPP
