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

class IAttachmentRepository {
public:
  virtual ~IAttachmentRepository() = default;
  virtual common::VoidResult CreateAttachmentMeta(const AttachmentRecord& record) = 0;
  virtual std::optional<AttachmentRecord> GetAttachmentMeta(const common::AttachmentId& attachment_id) = 0;
  virtual common::VoidResult MarkUploadComplete(const common::AttachmentId& attachment_id,
                                                  const std::string& ciphertext_hash) = 0;
  virtual common::VoidResult DeleteAttachment(const common::AttachmentId& attachment_id) = 0;
  virtual std::int64_t GetStorageUsedByUser(const common::UserId& user_id) = 0;
  virtual std::vector<AttachmentRecord> GetExpired(common::Timestamp now) = 0;
  virtual int DeleteExpired(common::Timestamp now) = 0;
};

class AttachmentRepository : public IAttachmentRepository {
public:
  explicit AttachmentRepository(IDatabase& db);

  common::VoidResult CreateAttachmentMeta(const AttachmentRecord& record) override;
  std::optional<AttachmentRecord> GetAttachmentMeta(const common::AttachmentId& attachment_id) override;
  common::VoidResult MarkUploadComplete(const common::AttachmentId& attachment_id,
                                        const std::string& ciphertext_hash) override;
  common::VoidResult DeleteAttachment(const common::AttachmentId& attachment_id) override;
  std::int64_t GetStorageUsedByUser(const common::UserId& user_id) override;
  std::vector<AttachmentRecord> GetExpired(common::Timestamp now) override;
  int DeleteExpired(common::Timestamp now) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_ATTACHMENT_REPOSITORY_HPP
