#include "lib/vox_store/envelope_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

constexpr int kServerTimestampParam = 5;
constexpr int kEnvelopeTypeParam = 6;
constexpr int kRetentionUntilParam = 7;

} // namespace

EnvelopeRepository::EnvelopeRepository(Database& db) : db_(db) {
}

common::VoidResult EnvelopeRepository::StoreEnvelope(const EnvelopeRecord& envelope) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO encrypted_envelopes (envelope_id, conversation_id, sender_device_id, "
                           "ciphertext, server_timestamp, envelope_type, retention_until) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, envelope.envelope_id);
    stmt.bind(2, envelope.conversation_id);
    stmt.bind(3, envelope.sender_device_id);
    stmt.bind(4, envelope.ciphertext);
    stmt.bind(kServerTimestampParam, envelope.server_timestamp);
    stmt.bind(kEnvelopeTypeParam, envelope.envelope_type);
    if (envelope.retention_until) {
      stmt.bind(kRetentionUntilParam, *envelope.retention_until);
    }
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    if (e.getErrorCode() == SQLITE_CONSTRAINT) {
      return std::unexpected(common::Error{common::ErrorCode::kDuplicate, "Envelope already exists"});
    }
    return std::unexpected(common::Error{common::ErrorCode::kInternal, e.what()});
  }
}

common::VoidResult EnvelopeRepository::AddDeliveryState(const common::EnvelopeId& envelope_id,
                                                        const common::DeviceId& target_device_id,
                                                        common::Timestamp now) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO delivery_state (envelope_id, target_device_id, queued_at) VALUES (?, ?, ?)");
    stmt.bind(1, envelope_id);
    stmt.bind(2, target_device_id);
    stmt.bind(3, now);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{common::ErrorCode::kInternal, e.what()});
  }
}

std::vector<EnvelopeRecord> EnvelopeRepository::GetPendingForDevice(const common::DeviceId& device_id,
                                                                    std::size_t limit) {
  std::vector<EnvelopeRecord> result;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT e.* FROM encrypted_envelopes e "
                         "JOIN delivery_state d ON e.envelope_id = d.envelope_id "
                         "WHERE d.target_device_id = ? AND d.delivered_at IS NULL AND d.acked_at IS NULL "
                         "ORDER BY e.server_timestamp ASC LIMIT ?");
  stmt.bind(1, device_id);
  stmt.bind(2, static_cast<std::int64_t>(limit));
  while (stmt.executeStep()) {
    EnvelopeRecord rec;
    rec.envelope_id = stmt.getColumn("envelope_id").getString();
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.sender_device_id = stmt.getColumn("sender_device_id").getString();
    rec.ciphertext = stmt.getColumn("ciphertext").getString();
    rec.server_timestamp = stmt.getColumn("server_timestamp").getInt64();
    rec.envelope_type = stmt.getColumn("envelope_type").getInt();
    if (!stmt.getColumn("retention_until").isNull()) {
      rec.retention_until = stmt.getColumn("retention_until").getInt64();
    }
    result.push_back(std::move(rec));
  }
  return result;
}

common::VoidResult EnvelopeRepository::MarkDelivered(const common::EnvelopeId& envelope_id,
                                                     const common::DeviceId& device_id,
                                                     common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE delivery_state SET delivered_at = ? WHERE envelope_id = ? AND target_device_id = ?");
  stmt.bind(1, now);
  stmt.bind(2, envelope_id);
  stmt.bind(3, device_id);
  int rows = stmt.exec();
  if (rows == 0) {
    return std::unexpected(common::Error{common::ErrorCode::kNotFound, "Delivery state not found"});
  }
  return {};
}

common::VoidResult EnvelopeRepository::MarkAcked(const common::EnvelopeId& envelope_id,
                                                 const common::DeviceId& device_id,
                                                 common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE delivery_state SET acked_at = ? WHERE envelope_id = ? AND target_device_id = ?");
  stmt.bind(1, now);
  stmt.bind(2, envelope_id);
  stmt.bind(3, device_id);
  int rows = stmt.exec();
  if (rows == 0) {
    return std::unexpected(common::Error{common::ErrorCode::kNotFound, "Delivery state not found"});
  }
  return {};
}

int EnvelopeRepository::DeleteExpired(common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Transaction txn(db_.Connection());

  SQLite::Statement del_delivery(
      db_.Connection(),
      "DELETE FROM delivery_state WHERE envelope_id IN "
      "(SELECT envelope_id FROM encrypted_envelopes WHERE retention_until IS NOT NULL AND retention_until < ?)");
  del_delivery.bind(1, now);
  del_delivery.exec();

  SQLite::Statement del_envelopes(
      db_.Connection(), "DELETE FROM encrypted_envelopes WHERE retention_until IS NOT NULL AND retention_until < ?");
  del_envelopes.bind(1, now);
  int deleted = del_envelopes.exec();

  txn.commit();
  return deleted;
}

bool EnvelopeRepository::CheckDuplicate(const common::EnvelopeId& envelope_id) {
  SQLite::Statement stmt(db_.Connection(), "SELECT 1 FROM encrypted_envelopes WHERE envelope_id = ?");
  stmt.bind(1, envelope_id);
  return stmt.executeStep();
}

std::optional<EnvelopeRecord> EnvelopeRepository::FindById(const common::EnvelopeId& envelope_id) {
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM encrypted_envelopes WHERE envelope_id = ?");
  stmt.bind(1, envelope_id);
  if (stmt.executeStep()) {
    EnvelopeRecord rec;
    rec.envelope_id = stmt.getColumn("envelope_id").getString();
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.sender_device_id = stmt.getColumn("sender_device_id").getString();
    rec.ciphertext = stmt.getColumn("ciphertext").getString();
    rec.server_timestamp = stmt.getColumn("server_timestamp").getInt64();
    rec.envelope_type = stmt.getColumn("envelope_type").getInt();
    if (!stmt.getColumn("retention_until").isNull()) {
      rec.retention_until = stmt.getColumn("retention_until").getInt64();
    }
    return rec;
  }
  return std::nullopt;
}

std::size_t EnvelopeRepository::CountPendingForDevice(const common::DeviceId& device_id) {
  SQLite::Statement stmt(
      db_.Connection(),
      "SELECT COUNT(*) FROM delivery_state WHERE target_device_id = ? AND delivered_at IS NULL AND acked_at IS NULL");
  stmt.bind(1, device_id);
  if (stmt.executeStep()) {
    return static_cast<std::size_t>(stmt.getColumn(0).getInt64());
  }
  return 0;
}

} // namespace vox::store
