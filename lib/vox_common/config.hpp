#ifndef VOX_COMMON_CONFIG_HPP
#define VOX_COMMON_CONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace vox::common {

struct ServerConfig {
  std::size_t cpu_pool_size = 2;
  std::size_t storage_pool_size = 2;
  std::size_t task_queue_capacity = 1024;

  std::filesystem::path db_path = "vox_server.db";
  std::filesystem::path blob_storage_path = "blobs";

  std::string listen_address = "127.0.0.1";
  std::uint16_t listen_port = 8080;
  std::size_t network_thread_count = 4;
  /// Server secret for HMAC-SHA256 of session tokens; must be set for production (e.g. VOX_SESSION_PEPPER).
  std::string session_token_pepper;
  /// If empty, admin HTTP routes are disabled.
  std::string admin_token;

  std::size_t max_group_size = 256;
  std::size_t max_channel_size = 10000;
  std::size_t max_queue_depth_per_device = 1000;
  std::size_t max_upload_size_bytes = 100 * 1024 * 1024;       // 100 MB
  std::size_t max_storage_per_user_bytes = 1024 * 1024 * 1024; // 1 GB
  /// Max JSON/body size for a single HTTP request (Beast reads full body before dispatch).
  std::size_t max_http_body_bytes = 2 * 1024 * 1024; // 2 MB
  /// 0 = disabled. Applied to POST /v1/register, /login, /refresh per client IP.
  std::size_t auth_rate_limit_max = 60;
  std::int64_t auth_rate_limit_window_seconds = 60;
  /// How often to run DB/blob expiry cleanup (also uses storage thread pool when non-zero interval).
  std::int64_t maintenance_purge_interval_seconds = 3600;

  std::int64_t access_token_lifetime_seconds = 15 * 60;     // 15 minutes
  std::int64_t refresh_token_lifetime_seconds = 30 * 86400; // 30 days
  std::int64_t message_retention_seconds = 90 * 86400;      // 90 days
  std::int64_t attachment_retention_seconds = 90 * 86400;   // 90 days

  std::uint32_t argon2_time_cost = 3;
  std::uint32_t argon2_memory_cost = 65536; // 64 MB
  std::uint32_t argon2_parallelism = 1;

  static ServerConfig Default();
};

} // namespace vox::common

#endif // VOX_COMMON_CONFIG_HPP
