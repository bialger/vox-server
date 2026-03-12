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

class AdminService {
 public:
  AdminService(store::Database& db,
               store::UserRepository& users,
               store::SessionRepository& sessions);

  ServerStats GetServerStats();
  common::VoidResult DeleteUser(const common::UserId& user_id);
  common::VoidResult ForceLogout(const common::UserId& user_id);

 private:
  common::Timestamp Now();

  store::Database& db_;
  store::UserRepository& users_;
  store::SessionRepository& sessions_;
};

}  // namespace vox::admin

#endif  // VOX_ADMIN_ADMIN_SERVICE_HPP
