#include "StoreTestSuite.hpp"

#include "lib/vox_common/uuid.hpp"

namespace {

constexpr vox::common::Timestamp kMakeUserCreatedAt = 1000000;

} // namespace

void StoreTestSuite::SetUp() {
  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  sessions_ = std::make_unique<vox::store::SessionRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  envelopes_ = std::make_unique<vox::store::EnvelopeRepository>(*db_);
  attachments_ = std::make_unique<vox::store::AttachmentRepository>(*db_);
}

void StoreTestSuite::TearDown() {
  attachments_.reset();
  envelopes_.reset();
  conversations_.reset();
  sessions_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
}

vox::store::UserRecord StoreTestSuite::MakeUser(const std::string& username) {
  vox::store::UserRecord user;
  user.user_id = vox::common::GenerateUuid();
  user.username = username;
  user.password_salt = "test_salt";
  user.password_verifier = "test_verifier";
  user.created_at = kMakeUserCreatedAt;
  return user;
}

vox::store::DeviceRecord StoreTestSuite::MakeDevice(const std::string& user_id, const std::string& device_id) {
  vox::store::DeviceRecord device;
  device.device_id = device_id;
  device.user_id = user_id;
  device.identity_key_public = "ik_pub_" + device_id;
  device.signed_prekey_public = "spk_pub_" + device_id;
  device.signed_prekey_signature = "spk_sig_" + device_id;
  return device;
}
