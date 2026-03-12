#include <gtest/gtest.h>

#include "test_suites/RelayTestSuite.hpp"
#include "lib/vox_common/uuid.hpp"

TEST_F(RelayTestSuite, SendMessageToDm) {
  auto alice = CreateTestUser("alice");
  auto bob = CreateTestUser("bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = alice.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "encrypted_hello";

  auto result = relay_->SendMessage(req);
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->envelope_id.empty());
  ASSERT_GT(result->server_timestamp, 0);
  ASSERT_EQ(result->delivered_to_count, 1u);
}

TEST_F(RelayTestSuite, SendMessageToGroup) {
  auto alice = CreateTestUser("alice_g");
  auto bob = CreateTestUser("bob_g");
  auto charlie = CreateTestUser("charlie_g");
  auto conv_id =
      CreateTestConversation(vox::common::ConversationType::kGroup, alice.user_id, {alice, bob, charlie});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = alice.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "group_message";

  auto result = relay_->SendMessage(req);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->delivered_to_count, 2u);
}

TEST_F(RelayTestSuite, DequeueReturnsInOrder) {
  auto alice = CreateTestUser("order_alice");
  auto bob = CreateTestUser("order_bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  for (int i = 0; i < 5; ++i) {
    vox::relay::SendMessageRequest req;
    req.sender_device_id = alice.device_id;
    req.conversation_id = conv_id;
    req.ciphertext = "msg_" + std::to_string(i);
    relay_->SendMessage(req);
  }

  auto queued = delivery_->Dequeue(bob.device_id, 10);
  ASSERT_EQ(queued.size(), 5u);
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(queued[i].ciphertext, "msg_" + std::to_string(i));
  }
}

TEST_F(RelayTestSuite, AcknowledgeRemovesFromDb) {
  auto alice = CreateTestUser("ack_alice");
  auto bob = CreateTestUser("ack_bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = alice.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "ack_test";
  auto send_result = relay_->SendMessage(req);

  auto queued = delivery_->Dequeue(bob.device_id, 10);
  ASSERT_EQ(queued.size(), 1u);

  envelopes_->AddDeliveryState(send_result->envelope_id, bob.device_id, send_result->server_timestamp);
  auto ack_result = relay_->AcknowledgeEnvelope(bob.device_id, send_result->envelope_id);
  ASSERT_TRUE(ack_result.has_value());
}

TEST_F(RelayTestSuite, SendToNonMemberFails) {
  auto alice = CreateTestUser("nm_alice");
  auto bob = CreateTestUser("nm_bob");
  auto outsider = CreateTestUser("nm_outsider");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = outsider.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "should_fail";

  auto result = relay_->SendMessage(req);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kForbidden);
}

TEST_F(RelayTestSuite, DuplicateEnvelopeRejected) {
  auto alice = CreateTestUser("dup_alice");
  auto bob = CreateTestUser("dup_bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  auto env_id = vox::common::GenerateUuid();

  vox::relay::SendMessageRequest req;
  req.sender_device_id = alice.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "first";
  req.envelope_id = env_id;

  ASSERT_TRUE(relay_->SendMessage(req).has_value());

  req.ciphertext = "duplicate";
  auto result = relay_->SendMessage(req);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kDuplicate);
}

TEST_F(RelayTestSuite, ChannelNonAdminPublishFails) {
  auto admin = CreateTestUser("ch_admin");
  auto subscriber = CreateTestUser("ch_sub");
  auto conv_id =
      CreateTestConversation(vox::common::ConversationType::kChannel, admin.user_id, {admin, subscriber});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = subscriber.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "unauthorized_post";

  auto result = relay_->SendMessage(req);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kForbidden);
}

TEST_F(RelayTestSuite, ChannelAdminPublishSucceeds) {
  auto admin = CreateTestUser("ch_admin2");
  auto subscriber = CreateTestUser("ch_sub2");
  auto conv_id =
      CreateTestConversation(vox::common::ConversationType::kChannel, admin.user_id, {admin, subscriber});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = admin.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "broadcast_msg";

  auto result = relay_->SendMessage(req);
  ASSERT_TRUE(result.has_value());
}

TEST_F(RelayTestSuite, QueueOverflowReturnsError) {
  auto alice = CreateTestUser("overflow_alice");
  auto bob = CreateTestUser("overflow_bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  delivery_ = std::make_unique<vox::relay::DeliveryManager>(*envelopes_, 3);
  relay_ = std::make_unique<vox::relay::RelayService>(*envelopes_, *conversations_, *devices_, *delivery_);

  for (int i = 0; i < 3; ++i) {
    vox::relay::SendMessageRequest req;
    req.sender_device_id = alice.device_id;
    req.conversation_id = conv_id;
    req.ciphertext = "msg_" + std::to_string(i);
    ASSERT_TRUE(relay_->SendMessage(req).has_value());
  }

  vox::relay::SendMessageRequest overflow_req;
  overflow_req.sender_device_id = alice.device_id;
  overflow_req.conversation_id = conv_id;
  overflow_req.ciphertext = "overflow_msg";
  auto result = relay_->SendMessage(overflow_req);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->delivered_to_count, 0u);
}

TEST_F(RelayTestSuite, SyncOfflineReturnsPersistedEnvelopes) {
  auto alice = CreateTestUser("sync_alice");
  auto bob = CreateTestUser("sync_bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = alice.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "offline_msg";
  auto send_result = relay_->SendMessage(req);
  ASSERT_TRUE(send_result.has_value());

  envelopes_->AddDeliveryState(send_result->envelope_id, bob.device_id, send_result->server_timestamp);

  auto pending = relay_->SyncOffline(bob.device_id, 100);
  ASSERT_EQ(pending.size(), 1u);
  ASSERT_EQ(pending[0].ciphertext, "offline_msg");
}

TEST_F(RelayTestSuite, EmptyCiphertextRejected) {
  auto alice = CreateTestUser("empty_ct_alice");
  auto bob = CreateTestUser("empty_ct_bob");
  auto conv_id = CreateTestConversation(vox::common::ConversationType::kDm, alice.user_id, {alice, bob});

  vox::relay::SendMessageRequest req;
  req.sender_device_id = alice.device_id;
  req.conversation_id = conv_id;
  req.ciphertext = "";

  auto result = relay_->SendMessage(req);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kInvalidArgument);
}
