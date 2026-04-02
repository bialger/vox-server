#include "lib/vox_store/user_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

constexpr int kCreateBindCreatedAt = 5;
constexpr int kCreateBindSyncKeyVersion = 6;
constexpr int kCreateBindWrappedSyncKey = 7;
constexpr int kCreateBindSyncWrapSalt = 8;
constexpr int kCreateBindSyncWrapParams = 9;
constexpr int kSetSyncBindUserId = 5;

UserRecord RowToUser(SQLite::Statement& stmt) {
  UserRecord record;
  record.user_id = stmt.getColumn("user_id").getString();
  record.username = stmt.getColumn("username").getString();
  record.password_salt = stmt.getColumn("password_salt").getString();
  record.password_verifier = stmt.getColumn("password_verifier").getString();
  record.created_at = stmt.getColumn("created_at").getInt64();
  if (!stmt.getColumn("disabled_at").isNull()) {
    record.disabled_at = stmt.getColumn("disabled_at").getInt64();
  }
  record.sync_key_version = stmt.getColumn("sync_key_version").getInt();
  record.wrapped_sync_key = stmt.getColumn("wrapped_sync_key").getString();
  record.sync_wrap_salt = stmt.getColumn("sync_wrap_salt").getString();
  record.sync_wrap_params = stmt.getColumn("sync_wrap_params").getString();
  return record;
}

} // namespace

UserRepository::UserRepository(IDatabase& db) : db_(db) {
}

common::VoidResult UserRepository::CreateUser(const UserRecord& user) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO users (user_id, username, password_salt, password_verifier, created_at, "
                           "sync_key_version, wrapped_sync_key, sync_wrap_salt, sync_wrap_params) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, user.user_id);
    stmt.bind(2, user.username);
    stmt.bind(3, user.password_salt);
    stmt.bind(4, user.password_verifier);
    stmt.bind(kCreateBindCreatedAt, user.created_at);
    stmt.bind(kCreateBindSyncKeyVersion, user.sync_key_version);
    stmt.bind(kCreateBindWrappedSyncKey, user.wrapped_sync_key);
    stmt.bind(kCreateBindSyncWrapSalt, user.sync_wrap_salt);
    stmt.bind(kCreateBindSyncWrapParams, user.sync_wrap_params);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    if (e.getErrorCode() == SQLITE_CONSTRAINT) {
      return std::unexpected(
          common::Error{.code = common::ErrorCode::kAlreadyExists, .message = "Username already taken"});
    }
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

std::optional<UserRecord> UserRepository::FindByUsername(const std::string& username) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM users WHERE username = ?");
  stmt.bind(1, username);
  if (stmt.executeStep()) {
    return RowToUser(stmt);
  }
  return std::nullopt;
}

std::optional<UserRecord> UserRepository::FindById(const common::UserId& user_id) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM users WHERE user_id = ?");
  stmt.bind(1, user_id);
  if (stmt.executeStep()) {
    return RowToUser(stmt);
  }
  return std::nullopt;
}

common::VoidResult UserRepository::DisableUser(const common::UserId& user_id, common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(), "UPDATE users SET disabled_at = ? WHERE user_id = ?");
  stmt.bind(1, now);
  stmt.bind(2, user_id);
  int rows = stmt.exec();
  if (rows == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "User not found"});
  }
  return {};
}

std::vector<UserRecord> UserRepository::ListUsers(std::size_t limit, std::size_t offset) {
  auto lock = db_.ReadLock();
  std::vector<UserRecord> result;
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM users ORDER BY created_at DESC LIMIT ? OFFSET ?");
  stmt.bind(1, static_cast<std::int64_t>(limit));
  stmt.bind(2, static_cast<std::int64_t>(offset));
  while (stmt.executeStep()) {
    result.push_back(RowToUser(stmt));
  }
  return result;
}

std::vector<UserRecord> UserRepository::SearchByUsernamePrefix(const std::string& query, std::size_t limit) {
  auto lock = db_.ReadLock();
  std::vector<UserRecord> result;
  if (query.empty()) {
    return result;
  }
  std::string pattern = query + "%";
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT * FROM users WHERE username LIKE ? COLLATE NOCASE AND disabled_at IS NULL "
                         "ORDER BY username ASC LIMIT ?");
  stmt.bind(1, pattern);
  stmt.bind(2, static_cast<std::int64_t>(limit));
  while (stmt.executeStep()) {
    result.push_back(RowToUser(stmt));
  }
  return result;
}

std::optional<SyncKeyBundleRecord> UserRepository::GetSyncKeyBundle(const common::UserId& user_id) {
  auto u = FindById(user_id);
  if (!u) {
    return std::nullopt;
  }
  SyncKeyBundleRecord r;
  r.sync_key_version = u->sync_key_version;
  r.wrapped_sync_key = u->wrapped_sync_key;
  r.sync_wrap_salt = u->sync_wrap_salt;
  r.sync_wrap_params = u->sync_wrap_params;
  return r;
}

common::VoidResult UserRepository::SetSyncKeyBundle(const common::UserId& user_id, const SyncKeyBundleRecord& bundle) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE users SET sync_key_version = ?, wrapped_sync_key = ?, sync_wrap_salt = ?, "
                         "sync_wrap_params = ? WHERE user_id = ?");
  stmt.bind(1, bundle.sync_key_version);
  stmt.bind(2, bundle.wrapped_sync_key);
  stmt.bind(3, bundle.sync_wrap_salt);
  stmt.bind(4, bundle.sync_wrap_params);
  stmt.bind(kSetSyncBindUserId, user_id);
  if (stmt.exec() == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "User not found"});
  }
  return {};
}

common::VoidResult UserRepository::UpdatePasswordCredentials(const common::UserId& user_id,
                                                             const std::string& password_salt,
                                                             const std::string& password_verifier) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE users SET password_salt = ?, password_verifier = ? WHERE user_id = ?");
  stmt.bind(1, password_salt);
  stmt.bind(2, password_verifier);
  stmt.bind(3, user_id);
  if (stmt.exec() == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "User not found"});
  }
  return {};
}

} // namespace vox::store
