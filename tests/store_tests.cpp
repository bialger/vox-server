#include <gtest/gtest.h>

#include "lib/vox_common/uuid.hpp"
#include "test_suites/StoreTestSuite.hpp"

TEST_F(StoreTestSuite, CreateAndFindUserByUsername) {
  auto user = MakeUser("alice");
  auto result = users_->CreateUser(user);
  ASSERT_TRUE(result.has_value());

  auto found = users_->FindByUsername("alice");
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->username, "alice");
  ASSERT_EQ(found->user_id, user.user_id);
}

TEST_F(StoreTestSuite, CreateAndFindUserById) {
  auto user = MakeUser("bob");
  users_->CreateUser(user);

  auto found = users_->FindById(user.user_id);
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->username, "bob");
}

TEST_F(StoreTestSuite, DuplicateUsernameRejected) {
  auto user1 = MakeUser("charlie");
  auto user2 = MakeUser("charlie");
  ASSERT_TRUE(users_->CreateUser(user1).has_value());
  auto result = users_->CreateUser(user2);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kAlreadyExists);
}

TEST_F(StoreTestSuite, FindNonExistentUserReturnsEmpty) {
  auto found = users_->FindByUsername("nonexistent");
  ASSERT_FALSE(found.has_value());
}

TEST_F(StoreTestSuite, DisableUser) {
  auto user = MakeUser("dave");
  users_->CreateUser(user);
  auto result = users_->DisableUser(user.user_id, 2000000);
  ASSERT_TRUE(result.has_value());

  auto found = users_->FindById(user.user_id);
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->disabled_at.has_value());
}

TEST_F(StoreTestSuite, ListUsers) {
  users_->CreateUser(MakeUser("u1"));
  users_->CreateUser(MakeUser("u2"));
  users_->CreateUser(MakeUser("u3"));

  auto list = users_->ListUsers(10, 0);
  ASSERT_EQ(list.size(), 3u);
}

TEST_F(StoreTestSuite, RegisterAndFindDevice) {
  auto user = MakeUser("eve");
  users_->CreateUser(user);

  auto device = MakeDevice(user.user_id, "dev1");
  auto result = devices_->RegisterDevice(device);
  ASSERT_TRUE(result.has_value());

  auto found = devices_->FindById("dev1");
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->user_id, user.user_id);
}

TEST_F(StoreTestSuite, GetDevicesForUser) {
  auto user = MakeUser("frank");
  users_->CreateUser(user);

  devices_->RegisterDevice(MakeDevice(user.user_id, "dev_a"));
  devices_->RegisterDevice(MakeDevice(user.user_id, "dev_b"));

  auto devs = devices_->GetDevicesForUser(user.user_id);
  ASSERT_EQ(devs.size(), 2u);
}

TEST_F(StoreTestSuite, StorePrekeyAndConsume) {
  auto user = MakeUser("grace");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "dev_pk");
  devices_->RegisterDevice(device);

  std::vector<vox::store::PrekeyRecord> prekeys;
  for (int i = 0; i < 3; ++i) {
    vox::store::PrekeyRecord pk;
    pk.prekey_id = "pk_" + std::to_string(i);
    pk.device_id = "dev_pk";
    pk.prekey_public = "pub_" + std::to_string(i);
    prekeys.push_back(pk);
  }
  devices_->StorePrekeys("dev_pk", prekeys);

  auto consumed = devices_->ConsumeOneTimePrekey("dev_pk");
  ASSERT_TRUE(consumed.has_value());
  ASSERT_TRUE(consumed->consumed_at.has_value());
}

TEST_F(StoreTestSuite, ConsumeAlreadyConsumedPrekeyFails) {
  auto user = MakeUser("henry");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "dev_pk2");
  devices_->RegisterDevice(device);

  std::vector<vox::store::PrekeyRecord> prekeys;
  vox::store::PrekeyRecord pk;
  pk.prekey_id = "single_pk";
  pk.device_id = "dev_pk2";
  pk.prekey_public = "pub_single";
  prekeys.push_back(pk);
  devices_->StorePrekeys("dev_pk2", prekeys);

  auto first = devices_->ConsumeOneTimePrekey("dev_pk2");
  ASSERT_TRUE(first.has_value());
  auto second = devices_->ConsumeOneTimePrekey("dev_pk2");
  ASSERT_FALSE(second.has_value());
  ASSERT_EQ(second.error().code, vox::common::ErrorCode::kNotFound);
}

TEST_F(StoreTestSuite, CreateConversationAndAddMembers) {
  auto alice = MakeUser("alice_conv");
  auto bob = MakeUser("bob_conv");
  users_->CreateUser(alice);
  users_->CreateUser(bob);

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kGroup;
  conv.created_by = alice.user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);

  conversations_->AddMember(conv.conversation_id, alice.user_id, vox::common::MemberRole::kOwner, 1000000);
  conversations_->AddMember(conv.conversation_id, bob.user_id, vox::common::MemberRole::kMember, 1000001);

  auto members = conversations_->GetMembers(conv.conversation_id);
  ASSERT_EQ(members.size(), 2u);
  ASSERT_TRUE(conversations_->IsUserInConversation(conv.conversation_id, alice.user_id));
  ASSERT_TRUE(conversations_->IsUserInConversation(conv.conversation_id, bob.user_id));
}

TEST_F(StoreTestSuite, RemoveMemberTwiceIsIdempotent) {
  auto user = MakeUser("remove_test");
  users_->CreateUser(user);

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kGroup;
  conv.created_by = user.user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);
  conversations_->AddMember(conv.conversation_id, user.user_id, vox::common::MemberRole::kOwner, 1000000);

  auto r1 = conversations_->RemoveMember(conv.conversation_id, user.user_id, 2000000);
  ASSERT_TRUE(r1.has_value());
  auto r2 = conversations_->RemoveMember(conv.conversation_id, user.user_id, 3000000);
  ASSERT_TRUE(r2.has_value());
}

TEST_F(StoreTestSuite, StoreAndRetrieveEnvelope) {
  auto user = MakeUser("env_user");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "env_dev");
  devices_->RegisterDevice(device);

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);

  vox::store::EnvelopeRecord env;
  env.envelope_id = vox::common::GenerateUuid();
  env.conversation_id = conv.conversation_id;
  env.sender_device_id = "env_dev";
  env.ciphertext = "encrypted_data";
  env.server_timestamp = 1000001;
  envelopes_->StoreEnvelope(env);

  auto found = envelopes_->FindById(env.envelope_id);
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->ciphertext, "encrypted_data");
}

TEST_F(StoreTestSuite, DuplicateEnvelopeRejected) {
  auto user = MakeUser("dup_env_user");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "dup_env_dev");
  devices_->RegisterDevice(device);

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);

  vox::store::EnvelopeRecord env;
  env.envelope_id = vox::common::GenerateUuid();
  env.conversation_id = conv.conversation_id;
  env.sender_device_id = "dup_env_dev";
  env.ciphertext = "data";
  env.server_timestamp = 1000001;

  ASSERT_TRUE(envelopes_->StoreEnvelope(env).has_value());
  ASSERT_FALSE(envelopes_->StoreEnvelope(env).has_value());
}

TEST_F(StoreTestSuite, CheckDuplicateDetection) {
  auto user = MakeUser("chk_dup_user");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "chk_dup_dev");
  devices_->RegisterDevice(device);

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);

  auto env_id = vox::common::GenerateUuid();
  ASSERT_FALSE(envelopes_->CheckDuplicate(env_id));

  vox::store::EnvelopeRecord env;
  env.envelope_id = env_id;
  env.conversation_id = conv.conversation_id;
  env.sender_device_id = "chk_dup_dev";
  env.ciphertext = "data";
  env.server_timestamp = 1000001;
  envelopes_->StoreEnvelope(env);

  ASSERT_TRUE(envelopes_->CheckDuplicate(env_id));
}

TEST_F(StoreTestSuite, ChannelSubscribeAndUnsubscribe) {
  auto admin = MakeUser("ch_admin");
  auto sub = MakeUser("ch_sub");
  users_->CreateUser(admin);
  users_->CreateUser(sub);

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kChannel;
  conv.created_by = admin.user_id;
  conv.created_at = 1000000;
  conversations_->CreateConversation(conv);
  conversations_->AddMember(conv.conversation_id, admin.user_id, vox::common::MemberRole::kOwner, 1000000);

  conversations_->Subscribe(conv.conversation_id, sub.user_id, 1000001);
  ASSERT_TRUE(conversations_->IsUserInConversation(conv.conversation_id, sub.user_id));

  auto subs = conversations_->GetSubscribers(conv.conversation_id);
  ASSERT_EQ(subs.size(), 1u);

  conversations_->Unsubscribe(conv.conversation_id, sub.user_id, 2000000);
  ASSERT_FALSE(conversations_->IsUserInConversation(conv.conversation_id, sub.user_id));
}

TEST_F(StoreTestSuite, SessionCreateAndFind) {
  auto user = MakeUser("sess_user");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "sess_dev");
  devices_->RegisterDevice(device);

  vox::store::SessionRecord session;
  session.session_id = vox::common::GenerateUuid();
  session.user_id = user.user_id;
  session.device_id = "sess_dev";
  session.access_token_hash = "ath_123";
  session.refresh_token_hash = "rth_456";
  session.access_expires_at = 9999999;
  session.refresh_expires_at = 99999999;
  session.created_at = 1000000;

  auto result = sessions_->CreateSession(session);
  ASSERT_TRUE(result.has_value());

  auto found = sessions_->FindByAccessToken("ath_123");
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->user_id, user.user_id);
}

TEST_F(StoreTestSuite, RevokeSessionPreventsFinding) {
  auto user = MakeUser("revoke_user");
  users_->CreateUser(user);
  auto device = MakeDevice(user.user_id, "revoke_dev");
  devices_->RegisterDevice(device);

  vox::store::SessionRecord session;
  session.session_id = vox::common::GenerateUuid();
  session.user_id = user.user_id;
  session.device_id = "revoke_dev";
  session.access_token_hash = "ath_revoke";
  session.refresh_token_hash = "rth_revoke";
  session.access_expires_at = 9999999;
  session.refresh_expires_at = 99999999;
  session.created_at = 1000000;
  sessions_->CreateSession(session);

  sessions_->RevokeSession(session.session_id, 2000000);

  auto found = sessions_->FindByAccessToken("ath_revoke");
  ASSERT_FALSE(found.has_value());
}
