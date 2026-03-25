#ifndef VOX_NET_WS_REGISTRY_HPP
#define VOX_NET_WS_REGISTRY_HPP

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vox::net {

/// Thread-safe registry of online device_id → outbound sender (WebSocket).
class WsPushRegistry {
public:
  using Sender = std::function<void(std::string)>;

  void Register(const std::string& device_id, Sender send);
  void Unregister(const std::string& device_id);
  void Notify(const std::string& device_id, const std::string& json_text);

private:
  std::mutex mutex_;
  std::unordered_map<std::string, Sender> senders_;
};

} // namespace vox::net

#endif // VOX_NET_WS_REGISTRY_HPP
