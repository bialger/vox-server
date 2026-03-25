#ifndef VOX_NET_RATE_LIMITER_HPP
#define VOX_NET_RATE_LIMITER_HPP

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vox::net {

/// Fixed-window rate limiter (in-memory, per-process). Thread-safe.
class AuthRateLimiter {
public:
  AuthRateLimiter(std::size_t max_events_per_window, std::chrono::seconds window);

  /// Returns true if the request is allowed; false if rate limited.
  bool Allow(const std::string& client_key);

private:
  struct WindowState {
    std::chrono::steady_clock::time_point window_start{};
    std::size_t count = 0;
  };

  const std::size_t max_events_;
  const std::chrono::seconds window_;
  std::mutex mutex_;
  std::unordered_map<std::string, WindowState> by_key_;
};

/// Same fixed-window logic as `AuthRateLimiter`; use for per-account keys.
using AccountRateLimiter = AuthRateLimiter;

} // namespace vox::net

#endif // VOX_NET_RATE_LIMITER_HPP
