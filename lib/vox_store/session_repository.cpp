#include "lib/vox_store/session_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

namespace vox::store {

namespace {

constexpr int kRefreshTokenHashParam = 5;
constexpr int kAccessExpiresParam = 6;
constexpr int kRefreshExpiresParam = 7;
constexpr int kCreatedAtParam = 8;

} // namespace

SessionRepository::SessionRepository(Database& db) : db_(db) {
}

common::VoidResult SessionRepository::CreateSession(const SessionRecord& session) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(
        db_.Connection(),
        "INSERT INTO sessions (session_id, user_id, device_id, access_token_hash, refresh_token_hash, "
        "access_expires_at, refresh_expires_at, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, session.session_id);
    stmt.bind(2, session.user_id);
    stmt.bind(3, session.device_id);
    stmt.bind(4, session.access_token_hash);
    stmt.bind(kRefreshTokenHashParam, session.refresh_token_hash);
    stmt.bind(kAccessExpiresParam, session.access_expires_at);
    stmt.bind(kRefreshExpiresParam, session.refresh_expires_at);
    stmt.bind(kCreatedAtParam, session.created_at);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

std::optional<SessionRecord> SessionRepository::FindByAccessToken(const std::string& access_token_hash) {
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM sessions WHERE access_token_hash = ? AND revoked_at IS NULL");
  stmt.bind(1, access_token_hash);
  if (stmt.executeStep()) {
    SessionRecord rec;
    rec.session_id = stmt.getColumn("session_id").getString();
    rec.user_id = stmt.getColumn("user_id").getString();
    rec.device_id = stmt.getColumn("device_id").getString();
    rec.access_token_hash = stmt.getColumn("access_token_hash").getString();
    rec.refresh_token_hash = stmt.getColumn("refresh_token_hash").getString();
    rec.access_expires_at = stmt.getColumn("access_expires_at").getInt64();
    rec.refresh_expires_at = stmt.getColumn("refresh_expires_at").getInt64();
    rec.created_at = stmt.getColumn("created_at").getInt64();
    if (!stmt.getColumn("revoked_at").isNull()) {
      rec.revoked_at = stmt.getColumn("revoked_at").getInt64();
    }
    return rec;
  }
  return std::nullopt;
}

std::optional<SessionRecord> SessionRepository::FindByRefreshToken(const std::string& refresh_token_hash) {
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT * FROM sessions WHERE refresh_token_hash = ? AND revoked_at IS NULL");
  stmt.bind(1, refresh_token_hash);
  if (stmt.executeStep()) {
    SessionRecord rec;
    rec.session_id = stmt.getColumn("session_id").getString();
    rec.user_id = stmt.getColumn("user_id").getString();
    rec.device_id = stmt.getColumn("device_id").getString();
    rec.access_token_hash = stmt.getColumn("access_token_hash").getString();
    rec.refresh_token_hash = stmt.getColumn("refresh_token_hash").getString();
    rec.access_expires_at = stmt.getColumn("access_expires_at").getInt64();
    rec.refresh_expires_at = stmt.getColumn("refresh_expires_at").getInt64();
    rec.created_at = stmt.getColumn("created_at").getInt64();
    if (!stmt.getColumn("revoked_at").isNull()) {
      rec.revoked_at = stmt.getColumn("revoked_at").getInt64();
    }
    return rec;
  }
  return std::nullopt;
}

common::VoidResult SessionRepository::RevokeSession(const std::string& session_id, common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE sessions SET revoked_at = ? WHERE session_id = ? AND revoked_at IS NULL");
  stmt.bind(1, now);
  stmt.bind(2, session_id);
  int rows = stmt.exec();
  if (rows == 0) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kNotFound, .message = "Session not found or already revoked"});
  }
  return {};
}

common::VoidResult SessionRepository::RevokeAllForUser(const common::UserId& user_id, common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE sessions SET revoked_at = ? WHERE user_id = ? AND revoked_at IS NULL");
  stmt.bind(1, now);
  stmt.bind(2, user_id);
  stmt.exec();
  return {};
}

int SessionRepository::CleanExpired(common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "DELETE FROM sessions WHERE refresh_expires_at < ? OR revoked_at IS NOT NULL");
  stmt.bind(1, now);
  return stmt.exec();
}

std::size_t SessionRepository::CountActiveForUser(const common::UserId& user_id, common::Timestamp now) {
  SQLite::Statement stmt(
      db_.Connection(),
      "SELECT COUNT(*) FROM sessions WHERE user_id = ? AND revoked_at IS NULL AND access_expires_at > ?");
  stmt.bind(1, user_id);
  stmt.bind(2, now);
  if (stmt.executeStep()) {
    return static_cast<std::size_t>(stmt.getColumn(0).getInt64());
  }
  return 0;
}

} // namespace vox::store
