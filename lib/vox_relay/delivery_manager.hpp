#ifndef VOX_RELAY_DELIVERY_MANAGER_HPP
#define VOX_RELAY_DELIVERY_MANAGER_HPP

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lib/vox_common/shard_map.hpp"
#include "lib/vox_common/types.hpp"
#include "lib/vox_store/envelope_repository.hpp"

namespace vox::relay {

struct QueuedEnvelope {
  common::EnvelopeId envelope_id;
  common::ConversationId conversation_id;
  common::DeviceId sender_device_id;
  std::string ciphertext;
  common::Timestamp server_timestamp;
  std::optional<std::int64_t> ordering_epoch;
};

class IDeliveryManager {
public:
  virtual ~IDeliveryManager() = default;
  /// Optional hook invoked after a successful in-memory enqueue (e.g. WebSocket push).
  virtual void SetEnqueueHook(std::function<void(const common::DeviceId&, const QueuedEnvelope&)> hook) = 0;

  virtual common::VoidResult Enqueue(const common::DeviceId& device_id, const QueuedEnvelope& envelope) = 0;
  virtual std::vector<QueuedEnvelope> Dequeue(const common::DeviceId& device_id, std::size_t max_count = 50) = 0;
  virtual common::VoidResult Acknowledge(const common::DeviceId& device_id, const common::EnvelopeId& envelope_id) = 0;
  virtual void SwitchToOffline(const common::DeviceId& device_id) = 0;
  virtual std::size_t QueueSize(const common::DeviceId& device_id) const = 0;
  /// Drops queued envelopes for `device_id` that belong to `conversation_id` (e.g. after membership change).
  virtual void PurgeConversationFromDeviceQueue(const common::ConversationId& conversation_id,
                                                const common::DeviceId& device_id) = 0;
};

/// In-memory per-device queues use `ShardMap` + mutexes (not Asio strands); ordering with WebSocket uses `post` on the
/// connection executor.
class DeliveryManager : public IDeliveryManager {
public:
  DeliveryManager(store::IEnvelopeRepository& envelopes, std::size_t max_queue_per_device);

  void SetEnqueueHook(std::function<void(const common::DeviceId&, const QueuedEnvelope&)> hook) override;

  common::VoidResult Enqueue(const common::DeviceId& device_id, const QueuedEnvelope& envelope) override;
  std::vector<QueuedEnvelope> Dequeue(const common::DeviceId& device_id, std::size_t max_count = 50) override;
  common::VoidResult Acknowledge(const common::DeviceId& device_id, const common::EnvelopeId& envelope_id) override;
  void SwitchToOffline(const common::DeviceId& device_id) override;
  std::size_t QueueSize(const common::DeviceId& device_id) const override;
  void PurgeConversationFromDeviceQueue(const common::ConversationId& conversation_id,
                                        const common::DeviceId& device_id) override;

private:
  struct DeviceQueue {
    std::deque<QueuedEnvelope> pending;
    mutable std::mutex mutex;
  };

  DeviceQueue& GetOrCreateQueue(const common::DeviceId& device_id);

  store::IEnvelopeRepository& envelopes_;
  std::size_t max_queue_per_device_;
  common::ShardMap<common::DeviceId, std::shared_ptr<DeviceQueue>> queues_;
  std::function<void(const common::DeviceId&, const QueuedEnvelope&)> enqueue_hook_;
};

} // namespace vox::relay

#endif // VOX_RELAY_DELIVERY_MANAGER_HPP
