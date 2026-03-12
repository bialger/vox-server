#include "lib/vox_auth/auth_service.hpp"

#include <chrono>

#include <spdlog/spdlog.h>

#include "lib/vox_common/uuid.hpp"

namespace vox::auth {

AuthService::AuthService(store::UserRepository& users,
                         store::DeviceRepository& devices,
                         PasswordHasher& hasher,
                         TokenManager& tokens,
                         common::ThreadPool& cpu_pool) :
    users_(users), devices_(devices), hasher_(hasher), tokens_(tokens), cpu_pool_(cpu_pool) {
}

common::Result<RegisterResponse> AuthService::Register(const RegisterRequest& request) {
  if (request.username.empty() || request.password_derived_value.empty()) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument, .message = "Username and password are required"});
  }

  auto existing = users_.FindByUsername(request.username);
  if (existing) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kAlreadyExists, .message = "Username already taken"});
  }

  auto hash_future = cpu_pool_.Submit([this, &request]() { return hasher_.Hash(request.password_derived_value); });
  auto hash_result = hash_future.get();
  if (!hash_result) {
    return std::unexpected(hash_result.error());
  }

  auto now = Now();
  auto user_id = common::GenerateUuid();

  store::UserRecord user;
  user.user_id = user_id;
  user.username = request.username;
  user.password_salt = hash_result->salt;
  user.password_verifier = hash_result->verifier;
  user.created_at = now;

  auto create_result = users_.CreateUser(user);
  if (!create_result) {
    return std::unexpected(create_result.error());
  }

  store::DeviceRecord device;
  device.device_id = request.device_id.empty() ? common::GenerateUuid() : request.device_id;
  device.user_id = user_id;
  device.identity_key_public = request.identity_key_public;
  device.signed_prekey_public = request.signed_prekey_public;
  device.signed_prekey_signature = request.signed_prekey_signature;

  auto device_result = devices_.RegisterDevice(device);
  if (!device_result) {
    return std::unexpected(device_result.error());
  }

  auto token_result = tokens_.IssueTokens(user_id, device.device_id, now);
  if (!token_result) {
    return std::unexpected(token_result.error());
  }

  spdlog::info("User registered: {} ({})", request.username, user_id);
  return RegisterResponse{.user_id = user_id, .tokens = *token_result};
}

common::Result<LoginResponse> AuthService::Login(const LoginRequest& request) {
  auto user = users_.FindByUsername(request.username);
  if (!user) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kUnauthorized, .message = "Invalid credentials"});
  }

  if (user->disabled_at.has_value()) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kForbidden, .message = "Account is disabled"});
  }

  auto verify_future = cpu_pool_.Submit([this, &request, &user]() {
    return hasher_.Verify(request.password_derived_value, user->password_salt, user->password_verifier);
  });
  bool valid = verify_future.get();

  if (!valid) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kUnauthorized, .message = "Invalid credentials"});
  }

  auto now = Now();
  auto device_id = request.device_id.empty() ? common::GenerateUuid() : request.device_id;

  auto existing_device = devices_.FindById(device_id);
  if (!existing_device) {
    store::DeviceRecord device;
    device.device_id = device_id;
    device.user_id = user->user_id;
    auto device_result = devices_.RegisterDevice(device);
    if (!device_result) {
      return std::unexpected(device_result.error());
    }
  }

  auto token_result = tokens_.IssueTokens(user->user_id, device_id, now);
  if (!token_result) {
    return std::unexpected(token_result.error());
  }

  spdlog::info("User logged in: {} ({})", request.username, user->user_id);
  return LoginResponse{.user_id = user->user_id, .tokens = *token_result};
}

common::VoidResult AuthService::Logout(const std::string& session_id) {
  auto now = Now();
  return tokens_.RevokeSession(session_id, now);
}

common::Result<TokenPair> AuthService::Refresh(const RefreshRequest& request) {
  auto now = Now();
  return tokens_.RefreshTokens(request.refresh_token, request.device_id, now);
}

common::Timestamp AuthService::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace vox::auth
