#include "lib/vox_auth/auth_service.hpp"

#include <chrono>

#include <spdlog/spdlog.h>

#include "lib/vox_common/uuid.hpp"

namespace vox::auth {

AuthService::AuthService(store::IUserRepository& users,
                         store::IDeviceRepository& devices,
                         PasswordHasher& hasher,
                         ITokenManager& tokens,
                         common::ThreadPool& cpu_pool) :
    users_(users), devices_(devices), hasher_(hasher), tokens_(tokens), cpu_pool_(cpu_pool) {
}

common::Result<RegisterResponse> AuthService::Register(const RegisterRequest& request) {
  if (request.username.empty() || request.password_derived_value.empty()) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument, .message = "Username and password are required"});
  }
  if (request.wrapped_sync_key.empty() || request.sync_wrap_salt.empty() || request.sync_wrap_params.empty()) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument,
                      .message = "wrapped_sync_key, sync_wrap_salt, and sync_wrap_params are required"});
  }
  if (request.identity_key_public.empty() || request.signed_prekey_public.empty() ||
      request.signed_prekey_signature.empty()) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInvalidArgument,
                                         .message = "Device public key material is required"});
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
  user.sync_key_version = 1;
  user.wrapped_sync_key = request.wrapped_sync_key;
  user.sync_wrap_salt = request.sync_wrap_salt;
  user.sync_wrap_params = request.sync_wrap_params;

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
  device.device_label = request.device_label;
  device.created_at = now;
  device.last_seen_at = now;

  auto device_result = devices_.RegisterDevice(device);
  if (!device_result) {
    return std::unexpected(device_result.error());
  }

  auto token_result = tokens_.IssueTokens(user_id, device.device_id, now);
  if (!token_result) {
    return std::unexpected(token_result.error());
  }

  spdlog::info("User registered: {} ({})", request.username, user_id);
  return RegisterResponse{
      .user_id = user_id, .tokens = *token_result, .device_status = "created", .sync_key_version = 1};
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

  auto existing_device = devices_.FindByUserAndDevice(user->user_id, device_id);
  std::string device_status = "existing";

  if (!existing_device) {
    if (request.identity_key_public.empty() || request.signed_prekey_public.empty() ||
        request.signed_prekey_signature.empty()) {
      return std::unexpected(common::Error{
          .code = common::ErrorCode::kInvalidArgument,
          .message =
              "identity_key_public, signed_prekey_public, and signed_prekey_signature are required for a new device"});
    }
    store::DeviceRecord device;
    device.device_id = device_id;
    device.user_id = user->user_id;
    device.identity_key_public = request.identity_key_public;
    device.signed_prekey_public = request.signed_prekey_public;
    device.signed_prekey_signature = request.signed_prekey_signature;
    device.device_label = request.device_label;
    device.created_at = now;
    device.last_seen_at = now;
    auto device_result = devices_.RegisterDevice(device);
    if (!device_result) {
      return std::unexpected(device_result.error());
    }
    device_status = "created";
  } else {
    if (auto seen = devices_.UpdateLastSeen(user->user_id, device_id, now); !seen) {
      return std::unexpected(seen.error());
    }
  }

  auto token_result = tokens_.IssueTokens(user->user_id, device_id, now);
  if (!token_result) {
    return std::unexpected(token_result.error());
  }

  spdlog::info("User logged in: {} ({})", request.username, user->user_id);
  return LoginResponse{.user_id = user->user_id,
                       .tokens = *token_result,
                       .device_status = device_status,
                       .sync_key_version = user->sync_key_version};
}

common::VoidResult AuthService::Logout(const std::string& session_id) {
  auto now = Now();
  return tokens_.RevokeSession(session_id, now);
}

common::VoidResult AuthService::LogoutWithAccessToken(const std::string& access_token) {
  return tokens_.RevokeByAccessToken(access_token, Now());
}

common::Result<TokenPair> AuthService::Refresh(const RefreshRequest& request) {
  auto now = Now();
  auto result = tokens_.RefreshTokens(request.refresh_token, request.device_id, now);
  if (result) {
    if (auto seen = devices_.UpdateLastSeen(result->user_id, request.device_id, now); !seen) {
      return std::unexpected(seen.error());
    }
    return result->tokens;
  }
  return std::unexpected(result.error());
}

common::Result<ChangePasswordResponse> AuthService::ChangePassword(const common::UserId& user_id,
                                                                   const ChangePasswordRequest& request) {
  auto user = users_.FindById(user_id);
  if (!user) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "User not found"});
  }
  if (request.wrapped_sync_key.empty() || request.sync_wrap_salt.empty() || request.sync_wrap_params.empty()) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument,
                      .message = "wrapped_sync_key, sync_wrap_salt, and sync_wrap_params are required"});
  }

  auto verify_future = cpu_pool_.Submit([this, &request, &user]() {
    return hasher_.Verify(request.current_password_derived_value, user->password_salt, user->password_verifier);
  });
  if (!verify_future.get()) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kUnauthorized, .message = "Invalid credentials"});
  }

  auto hash_future = cpu_pool_.Submit([this, &request]() { return hasher_.Hash(request.new_password_derived_value); });
  auto hash_result = hash_future.get();
  if (!hash_result) {
    return std::unexpected(hash_result.error());
  }

  auto cred = users_.UpdatePasswordCredentials(user_id, hash_result->salt, hash_result->verifier);
  if (!cred) {
    return std::unexpected(cred.error());
  }

  const int new_ver = user->sync_key_version + 1;
  store::SyncKeyBundleRecord bundle;
  bundle.sync_key_version = new_ver;
  bundle.wrapped_sync_key = request.wrapped_sync_key;
  bundle.sync_wrap_salt = request.sync_wrap_salt;
  bundle.sync_wrap_params = request.sync_wrap_params;

  auto sync = users_.SetSyncKeyBundle(user_id, bundle);
  if (!sync) {
    return std::unexpected(sync.error());
  }

  return ChangePasswordResponse{.sync_key_version = new_ver};
}

common::Timestamp AuthService::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace vox::auth
