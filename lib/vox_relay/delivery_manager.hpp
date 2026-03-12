#ifndef VOX_RELAY_DELIVERY_MANAGER_HPP
#define VOX_RELAY_DELIVERY_MANAGER_HPP

#include <deque>
#include <mutex>
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
};

class DeliveryManager {
 public:
  DeliveryManager(store::EnvelopeRepository& envelopes, std::size_t max_queue_per_device);

  common::VoidResult Enqueue(const common::DeviceId& device_id, const QueuedEnvelope& envelope);
  std::vector<QueuedEnvelope> Dequeue(const common::DeviceId& device_id, std::size_t max_count = 50);
  common::VoidResult Acknowledge(const common::DeviceId& device_id, const common::EnvelopeId& envelope_id);
  void SwitchToOffline(const common::DeviceId& device_id);
  std::size_t QueueSize(const common::DeviceId& device_id) const;

 private:
  struct DeviceQueue {
    std::deque<QueuedEnvelope> pending;
    mutable std::mutex mutex;
  };

  DeviceQueue& GetOrCreateQueue(const common::DeviceId& device_id);

  store::EnvelopeRepository& envelopes_;
  std::size_t max_queue_per_device_;
  common::ShardMap<common::DeviceId, std::shared_ptr<DeviceQueue>> queues_;
};

}  // namespace vox::relay

#endif  // VOX_RELAY_DELIVERY_MANAGER_HPP
