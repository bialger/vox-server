#include "lib/vox_store/envelope_repository.hpp"

#include <sstream>

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

constexpr int kServerTimestampParam = 5;
constexpr int kEnvelopeTypeParam = 6;
constexpr int kRetentionUntilParam = 7;
constexpr int kOrderingEpochParam = 8;

EnvelopeRecord RowToEnvelope(SQLite::Statement& stmt) {
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
  if (!stmt.getColumn("ordering_epoch").isNull()) {
    rec.ordering_epoch = stmt.getColumn("ordering_epoch").getInt64();
  }
  return rec;
}

std::string MakeEnvCursor(common::Timestamp ts, const common::EnvelopeId& eid) {
  std::ostringstream oss;
  oss << ts << '|' << eid;
  return oss.str();
}

bool ParseEnvCursor(const std::string& cursor, common::Timestamp& out_ts, common::EnvelopeId& out_eid) {
  if (cursor.empty()) {
    out_ts = 0;
    out_eid.clear();
    return true;
  }
  const auto pos = cursor.find('|');
  if (pos == std::string::npos || pos == 0) {
    return false;
  }
  try {
    out_ts = static_cast<common::Timestamp>(std::stoll(cursor.substr(0, pos)));
  } catch (...) {
    return false;
  }
  out_eid = cursor.substr(pos + 1);
  return !out_eid.empty();
}

} // namespace

EnvelopeRepository::EnvelopeRepository(IDatabase& db) : db_(db) {
}

common::VoidResult EnvelopeRepository::StoreEnvelope(const EnvelopeRecord& envelope) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO encrypted_envelopes (envelope_id, conversation_id, sender_device_id, "
                           "ciphertext, server_timestamp, envelope_type, retention_until, ordering_epoch) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, envelope.envelope_id);
    stmt.bind(2, envelope.conversation_id);
    stmt.bind(3, envelope.sender_device_id);
    stmt.bind(4, envelope.ciphertext);
    stmt.bind(kServerTimestampParam, envelope.server_timestamp);
    stmt.bind(kEnvelopeTypeParam, envelope.envelope_type);
    if (envelope.retention_until) {
      stmt.bind(kRetentionUntilParam, *envelope.retention_until);
    } else {
      stmt.bind(kRetentionUntilParam);
    }
    if (envelope.ordering_epoch) {
      stmt.bind(kOrderingEpochParam, *envelope.ordering_epoch);
    } else {
      stmt.bind(kOrderingEpochParam);
    }
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    if (e.getErrorCode() == SQLITE_CONSTRAINT) {
      return std::unexpected(
          common::Error{.code = common::ErrorCode::kDuplicate, .message = "Envelope already exists"});
    }
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
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
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

std::vector<EnvelopeRecord> EnvelopeRepository::GetPendingForDevice(const common::DeviceId& device_id,
                                                                    std::size_t limit) {
  auto lock = db_.ReadLock();
  std::vector<EnvelopeRecord> result;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT e.* FROM encrypted_envelopes e "
                         "JOIN delivery_state d ON e.envelope_id = d.envelope_id "
                         "WHERE d.target_device_id = ? AND d.delivered_at IS NULL AND d.acked_at IS NULL "
                         "ORDER BY e.server_timestamp ASC LIMIT ?");
  stmt.bind(1, device_id);
  stmt.bind(2, static_cast<std::int64_t>(limit));
  while (stmt.executeStep()) {
    result.push_back(RowToEnvelope(stmt));
  }
  return result;
}

IEnvelopeRepository::EnvelopePage EnvelopeRepository::GetPendingForDeviceCursored(const common::DeviceId& device_id,
                                                                                  const std::string& cursor,
                                                                                  std::size_t limit) {
  EnvelopePage page;
  common::Timestamp last_ts = 0;
  common::EnvelopeId last_eid;
  if (!cursor.empty()) {
    if (!ParseEnvCursor(cursor, last_ts, last_eid)) {
      page.has_more = false;
      return page;
    }
  }

  auto lock = db_.ReadLock();
  std::string sql =
      "SELECT e.* FROM encrypted_envelopes e "
      "JOIN delivery_state d ON e.envelope_id = d.envelope_id "
      "WHERE d.target_device_id = ? AND d.delivered_at IS NULL AND d.acked_at IS NULL ";
  if (!cursor.empty()) {
    sql += "AND ((e.server_timestamp > ?) OR (e.server_timestamp = ? AND e.envelope_id > ?)) ";
  }
  sql += "ORDER BY e.server_timestamp ASC, e.envelope_id ASC LIMIT ?";

  SQLite::Statement stmt(db_.Connection(), sql);
  int bi = 1;
  stmt.bind(bi++, device_id);
  if (!cursor.empty()) {
    stmt.bind(bi++, last_ts);
    stmt.bind(bi++, last_ts);
    stmt.bind(bi++, last_eid);
  }
  stmt.bind(bi++, static_cast<std::int64_t>(limit + 1));

  while (stmt.executeStep()) {
    page.envelopes.push_back(RowToEnvelope(stmt));
  }
  if (page.envelopes.size() > limit) {
    page.has_more = true;
    page.envelopes.pop_back();
  }
  if (!page.envelopes.empty()) {
    const auto& last = page.envelopes.back();
    page.next_cursor = MakeEnvCursor(last.server_timestamp, last.envelope_id);
  }
  return page;
}

std::vector<EnvelopeRecord> EnvelopeRepository::ListForConversation(const common::ConversationId& conversation_id,
                                                                    common::Timestamp since_exclusive,
                                                                    std::size_t limit) {
  auto lock = db_.ReadLock();
  std::vector<EnvelopeRecord> result;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT * FROM encrypted_envelopes WHERE conversation_id = ? AND server_timestamp > ? "
                         "ORDER BY server_timestamp ASC LIMIT ?");
  stmt.bind(1, conversation_id);
  stmt.bind(2, since_exclusive);
  stmt.bind(3, static_cast<std::int64_t>(limit));
  while (stmt.executeStep()) {
    result.push_back(RowToEnvelope(stmt));
  }
  return result;
}

IEnvelopeRepository::EnvelopePage EnvelopeRepository::ListForConversationCursored(
    const common::ConversationId& conversation_id, const std::string& cursor, std::size_t limit) {
  EnvelopePage page;
  common::Timestamp last_ts = 0;
  common::EnvelopeId last_eid;
  if (!cursor.empty() && !ParseEnvCursor(cursor, last_ts, last_eid)) {
    page.has_more = false;
    return page;
  }

  auto lock = db_.ReadLock();
  std::string sql = "SELECT * FROM encrypted_envelopes WHERE conversation_id = ? ";
  if (!cursor.empty()) {
    sql += "AND ((server_timestamp > ?) OR (server_timestamp = ? AND envelope_id > ?)) ";
  }
  sql += "ORDER BY server_timestamp ASC, envelope_id ASC LIMIT ?";

  SQLite::Statement stmt(db_.Connection(), sql);
  int bi = 1;
  stmt.bind(bi++, conversation_id);
  if (!cursor.empty()) {
    stmt.bind(bi++, last_ts);
    stmt.bind(bi++, last_ts);
    stmt.bind(bi++, last_eid);
  }
  stmt.bind(bi++, static_cast<std::int64_t>(limit + 1));

  while (stmt.executeStep()) {
    page.envelopes.push_back(RowToEnvelope(stmt));
  }
  if (page.envelopes.size() > limit) {
    page.has_more = true;
    page.envelopes.pop_back();
  }
  if (!page.envelopes.empty()) {
    const auto& last = page.envelopes.back();
    page.next_cursor = MakeEnvCursor(last.server_timestamp, last.envelope_id);
  }
  return page;
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
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Delivery state not found"});
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
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Delivery state not found"});
  }
  return {};
}

common::VoidResult EnvelopeRepository::DeletePendingDeliveryForUserInConversation(
    const common::ConversationId& conversation_id, const common::UserId& user_id) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "DELETE FROM delivery_state WHERE target_device_id IN "
                           "(SELECT device_id FROM devices WHERE user_id = ?) "
                           "AND envelope_id IN "
                           "(SELECT envelope_id FROM encrypted_envelopes WHERE conversation_id = ?)");
    stmt.bind(1, user_id);
    stmt.bind(2, conversation_id);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
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
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(), "SELECT 1 FROM encrypted_envelopes WHERE envelope_id = ?");
  stmt.bind(1, envelope_id);
  return stmt.executeStep();
}

std::optional<EnvelopeRecord> EnvelopeRepository::FindById(const common::EnvelopeId& envelope_id) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM encrypted_envelopes WHERE envelope_id = ?");
  stmt.bind(1, envelope_id);
  if (stmt.executeStep()) {
    return RowToEnvelope(stmt);
  }
  return std::nullopt;
}

std::size_t EnvelopeRepository::CountPendingForDevice(const common::DeviceId& device_id) {
  auto lock = db_.ReadLock();
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
