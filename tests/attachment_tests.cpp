#include <fstream>

#include <gtest/gtest.h>

#include "test_suites/AttachmentTestSuite.hpp"

TEST_F(AttachmentTestSuite, InitUploadAndWriteChunk) {
  auto alice = CreateTestUser("att_alice");
  auto bob = CreateTestUser("att_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, 1024, "application/octet-stream");
  ASSERT_TRUE(init.has_value());
  ASSERT_FALSE(init->attachment_id.empty());

  std::string data(1024, 'X');
  auto write_result = service_->WriteChunk(init->attachment_id, 0, data);
  ASSERT_TRUE(write_result.has_value());
}

TEST_F(AttachmentTestSuite, FinalizeUpload) {
  auto alice = CreateTestUser("fin_alice");
  auto bob = CreateTestUser("fin_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, 512, "image/png");
  ASSERT_TRUE(init.has_value());

  std::string data(512, 'Y');
  service_->WriteChunk(init->attachment_id, 0, data);

  auto fin = service_->FinalizeUpload(init->attachment_id, "deadbeef");
  ASSERT_TRUE(fin.has_value());

  auto path = service_->GetAttachment(init->attachment_id, alice.user_id);
  ASSERT_TRUE(path.has_value());
  ASSERT_TRUE(std::filesystem::exists(*path));
}

TEST_F(AttachmentTestSuite, GetAttachmentByAuthorizedUser) {
  auto alice = CreateTestUser("auth_alice");
  auto bob = CreateTestUser("auth_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, 256, "text/plain");
  std::string data(256, 'Z');
  service_->WriteChunk(init->attachment_id, 0, data);
  service_->FinalizeUpload(init->attachment_id, "hash123");

  auto path = service_->GetAttachment(init->attachment_id, bob.user_id);
  ASSERT_TRUE(path.has_value());
}

TEST_F(AttachmentTestSuite, GetAttachmentByUnauthorizedUserFails) {
  auto alice = CreateTestUser("unauth_alice");
  auto bob = CreateTestUser("unauth_bob");
  auto outsider = CreateTestUser("unauth_outsider");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, 256, "text/plain");
  std::string data(256, 'A');
  service_->WriteChunk(init->attachment_id, 0, data);
  service_->FinalizeUpload(init->attachment_id, "hash456");

  auto path = service_->GetAttachment(init->attachment_id, outsider.user_id);
  ASSERT_FALSE(path.has_value());
  ASSERT_EQ(path.error().code, vox::common::ErrorCode::kForbidden);
}

TEST_F(AttachmentTestSuite, ExceedQuotaFails) {
  auto alice = CreateTestUser("quota_alice");
  auto bob = CreateTestUser("quota_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto result = service_->InitUpload(alice.user_id, conv_id, 20 * 1024 * 1024, "application/octet-stream");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kQuotaExceeded);
}

TEST_F(AttachmentTestSuite, ExceedMaxUploadSizeFails) {
  auto alice = CreateTestUser("maxsz_alice");
  auto bob = CreateTestUser("maxsz_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto result = service_->InitUpload(alice.user_id, conv_id, 2 * 1024 * 1024, "application/octet-stream");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kQuotaExceeded);
}

TEST_F(AttachmentTestSuite, WriteChunkToNonexistentUploadFails) {
  auto result = service_->WriteChunk("nonexistent_id", 0, "data");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kNotFound);
}

TEST_F(AttachmentTestSuite, FinalizeWithNonexistentAttachmentFails) {
  auto result = service_->FinalizeUpload("nonexistent_id", "hash");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kNotFound);
}

TEST_F(AttachmentTestSuite, NonMemberCannotInitUpload) {
  auto alice = CreateTestUser("nmi_alice");
  auto bob = CreateTestUser("nmi_bob");
  auto outsider = CreateTestUser("nmi_outsider");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto result = service_->InitUpload(outsider.user_id, conv_id, 256, "text/plain");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kForbidden);
}
