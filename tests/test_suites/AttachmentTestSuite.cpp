#include "AttachmentTestSuite.hpp"

#include "lib/vox_common/uuid.hpp"

const std::filesystem::path AttachmentTestSuite::kBlobDir =
    std::filesystem::temp_directory_path() / "vox_test_blobs";

void AttachmentTestSuite::SetUp() {
  std::filesystem::create_directories(kBlobDir);

  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  attachments_ = std::make_unique<vox::store::AttachmentRepository>(*db_);

  vox::common::ServerConfig config;
  config.blob_storage_path = kBlobDir;
  config.max_upload_size_bytes = 1024 * 1024;
  config.max_storage_per_user_bytes = 10 * 1024 * 1024;
  config.attachment_retention_seconds = 3600;

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
  user.created_at = 1000000;
  users_->CreateUser(user);

  auto dev_id = vox::common::GenerateUuid();
  vox::store::DeviceRecord device;
  device.device_id = dev_id;
  device.user_id = user.user_id;
  devices_->RegisterDevice(device);

  return {user.user_id, dev_id};
}

std::string AttachmentTestSuite::CreateTestConversation(const std::string& creator_user_id,
                                                        const std::vector<TestUser>& members) {
  auto conv_id = vox::common::GenerateUuid();
  vox::store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = vox::common::ConversationType::kGroup;
  conv.created_by = creator_user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);

  bool first = true;
  for (const auto& m : members) {
    auto role = first ? vox::common::MemberRole::kOwner : vox::common::MemberRole::kMember;
    conversations_->AddMember(conv_id, m.user_id, role, 1000000);
    first = false;
  }
  return conv_id;
}
