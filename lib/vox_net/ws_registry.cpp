#include "lib/vox_net/ws_registry.hpp"

namespace vox::net {

void WsPushRegistry::Register(const std::string& device_id, Sender send) {
  std::lock_guard lock(mutex_);
  senders_[device_id] = std::move(send);
}

void WsPushRegistry::Unregister(const std::string& device_id) {
  std::lock_guard lock(mutex_);
  senders_.erase(device_id);
}

void WsPushRegistry::Notify(const std::string& device_id, const std::string& json_text) {
  Sender fn;
  {
    std::lock_guard lock(mutex_);
    auto it = senders_.find(device_id);
    if (it == senders_.end()) {
      return;
    }
    fn = it->second;
  }
  if (fn) {
    fn(json_text);
  }
}

} // namespace vox::net
