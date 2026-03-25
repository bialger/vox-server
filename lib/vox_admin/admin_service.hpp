#ifndef VOX_ADMIN_ADMIN_SERVICE_HPP
#define VOX_ADMIN_ADMIN_SERVICE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/session_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

namespace vox::admin {

struct ServerStats {
  std::size_t user_count = 0;
  std::size_t device_count = 0;
  std::size_t active_session_count = 0;
  std::size_t conversation_count = 0;
  std::size_t pending_envelope_count = 0;
  std::int64_t total_storage_bytes = 0;
};

class IAdminService {
public:
  virtual ~IAdminService() = default;
  virtual ServerStats GetServerStats() = 0;
  virtual common::VoidResult DeleteUser(const common::UserId& user_id) = 0;
  virtual common::VoidResult ForceLogout(const common::UserId& user_id) = 0;
};

class AdminService : public IAdminService {
public:
  AdminService(store::IDatabase& db, store::IUserRepository& users, store::ISessionRepository& sessions);

  ServerStats GetServerStats() override;
  common::VoidResult DeleteUser(const common::UserId& user_id) override;
  common::VoidResult ForceLogout(const common::UserId& user_id) override;

private:
  common::Timestamp Now();

  store::IDatabase& db_;
  store::IUserRepository& users_;
  store::ISessionRepository& sessions_;
};

} // namespace vox::admin

#endif // VOX_ADMIN_ADMIN_SERVICE_HPP
