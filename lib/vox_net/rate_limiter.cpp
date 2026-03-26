#include "lib/vox_net/rate_limiter.hpp"

namespace vox::net {

AuthRateLimiter::AuthRateLimiter(std::size_t max_events_per_window, std::chrono::seconds window) :
    max_events_(max_events_per_window), window_(window) {
}

bool AuthRateLimiter::Allow(const std::string& client_key) {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  auto& w = by_key_[client_key];
  if (w.count == 0) {
    w.window_start = now;
    w.count = 1;
    return true;
  }
  if (now - w.window_start >= window_) {
    w.window_start = now;
    w.count = 1;
    return true;
  }
  if (w.count >= max_events_) {
    return false;
  }
  ++w.count;
  return true;
}

} // namespace vox::net
