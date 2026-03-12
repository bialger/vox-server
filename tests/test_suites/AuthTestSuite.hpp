#ifndef AUTHTESTSUITE_HPP
#define AUTHTESTSUITE_HPP

#include <memory>

#include <gtest/gtest.h>

#include "lib/vox_auth/auth_service.hpp"
#include "lib/vox_auth/password_hasher.hpp"
#include "lib/vox_auth/token_manager.hpp"
#include "lib/vox_common/thread_pool.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/session_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

class AuthTestSuite : public testing::Test {
protected:
  std::unique_ptr<vox::store::Database> db_;
  std::unique_ptr<vox::store::UserRepository> users_;
  std::unique_ptr<vox::store::DeviceRepository> devices_;
  std::unique_ptr<vox::store::SessionRepository> sessions_;
  std::unique_ptr<vox::auth::PasswordHasher> hasher_;
  std::unique_ptr<vox::auth::TokenManager> tokens_;
  std::unique_ptr<vox::common::ThreadPool> cpu_pool_;
  std::unique_ptr<vox::auth::AuthService> auth_;

  void SetUp() override;
  void TearDown() override;
};

#endif // AUTHTESTSUITE_HPP
