#ifndef VOX_STORE_ATTACHMENT_REPOSITORY_HPP
#define VOX_STORE_ATTACHMENT_REPOSITORY_HPP

#include <optional>
#include <string>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct AttachmentRecord {
  common::AttachmentId attachment_id;
  common::UserId user_id;
  common::ConversationId conversation_id;
  std::int64_t file_size;
  std::string mime_hint;
  std::string ciphertext_hash;
  std::string blob_path;
  bool upload_complete = false;
  common::Timestamp created_at;
  std::optional<common::Timestamp> retention_until;
};

class AttachmentRepository {
 public:
  explicit AttachmentRepository(Database& db);

  common::VoidResult CreateAttachmentMeta(const AttachmentRecord& record);
  std::optional<AttachmentRecord> GetAttachmentMeta(const common::AttachmentId& attachment_id);
  common::VoidResult MarkUploadComplete(const common::AttachmentId& attachment_id,
                                        const std::string& ciphertext_hash);
  common::VoidResult DeleteAttachment(const common::AttachmentId& attachment_id);
  std::int64_t GetStorageUsedByUser(const common::UserId& user_id);
  std::vector<AttachmentRecord> GetExpired(common::Timestamp now);
  int DeleteExpired(common::Timestamp now);

 private:
  Database& db_;
};

}  // namespace vox::store

#endif  // VOX_STORE_ATTACHMENT_REPOSITORY_HPP
