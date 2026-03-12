#include "lib/vox_store/user_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

constexpr int kCreatedAtParam = 5;

} // namespace

UserRepository::UserRepository(Database& db) : db_(db) {
}

common::VoidResult UserRepository::CreateUser(const UserRecord& user) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO users (user_id, username, password_salt, password_verifier, created_at) "
                           "VALUES (?, ?, ?, ?, ?)");
    stmt.bind(1, user.user_id);
    stmt.bind(2, user.username);
    stmt.bind(3, user.password_salt);
    stmt.bind(4, user.password_verifier);
    stmt.bind(kCreatedAtParam, user.created_at);
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
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM users WHERE username = ?");
  stmt.bind(1, username);
  if (stmt.executeStep()) {
    UserRecord record;
    record.user_id = stmt.getColumn("user_id").getString();
    record.username = stmt.getColumn("username").getString();
    record.password_salt = stmt.getColumn("password_salt").getString();
    record.password_verifier = stmt.getColumn("password_verifier").getString();
    record.created_at = stmt.getColumn("created_at").getInt64();
    if (!stmt.getColumn("disabled_at").isNull()) {
      record.disabled_at = stmt.getColumn("disabled_at").getInt64();
    }
    return record;
  }
  return std::nullopt;
}

std::optional<UserRecord> UserRepository::FindById(const common::UserId& user_id) {
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM users WHERE user_id = ?");
  stmt.bind(1, user_id);
  if (stmt.executeStep()) {
    UserRecord record;
    record.user_id = stmt.getColumn("user_id").getString();
    record.username = stmt.getColumn("username").getString();
    record.password_salt = stmt.getColumn("password_salt").getString();
    record.password_verifier = stmt.getColumn("password_verifier").getString();
    record.created_at = stmt.getColumn("created_at").getInt64();
    if (!stmt.getColumn("disabled_at").isNull()) {
      record.disabled_at = stmt.getColumn("disabled_at").getInt64();
    }
    return record;
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
  std::vector<UserRecord> result;
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM users ORDER BY created_at DESC LIMIT ? OFFSET ?");
  stmt.bind(1, static_cast<std::int64_t>(limit));
  stmt.bind(2, static_cast<std::int64_t>(offset));
  while (stmt.executeStep()) {
    UserRecord record;
    record.user_id = stmt.getColumn("user_id").getString();
    record.username = stmt.getColumn("username").getString();
    record.password_salt = stmt.getColumn("password_salt").getString();
    record.password_verifier = stmt.getColumn("password_verifier").getString();
    record.created_at = stmt.getColumn("created_at").getInt64();
    if (!stmt.getColumn("disabled_at").isNull()) {
      record.disabled_at = stmt.getColumn("disabled_at").getInt64();
    }
    result.push_back(std::move(record));
  }
  return result;
}

} // namespace vox::store
