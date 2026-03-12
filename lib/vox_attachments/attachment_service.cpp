#include "lib/vox_attachments/attachment_service.hpp"

#include <chrono>
#include <fstream>

#include <spdlog/spdlog.h>

#include "lib/vox_common/uuid.hpp"

namespace vox::attachments {

AttachmentService::AttachmentService(store::AttachmentRepository& attachments,
                                     store::ConversationRepository& conversations,
                                     common::ServerConfig config) :
    attachments_(attachments), conversations_(conversations), config_(std::move(config)) {
  std::filesystem::create_directories(config_.blob_storage_path);
}

common::Result<InitUploadResponse> AttachmentService::InitUpload(const common::UserId& user_id,
                                                                 const common::ConversationId& conversation_id,
                                                                 std::int64_t file_size,
                                                                 const std::string& mime_hint) {
  if (file_size <= 0) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument, .message = "File size must be positive"});
  }
  if (file_size > static_cast<std::int64_t>(config_.max_upload_size_bytes)) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kQuotaExceeded, .message = "File exceeds maximum upload size"});
  }

  auto used = attachments_.GetStorageUsedByUser(user_id);
  if (used + file_size > static_cast<std::int64_t>(config_.max_storage_per_user_bytes)) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kQuotaExceeded, .message = "User storage quota exceeded"});
  }

  bool is_member = conversations_.IsUserInConversation(conversation_id, user_id);
  if (!is_member) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kForbidden, .message = "User is not a member of this conversation"});
  }

  auto now = Now();
  auto attachment_id = common::GenerateUuid();
  auto blob_path = config_.blob_storage_path / (attachment_id + ".blob");

  store::AttachmentRecord record;
  record.attachment_id = attachment_id;
  record.user_id = user_id;
  record.conversation_id = conversation_id;
  record.file_size = file_size;
  record.mime_hint = mime_hint;
  record.blob_path = blob_path.string();
  record.created_at = now;
  if (config_.attachment_retention_seconds > 0) {
    record.retention_until = now + config_.attachment_retention_seconds;
  }

  auto result = attachments_.CreateAttachmentMeta(record);
  if (!result) {
    return std::unexpected(result.error());
  }

  return InitUploadResponse{.attachment_id = attachment_id, .blob_path = blob_path.string()};
}

common::VoidResult AttachmentService::WriteChunk(const common::AttachmentId& attachment_id,
                                                 std::int64_t offset,
                                                 const std::string& data) {
  auto meta = attachments_.GetAttachmentMeta(attachment_id);
  if (!meta) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Attachment not found"});
  }
  if (meta->upload_complete) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument, .message = "Upload already complete"});
  }

  std::ofstream file(meta->blob_path, std::ios::binary | std::ios::in | std::ios::out);
  if (!file.is_open()) {
    file.open(meta->blob_path, std::ios::binary | std::ios::out);
  }
  if (!file.is_open()) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = "Cannot open blob file"});
  }
  file.seekp(offset);
  file.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!file.good()) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = "Failed to write chunk"});
  }
  return {};
}

common::VoidResult AttachmentService::FinalizeUpload(const common::AttachmentId& attachment_id,
                                                     const std::string& ciphertext_hash) {
  auto meta = attachments_.GetAttachmentMeta(attachment_id);
  if (!meta) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Attachment not found"});
  }
  if (meta->upload_complete) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument, .message = "Upload already finalized"});
  }

  if (!std::filesystem::exists(meta->blob_path)) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInternal, .message = "Blob file not found on disk"});
  }

  return attachments_.MarkUploadComplete(attachment_id, ciphertext_hash);
}

common::Result<std::filesystem::path> AttachmentService::GetAttachment(const common::AttachmentId& attachment_id,
                                                                       const common::UserId& user_id) {
  auto meta = attachments_.GetAttachmentMeta(attachment_id);
  if (!meta) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Attachment not found"});
  }
  if (!meta->upload_complete) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kNotFound, .message = "Attachment upload not complete"});
  }

  bool is_member = conversations_.IsUserInConversation(meta->conversation_id, user_id);
  if (!is_member) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kForbidden, .message = "Not authorized to access this attachment"});
  }

  return std::filesystem::path(meta->blob_path);
}

int AttachmentService::DeleteExpired() {
  auto now = Now();
  auto expired = attachments_.GetExpired(now);
  for (const auto& att : expired) {
    std::error_code ec;
    std::filesystem::remove(att.blob_path, ec);
    if (ec) {
      spdlog::warn("Failed to delete blob file {}: {}", att.blob_path, ec.message());
    }
  }
  return attachments_.DeleteExpired(now);
}

common::Timestamp AttachmentService::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace vox::attachments
