#include "lib/vox_store/database.hpp"

#include <cstring>

#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>

namespace vox::store {

Database::Database(const std::string& db_path) :
    db_(std::make_unique<SQLite::Database>(db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)) {
  db_->exec("PRAGMA journal_mode=WAL");
  db_->exec("PRAGMA foreign_keys=ON");
  db_->exec("PRAGMA busy_timeout=5000");
  CreateSchema();
  spdlog::info("Database initialized at {}", db_path);
}

SQLite::Database& Database::Connection() {
  return *db_;
}

std::unique_lock<std::recursive_mutex> Database::WriteLock() {
  return std::unique_lock(connection_mutex_);
}

std::unique_lock<std::recursive_mutex> Database::ReadLock() {
  return std::unique_lock(connection_mutex_);
}

void Database::CreateSchema() {
  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS users (
      user_id       TEXT PRIMARY KEY,
      username      TEXT NOT NULL UNIQUE,
      password_salt TEXT NOT NULL,
      password_verifier TEXT NOT NULL,
      created_at    INTEGER NOT NULL,
      disabled_at   INTEGER,
      sync_key_version INTEGER NOT NULL DEFAULT 0,
      wrapped_sync_key TEXT NOT NULL DEFAULT '',
      sync_wrap_salt TEXT NOT NULL DEFAULT '',
      sync_wrap_params TEXT NOT NULL DEFAULT ''
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS devices (
      device_id              TEXT PRIMARY KEY,
      user_id                TEXT NOT NULL REFERENCES users(user_id),
      identity_key_public    TEXT,
      signed_prekey_public   TEXT,
      signed_prekey_signature TEXT,
      last_prekey_refresh_at INTEGER,
      client_protocol_version INTEGER DEFAULT 1,
      device_label           TEXT NOT NULL DEFAULT '',
      created_at             INTEGER NOT NULL DEFAULT 0,
      last_seen_at           INTEGER NOT NULL DEFAULT 0,
      revoked_at             INTEGER
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS one_time_prekeys (
      prekey_id   TEXT PRIMARY KEY,
      device_id   TEXT NOT NULL REFERENCES devices(device_id),
      prekey_public TEXT NOT NULL,
      consumed_at INTEGER
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS sessions (
      session_id            TEXT PRIMARY KEY,
      user_id               TEXT NOT NULL REFERENCES users(user_id),
      device_id             TEXT NOT NULL REFERENCES devices(device_id),
      access_token_hash     TEXT NOT NULL UNIQUE,
      refresh_token_hash    TEXT NOT NULL UNIQUE,
      access_expires_at     INTEGER NOT NULL,
      refresh_expires_at    INTEGER NOT NULL,
      created_at            INTEGER NOT NULL,
      revoked_at            INTEGER
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS conversations (
      conversation_id TEXT PRIMARY KEY,
      type            INTEGER NOT NULL,
      created_by      TEXT NOT NULL REFERENCES users(user_id),
      created_at      INTEGER NOT NULL,
      policy_blob     TEXT,
      membership_version INTEGER NOT NULL DEFAULT 0
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS conversation_members (
      conversation_id TEXT NOT NULL REFERENCES conversations(conversation_id),
      user_id         TEXT NOT NULL REFERENCES users(user_id),
      role            INTEGER NOT NULL DEFAULT 2,
      added_at        INTEGER NOT NULL,
      removed_at      INTEGER,
      PRIMARY KEY (conversation_id, user_id)
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS channel_subscriptions (
      conversation_id TEXT NOT NULL REFERENCES conversations(conversation_id),
      user_id         TEXT NOT NULL REFERENCES users(user_id),
      subscribed_at   INTEGER NOT NULL,
      unsubscribed_at INTEGER,
      PRIMARY KEY (conversation_id, user_id)
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS encrypted_envelopes (
      envelope_id       TEXT PRIMARY KEY,
      conversation_id   TEXT NOT NULL REFERENCES conversations(conversation_id),
      sender_device_id  TEXT NOT NULL REFERENCES devices(device_id),
      ciphertext        BLOB NOT NULL,
      server_timestamp  INTEGER NOT NULL,
      envelope_type     INTEGER NOT NULL DEFAULT 0,
      retention_until   INTEGER,
      ordering_epoch    INTEGER
    )
  )SQL");

  try {
    db_->exec("ALTER TABLE encrypted_envelopes ADD COLUMN ordering_epoch INTEGER");
  } catch (const SQLite::Exception& e) {
    const char* msg = e.what();
    if (msg == nullptr || std::strstr(msg, "duplicate") == nullptr) {
      throw;
    }
  }

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS delivery_state (
      envelope_id      TEXT NOT NULL REFERENCES encrypted_envelopes(envelope_id),
      target_device_id TEXT NOT NULL REFERENCES devices(device_id),
      queued_at        INTEGER NOT NULL,
      delivered_at     INTEGER,
      acked_at         INTEGER,
      PRIMARY KEY (envelope_id, target_device_id)
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS attachment_metadata (
      attachment_id     TEXT PRIMARY KEY,
      user_id           TEXT NOT NULL REFERENCES users(user_id),
      conversation_id   TEXT NOT NULL REFERENCES conversations(conversation_id),
      file_size         INTEGER NOT NULL,
      mime_hint         TEXT,
      ciphertext_hash   TEXT,
      blob_path         TEXT,
      upload_complete   INTEGER NOT NULL DEFAULT 0,
      created_at        INTEGER NOT NULL,
      retention_until   INTEGER
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS sync_records (
      id                INTEGER PRIMARY KEY AUTOINCREMENT,
      user_id           TEXT NOT NULL REFERENCES users(user_id),
      collection        TEXT NOT NULL,
      record_id         TEXT NOT NULL,
      ciphertext        TEXT NOT NULL,
      content_hash      TEXT NOT NULL,
      version           INTEGER NOT NULL,
      deleted           INTEGER NOT NULL DEFAULT 0,
      server_updated_at INTEGER NOT NULL,
      UNIQUE(user_id, collection, record_id)
    )
  )SQL");

  db_->exec(
      "CREATE INDEX IF NOT EXISTS idx_sync_records_user_coll_time ON sync_records (user_id, collection, "
      "server_updated_at, id)");

  MigrateLegacySchema();
}

void Database::MigrateLegacySchema() {
  auto try_exec = [this](const char* sql) {
    try {
      db_->exec(sql);
    } catch (const SQLite::Exception& e) {
      const char* msg = e.what();
      if (msg == nullptr || std::strstr(msg, "duplicate") == nullptr) {
        throw;
      }
    }
  };

  try_exec("ALTER TABLE users ADD COLUMN sync_key_version INTEGER NOT NULL DEFAULT 0");
  try_exec("ALTER TABLE users ADD COLUMN wrapped_sync_key TEXT NOT NULL DEFAULT ''");
  try_exec("ALTER TABLE users ADD COLUMN sync_wrap_salt TEXT NOT NULL DEFAULT ''");
  try_exec("ALTER TABLE users ADD COLUMN sync_wrap_params TEXT NOT NULL DEFAULT ''");

  try_exec("ALTER TABLE devices ADD COLUMN device_label TEXT NOT NULL DEFAULT ''");
  try_exec("ALTER TABLE devices ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0");
  try_exec("ALTER TABLE devices ADD COLUMN last_seen_at INTEGER NOT NULL DEFAULT 0");
  try_exec("ALTER TABLE devices ADD COLUMN revoked_at INTEGER");

  try_exec("ALTER TABLE conversations ADD COLUMN membership_version INTEGER NOT NULL DEFAULT 0");
}

} // namespace vox::store
