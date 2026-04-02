#ifndef VOX_AUTH_AUTH_SERVICE_HPP
#define VOX_AUTH_AUTH_SERVICE_HPP

#include <memory>
#include <string>

#include "lib/vox_auth/password_hasher.hpp"
#include "lib/vox_auth/token_manager.hpp"
#include "lib/vox_common/thread_pool.hpp"
#include "lib/vox_common/types.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

namespace vox::auth {

struct RegisterRequest {
  std::string username;
  std::string password_derived_value;
  std::string device_id;
  std::string device_label;
  std::string identity_key_public;
  std::string signed_prekey_public;
  std::string signed_prekey_signature;
  std::string wrapped_sync_key;
  std::string sync_wrap_salt;
  /// JSON object serialized as string (opaque to server).
  std::string sync_wrap_params;
};

struct RegisterResponse {
  common::UserId user_id;
  TokenPair tokens;
  std::string device_status;
  int sync_key_version = 1;
};

struct LoginRequest {
  std::string username;
  std::string password_derived_value;
  std::string device_id;
  std::string device_label;
  std::string identity_key_public;
  std::string signed_prekey_public;
  std::string signed_prekey_signature;
};

struct LoginResponse {
  common::UserId user_id;
  TokenPair tokens;
  std::string device_status;
  int sync_key_version = 1;
};

struct RefreshRequest {
  std::string refresh_token;
  std::string device_id;
};

struct ChangePasswordRequest {
  std::string current_password_derived_value;
  std::string new_password_derived_value;
  std::string wrapped_sync_key;
  std::string sync_wrap_salt;
  std::string sync_wrap_params;
};

struct ChangePasswordResponse {
  int sync_key_version = 1;
};

class IAuthService {
public:
  virtual ~IAuthService() = default;
  virtual common::Result<RegisterResponse> Register(const RegisterRequest& request) = 0;
  virtual common::Result<LoginResponse> Login(const LoginRequest& request) = 0;
  virtual common::VoidResult Logout(const std::string& session_id) = 0;
  virtual common::VoidResult LogoutWithAccessToken(const std::string& access_token) = 0;
  virtual common::Result<TokenPair> Refresh(const RefreshRequest& request) = 0;
  virtual common::Result<ChangePasswordResponse> ChangePassword(const common::UserId& user_id,
                                                               const ChangePasswordRequest& request) = 0;
};

class AuthService : public IAuthService {
public:
  AuthService(store::IUserRepository& users,
              store::IDeviceRepository& devices,
              PasswordHasher& hasher,
              ITokenManager& tokens,
              common::ThreadPool& cpu_pool);

  common::Result<RegisterResponse> Register(const RegisterRequest& request) override;
  common::Result<LoginResponse> Login(const LoginRequest& request) override;
  common::VoidResult Logout(const std::string& session_id) override;
  common::VoidResult LogoutWithAccessToken(const std::string& access_token) override;
  common::Result<TokenPair> Refresh(const RefreshRequest& request) override;
  common::Result<ChangePasswordResponse> ChangePassword(const common::UserId& user_id,
                                                         const ChangePasswordRequest& request) override;

private:
  common::Timestamp Now();

  store::IUserRepository& users_;
  store::IDeviceRepository& devices_;
  PasswordHasher& hasher_;
  ITokenManager& tokens_;
  common::ThreadPool& cpu_pool_;
};

} // namespace vox::auth

#endif // VOX_AUTH_AUTH_SERVICE_HPP
