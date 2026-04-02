#include "lib/vox_relay/delivery_manager.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace vox::relay {

DeliveryManager::DeliveryManager(store::IEnvelopeRepository& envelopes, std::size_t max_queue_per_device) :
    envelopes_(envelopes), max_queue_per_device_(max_queue_per_device) {
}

void DeliveryManager::SetEnqueueHook(
    std::function<void(const std::string& device_scope_key, const QueuedEnvelope&)> hook) {
  enqueue_hook_ = std::move(hook);
}

common::VoidResult DeliveryManager::Enqueue(const common::UserId& user_id,
                                            const common::DeviceId& device_id,
                                            const QueuedEnvelope& envelope) {
  const std::string key = common::DeviceScopeKey(user_id, device_id);
  auto& queue = GetOrCreateQueue(user_id, device_id);
  std::lock_guard lock(queue.mutex);

  if (queue.pending.size() >= max_queue_per_device_) {
    spdlog::warn("Queue overflow for device {}, switching to offline", device_id);
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kQueueFull, .message = "Device queue full, use offline delivery"});
  }

  queue.pending.push_back(envelope);
  if (enqueue_hook_) {
    enqueue_hook_(key, envelope);
  }
  return {};
}

std::vector<QueuedEnvelope> DeliveryManager::Dequeue(const common::UserId& user_id,
                                                     const common::DeviceId& device_id,
                                                     std::size_t max_count) {
  std::vector<QueuedEnvelope> result;
  const std::string key = common::DeviceScopeKey(user_id, device_id);
  auto found = queues_.Find(key);
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

common::VoidResult DeliveryManager::Acknowledge(const common::UserId& user_id,
                                                const common::DeviceId& device_id,
                                                const common::EnvelopeId& envelope_id) {
  auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  return envelopes_.MarkAcked(envelope_id, user_id, device_id, now);
}

void DeliveryManager::SwitchToOffline(const common::UserId& user_id, const common::DeviceId& device_id) {
  const std::string key = common::DeviceScopeKey(user_id, device_id);
  auto found = queues_.Find(key);
  if (!found) {
    return;
  }

  auto& queue_ptr = *found;
  std::lock_guard lock(queue_ptr->mutex);

  auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

  for (const auto& env : queue_ptr->pending) {
    if (auto add_result = envelopes_.AddDeliveryState(env.envelope_id, user_id, device_id, now); !add_result) {
      spdlog::warn("Failed to add delivery state for envelope {}: {}", env.envelope_id, add_result.error().message);
    }
  }
  queue_ptr->pending.clear();
  spdlog::info("Switched device {} to offline delivery, persisted queued envelopes", device_id);
}

std::size_t DeliveryManager::QueueSize(const common::UserId& user_id, const common::DeviceId& device_id) const {
  const std::string key = common::DeviceScopeKey(user_id, device_id);
  auto found = queues_.Find(key);
  if (!found) {
    return 0;
  }
  std::lock_guard lock((*found)->mutex);
  return (*found)->pending.size();
}

DeliveryManager::DeviceQueue& DeliveryManager::GetOrCreateQueue(const common::UserId& user_id,
                                                                const common::DeviceId& device_id) {
  const std::string key = common::DeviceScopeKey(user_id, device_id);
  return *queues_.WithShard(key, [&](auto& map) -> std::shared_ptr<DeviceQueue> {
    auto it = map.find(key);
    if (it == map.end()) {
      auto q = std::make_shared<DeviceQueue>();
      map.emplace(key, q);
      return q;
    }
    return it->second;
  });
}

void DeliveryManager::PurgeConversationFromDeviceQueue(const common::ConversationId& conversation_id,
                                                       const common::UserId& user_id,
                                                       const common::DeviceId& device_id) {
  const std::string key = common::DeviceScopeKey(user_id, device_id);
  auto found = queues_.Find(key);
  if (!found) {
    return;
  }
  auto& queue = *found;
  std::lock_guard lock(queue->mutex);
  const auto not_conv = [&](const QueuedEnvelope& q) { return q.conversation_id != conversation_id; };
  const auto new_end = std::stable_partition(queue->pending.begin(), queue->pending.end(), not_conv);
  queue->pending.erase(new_end, queue->pending.end());
}

} // namespace vox::relay
