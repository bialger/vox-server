#ifndef RELAYTESTSUITE_HPP
#define RELAYTESTSUITE_HPP

#include <memory>

#include <gtest/gtest.h>

#include "lib/vox_common/uuid.hpp"
#include "lib/vox_relay/delivery_manager.hpp"
#include "lib/vox_relay/relay_service.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/envelope_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

class RelayTestSuite : public testing::Test {
protected:
  std::unique_ptr<vox::store::Database> db_;
  std::unique_ptr<vox::store::UserRepository> users_;
  std::unique_ptr<vox::store::DeviceRepository> devices_;
  std::unique_ptr<vox::store::ConversationRepository> conversations_;
  std::unique_ptr<vox::store::EnvelopeRepository> envelopes_;
  std::unique_ptr<vox::relay::DeliveryManager> delivery_;
  std::unique_ptr<vox::relay::RelayService> relay_;

  void SetUp() override;
  void TearDown() override;

  struct TestUser {
    std::string user_id;
    std::string device_id;
  };

  TestUser CreateTestUser(const std::string& username);
  std::string CreateTestConversation(vox::common::ConversationType type,
                                     const std::string& creator_user_id,
                                     const std::vector<TestUser>& members);
};

#endif // RELAYTESTSUITE_HPP
