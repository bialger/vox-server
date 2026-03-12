#include "lib/vox_auth/token_manager.hpp"

#include <array>
#include <functional>
#include <random>
#include <sstream>

#include <fmt/format.h>

#include "lib/vox_common/uuid.hpp"

namespace vox::auth {

namespace {

std::string SimpleHash(const std::string& input) {
  std::size_t h = std::hash<std::string>{}(input);
  auto h2 = std::hash<std::string>{}(input + "vox_salt_suffix");
  return fmt::format("{:016x}{:016x}", h, h2);
}

} // namespace

TokenManager::TokenManager(store::SessionRepository& sessions,
                           common::Timestamp access_lifetime_seconds,
                           common::Timestamp refresh_lifetime_seconds) :
    sessions_(sessions), access_lifetime_seconds_(access_lifetime_seconds),
    refresh_lifetime_seconds_(refresh_lifetime_seconds) {
}

std::string TokenManager::GenerateToken() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> dist;
  return fmt::format("{:016x}{:016x}{:016x}{:016x}", dist(rng), dist(rng), dist(rng), dist(rng));
}

std::string TokenManager::HashToken(const std::string& token) {
  return SimpleHash(token);
}

common::Result<TokenPair> TokenManager::IssueTokens(const common::UserId& user_id,
                                                    const common::DeviceId& device_id,
                                                    common::Timestamp now) {
  TokenPair pair;
  pair.access_token = GenerateToken();
  pair.refresh_token = GenerateToken();

  store::SessionRecord session;
  session.session_id = common::GenerateUuid();
  session.user_id = user_id;
  session.device_id = device_id;
  session.access_token_hash = HashToken(pair.access_token);
  session.refresh_token_hash = HashToken(pair.refresh_token);
  session.access_expires_at = now + access_lifetime_seconds_;
  session.refresh_expires_at = now + refresh_lifetime_seconds_;
  session.created_at = now;

  auto result = sessions_.CreateSession(session);
  if (!result) {
    return std::unexpected(result.error());
  }
  return pair;
}

common::Result<store::SessionRecord> TokenManager::ValidateAccessToken(const std::string& access_token,
                                                                       common::Timestamp now) {
  auto hash = HashToken(access_token);
  auto session = sessions_.FindByAccessToken(hash);
  if (!session) {
    return std::unexpected(common::Error{common::ErrorCode::kUnauthorized, "Invalid access token"});
  }
  if (session->access_expires_at <= now) {
    return std::unexpected(common::Error{common::ErrorCode::kExpired, "Access token expired"});
  }
  return *session;
}

common::Result<TokenPair> TokenManager::RefreshTokens(const std::string& refresh_token,
                                                      const common::DeviceId& device_id,
                                                      common::Timestamp now) {
  auto hash = HashToken(refresh_token);
  auto session = sessions_.FindByRefreshToken(hash);
  if (!session) {
    return std::unexpected(common::Error{common::ErrorCode::kUnauthorized, "Invalid refresh token"});
  }
  if (session->refresh_expires_at <= now) {
    return std::unexpected(common::Error{common::ErrorCode::kExpired, "Refresh token expired"});
  }
  if (session->device_id != device_id) {
    return std::unexpected(common::Error{common::ErrorCode::kForbidden, "Device mismatch on refresh"});
  }

  sessions_.RevokeSession(session->session_id, now);
  return IssueTokens(session->user_id, device_id, now);
}

common::VoidResult TokenManager::RevokeSession(const std::string& session_id, common::Timestamp now) {
  return sessions_.RevokeSession(session_id, now);
}

common::VoidResult TokenManager::RevokeAllForUser(const common::UserId& user_id, common::Timestamp now) {
  return sessions_.RevokeAllForUser(user_id, now);
}

} // namespace vox::auth
