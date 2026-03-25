#include <gtest/gtest.h>

#include "lib/vox_common/uuid.hpp"
#include "test_suites/StoreTestSuite.hpp"

namespace {

constexpr vox::common::Timestamp kTestBaseTimestamp = 1000000;
constexpr vox::common::Timestamp kTestTimestampOffset1 = 1000001;
constexpr vox::common::Timestamp kDisableUserTimestamp = 2000000;
constexpr vox::common::Timestamp kRemoveMemberTimestamp1 = 2000000;
constexpr vox::common::Timestamp kRemoveMemberTimestamp2 = 3000000;
constexpr vox::common::Timestamp kUnsubscribeTimestamp = 2000000;
constexpr std::size_t kListUsersLimit = 10;
constexpr std::size_t kEnvelopeListPageSize = 10;
constexpr std::size_t kListUsersOffset = 0;
constexpr int kPrekeyCount = 3;
constexpr vox::common::Timestamp kTestAccessExpiry = 9999999;
constexpr vox::common::Timestamp kTestRefreshExpiry = 99999999;

} // namespace

TEST_F(StoreTestSuite, CreateAndFindUserByUsername) {
  auto user = MakeUser("alice");
  auto result = users_->CreateUser(user);
  ASSERT_TRUE(result.has_value());

  auto found = users_->FindByUsername("alice");
  ASSERT_TRUE(found.has_value());
  if (found) {
    const auto& found_ref = *found;
    ASSERT_EQ(found_ref.username, "alice");
    ASSERT_EQ(found_ref.user_id, user.user_id);
  }
}

TEST_F(StoreTestSuite, CreateAndFindUserById) {
  auto user = MakeUser("bob");
  ASSERT_TRUE(users_->CreateUser(user));

  auto found = users_->FindById(user.user_id);
  ASSERT_TRUE(found.has_value());
  if (found) {
    ASSERT_EQ(found->username, "bob");
  }
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
  ASSERT_TRUE(users_->CreateUser(user));
  auto result = users_->DisableUser(user.user_id, kDisableUserTimestamp);
  ASSERT_TRUE(result.has_value());

  auto found = users_->FindById(user.user_id);
  ASSERT_TRUE(found.has_value());
  if (found) {
    ASSERT_TRUE(found->disabled_at.has_value());
  }
}

TEST_F(StoreTestSuite, ListUsers) {
  ASSERT_TRUE(users_->CreateUser(MakeUser("u1")));
  ASSERT_TRUE(users_->CreateUser(MakeUser("u2")));
  ASSERT_TRUE(users_->CreateUser(MakeUser("u3")));

  auto list = users_->ListUsers(kListUsersLimit, kListUsersOffset);
  ASSERT_EQ(list.size(), 3u);
}

TEST_F(StoreTestSuite, RegisterAndFindDevice) {
  auto user = MakeUser("eve");
  ASSERT_TRUE(users_->CreateUser(user));

  auto device = MakeDevice(user.user_id, "dev1");
  auto result = devices_->RegisterDevice(device);
  ASSERT_TRUE(result.has_value());

  auto found = devices_->FindById("dev1");
  ASSERT_TRUE(found.has_value());
  if (found) {
    const auto& found_ref = *found;
    ASSERT_EQ(found_ref.user_id, user.user_id);
  }
}

TEST_F(StoreTestSuite, GetDevicesForUser) {
  auto user = MakeUser("frank");
  ASSERT_TRUE(users_->CreateUser(user));

  ASSERT_TRUE(devices_->RegisterDevice(MakeDevice(user.user_id, "dev_a")));
  ASSERT_TRUE(devices_->RegisterDevice(MakeDevice(user.user_id, "dev_b")));

  auto devs = devices_->GetDevicesForUser(user.user_id);
  ASSERT_EQ(devs.size(), 2u);
}

TEST_F(StoreTestSuite, StorePrekeyAndConsume) {
  auto user = MakeUser("grace");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "dev_pk");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  std::vector<vox::store::PrekeyRecord> prekeys;
  for (int i = 0; i < kPrekeyCount; ++i) {
    vox::store::PrekeyRecord pk;
    pk.prekey_id = "pk_" + std::to_string(i);
    pk.device_id = "dev_pk";
    pk.prekey_public = "pub_" + std::to_string(i);
    prekeys.push_back(pk);
  }
  ASSERT_TRUE(devices_->StorePrekeys("dev_pk", prekeys));

  auto consumed = devices_->ConsumeOneTimePrekey("dev_pk");
  ASSERT_TRUE(consumed.has_value());
  if (consumed.has_value()) {
    const auto& consumed_ref = consumed.value();
    ASSERT_TRUE(consumed_ref.consumed_at.has_value());
  }
}

TEST_F(StoreTestSuite, ConsumeAlreadyConsumedPrekeyFails) {
  auto user = MakeUser("henry");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "dev_pk2");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  std::vector<vox::store::PrekeyRecord> prekeys;
  vox::store::PrekeyRecord pk;
  pk.prekey_id = "single_pk";
  pk.device_id = "dev_pk2";
  pk.prekey_public = "pub_single";
  prekeys.push_back(pk);
  ASSERT_TRUE(devices_->StorePrekeys("dev_pk2", prekeys));

  auto first = devices_->ConsumeOneTimePrekey("dev_pk2");
  ASSERT_TRUE(first.has_value());
  auto second = devices_->ConsumeOneTimePrekey("dev_pk2");
  ASSERT_FALSE(second.has_value());
  ASSERT_EQ(second.error().code, vox::common::ErrorCode::kNotFound);
}

TEST_F(StoreTestSuite, CreateConversationAndAddMembers) {
  auto alice = MakeUser("alice_conv");
  auto bob = MakeUser("bob_conv");
  ASSERT_TRUE(users_->CreateUser(alice));
  ASSERT_TRUE(users_->CreateUser(bob));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kGroup;
  conv.created_by = alice.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));

  ASSERT_TRUE(conversations_->AddMember(
      conv.conversation_id, alice.user_id, vox::common::MemberRole::kOwner, kTestBaseTimestamp));
  ASSERT_TRUE(conversations_->AddMember(
      conv.conversation_id, bob.user_id, vox::common::MemberRole::kMember, kTestTimestampOffset1));

  auto members = conversations_->GetMembers(conv.conversation_id);
  ASSERT_EQ(members.size(), 2u);
  ASSERT_TRUE(conversations_->IsUserInConversation(conv.conversation_id, alice.user_id));
  ASSERT_TRUE(conversations_->IsUserInConversation(conv.conversation_id, bob.user_id));
}

TEST_F(StoreTestSuite, RemoveMemberTwiceIsIdempotent) {
  auto user = MakeUser("remove_test");
  ASSERT_TRUE(users_->CreateUser(user));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kGroup;
  conv.created_by = user.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));
  ASSERT_TRUE(conversations_->AddMember(
      conv.conversation_id, user.user_id, vox::common::MemberRole::kOwner, kTestBaseTimestamp));

  auto r1 = conversations_->RemoveMember(conv.conversation_id, user.user_id, kRemoveMemberTimestamp1);
  ASSERT_TRUE(r1.has_value());
  auto r2 = conversations_->RemoveMember(conv.conversation_id, user.user_id, kRemoveMemberTimestamp2);
  ASSERT_TRUE(r2.has_value());
}

TEST_F(StoreTestSuite, StoreAndRetrieveEnvelope) {
  auto user = MakeUser("env_user");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "env_dev");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));

  vox::store::EnvelopeRecord env;
  env.envelope_id = vox::common::GenerateUuid();
  env.conversation_id = conv.conversation_id;
  env.sender_device_id = "env_dev";
  env.ciphertext = "encrypted_data";
  env.server_timestamp = kTestTimestampOffset1;
  ASSERT_TRUE(envelopes_->StoreEnvelope(env));

  auto found = envelopes_->FindById(env.envelope_id);
  ASSERT_TRUE(found.has_value());
  if (found) {
    ASSERT_EQ(found->ciphertext, "encrypted_data");
  }
}

TEST_F(StoreTestSuite, DuplicateEnvelopeRejected) {
  auto user = MakeUser("dup_env_user");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "dup_env_dev");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));

  vox::store::EnvelopeRecord env;
  env.envelope_id = vox::common::GenerateUuid();
  env.conversation_id = conv.conversation_id;
  env.sender_device_id = "dup_env_dev";
  env.ciphertext = "data";
  env.server_timestamp = kTestTimestampOffset1;

  ASSERT_TRUE(envelopes_->StoreEnvelope(env).has_value());
  ASSERT_FALSE(envelopes_->StoreEnvelope(env).has_value());
}

TEST_F(StoreTestSuite, CheckDuplicateDetection) {
  auto user = MakeUser("chk_dup_user");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "chk_dup_dev");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));

  auto env_id = vox::common::GenerateUuid();
  ASSERT_FALSE(envelopes_->CheckDuplicate(env_id));

  vox::store::EnvelopeRecord env;
  env.envelope_id = env_id;
  env.conversation_id = conv.conversation_id;
  env.sender_device_id = "chk_dup_dev";
  env.ciphertext = "data";
  env.server_timestamp = kTestTimestampOffset1;
  ASSERT_TRUE(envelopes_->StoreEnvelope(env));

  ASSERT_TRUE(envelopes_->CheckDuplicate(env_id));
}

TEST_F(StoreTestSuite, ListForConversationPagination) {
  auto user = MakeUser("list_conv_user");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "list_conv_dev");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = user.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));

  for (int i = 1; i <= 3; ++i) {
    vox::store::EnvelopeRecord env;
    env.envelope_id = vox::common::GenerateUuid();
    env.conversation_id = conv.conversation_id;
    env.sender_device_id = "list_conv_dev";
    env.ciphertext = "m" + std::to_string(i);
    env.server_timestamp = kTestTimestampOffset1 + i;
    ASSERT_TRUE(envelopes_->StoreEnvelope(env));
  }

  auto first = envelopes_->ListForConversation(conv.conversation_id, 0, kEnvelopeListPageSize);
  ASSERT_EQ(first.size(), 3u);
  ASSERT_EQ(first[0].ciphertext, "m1");

  auto after_first =
      envelopes_->ListForConversation(conv.conversation_id, first[0].server_timestamp, kEnvelopeListPageSize);
  ASSERT_EQ(after_first.size(), 2u);
  ASSERT_EQ(after_first[0].ciphertext, "m2");

  auto other_conv = envelopes_->ListForConversation("other-id", 0, kEnvelopeListPageSize);
  ASSERT_TRUE(other_conv.empty());
}

TEST_F(StoreTestSuite, ChannelSubscribeAndUnsubscribe) {
  auto admin = MakeUser("ch_admin");
  auto sub = MakeUser("ch_sub");
  ASSERT_TRUE(users_->CreateUser(admin));
  ASSERT_TRUE(users_->CreateUser(sub));

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kChannel;
  conv.created_by = admin.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));
  ASSERT_TRUE(conversations_->AddMember(
      conv.conversation_id, admin.user_id, vox::common::MemberRole::kOwner, kTestBaseTimestamp));

  ASSERT_TRUE(conversations_->Subscribe(conv.conversation_id, sub.user_id, kTestTimestampOffset1));
  ASSERT_TRUE(conversations_->IsUserInConversation(conv.conversation_id, sub.user_id));

  auto subs = conversations_->GetSubscribers(conv.conversation_id);
  ASSERT_EQ(subs.size(), 1u);

  ASSERT_TRUE(conversations_->Unsubscribe(conv.conversation_id, sub.user_id, kUnsubscribeTimestamp));
  ASSERT_FALSE(conversations_->IsUserInConversation(conv.conversation_id, sub.user_id));
}

TEST_F(StoreTestSuite, SessionCreateAndFind) {
  auto user = MakeUser("sess_user");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "sess_dev");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  vox::store::SessionRecord session;
  session.session_id = vox::common::GenerateUuid();
  session.user_id = user.user_id;
  session.device_id = "sess_dev";
  session.access_token_hash = "ath_123";
  session.refresh_token_hash = "rth_456";
  session.access_expires_at = kTestAccessExpiry;
  session.refresh_expires_at = kTestRefreshExpiry;
  session.created_at = kTestBaseTimestamp;

  auto result = sessions_->CreateSession(session);
  ASSERT_TRUE(result.has_value());

  auto found = sessions_->FindByAccessToken("ath_123");
  ASSERT_TRUE(found.has_value());
  if (found.has_value()) {
    const auto& found_ref = found.value();
    ASSERT_EQ(found_ref.user_id, user.user_id);
  }
}

TEST_F(StoreTestSuite, RevokeSessionPreventsFinding) {
  auto user = MakeUser("revoke_user");
  ASSERT_TRUE(users_->CreateUser(user));
  auto device = MakeDevice(user.user_id, "revoke_dev");
  ASSERT_TRUE(devices_->RegisterDevice(device));

  vox::store::SessionRecord session;
  session.session_id = vox::common::GenerateUuid();
  session.user_id = user.user_id;
  session.device_id = "revoke_dev";
  session.access_token_hash = "ath_revoke";
  session.refresh_token_hash = "rth_revoke";
  session.access_expires_at = kTestAccessExpiry;
  session.refresh_expires_at = kTestRefreshExpiry;
  session.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(sessions_->CreateSession(session));

  ASSERT_TRUE(sessions_->RevokeSession(session.session_id, kDisableUserTimestamp));

  auto found = sessions_->FindByAccessToken("ath_revoke");
  ASSERT_FALSE(found.has_value());
}
