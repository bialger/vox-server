#include <gtest/gtest.h>

#include "lib/vox_common/uuid.hpp"
#include "test_suites/AdminTestSuite.hpp"

namespace {

constexpr vox::common::Timestamp kTestBaseTimestamp = 1000000;
constexpr vox::common::Timestamp kTestAccessExpiry = 9999999;
constexpr vox::common::Timestamp kTestRefreshExpiry = 99999999;
constexpr int kForceLogoutSessionCount = 3;

} // namespace

TEST_F(AdminTestSuite, EmptyDbStatsReturnsZeros) {
  auto stats = admin_->GetServerStats();
  ASSERT_EQ(stats.user_count, 0u);
  ASSERT_EQ(stats.device_count, 0u);
  ASSERT_EQ(stats.active_session_count, 0u);
  ASSERT_EQ(stats.conversation_count, 0u);
  ASSERT_EQ(stats.pending_envelope_count, 0u);
  ASSERT_EQ(stats.total_storage_bytes, 0);
}

TEST_F(AdminTestSuite, StatsReturnsCorrectCounts) {
  auto alice = CreateTestUser("stats_alice");
  auto bob = CreateTestUser("stats_bob");

  vox::store::ConversationRecord conv;
  conv.conversation_id = vox::common::GenerateUuid();
  conv.type = vox::common::ConversationType::kDm;
  conv.created_by = alice.user_id;
  conv.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(conversations_->CreateConversation(conv));

  auto stats = admin_->GetServerStats();
  ASSERT_EQ(stats.user_count, 2u);
  ASSERT_EQ(stats.device_count, 2u);
  ASSERT_EQ(stats.conversation_count, 1u);
}

TEST_F(AdminTestSuite, DeleteUserCascadesCleanly) {
  auto alice = CreateTestUser("del_alice");

  vox::store::SessionRecord session;
  session.session_id = vox::common::GenerateUuid();
  session.user_id = alice.user_id;
  session.device_id = alice.device_id;
  session.access_token_hash = "ath_del";
  session.refresh_token_hash = "rth_del";
  session.access_expires_at = kTestAccessExpiry;
  session.refresh_expires_at = kTestRefreshExpiry;
  session.created_at = kTestBaseTimestamp;
  ASSERT_TRUE(sessions_->CreateSession(session));

  auto result = admin_->DeleteUser(alice.user_id);
  ASSERT_TRUE(result.has_value());

  ASSERT_FALSE(users_->FindById(alice.user_id).has_value());
  ASSERT_TRUE(devices_->GetDevicesForUser(alice.user_id).empty());

  auto stats = admin_->GetServerStats();
  ASSERT_EQ(stats.user_count, 0u);
  ASSERT_EQ(stats.device_count, 0u);
}

TEST_F(AdminTestSuite, DeleteNonExistentUserFails) {
  auto result = admin_->DeleteUser("nonexistent_id");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kNotFound);
}

TEST_F(AdminTestSuite, ForceLogoutRevokesAllSessions) {
  auto alice = CreateTestUser("logout_alice");

  for (int i = 0; i < kForceLogoutSessionCount; ++i) {
    vox::store::SessionRecord session;
    session.session_id = vox::common::GenerateUuid();
    session.user_id = alice.user_id;
    session.device_id = alice.device_id;
    session.access_token_hash = "ath_" + std::to_string(i);
    session.refresh_token_hash = "rth_" + std::to_string(i);
    session.access_expires_at = kTestAccessExpiry;
    session.refresh_expires_at = kTestRefreshExpiry;
    session.created_at = kTestBaseTimestamp + i;
    ASSERT_TRUE(sessions_->CreateSession(session));
  }

  auto count_before = sessions_->CountActiveForUser(alice.user_id, kTestBaseTimestamp);
  ASSERT_EQ(count_before, static_cast<std::size_t>(kForceLogoutSessionCount));

  auto result = admin_->ForceLogout(alice.user_id);
  ASSERT_TRUE(result.has_value());

  auto count_after = sessions_->CountActiveForUser(alice.user_id, kTestBaseTimestamp);
  ASSERT_EQ(count_after, 0u);
}

TEST_F(AdminTestSuite, ForceLogoutNonExistentUserFails) {
  auto result = admin_->ForceLogout("nonexistent_id");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kNotFound);
}
