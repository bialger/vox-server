#include "lib/vox_store/database.hpp"

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

std::unique_lock<std::mutex> Database::WriteLock() {
  return std::unique_lock(write_mutex_);
}

void Database::CreateSchema() {
  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS users (
      user_id       TEXT PRIMARY KEY,
      username      TEXT NOT NULL UNIQUE,
      password_salt TEXT NOT NULL,
      password_verifier TEXT NOT NULL,
      created_at    INTEGER NOT NULL,
      disabled_at   INTEGER
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
      client_protocol_version INTEGER DEFAULT 1
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
      policy_blob     TEXT
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
      retention_until   INTEGER
    )
  )SQL");

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
}

} // namespace vox::store
