#ifndef VOX_STORE_USER_REPOSITORY_HPP
#define VOX_STORE_USER_REPOSITORY_HPP

#include <optional>
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
};

class UserRepository {
public:
  explicit UserRepository(Database& db);

  common::VoidResult CreateUser(const UserRecord& user);
  std::optional<UserRecord> FindByUsername(const std::string& username);
  std::optional<UserRecord> FindById(const common::UserId& user_id);
  common::VoidResult DisableUser(const common::UserId& user_id, common::Timestamp now);
  std::vector<UserRecord> ListUsers(std::size_t limit = 100, std::size_t offset = 0);

private:
  Database& db_;
};

} // namespace vox::store

#endif // VOX_STORE_USER_REPOSITORY_HPP
