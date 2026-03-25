#include "RelayTestSuite.hpp"

#include <stdexcept>

#include "lib/vox_common/config.hpp"

namespace {

constexpr std::size_t kDeliveryManagerMaxQueuePerDevice = 100;
constexpr vox::common::Timestamp kTestCreatedAt = 1000000;

} // namespace

void RelayTestSuite::SetUp() {
  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  envelopes_ = std::make_unique<vox::store::EnvelopeRepository>(*db_);
  delivery_ = std::make_unique<vox::relay::DeliveryManager>(*envelopes_, kDeliveryManagerMaxQueuePerDevice);
  relay_ = std::make_unique<vox::relay::RelayService>(*envelopes_, *conversations_, *devices_, *delivery_);
  conv_service_ = std::make_unique<vox::relay::ConversationService>(*conversations_, vox::common::ServerConfig::Default());
}

void RelayTestSuite::TearDown() {
  conv_service_.reset();
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
  user.created_at = kTestCreatedAt;
  auto create_result = users_->CreateUser(user);
  if (!create_result.has_value()) {
    throw std::runtime_error("CreateUser failed");
  }

  auto dev_id = vox::common::GenerateUuid();
  vox::store::DeviceRecord device;
  device.device_id = dev_id;
  device.user_id = user.user_id;
  device.identity_key_public = "ik_" + dev_id;
  device.signed_prekey_public = "spk_" + dev_id;
  device.signed_prekey_signature = "sig_" + dev_id;
  auto reg_result = devices_->RegisterDevice(device);
  if (!reg_result.has_value()) {
    throw std::runtime_error("RegisterDevice failed");
  }

  return {.user_id = user.user_id, .device_id = dev_id};
}

std::string RelayTestSuite::CreateTestConversation(vox::common::ConversationType type,
                                                   const std::string& creator_user_id,
                                                   const std::vector<TestUser>& members) {
  auto conv_id = vox::common::GenerateUuid();
  vox::store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = type;
  conv.created_by = creator_user_id;
  conv.created_at = kTestCreatedAt;
  auto conv_result = conversations_->CreateConversation(conv);
  if (!conv_result.has_value()) {
    throw std::runtime_error("CreateConversation failed");
  }

  bool first = true;
  for (const auto& m : members) {
    auto role = first ? vox::common::MemberRole::kOwner : vox::common::MemberRole::kMember;
    auto add_result = conversations_->AddMember(conv_id, m.user_id, role, kTestCreatedAt);
    if (!add_result.has_value()) {
      throw std::runtime_error("AddMember failed");
    }
    if (type == vox::common::ConversationType::kChannel) {
      auto sub_result = conversations_->Subscribe(conv_id, m.user_id, kTestCreatedAt);
      if (!sub_result.has_value()) {
        throw std::runtime_error("Subscribe failed");
      }
    }
    first = false;
  }
  return conv_id;
}
