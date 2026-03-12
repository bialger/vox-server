#include <fstream>

#include <gtest/gtest.h>

#include "test_suites/AttachmentTestSuite.hpp"

namespace {

constexpr std::int64_t kInitUploadChunkSize = 1024;
constexpr std::int64_t kFinalizeUploadChunkSize = 512;
constexpr std::int64_t kGetAttachmentChunkSize = 256;
constexpr std::int64_t kBytesPerKib = 1024;
constexpr std::int64_t kMebibytesForQuotaTest = 20;
constexpr std::int64_t kMebibytesForMaxSizeTest = 2;
constexpr std::int64_t kChunkOffset = 0;

} // namespace

TEST_F(AttachmentTestSuite, InitUploadAndWriteChunk) {
  auto alice = CreateTestUser("att_alice");
  auto bob = CreateTestUser("att_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, kInitUploadChunkSize, "application/octet-stream");
  ASSERT_TRUE(init.has_value());
  ASSERT_FALSE(init->attachment_id.empty());

  std::string data(static_cast<std::size_t>(kInitUploadChunkSize), 'X');
  auto write_result = service_->WriteChunk(init->attachment_id, kChunkOffset, data);
  ASSERT_TRUE(write_result.has_value());
}

TEST_F(AttachmentTestSuite, FinalizeUpload) {
  auto alice = CreateTestUser("fin_alice");
  auto bob = CreateTestUser("fin_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, kFinalizeUploadChunkSize, "image/png");
  ASSERT_TRUE(init.has_value());

  std::string data(static_cast<std::size_t>(kFinalizeUploadChunkSize), 'Y');
  ASSERT_TRUE(service_->WriteChunk(init->attachment_id, kChunkOffset, data));

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

  auto init = service_->InitUpload(alice.user_id, conv_id, kGetAttachmentChunkSize, "text/plain");
  ASSERT_TRUE(init.has_value());
  std::string data(static_cast<std::size_t>(kGetAttachmentChunkSize), 'Z');
  ASSERT_TRUE(service_->WriteChunk(init->attachment_id, kChunkOffset, data));
  ASSERT_TRUE(service_->FinalizeUpload(init->attachment_id, "hash123"));

  auto path = service_->GetAttachment(init->attachment_id, bob.user_id);
  ASSERT_TRUE(path.has_value());
}

TEST_F(AttachmentTestSuite, GetAttachmentByUnauthorizedUserFails) {
  auto alice = CreateTestUser("unauth_alice");
  auto bob = CreateTestUser("unauth_bob");
  auto outsider = CreateTestUser("unauth_outsider");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto init = service_->InitUpload(alice.user_id, conv_id, kGetAttachmentChunkSize, "text/plain");
  ASSERT_TRUE(init.has_value());
  std::string data(static_cast<std::size_t>(kGetAttachmentChunkSize), 'A');
  ASSERT_TRUE(service_->WriteChunk(init->attachment_id, kChunkOffset, data));
  ASSERT_TRUE(service_->FinalizeUpload(init->attachment_id, "hash456"));

  auto path = service_->GetAttachment(init->attachment_id, outsider.user_id);
  ASSERT_FALSE(path.has_value());
  ASSERT_EQ(path.error().code, vox::common::ErrorCode::kForbidden);
}

TEST_F(AttachmentTestSuite, ExceedQuotaFails) {
  auto alice = CreateTestUser("quota_alice");
  auto bob = CreateTestUser("quota_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto result = service_->InitUpload(
      alice.user_id, conv_id, kMebibytesForQuotaTest * kBytesPerKib * kBytesPerKib, "application/octet-stream");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kQuotaExceeded);
}

TEST_F(AttachmentTestSuite, ExceedMaxUploadSizeFails) {
  auto alice = CreateTestUser("maxsz_alice");
  auto bob = CreateTestUser("maxsz_bob");
  auto conv_id = CreateTestConversation(alice.user_id, {alice, bob});

  auto result = service_->InitUpload(
      alice.user_id, conv_id, kMebibytesForMaxSizeTest * kBytesPerKib * kBytesPerKib, "application/octet-stream");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kQuotaExceeded);
}

TEST_F(AttachmentTestSuite, WriteChunkToNonexistentUploadFails) {
  auto result = service_->WriteChunk("nonexistent_id", kChunkOffset, "data");
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

  auto result = service_->InitUpload(outsider.user_id, conv_id, kGetAttachmentChunkSize, "text/plain");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kForbidden);
}
