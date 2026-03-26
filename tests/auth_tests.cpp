#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "test_suites/AuthTestSuite.hpp"

namespace {

constexpr vox::common::Timestamp kDisableUserTimestamp = 9999999;
constexpr int kConcurrentRegistrationAttempts = 10;
constexpr int kExpectedSingleSuccess = 1;

} // namespace

TEST_F(AuthTestSuite, RegisterSuccessfully) {
  vox::auth::RegisterRequest req;
  req.username = "alice";
  req.password_derived_value = "client_hashed_pw";
  req.device_id = "dev1";
  req.identity_key_public = "ik_pub";
  req.signed_prekey_public = "spk_pub";
  req.signed_prekey_signature = "spk_sig";

  auto result = auth_->Register(req);
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->user_id.empty());
  ASSERT_FALSE(result->tokens.access_token.empty());
  ASSERT_FALSE(result->tokens.refresh_token.empty());
}

TEST_F(AuthTestSuite, RegisterDuplicateUsernameFails) {
  vox::auth::RegisterRequest req;
  req.username = "bob";
  req.password_derived_value = "pw1";
  req.device_id = "dev1";

  ASSERT_TRUE(auth_->Register(req).has_value());

  req.device_id = "dev2";
  auto result = auth_->Register(req);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kAlreadyExists);
}

TEST_F(AuthTestSuite, RegisterEmptyUsernameFails) {
  vox::auth::RegisterRequest req;
  req.username = "";
  req.password_derived_value = "pw";
  req.device_id = "dev1";

  auto result = auth_->Register(req);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kInvalidArgument);
}

TEST_F(AuthTestSuite, LoginWithCorrectPassword) {
  vox::auth::RegisterRequest reg;
  reg.username = "charlie";
  reg.password_derived_value = "correct_pw";
  reg.device_id = "dev1";
  ASSERT_TRUE(auth_->Register(reg).has_value());

  vox::auth::LoginRequest login;
  login.username = "charlie";
  login.password_derived_value = "correct_pw";
  login.device_id = "dev1";

  auto result = auth_->Login(login);
  ASSERT_TRUE(result.has_value());
  ASSERT_FALSE(result->tokens.access_token.empty());
}

TEST_F(AuthTestSuite, LoginWithWrongPasswordFails) {
  vox::auth::RegisterRequest reg;
  reg.username = "dave";
  reg.password_derived_value = "correct_pw";
  reg.device_id = "dev1";
  ASSERT_TRUE(auth_->Register(reg).has_value());

  vox::auth::LoginRequest login;
  login.username = "dave";
  login.password_derived_value = "wrong_pw";
  login.device_id = "dev1";

  auto result = auth_->Login(login);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kUnauthorized);
}

TEST_F(AuthTestSuite, LoginNonExistentUserFails) {
  vox::auth::LoginRequest login;
  login.username = "nonexistent";
  login.password_derived_value = "pw";
  login.device_id = "dev1";

  auto result = auth_->Login(login);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kUnauthorized);
}

TEST_F(AuthTestSuite, RefreshTokenReturnsNewTokens) {
  vox::auth::RegisterRequest reg;
  reg.username = "eve";
  reg.password_derived_value = "pw";
  reg.device_id = "dev1";
  auto reg_result = auth_->Register(reg);
  ASSERT_TRUE(reg_result.has_value());

  vox::auth::RefreshRequest refresh;
  refresh.refresh_token = reg_result->tokens.refresh_token;
  refresh.device_id = "dev1";

  auto result = auth_->Refresh(refresh);
  ASSERT_TRUE(result.has_value());
  ASSERT_NE(result->access_token, reg_result->tokens.access_token);
}

TEST_F(AuthTestSuite, RefreshWithWrongDeviceFails) {
  vox::auth::RegisterRequest reg;
  reg.username = "frank";
  reg.password_derived_value = "pw";
  reg.device_id = "dev1";
  auto reg_result = auth_->Register(reg);

  ASSERT_TRUE(reg_result.has_value());
  vox::auth::RefreshRequest refresh;
  refresh.refresh_token = reg_result->tokens.refresh_token;
  refresh.device_id = "wrong_device";

  auto result = auth_->Refresh(refresh);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kForbidden);
}

TEST_F(AuthTestSuite, RevokedRefreshTokenFails) {
  vox::auth::RegisterRequest reg;
  reg.username = "grace";
  reg.password_derived_value = "pw";
  reg.device_id = "dev1";
  auto reg_result = auth_->Register(reg);
  ASSERT_TRUE(reg_result.has_value());

  vox::auth::RefreshRequest refresh;
  refresh.refresh_token = reg_result->tokens.refresh_token;
  refresh.device_id = "dev1";
  auto new_tokens = auth_->Refresh(refresh);
  ASSERT_TRUE(new_tokens.has_value());

  refresh.refresh_token = reg_result->tokens.refresh_token;
  auto result = auth_->Refresh(refresh);
  ASSERT_FALSE(result.has_value());
}

TEST_F(AuthTestSuite, LogoutRevokesSession) {
  vox::auth::RegisterRequest reg;
  reg.username = "henry";
  reg.password_derived_value = "pw";
  reg.device_id = "dev1";
  auto reg_result = auth_->Register(reg);
  ASSERT_TRUE(reg_result.has_value());
  auto access_hash = tokens_->HashToken(reg_result.value().tokens.access_token);

  auto session = sessions_->FindByAccessToken(access_hash);
  ASSERT_TRUE(session.has_value());
  if (session.has_value()) {
    auto result = auth_->Logout(session->session_id);
    ASSERT_TRUE(result.has_value());
  }

  auto found = sessions_->FindByAccessToken(access_hash);
  ASSERT_FALSE(found.has_value());
}

TEST_F(AuthTestSuite, LogoutWithAccessTokenRevokesSession) {
  vox::auth::RegisterRequest reg;
  reg.username = "ida";
  reg.password_derived_value = "pw";
  reg.device_id = "dev1";
  auto reg_result = auth_->Register(reg);
  ASSERT_TRUE(reg_result.has_value());
  const std::string& token = reg_result->tokens.access_token;
  auto access_hash = tokens_->HashToken(token);

  ASSERT_TRUE(auth_->LogoutWithAccessToken(token).has_value());

  ASSERT_FALSE(sessions_->FindByAccessToken(access_hash).has_value());
}

TEST_F(AuthTestSuite, LogoutWithAccessTokenInvalidFails) {
  auto result = auth_->LogoutWithAccessToken("not-a-valid-token");
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kUnauthorized);
}

TEST_F(AuthTestSuite, ConcurrentRegistrationsSameUsername) {
  std::atomic<int> success_count{0};
  std::vector<std::jthread> threads;
  threads.reserve(kConcurrentRegistrationAttempts);

  for (int i = 0; i < kConcurrentRegistrationAttempts; ++i) {
    threads.emplace_back([this, i, &success_count]() {
      vox::auth::RegisterRequest req;
      req.username = "race_user";
      req.password_derived_value = "pw_" + std::to_string(i);
      req.device_id = "dev_" + std::to_string(i);
      auto result = auth_->Register(req);
      if (result.has_value()) {
        success_count.fetch_add(1);
      }
    });
  }

  threads.clear();
  ASSERT_EQ(success_count.load(), kExpectedSingleSuccess);
}

TEST_F(AuthTestSuite, LoginDisabledAccountFails) {
  vox::auth::RegisterRequest reg;
  reg.username = "disabled";
  reg.password_derived_value = "pw";
  reg.device_id = "dev1";
  auto reg_result = auth_->Register(reg);
  ASSERT_TRUE(reg_result.has_value());
  ASSERT_TRUE(users_->DisableUser(reg_result->user_id, kDisableUserTimestamp));

  vox::auth::LoginRequest login;
  login.username = "disabled";
  login.password_derived_value = "pw";
  login.device_id = "dev1";

  auto result = auth_->Login(login);
  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().code, vox::common::ErrorCode::kForbidden);
}
