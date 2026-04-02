#include "lib/vox_store/device_repository.hpp"

#include <chrono>

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

// INSERT column positions (SQLite bind indices are 1-based).
constexpr int kBindSignedPrekeySignature = 5;
constexpr int kBindLastPrekeyRefreshAt = 6;
constexpr int kBindClientProtocolVersion = 7;
constexpr int kBindDeviceLabel = 8;
constexpr int kBindCreatedAt = 9;
constexpr int kBindLastSeenAt = 10;
constexpr int kBindRevokedAt = 11;

DeviceRecord RowToDevice(SQLite::Statement& stmt) {
  DeviceRecord rec;
  rec.device_id = stmt.getColumn("device_id").getString();
  rec.user_id = stmt.getColumn("user_id").getString();
  rec.identity_key_public = stmt.getColumn("identity_key_public").getString();
  rec.signed_prekey_public = stmt.getColumn("signed_prekey_public").getString();
  rec.signed_prekey_signature = stmt.getColumn("signed_prekey_signature").getString();
  if (!stmt.getColumn("last_prekey_refresh_at").isNull()) {
    rec.last_prekey_refresh_at = stmt.getColumn("last_prekey_refresh_at").getInt64();
  }
  rec.client_protocol_version = stmt.getColumn("client_protocol_version").getInt();
  rec.device_label = stmt.getColumn("device_label").getString();
  rec.created_at = stmt.getColumn("created_at").getInt64();
  rec.last_seen_at = stmt.getColumn("last_seen_at").getInt64();
  if (!stmt.getColumn("revoked_at").isNull()) {
    rec.revoked_at = stmt.getColumn("revoked_at").getInt64();
  }
  return rec;
}

} // namespace

DeviceRepository::DeviceRepository(IDatabase& db) : db_(db) {
}

common::VoidResult DeviceRepository::RegisterDevice(const DeviceRecord& device) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO devices (device_id, user_id, identity_key_public, signed_prekey_public, "
                           "signed_prekey_signature, last_prekey_refresh_at, client_protocol_version, device_label, "
                           "created_at, last_seen_at, revoked_at) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, device.device_id);
    stmt.bind(2, device.user_id);
    stmt.bind(3, device.identity_key_public);
    stmt.bind(4, device.signed_prekey_public);
    stmt.bind(kBindSignedPrekeySignature, device.signed_prekey_signature);
    if (device.last_prekey_refresh_at) {
      stmt.bind(kBindLastPrekeyRefreshAt, *device.last_prekey_refresh_at);
    } else {
      stmt.bind(kBindLastPrekeyRefreshAt);
    }
    stmt.bind(kBindClientProtocolVersion, device.client_protocol_version);
    stmt.bind(kBindDeviceLabel, device.device_label);
    stmt.bind(kBindCreatedAt, device.created_at);
    stmt.bind(kBindLastSeenAt, device.last_seen_at);
    if (device.revoked_at) {
      stmt.bind(kBindRevokedAt, *device.revoked_at);
    } else {
      stmt.bind(kBindRevokedAt);
    }
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    if (e.getErrorCode() == SQLITE_CONSTRAINT) {
      return std::unexpected(
          common::Error{.code = common::ErrorCode::kAlreadyExists, .message = "Device already registered"});
    }
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

std::vector<DeviceRecord> DeviceRepository::GetDevicesForUser(const common::UserId& user_id) {
  auto lock = db_.ReadLock();
  std::vector<DeviceRecord> result;
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM devices WHERE user_id = ? ORDER BY created_at ASC");
  stmt.bind(1, user_id);
  while (stmt.executeStep()) {
    result.push_back(RowToDevice(stmt));
  }
  return result;
}

std::optional<DeviceRecord> DeviceRepository::FindById(const common::DeviceId& device_id) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM devices WHERE device_id = ?");
  stmt.bind(1, device_id);
  if (stmt.executeStep()) {
    return RowToDevice(stmt);
  }
  return std::nullopt;
}

common::VoidResult DeviceRepository::StorePrekeys(const common::DeviceId& device_id,
                                                  const std::vector<PrekeyRecord>& prekeys) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Transaction txn(db_.Connection());
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO one_time_prekeys (prekey_id, device_id, prekey_public) VALUES (?, ?, ?)");
    for (const auto& pk : prekeys) {
      stmt.bind(1, pk.prekey_id);
      stmt.bind(2, device_id);
      stmt.bind(3, pk.prekey_public);
      stmt.exec();
      stmt.reset();
    }
    auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    SQLite::Statement update(db_.Connection(), "UPDATE devices SET last_prekey_refresh_at = ? WHERE device_id = ?");
    update.bind(1, now);
    update.bind(2, device_id);
    update.exec();
    txn.commit();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

std::optional<PrekeyRecord> DeviceRepository::ConsumeOneAvailableOtpLocked(const common::DeviceId& device_id) {
  SQLite::Statement select(db_.Connection(),
                           "SELECT prekey_id, prekey_public FROM one_time_prekeys "
                           "WHERE device_id = ? AND consumed_at IS NULL LIMIT 1");
  select.bind(1, device_id);
  if (!select.executeStep()) {
    return std::nullopt;
  }

  PrekeyRecord record;
  record.prekey_id = select.getColumn(0).getString();
  record.device_id = device_id;
  record.prekey_public = select.getColumn(1).getString();

  auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  SQLite::Statement update(db_.Connection(),
                           "UPDATE one_time_prekeys SET consumed_at = ? WHERE prekey_id = ? AND consumed_at IS NULL");
  update.bind(1, now);
  update.bind(2, record.prekey_id);
  int rows = update.exec();
  if (rows == 0) {
    return std::nullopt;
  }

  record.consumed_at = now;
  return record;
}

common::Result<PrekeyBundle> DeviceRepository::GetPrekeyBundle(const common::DeviceId& device_id) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Transaction txn(db_.Connection());

    SQLite::Statement dev_stmt(db_.Connection(), "SELECT * FROM devices WHERE device_id = ?");
    dev_stmt.bind(1, device_id);
    if (!dev_stmt.executeStep()) {
      return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Device not found"});
    }
    if (!dev_stmt.getColumn("revoked_at").isNull()) {
      return std::unexpected(common::Error{.code = common::ErrorCode::kForbidden, .message = "Device revoked"});
    }

    PrekeyBundle bundle;
    bundle.identity_key_public = dev_stmt.getColumn("identity_key_public").getString();
    bundle.signed_prekey_public = dev_stmt.getColumn("signed_prekey_public").getString();
    bundle.signed_prekey_signature = dev_stmt.getColumn("signed_prekey_signature").getString();

    if (auto otp = ConsumeOneAvailableOtpLocked(device_id)) {
      bundle.one_time_prekey_id = otp->prekey_id;
      bundle.one_time_prekey_public = otp->prekey_public;
    }

    txn.commit();
    return bundle;
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

common::Result<PrekeyRecord> DeviceRepository::ConsumeOneTimePrekey(const common::DeviceId& device_id) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Transaction txn(db_.Connection());
    auto consumed = ConsumeOneAvailableOtpLocked(device_id);
    if (!consumed) {
      return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "No available prekeys"});
    }
    txn.commit();
    return *consumed;
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

common::VoidResult DeviceRepository::UpdateSignedPrekey(const common::DeviceId& device_id,
                                                        const std::string& signed_prekey_public,
                                                        const std::string& signed_prekey_signature,
                                                        common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE devices SET signed_prekey_public = ?, signed_prekey_signature = ?, "
                         "last_prekey_refresh_at = ? WHERE device_id = ? AND revoked_at IS NULL");
  stmt.bind(1, signed_prekey_public);
  stmt.bind(2, signed_prekey_signature);
  stmt.bind(3, now);
  stmt.bind(4, device_id);
  if (stmt.exec() == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Device not found"});
  }
  return {};
}

common::VoidResult DeviceRepository::RevokeDevice(const common::DeviceId& device_id, common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Transaction txn(db_.Connection());
  SQLite::Statement upd(db_.Connection(),
                        "UPDATE devices SET revoked_at = ? WHERE device_id = ? AND revoked_at IS NULL");
  upd.bind(1, now);
  upd.bind(2, device_id);
  if (upd.exec() == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Device not found"});
  }
  SQLite::Statement del_otp(db_.Connection(), "DELETE FROM one_time_prekeys WHERE device_id = ?");
  del_otp.bind(1, device_id);
  del_otp.exec();
  txn.commit();
  return {};
}

common::VoidResult DeviceRepository::UpdateLastSeen(const common::DeviceId& device_id, common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(), "UPDATE devices SET last_seen_at = ? WHERE device_id = ?");
  stmt.bind(1, now);
  stmt.bind(2, device_id);
  stmt.exec();
  return {};
}

std::size_t DeviceRepository::CountAvailableOneTimePrekeys(const common::DeviceId& device_id) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT COUNT(*) FROM one_time_prekeys WHERE device_id = ? AND consumed_at IS NULL");
  stmt.bind(1, device_id);
  if (stmt.executeStep()) {
    return static_cast<std::size_t>(stmt.getColumn(0).getInt64());
  }
  return 0;
}

} // namespace vox::store
