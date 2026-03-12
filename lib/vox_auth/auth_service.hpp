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
  std::string identity_key_public;
  std::string signed_prekey_public;
  std::string signed_prekey_signature;
};

struct RegisterResponse {
  common::UserId user_id;
  TokenPair tokens;
};

struct LoginRequest {
  std::string username;
  std::string password_derived_value;
  std::string device_id;
};

struct LoginResponse {
  common::UserId user_id;
  TokenPair tokens;
};

struct RefreshRequest {
  std::string refresh_token;
  std::string device_id;
};

class AuthService {
public:
  AuthService(store::UserRepository& users,
              store::DeviceRepository& devices,
              PasswordHasher& hasher,
              TokenManager& tokens,
              common::ThreadPool& cpu_pool);

  common::Result<RegisterResponse> Register(const RegisterRequest& request);
  common::Result<LoginResponse> Login(const LoginRequest& request);
  common::VoidResult Logout(const std::string& session_id);
  common::Result<TokenPair> Refresh(const RefreshRequest& request);

private:
  common::Timestamp Now();

  store::UserRepository& users_;
  store::DeviceRepository& devices_;
  PasswordHasher& hasher_;
  TokenManager& tokens_;
  common::ThreadPool& cpu_pool_;
};

} // namespace vox::auth

#endif // VOX_AUTH_AUTH_SERVICE_HPP
