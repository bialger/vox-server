#include "lib/vox_store/attachment_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>

namespace vox::store {

namespace {

constexpr int kMimeHintParam = 5;
constexpr int kBlobPathParam = 6;
constexpr int kCreatedAtParam = 7;
constexpr int kRetentionUntilParam = 8;

} // namespace

AttachmentRepository::AttachmentRepository(IDatabase& db) : db_(db) {
}

common::VoidResult AttachmentRepository::CreateAttachmentMeta(const AttachmentRecord& record) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO attachment_metadata (attachment_id, user_id, conversation_id, file_size, "
                           "mime_hint, blob_path, upload_complete, created_at, retention_until) "
                           "VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)");
    stmt.bind(1, record.attachment_id);
    stmt.bind(2, record.user_id);
    stmt.bind(3, record.conversation_id);
    stmt.bind(4, record.file_size);
    stmt.bind(kMimeHintParam, record.mime_hint);
    stmt.bind(kBlobPathParam, record.blob_path);
    stmt.bind(kCreatedAtParam, record.created_at);
    if (record.retention_until) {
      stmt.bind(kRetentionUntilParam, *record.retention_until);
    }
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

std::optional<AttachmentRecord> AttachmentRepository::GetAttachmentMeta(const common::AttachmentId& attachment_id) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM attachment_metadata WHERE attachment_id = ?");
  stmt.bind(1, attachment_id);
  if (stmt.executeStep()) {
    AttachmentRecord rec;
    rec.attachment_id = stmt.getColumn("attachment_id").getString();
    rec.user_id = stmt.getColumn("user_id").getString();
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.file_size = stmt.getColumn("file_size").getInt64();
    rec.mime_hint = stmt.getColumn("mime_hint").getString();
    rec.ciphertext_hash =
        stmt.getColumn("ciphertext_hash").isNull() ? "" : stmt.getColumn("ciphertext_hash").getString();
    rec.blob_path = stmt.getColumn("blob_path").getString();
    rec.upload_complete = stmt.getColumn("upload_complete").getInt() != 0;
    rec.created_at = stmt.getColumn("created_at").getInt64();
    if (!stmt.getColumn("retention_until").isNull()) {
      rec.retention_until = stmt.getColumn("retention_until").getInt64();
    }
    return rec;
  }
  return std::nullopt;
}

common::VoidResult AttachmentRepository::MarkUploadComplete(const common::AttachmentId& attachment_id,
                                                            const std::string& ciphertext_hash) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(
      db_.Connection(),
      "UPDATE attachment_metadata SET upload_complete = 1, ciphertext_hash = ? WHERE attachment_id = ?");
  stmt.bind(1, ciphertext_hash);
  stmt.bind(2, attachment_id);
  int rows = stmt.exec();
  if (rows == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Attachment not found"});
  }
  return {};
}

common::VoidResult AttachmentRepository::DeleteAttachment(const common::AttachmentId& attachment_id) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(), "DELETE FROM attachment_metadata WHERE attachment_id = ?");
  stmt.bind(1, attachment_id);
  int rows = stmt.exec();
  if (rows == 0) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Attachment not found"});
  }
  return {};
}

std::int64_t AttachmentRepository::GetStorageUsedByUser(const common::UserId& user_id) {
  auto lock = db_.ReadLock();
  SQLite::Statement stmt(
      db_.Connection(),
      "SELECT COALESCE(SUM(file_size), 0) FROM attachment_metadata WHERE user_id = ? AND upload_complete = 1");
  stmt.bind(1, user_id);
  if (stmt.executeStep()) {
    return stmt.getColumn(0).getInt64();
  }
  return 0;
}

std::vector<AttachmentRecord> AttachmentRepository::GetExpired(common::Timestamp now) {
  auto lock = db_.ReadLock();
  std::vector<AttachmentRecord> result;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT * FROM attachment_metadata WHERE retention_until IS NOT NULL AND retention_until < ?");
  stmt.bind(1, now);
  while (stmt.executeStep()) {
    AttachmentRecord rec;
    rec.attachment_id = stmt.getColumn("attachment_id").getString();
    rec.user_id = stmt.getColumn("user_id").getString();
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.file_size = stmt.getColumn("file_size").getInt64();
    rec.blob_path = stmt.getColumn("blob_path").getString();
    rec.upload_complete = stmt.getColumn("upload_complete").getInt() != 0;
    rec.created_at = stmt.getColumn("created_at").getInt64();
    result.push_back(std::move(rec));
  }
  return result;
}

int AttachmentRepository::DeleteExpired(common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "DELETE FROM attachment_metadata WHERE retention_until IS NOT NULL AND retention_until < ?");
  stmt.bind(1, now);
  return stmt.exec();
}

} // namespace vox::store
