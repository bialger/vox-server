#ifndef ATTACHMENTTESTSUITE_HPP
#define ATTACHMENTTESTSUITE_HPP

#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "lib/vox_attachments/attachment_service.hpp"
#include "lib/vox_common/config.hpp"
#include "lib/vox_store/attachment_repository.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

class AttachmentTestSuite : public testing::Test {
 protected:
  static const std::filesystem::path kBlobDir;

  std::unique_ptr<vox::store::Database> db_;
  std::unique_ptr<vox::store::UserRepository> users_;
  std::unique_ptr<vox::store::DeviceRepository> devices_;
  std::unique_ptr<vox::store::ConversationRepository> conversations_;
  std::unique_ptr<vox::store::AttachmentRepository> attachments_;
  std::unique_ptr<vox::attachments::AttachmentService> service_;

  void SetUp() override;
  void TearDown() override;

  struct TestUser {
    std::string user_id;
    std::string device_id;
  };

  TestUser CreateTestUser(const std::string& username);
  std::string CreateTestConversation(const std::string& creator_user_id, const std::vector<TestUser>& members);
};

#endif  // ATTACHMENTTESTSUITE_HPP
