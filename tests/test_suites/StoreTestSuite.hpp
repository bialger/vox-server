#ifndef STORETESTSUITE_HPP
#define STORETESTSUITE_HPP

#include <memory>

#include <gtest/gtest.h>

#include "lib/vox_store/attachment_repository.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/envelope_repository.hpp"
#include "lib/vox_store/session_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

class StoreTestSuite : public testing::Test {
protected:
  std::unique_ptr<vox::store::Database> db_;
  std::unique_ptr<vox::store::UserRepository> users_;
  std::unique_ptr<vox::store::DeviceRepository> devices_;
  std::unique_ptr<vox::store::SessionRepository> sessions_;
  std::unique_ptr<vox::store::ConversationRepository> conversations_;
  std::unique_ptr<vox::store::EnvelopeRepository> envelopes_;
  std::unique_ptr<vox::store::AttachmentRepository> attachments_;

  void SetUp() override;
  void TearDown() override;

  vox::store::UserRecord MakeUser(const std::string& username);
  vox::store::DeviceRecord MakeDevice(const std::string& user_id, const std::string& device_id);
};

#endif // STORETESTSUITE_HPP
