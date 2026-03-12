#include "AttachmentTestSuite.hpp"

#include <stdexcept>

#include "lib/vox_common/uuid.hpp"

namespace {

constexpr std::size_t kBytesPerKib = 1024;
constexpr std::size_t kMaxUploadMebib = 1;
constexpr std::size_t kMaxStorageMebib = 10;
constexpr std::int64_t kTestCreatedAt = 1000000;
constexpr std::int64_t kAttachmentRetentionSeconds = 3600;

} // namespace

const std::filesystem::path AttachmentTestSuite::kBlobDir = std::filesystem::temp_directory_path() / "vox_test_blobs";

void AttachmentTestSuite::SetUp() {
  std::filesystem::create_directories(kBlobDir);

  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  attachments_ = std::make_unique<vox::store::AttachmentRepository>(*db_);

  vox::common::ServerConfig config;
  config.blob_storage_path = kBlobDir;
  config.max_upload_size_bytes = kMaxUploadMebib * kBytesPerKib * kBytesPerKib;
  config.max_storage_per_user_bytes = kMaxStorageMebib * kBytesPerKib * kBytesPerKib;
  config.attachment_retention_seconds = kAttachmentRetentionSeconds;

  service_ = std::make_unique<vox::attachments::AttachmentService>(*attachments_, *conversations_, config);
}

void AttachmentTestSuite::TearDown() {
  service_.reset();
  attachments_.reset();
  conversations_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
  std::error_code ec;
  std::filesystem::remove_all(kBlobDir, ec);
}

AttachmentTestSuite::TestUser AttachmentTestSuite::CreateTestUser(const std::string& username) {
  vox::store::UserRecord user;
  user.user_id = vox::common::GenerateUuid();
  user.username = username;
  user.password_salt = "salt";
  user.password_verifier = "verifier";
  user.created_at = kTestCreatedAt;
  auto create_result = users_->CreateUser(user);
  if (!create_result.has_value()) {
    throw std::runtime_error("CreateUser failed");
  }

  auto dev_id = vox::common::GenerateUuid();
  vox::store::DeviceRecord device;
  device.device_id = dev_id;
  device.user_id = user.user_id;
  auto reg_result = devices_->RegisterDevice(device);
  if (!reg_result.has_value()) {
    throw std::runtime_error("RegisterDevice failed");
  }

  return {user.user_id, dev_id};
}

std::string AttachmentTestSuite::CreateTestConversation(const std::string& creator_user_id,
                                                        const std::vector<TestUser>& members) {
  auto conv_id = vox::common::GenerateUuid();
  vox::store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = vox::common::ConversationType::kGroup;
  conv.created_by = creator_user_id;
  conv.created_at = kTestCreatedAt;
  auto create_conv = conversations_->CreateConversation(conv);
  if (!create_conv.has_value()) {
    throw std::runtime_error("CreateConversation failed");
  }

  bool first = true;
  for (const auto& m : members) {
    auto role = first ? vox::common::MemberRole::kOwner : vox::common::MemberRole::kMember;
    auto add_result = conversations_->AddMember(conv_id, m.user_id, role, kTestCreatedAt);
    if (!add_result.has_value()) {
      throw std::runtime_error("AddMember failed");
    }
    first = false;
  }
  return conv_id;
}
