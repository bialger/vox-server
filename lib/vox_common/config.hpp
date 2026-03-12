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

  std::size_t max_group_size = 256;
  std::size_t max_channel_size = 10000;
  std::size_t max_queue_depth_per_device = 1000;
  std::size_t max_upload_size_bytes = 100 * 1024 * 1024;  // 100 MB
  std::size_t max_storage_per_user_bytes = 1024 * 1024 * 1024;  // 1 GB

  std::int64_t access_token_lifetime_seconds = 15 * 60;       // 15 minutes
  std::int64_t refresh_token_lifetime_seconds = 30 * 86400;   // 30 days
  std::int64_t message_retention_seconds = 90 * 86400;        // 90 days
  std::int64_t attachment_retention_seconds = 90 * 86400;     // 90 days

  std::uint32_t argon2_time_cost = 3;
  std::uint32_t argon2_memory_cost = 65536;  // 64 MB
  std::uint32_t argon2_parallelism = 1;

  static ServerConfig Default();
};

}  // namespace vox::common

#endif  // VOX_COMMON_CONFIG_HPP
