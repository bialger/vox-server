#include "lib/vox_relay/delivery_manager.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace vox::relay {

DeliveryManager::DeliveryManager(store::EnvelopeRepository& envelopes, std::size_t max_queue_per_device) :
    envelopes_(envelopes), max_queue_per_device_(max_queue_per_device) {
}

common::VoidResult DeliveryManager::Enqueue(const common::DeviceId& device_id, const QueuedEnvelope& envelope) {
  auto& queue = GetOrCreateQueue(device_id);
  std::lock_guard lock(queue.mutex);

  if (queue.pending.size() >= max_queue_per_device_) {
    spdlog::warn("Queue overflow for device {}, switching to offline", device_id);
    return std::unexpected(common::Error{common::ErrorCode::kQueueFull, "Device queue full, use offline delivery"});
  }

  queue.pending.push_back(envelope);
  return {};
}

std::vector<QueuedEnvelope> DeliveryManager::Dequeue(const common::DeviceId& device_id, std::size_t max_count) {
  std::vector<QueuedEnvelope> result;
  auto found = queues_.Find(device_id);
  if (!found) {
    return result;
  }

  auto& queue = *found;
  std::lock_guard lock(queue->mutex);

  std::size_t count = std::min(max_count, queue->pending.size());
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    result.push_back(std::move(queue->pending.front()));
    queue->pending.pop_front();
  }
  return result;
}

common::VoidResult DeliveryManager::Acknowledge(const common::DeviceId& device_id,
                                                const common::EnvelopeId& envelope_id) {
  auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  return envelopes_.MarkAcked(envelope_id, device_id, now);
}

void DeliveryManager::SwitchToOffline(const common::DeviceId& device_id) {
  auto found = queues_.Find(device_id);
  if (!found) {
    return;
  }

  auto& queue = *found;
  std::lock_guard lock(queue->mutex);

  auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  for (const auto& env : queue->pending) {
    envelopes_.AddDeliveryState(env.envelope_id, device_id, now);
  }
  queue->pending.clear();
  spdlog::info("Switched device {} to offline delivery, persisted queued envelopes", device_id);
}

std::size_t DeliveryManager::QueueSize(const common::DeviceId& device_id) const {
  auto found = queues_.Find(device_id);
  if (!found) {
    return 0;
  }
  std::lock_guard lock((*found)->mutex);
  return (*found)->pending.size();
}

DeliveryManager::DeviceQueue& DeliveryManager::GetOrCreateQueue(const common::DeviceId& device_id) {
  return *queues_.WithShard(device_id, [&](auto& map) -> std::shared_ptr<DeviceQueue> {
    auto it = map.find(device_id);
    if (it == map.end()) {
      auto q = std::make_shared<DeviceQueue>();
      map.emplace(device_id, q);
      return q;
    }
    return it->second;
  });
}

} // namespace vox::relay
