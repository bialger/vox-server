#ifndef VOX_ATTACHMENTS_ATTACHMENT_SERVICE_HPP
#define VOX_ATTACHMENTS_ATTACHMENT_SERVICE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "lib/vox_common/config.hpp"
#include "lib/vox_common/types.hpp"
#include "lib/vox_store/attachment_repository.hpp"
#include "lib/vox_store/conversation_repository.hpp"

namespace vox::attachments {

struct InitUploadResponse {
  common::AttachmentId attachment_id;
  std::string blob_path;
};

class IAttachmentService {
public:
  virtual ~IAttachmentService() = default;
  virtual common::Result<InitUploadResponse> InitUpload(const common::UserId& user_id,
                                                        const common::ConversationId& conversation_id,
                                                        std::int64_t file_size,
                                                        const std::string& mime_hint) = 0;

  virtual common::VoidResult WriteChunk(const common::AttachmentId& attachment_id,
                                        std::int64_t offset,
                                        const std::string& data) = 0;

  virtual common::VoidResult FinalizeUpload(const common::AttachmentId& attachment_id,
                                            const std::string& ciphertext_hash) = 0;

  virtual common::Result<std::filesystem::path> GetAttachment(const common::AttachmentId& attachment_id,
                                                              const common::UserId& user_id) = 0;

  virtual int DeleteExpired() = 0;
};

class AttachmentService : public IAttachmentService {
public:
  AttachmentService(store::IAttachmentRepository& attachments,
                    store::IConversationRepository& conversations,
                    common::ServerConfig config);

  common::Result<InitUploadResponse> InitUpload(const common::UserId& user_id,
                                                const common::ConversationId& conversation_id,
                                                std::int64_t file_size,
                                                const std::string& mime_hint) override;

  common::VoidResult WriteChunk(const common::AttachmentId& attachment_id,
                                std::int64_t offset,
                                const std::string& data) override;

  common::VoidResult FinalizeUpload(const common::AttachmentId& attachment_id,
                                    const std::string& ciphertext_hash) override;

  common::Result<std::filesystem::path> GetAttachment(const common::AttachmentId& attachment_id,
                                                      const common::UserId& user_id) override;

  int DeleteExpired() override;

private:
  common::Timestamp Now();

  store::IAttachmentRepository& attachments_;
  store::IConversationRepository& conversations_;
  common::ServerConfig config_;
};

} // namespace vox::attachments

#endif // VOX_ATTACHMENTS_ATTACHMENT_SERVICE_HPP
