#include "lib/vox_common/config.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace vox::common {

namespace {

/// Upper bound for TCP/UDP port values stored in `ServerConfig::listen_port`.
constexpr std::uint64_t kMaxListenPort = static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max());

void TrimInPlace(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.pop_back();
  }
}

std::string ParseStringValue(std::string_view v) {
  std::string s(v);
  TrimInPlace(s);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

bool ParseUInt64(std::string_view s, std::uint64_t& out, std::string* err) {
  std::string buf(s);
  TrimInPlace(buf);
  if (buf.empty()) {
    if (err != nullptr) {
      *err = "empty value";
    }
    return false;
  }
  const char* begin = buf.data();
  const char* end = buf.data() + buf.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  if (ec != std::errc{} || ptr != end) {
    if (err != nullptr) {
      *err = "invalid unsigned integer";
    }
    return false;
  }
  return true;
}

bool ParseInt64(std::string_view s, std::int64_t& out, std::string* err) {
  std::string buf(s);
  TrimInPlace(buf);
  if (buf.empty()) {
    if (err != nullptr) {
      *err = "empty value";
    }
    return false;
  }
  const char* begin = buf.data();
  const char* end = buf.data() + buf.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  if (ec != std::errc{} || ptr != end) {
    if (err != nullptr) {
      *err = "invalid integer";
    }
    return false;
  }
  return true;
}

bool ApplyKey(std::string_view key, std::string_view value, ServerConfig& c, std::size_t line_no, std::string* err) {
  std::string err_local;
  std::string* e = err != nullptr ? err : &err_local;

  auto u64 = [&](std::size_t& field) -> bool {
    std::uint64_t v = 0;
    if (!ParseUInt64(value, v, e)) {
      return false;
    }
    if (v > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      *e = "value out of range";
      return false;
    }
    field = static_cast<std::size_t>(v);
    return true;
  };

  auto u64_u32 = [&](std::uint32_t& field) -> bool {
    std::uint64_t v = 0;
    if (!ParseUInt64(value, v, e)) {
      return false;
    }
    if (v > std::numeric_limits<std::uint32_t>::max()) {
      *e = "value out of range";
      return false;
    }
    field = static_cast<std::uint32_t>(v);
    return true;
  };

  auto i64 = [&](std::int64_t& field) -> bool { return ParseInt64(value, field, e); };

  if (key == "cpu_pool_size") {
    return u64(c.cpu_pool_size);
  }
  if (key == "storage_pool_size") {
    return u64(c.storage_pool_size);
  }
  if (key == "task_queue_capacity") {
    return u64(c.task_queue_capacity);
  }
  if (key == "db_path") {
    c.db_path = ParseStringValue(value);
    return true;
  }
  if (key == "blob_storage_path") {
    c.blob_storage_path = ParseStringValue(value);
    return true;
  }
  if (key == "listen_address") {
    c.listen_address = ParseStringValue(value);
    return true;
  }
  if (key == "listen_port") {
    std::uint64_t v = 0;
    if (!ParseUInt64(value, v, e) || v > kMaxListenPort) {
      *e = "listen_port must fit uint16_t";
      return false;
    }
    c.listen_port = static_cast<std::uint16_t>(v);
    return true;
  }
  if (key == "network_thread_count") {
    return u64(c.network_thread_count);
  }
  if (key == "session_token_pepper") {
    c.session_token_pepper = ParseStringValue(value);
    return true;
  }
  if (key == "admin_token") {
    c.admin_token = ParseStringValue(value);
    return true;
  }
  if (key == "max_group_size") {
    return u64(c.max_group_size);
  }
  if (key == "max_channel_size") {
    return u64(c.max_channel_size);
  }
  if (key == "max_queue_depth_per_device") {
    return u64(c.max_queue_depth_per_device);
  }
  if (key == "max_ws_outbound_queue") {
    return u64(c.max_ws_outbound_queue);
  }
  if (key == "max_upload_size_bytes") {
    return u64(c.max_upload_size_bytes);
  }
  if (key == "max_storage_per_user_bytes") {
    return u64(c.max_storage_per_user_bytes);
  }
  if (key == "max_http_body_bytes") {
    return u64(c.max_http_body_bytes);
  }
  if (key == "auth_rate_limit_max") {
    return u64(c.auth_rate_limit_max);
  }
  if (key == "auth_rate_limit_window_seconds") {
    return i64(c.auth_rate_limit_window_seconds);
  }
  if (key == "account_rate_limit_max") {
    return u64(c.account_rate_limit_max);
  }
  if (key == "account_rate_limit_window_seconds") {
    return i64(c.account_rate_limit_window_seconds);
  }
  if (key == "maintenance_purge_interval_seconds") {
    return i64(c.maintenance_purge_interval_seconds);
  }
  if (key == "access_token_lifetime_seconds") {
    return i64(c.access_token_lifetime_seconds);
  }
  if (key == "refresh_token_lifetime_seconds") {
    return i64(c.refresh_token_lifetime_seconds);
  }
  if (key == "message_retention_seconds") {
    return i64(c.message_retention_seconds);
  }
  if (key == "attachment_retention_seconds") {
    return i64(c.attachment_retention_seconds);
  }
  if (key == "argon2_time_cost") {
    return u64_u32(c.argon2_time_cost);
  }
  if (key == "argon2_memory_cost") {
    return u64_u32(c.argon2_memory_cost);
  }
  if (key == "argon2_parallelism") {
    return u64_u32(c.argon2_parallelism);
  }

  spdlog::warn("vox.conf:{}: unknown key '{}' (ignored)", line_no, key);
  return true;
}

} // namespace

ServerConfig ServerConfig::Default() {
  return ServerConfig{};
}

bool LoadServerConfigFile(const std::filesystem::path& path, ServerConfig& config, std::string* error_out) {
  std::ifstream in(path);
  if (!in) {
    if (error_out != nullptr) {
      *error_out = "cannot open file";
    }
    return false;
  }

  std::string line;
  std::size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    if (const auto hash = line.find('#'); hash != std::string::npos) {
      line.resize(hash);
    }
    TrimInPlace(line);
    if (line.empty()) {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      if (error_out != nullptr) {
        error_out->clear();
        fmt::format_to(std::back_inserter(*error_out), FMT_STRING("line {}: expected key = value"), line_no);
      }
      return false;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    TrimInPlace(key);
    TrimInPlace(value);
    if (key.empty()) {
      if (error_out != nullptr) {
        error_out->clear();
        fmt::format_to(std::back_inserter(*error_out), FMT_STRING("line {}: empty key"), line_no);
      }
      return false;
    }

    std::string err;
    if (!ApplyKey(key, value, config, line_no, &err)) {
      if (error_out != nullptr) {
        error_out->clear();
        fmt::format_to(std::back_inserter(*error_out), FMT_STRING("line {} ({}): {}"), line_no, key, err);
      }
      return false;
    }
  }

  return true;
}

} // namespace vox::common
