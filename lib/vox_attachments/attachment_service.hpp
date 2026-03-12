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

class AttachmentService {
public:
  AttachmentService(store::AttachmentRepository& attachments,
                    store::ConversationRepository& conversations,
                    common::ServerConfig config);

  common::Result<InitUploadResponse> InitUpload(const common::UserId& user_id,
                                                const common::ConversationId& conversation_id,
                                                std::int64_t file_size,
                                                const std::string& mime_hint);

  common::VoidResult WriteChunk(const common::AttachmentId& attachment_id,
                                std::int64_t offset,
                                const std::string& data);

  common::VoidResult FinalizeUpload(const common::AttachmentId& attachment_id, const std::string& ciphertext_hash);

  common::Result<std::filesystem::path> GetAttachment(const common::AttachmentId& attachment_id,
                                                      const common::UserId& user_id);

  int DeleteExpired();

private:
  common::Timestamp Now();

  store::AttachmentRepository& attachments_;
  store::ConversationRepository& conversations_;
  common::ServerConfig config_;
};

} // namespace vox::attachments

#endif // VOX_ATTACHMENTS_ATTACHMENT_SERVICE_HPP
