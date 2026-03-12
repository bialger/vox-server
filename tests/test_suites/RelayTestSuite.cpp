#include "RelayTestSuite.hpp"

void RelayTestSuite::SetUp() {
  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  envelopes_ = std::make_unique<vox::store::EnvelopeRepository>(*db_);
  delivery_ = std::make_unique<vox::relay::DeliveryManager>(*envelopes_, 100);
  relay_ = std::make_unique<vox::relay::RelayService>(*envelopes_, *conversations_, *devices_, *delivery_);
}

void RelayTestSuite::TearDown() {
  relay_.reset();
  delivery_.reset();
  envelopes_.reset();
  conversations_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
}

RelayTestSuite::TestUser RelayTestSuite::CreateTestUser(const std::string& username) {
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
  device.identity_key_public = "ik_" + dev_id;
  device.signed_prekey_public = "spk_" + dev_id;
  device.signed_prekey_signature = "sig_" + dev_id;
  devices_->RegisterDevice(device);

  return {user.user_id, dev_id};
}

std::string RelayTestSuite::CreateTestConversation(vox::common::ConversationType type,
                                                   const std::string& creator_user_id,
                                                   const std::vector<TestUser>& members) {
  auto conv_id = vox::common::GenerateUuid();
  vox::store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = type;
  conv.created_by = creator_user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);

  bool first = true;
  for (const auto& m : members) {
    auto role = first ? vox::common::MemberRole::kOwner : vox::common::MemberRole::kMember;
    conversations_->AddMember(conv_id, m.user_id, role, 1000000);
    if (type == vox::common::ConversationType::kChannel) {
      conversations_->Subscribe(conv_id, m.user_id, 1000000);
    }
    first = false;
  }
  return conv_id;
}
