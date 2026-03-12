#include "AdminTestSuite.hpp"

#include "lib/vox_common/uuid.hpp"

void AdminTestSuite::SetUp() {
  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  sessions_ = std::make_unique<vox::store::SessionRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  admin_ = std::make_unique<vox::admin::AdminService>(*db_, *users_, *sessions_);
}

void AdminTestSuite::TearDown() {
  admin_.reset();
  conversations_.reset();
  sessions_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
}

AdminTestSuite::TestUser AdminTestSuite::CreateTestUser(const std::string& username) {
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
