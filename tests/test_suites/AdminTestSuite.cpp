#include "AdminTestSuite.hpp"

#include <stdexcept>

#include "lib/vox_common/uuid.hpp"

namespace {

constexpr vox::common::Timestamp kTestCreatedAt = 1000000;

} // namespace

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
