#ifndef ADMINTESTSUITE_HPP
#define ADMINTESTSUITE_HPP

#include <memory>

#include <gtest/gtest.h>

#include "lib/vox_admin/admin_service.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/session_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

class AdminTestSuite : public testing::Test {
 protected:
  std::unique_ptr<vox::store::Database> db_;
  std::unique_ptr<vox::store::UserRepository> users_;
  std::unique_ptr<vox::store::DeviceRepository> devices_;
  std::unique_ptr<vox::store::SessionRepository> sessions_;
  std::unique_ptr<vox::store::ConversationRepository> conversations_;
  std::unique_ptr<vox::admin::AdminService> admin_;

  void SetUp() override;
  void TearDown() override;

  struct TestUser {
    std::string user_id;
    std::string device_id;
  };

  TestUser CreateTestUser(const std::string& username);
};

#endif  // ADMINTESTSUITE_HPP
