#ifndef VOX_STORE_ENVELOPE_REPOSITORY_HPP
#define VOX_STORE_ENVELOPE_REPOSITORY_HPP

#include <optional>
#include <string>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct EnvelopeRecord {
  common::EnvelopeId envelope_id;
  common::ConversationId conversation_id;
  common::DeviceId sender_device_id;
  std::string ciphertext;
  common::Timestamp server_timestamp;
  int envelope_type = 0;
  std::optional<common::Timestamp> retention_until;
};

struct DeliveryStateRecord {
  common::EnvelopeId envelope_id;
  common::DeviceId target_device_id;
  common::Timestamp queued_at;
  std::optional<common::Timestamp> delivered_at;
  std::optional<common::Timestamp> acked_at;
};

class EnvelopeRepository {
 public:
  explicit EnvelopeRepository(Database& db);

  common::VoidResult StoreEnvelope(const EnvelopeRecord& envelope);
  common::VoidResult AddDeliveryState(const common::EnvelopeId& envelope_id,
                                      const common::DeviceId& target_device_id,
                                      common::Timestamp now);
  std::vector<EnvelopeRecord> GetPendingForDevice(const common::DeviceId& device_id, std::size_t limit = 100);
  common::VoidResult MarkDelivered(const common::EnvelopeId& envelope_id,
                                   const common::DeviceId& device_id,
                                   common::Timestamp now);
  common::VoidResult MarkAcked(const common::EnvelopeId& envelope_id,
                               const common::DeviceId& device_id,
                               common::Timestamp now);
  int DeleteExpired(common::Timestamp now);
  bool CheckDuplicate(const common::EnvelopeId& envelope_id);
  std::optional<EnvelopeRecord> FindById(const common::EnvelopeId& envelope_id);
  std::size_t CountPendingForDevice(const common::DeviceId& device_id);

 private:
  Database& db_;
};

}  // namespace vox::store

#endif  // VOX_STORE_ENVELOPE_REPOSITORY_HPP
