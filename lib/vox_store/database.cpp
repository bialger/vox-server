#include "lib/vox_store/database.hpp"

#include <chrono>
#include <cstring>
#include <thread>

#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

constexpr int kSqliteBusyHandlerMaxInvocations = 300;
constexpr int kSqliteBusyHandlerSleepMs = 25;
constexpr int kSchemaInitMaxAttempts = 8;
constexpr int kSchemaInitBackoffBaseMs = 250;

/// Retries for SQLITE_BUSY (and similar) when another connection/process touches the same DB file.
/// Registering this replaces PRAGMA busy_timeout; keep sleeps bounded so startup cannot hang forever.
int SqliteBusyHandler(void*, int attempt) {
  if (attempt > kSqliteBusyHandlerMaxInvocations) {
    return 0;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(kSqliteBusyHandlerSleepMs));
  return 1;
}

} // namespace

Database::Database(const std::string& db_path) :
    db_(std::make_unique<SQLite::Database>(
        db_path, static_cast<int>(SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX))) {
  db_->exec("PRAGMA journal_mode=WAL");
  db_->exec("PRAGMA foreign_keys=ON");
  // WAL + NORMAL is the usual production pairing; less fsync pressure than FULL.
  db_->exec("PRAGMA synchronous=NORMAL");
  sqlite3_busy_handler(db_->getHandle(), SqliteBusyHandler, nullptr);

  for (int attempt = 1; attempt <= kSchemaInitMaxAttempts; ++attempt) {
    try {
      CreateSchema();
      break;
    } catch (const SQLite::Exception& e) {
      const int code = e.getErrorCode();
      if (attempt < kSchemaInitMaxAttempts && (code == SQLITE_BUSY || code == SQLITE_LOCKED)) {
        spdlog::warn("Database schema init attempt {}/{}: {}", attempt, kSchemaInitMaxAttempts, e.what());
        std::this_thread::sleep_for(std::chrono::milliseconds(kSchemaInitBackoffBaseMs * attempt));
        continue;
      }
      throw;
    }
  }
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
      user_id                TEXT NOT NULL REFERENCES users(user_id),
      device_id              TEXT NOT NULL,
      identity_key_public    TEXT,
      signed_prekey_public   TEXT,
      signed_prekey_signature TEXT,
      last_prekey_refresh_at INTEGER,
      client_protocol_version INTEGER DEFAULT 1,
      device_label           TEXT NOT NULL DEFAULT '',
      created_at             INTEGER NOT NULL DEFAULT 0,
      last_seen_at           INTEGER NOT NULL DEFAULT 0,
      revoked_at             INTEGER,
      PRIMARY KEY (user_id, device_id)
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS one_time_prekeys (
      prekey_id     TEXT PRIMARY KEY,
      user_id       TEXT NOT NULL,
      device_id     TEXT NOT NULL,
      prekey_public TEXT NOT NULL,
      consumed_at   INTEGER,
      FOREIGN KEY (user_id, device_id) REFERENCES devices(user_id, device_id)
    )
  )SQL");

  db_->exec(R"SQL(
    CREATE TABLE IF NOT EXISTS sessions (
      session_id            TEXT PRIMARY KEY,
      user_id               TEXT NOT NULL REFERENCES users(user_id),
      device_id             TEXT NOT NULL,
      access_token_hash     TEXT NOT NULL UNIQUE,
      refresh_token_hash    TEXT NOT NULL UNIQUE,
      access_expires_at     INTEGER NOT NULL,
      refresh_expires_at    INTEGER NOT NULL,
      created_at            INTEGER NOT NULL,
      revoked_at            INTEGER,
      FOREIGN KEY (user_id, device_id) REFERENCES devices(user_id, device_id)
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
      sender_user_id    TEXT NOT NULL,
      sender_device_id  TEXT NOT NULL,
      ciphertext        BLOB NOT NULL,
      server_timestamp  INTEGER NOT NULL,
      envelope_type     INTEGER NOT NULL DEFAULT 0,
      retention_until   INTEGER,
      ordering_epoch    INTEGER,
      FOREIGN KEY (sender_user_id, sender_device_id) REFERENCES devices(user_id, device_id)
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
      target_user_id   TEXT NOT NULL,
      target_device_id TEXT NOT NULL,
      queued_at        INTEGER NOT NULL,
      delivered_at     INTEGER,
      acked_at         INTEGER,
      PRIMARY KEY (envelope_id, target_user_id, target_device_id),
      FOREIGN KEY (target_user_id, target_device_id) REFERENCES devices(user_id, device_id)
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

void Database::MigrateDevicesCompositePrimaryKey() {
  // Keep these Statements scoped: an active prepared statement on the same connection
  // during schema changes / Transaction can cause SQLITE_LOCKED ("database table is locked").
  {
    SQLite::Statement exists(*db_, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='devices'");
    if (!exists.executeStep() || exists.getColumn(0).getInt() == 0) {
      return;
    }
  }
  {
    SQLite::Statement pk_cols(*db_, "SELECT COUNT(*) FROM pragma_table_info('devices') WHERE pk > 0");
    pk_cols.executeStep();
    if (pk_cols.getColumn(0).getInt() >= 2) {
      return;
    }
  }

  db_->exec("PRAGMA foreign_keys=OFF");
  SQLite::Transaction txn(*db_);

  db_->exec("ALTER TABLE devices RENAME TO devices_old");

  db_->exec(R"SQL(
    CREATE TABLE devices (
      user_id                TEXT NOT NULL REFERENCES users(user_id),
      device_id              TEXT NOT NULL,
      identity_key_public    TEXT,
      signed_prekey_public   TEXT,
      signed_prekey_signature TEXT,
      last_prekey_refresh_at INTEGER,
      client_protocol_version INTEGER DEFAULT 1,
      device_label           TEXT NOT NULL DEFAULT '',
      created_at             INTEGER NOT NULL DEFAULT 0,
      last_seen_at           INTEGER NOT NULL DEFAULT 0,
      revoked_at             INTEGER,
      PRIMARY KEY (user_id, device_id)
    )
  )SQL");
  db_->exec(
      "INSERT INTO devices (user_id, device_id, identity_key_public, signed_prekey_public, signed_prekey_signature, "
      "last_prekey_refresh_at, client_protocol_version, device_label, created_at, last_seen_at, revoked_at) "
      "SELECT user_id, device_id, identity_key_public, signed_prekey_public, signed_prekey_signature, "
      "last_prekey_refresh_at, client_protocol_version, device_label, created_at, last_seen_at, revoked_at "
      "FROM devices_old");
  db_->exec("DROP TABLE devices_old");

  db_->exec("ALTER TABLE one_time_prekeys RENAME TO one_time_prekeys_old");
  db_->exec(R"SQL(
    CREATE TABLE one_time_prekeys (
      prekey_id     TEXT PRIMARY KEY,
      user_id       TEXT NOT NULL,
      device_id     TEXT NOT NULL,
      prekey_public TEXT NOT NULL,
      consumed_at   INTEGER,
      FOREIGN KEY (user_id, device_id) REFERENCES devices(user_id, device_id)
    )
  )SQL");
  db_->exec(
      "INSERT INTO one_time_prekeys (prekey_id, user_id, device_id, prekey_public, consumed_at) "
      "SELECT otp.prekey_id, d.user_id, otp.device_id, otp.prekey_public, otp.consumed_at "
      "FROM one_time_prekeys_old otp "
      "INNER JOIN devices d ON d.device_id = otp.device_id");
  db_->exec("DROP TABLE one_time_prekeys_old");

  db_->exec("ALTER TABLE sessions RENAME TO sessions_old");
  db_->exec(R"SQL(
    CREATE TABLE sessions (
      session_id            TEXT PRIMARY KEY,
      user_id               TEXT NOT NULL REFERENCES users(user_id),
      device_id             TEXT NOT NULL,
      access_token_hash     TEXT NOT NULL UNIQUE,
      refresh_token_hash    TEXT NOT NULL UNIQUE,
      access_expires_at     INTEGER NOT NULL,
      refresh_expires_at    INTEGER NOT NULL,
      created_at            INTEGER NOT NULL,
      revoked_at            INTEGER,
      FOREIGN KEY (user_id, device_id) REFERENCES devices(user_id, device_id)
    )
  )SQL");
  db_->exec("INSERT INTO sessions SELECT * FROM sessions_old");
  db_->exec("DROP TABLE sessions_old");

  db_->exec("ALTER TABLE encrypted_envelopes RENAME TO encrypted_envelopes_old");
  db_->exec(R"SQL(
    CREATE TABLE encrypted_envelopes (
      envelope_id       TEXT PRIMARY KEY,
      conversation_id   TEXT NOT NULL REFERENCES conversations(conversation_id),
      sender_user_id    TEXT NOT NULL,
      sender_device_id  TEXT NOT NULL,
      ciphertext        BLOB NOT NULL,
      server_timestamp  INTEGER NOT NULL,
      envelope_type     INTEGER NOT NULL DEFAULT 0,
      retention_until   INTEGER,
      ordering_epoch    INTEGER,
      FOREIGN KEY (sender_user_id, sender_device_id) REFERENCES devices(user_id, device_id)
    )
  )SQL");
  db_->exec(
      "INSERT INTO encrypted_envelopes (envelope_id, conversation_id, sender_user_id, sender_device_id, ciphertext, "
      "server_timestamp, envelope_type, retention_until, ordering_epoch) "
      "SELECT e.envelope_id, e.conversation_id, d.user_id, e.sender_device_id, e.ciphertext, e.server_timestamp, "
      "e.envelope_type, e.retention_until, e.ordering_epoch "
      "FROM encrypted_envelopes_old e JOIN devices d ON e.sender_device_id = d.device_id");
  db_->exec("DROP TABLE encrypted_envelopes_old");

  db_->exec("ALTER TABLE delivery_state RENAME TO delivery_state_old");
  db_->exec(R"SQL(
    CREATE TABLE delivery_state (
      envelope_id      TEXT NOT NULL REFERENCES encrypted_envelopes(envelope_id),
      target_user_id   TEXT NOT NULL,
      target_device_id TEXT NOT NULL,
      queued_at        INTEGER NOT NULL,
      delivered_at     INTEGER,
      acked_at         INTEGER,
      PRIMARY KEY (envelope_id, target_user_id, target_device_id),
      FOREIGN KEY (target_user_id, target_device_id) REFERENCES devices(user_id, device_id)
    )
  )SQL");
  db_->exec(
      "INSERT INTO delivery_state (envelope_id, target_user_id, target_device_id, queued_at, delivered_at, acked_at) "
      "SELECT ds.envelope_id, d.user_id, ds.target_device_id, ds.queued_at, ds.delivered_at, ds.acked_at "
      "FROM delivery_state_old ds JOIN devices d ON ds.target_device_id = d.device_id");
  db_->exec("DROP TABLE delivery_state_old");

  txn.commit();
  db_->exec("PRAGMA foreign_keys=ON");
}

void Database::MigrateLegacySchema() {
  MigrateDevicesCompositePrimaryKey();
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
