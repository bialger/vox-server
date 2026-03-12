#include "lib/vox_admin/admin_service.hpp"

#include <chrono>

#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>

namespace vox::admin {

AdminService::AdminService(store::Database& db, store::UserRepository& users, store::SessionRepository& sessions) :
    db_(db), users_(users), sessions_(sessions) {
}

ServerStats AdminService::GetServerStats() {
  ServerStats stats;

  auto get_count = [this](const std::string& table) -> std::size_t {
    SQLite::Statement stmt(db_.Connection(), "SELECT COUNT(*) FROM " + table);
    if (stmt.executeStep()) {
      return static_cast<std::size_t>(stmt.getColumn(0).getInt64());
    }
    return 0;
  };

  stats.user_count = get_count("users");
  stats.device_count = get_count("devices");
  stats.conversation_count = get_count("conversations");

  {
    auto now = Now();
    SQLite::Statement stmt(db_.Connection(),
                           "SELECT COUNT(*) FROM sessions WHERE revoked_at IS NULL AND access_expires_at > ?");
    stmt.bind(1, now);
    if (stmt.executeStep()) {
      stats.active_session_count = static_cast<std::size_t>(stmt.getColumn(0).getInt64());
    }
  }

  {
    SQLite::Statement stmt(db_.Connection(),
                           "SELECT COUNT(*) FROM delivery_state WHERE delivered_at IS NULL AND acked_at IS NULL");
    if (stmt.executeStep()) {
      stats.pending_envelope_count = static_cast<std::size_t>(stmt.getColumn(0).getInt64());
    }
  }

  {
    SQLite::Statement stmt(db_.Connection(),
                           "SELECT COALESCE(SUM(file_size), 0) FROM attachment_metadata WHERE upload_complete = 1");
    if (stmt.executeStep()) {
      stats.total_storage_bytes = stmt.getColumn(0).getInt64();
    }
  }

  return stats;
}

common::VoidResult AdminService::DeleteUser(const common::UserId& user_id) {
  auto user = users_.FindById(user_id);
  if (!user) {
    return std::unexpected(common::Error{common::ErrorCode::kNotFound, "User not found"});
  }

  auto now = Now();
  auto lock = db_.WriteLock();

  try {
    SQLite::Transaction txn(db_.Connection());

    db_.Connection().exec(
        "DELETE FROM delivery_state WHERE target_device_id IN "
        "(SELECT device_id FROM devices WHERE user_id = '" +
        user_id + "')");

    db_.Connection().exec(
        "DELETE FROM delivery_state WHERE envelope_id IN "
        "(SELECT envelope_id FROM encrypted_envelopes WHERE sender_device_id IN "
        "(SELECT device_id FROM devices WHERE user_id = '" +
        user_id + "'))");

    db_.Connection().exec(
        "DELETE FROM encrypted_envelopes WHERE sender_device_id IN "
        "(SELECT device_id FROM devices WHERE user_id = '" +
        user_id + "')");

    db_.Connection().exec("DELETE FROM attachment_metadata WHERE user_id = '" + user_id + "'");

    db_.Connection().exec("DELETE FROM channel_subscriptions WHERE user_id = '" + user_id + "'");

    db_.Connection().exec("DELETE FROM conversation_members WHERE user_id = '" + user_id + "'");

    db_.Connection().exec("DELETE FROM sessions WHERE user_id = '" + user_id + "'");

    db_.Connection().exec(
        "DELETE FROM one_time_prekeys WHERE device_id IN "
        "(SELECT device_id FROM devices WHERE user_id = '" +
        user_id + "')");

    db_.Connection().exec("DELETE FROM devices WHERE user_id = '" + user_id + "'");

    db_.Connection().exec("DELETE FROM users WHERE user_id = '" + user_id + "'");

    txn.commit();
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{common::ErrorCode::kInternal, e.what()});
  }

  spdlog::info("User deleted: {} ({})", user->username, user_id);
  return {};
}

common::VoidResult AdminService::ForceLogout(const common::UserId& user_id) {
  auto user = users_.FindById(user_id);
  if (!user) {
    return std::unexpected(common::Error{common::ErrorCode::kNotFound, "User not found"});
  }
  auto now = Now();
  auto revoke_result = sessions_.RevokeAllForUser(user_id, now);
  if (!revoke_result) {
    return revoke_result;
  }
  spdlog::info("Forced logout for user: {} ({})", user->username, user_id);
  return {};
}

common::Timestamp AdminService::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace vox::admin
