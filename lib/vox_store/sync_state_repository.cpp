#include "lib/vox_store/sync_state_repository.hpp"

#include <charconv>
#include <cstring>

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

namespace {

constexpr int kInitialRecordVersion = 1;
constexpr int kListColDeleted = 5;
constexpr int kListColServerUpdatedAt = 6;
constexpr int kBindUpdWhereUserId = 5;
constexpr int kBindUpdWhereCollection = 6;
constexpr int kBindUpdWhereRecordId = 7;
constexpr int kBindInsContentHash = 5;
constexpr int kBindInsVersion = 6;
constexpr int kBindInsServerUpdatedAt = 7;
constexpr int kBindTombWhereRecordId = 5;

std::optional<std::int64_t> ParseCursorId(std::string_view cursor) {
  if (cursor.empty()) {
    return 0;
  }
  std::int64_t v = 0;
  const char* begin = cursor.data();
  const char* end = cursor.data() + cursor.size();
  auto [ptr, ec] = std::from_chars(begin, end, v);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return v;
}

} // namespace

SyncStateRepository::SyncStateRepository(IDatabase& db) : db_(db) {
}

SyncChangesPage SyncStateRepository::ListChangesAfterCursor(const common::UserId& user_id,
                                                            const std::string& collection,
                                                            const std::string& cursor,
                                                            std::size_t limit) {
  SyncChangesPage page;
  auto last_id = ParseCursorId(cursor);
  if (!last_id.has_value()) {
    page.next_cursor = "";
    page.has_more = false;
    return page;
  }

  auto lock = db_.ReadLock();
  const std::int64_t after_id = *last_id;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT id, record_id, ciphertext, content_hash, version, deleted, server_updated_at "
                         "FROM sync_records WHERE user_id = ? AND collection = ? AND id > ? "
                         "ORDER BY id ASC LIMIT ?");
  stmt.bind(1, user_id);
  stmt.bind(2, collection);
  stmt.bind(3, after_id);
  stmt.bind(4, static_cast<std::int64_t>(limit + 1));

  while (stmt.executeStep()) {
    SyncRecordRow row;
    row.row_id = stmt.getColumn(0).getInt64();
    row.record_id = stmt.getColumn(1).getString();
    row.ciphertext = stmt.getColumn(2).getString();
    row.content_hash = stmt.getColumn(3).getString();
    row.version = stmt.getColumn(4).getInt();
    row.deleted = stmt.getColumn(kListColDeleted).getInt() != 0;
    row.server_updated_at = stmt.getColumn(kListColServerUpdatedAt).getInt64();
    page.changes.push_back(std::move(row));
  }

  if (page.changes.size() > limit) {
    page.has_more = true;
    page.changes.pop_back();
  }
  if (!page.changes.empty()) {
    page.next_cursor = std::to_string(page.changes.back().row_id);
  } else {
    page.next_cursor = cursor.empty() ? "" : cursor;
  }
  return page;
}

common::Result<SyncPutResult> SyncStateRepository::UpsertRecord(const common::UserId& user_id,
                                                                const std::string& collection,
                                                                const std::string& record_id,
                                                                const std::string& ciphertext,
                                                                const std::string& content_hash,
                                                                int base_version,
                                                                common::Timestamp client_updated_at,
                                                                common::Timestamp server_now) {
  (void) client_updated_at;
  try {
    auto lock = db_.WriteLock();
    SQLite::Transaction txn(db_.Connection());

    SQLite::Statement find(
        db_.Connection(),
        "SELECT id, version FROM sync_records WHERE user_id = ? AND collection = ? AND record_id = ?");
    find.bind(1, user_id);
    find.bind(2, collection);
    find.bind(3, record_id);

    if (find.executeStep()) {
      const int current = find.getColumn(1).getInt();
      if (current != base_version) {
        return std::unexpected(
            common::Error{.code = common::ErrorCode::kDuplicate, .message = "Version conflict for sync record"});
      }
      const int new_ver = current + 1;
      SQLite::Statement upd(db_.Connection(),
                            "UPDATE sync_records SET ciphertext = ?, content_hash = ?, version = ?, deleted = 0, "
                            "server_updated_at = ? WHERE user_id = ? AND collection = ? AND record_id = ?");
      upd.bind(1, ciphertext);
      upd.bind(2, content_hash);
      upd.bind(3, new_ver);
      upd.bind(4, server_now);
      upd.bind(kBindUpdWhereUserId, user_id);
      upd.bind(kBindUpdWhereCollection, collection);
      upd.bind(kBindUpdWhereRecordId, record_id);
      upd.exec();
      txn.commit();
      return SyncPutResult{
          .record_id = record_id, .version = new_ver, .server_updated_at = server_now, .deleted = false};
    }

    if (base_version != 0) {
      return std::unexpected(
          common::Error{.code = common::ErrorCode::kDuplicate, .message = "Version conflict for sync record"});
    }

    SQLite::Statement ins(
        db_.Connection(),
        "INSERT INTO sync_records (user_id, collection, record_id, ciphertext, content_hash, version, "
        "deleted, server_updated_at) VALUES (?, ?, ?, ?, ?, ?, 0, ?)");
    ins.bind(1, user_id);
    ins.bind(2, collection);
    ins.bind(3, record_id);
    ins.bind(4, ciphertext);
    ins.bind(kBindInsContentHash, content_hash);
    ins.bind(kBindInsVersion, kInitialRecordVersion);
    ins.bind(kBindInsServerUpdatedAt, server_now);
    ins.exec();
    txn.commit();
    return SyncPutResult{.record_id = record_id, .version = 1, .server_updated_at = server_now, .deleted = false};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

common::Result<SyncPutResult> SyncStateRepository::TombstoneRecord(const common::UserId& user_id,
                                                                   const std::string& collection,
                                                                   const std::string& record_id,
                                                                   int base_version,
                                                                   common::Timestamp client_updated_at,
                                                                   common::Timestamp server_now) {
  (void) client_updated_at;
  try {
    auto lock = db_.WriteLock();
    SQLite::Transaction txn(db_.Connection());

    SQLite::Statement find(db_.Connection(),
                           "SELECT version FROM sync_records WHERE user_id = ? AND collection = ? AND record_id = ?");
    find.bind(1, user_id);
    find.bind(2, collection);
    find.bind(3, record_id);
    if (!find.executeStep()) {
      return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Sync record not found"});
    }
    const int current = find.getColumn(0).getInt();
    if (current != base_version) {
      return std::unexpected(
          common::Error{.code = common::ErrorCode::kDuplicate, .message = "Version conflict for sync record"});
    }
    const int new_ver = current + 1;
    SQLite::Statement upd(
        db_.Connection(),
        "UPDATE sync_records SET ciphertext = '', content_hash = '', version = ?, deleted = 1, server_updated_at = ? "
        "WHERE user_id = ? AND collection = ? AND record_id = ?");
    upd.bind(1, new_ver);
    upd.bind(2, server_now);
    upd.bind(3, user_id);
    upd.bind(4, collection);
    upd.bind(kBindTombWhereRecordId, record_id);
    upd.exec();
    txn.commit();
    return SyncPutResult{.record_id = record_id, .version = new_ver, .server_updated_at = server_now, .deleted = true};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

} // namespace vox::store
