#ifndef VOX_RELAY_RELAY_SERVICE_HPP
#define VOX_RELAY_RELAY_SERVICE_HPP

#include <string>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_relay/delivery_manager.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/envelope_repository.hpp"

namespace vox::relay {

struct SendMessageRequest {
  common::DeviceId sender_device_id;
  common::ConversationId conversation_id;
  std::string ciphertext;
  std::string envelope_id;
  int envelope_type = 0;
};

struct SendMessageResponse {
  common::EnvelopeId envelope_id;
  common::Timestamp server_timestamp;
  std::size_t delivered_to_count;
};

class IRelayService {
public:
  virtual ~IRelayService() = default;
  virtual common::Result<SendMessageResponse> SendEnvelope(const SendMessageRequest& request) = 0;
  virtual std::vector<store::EnvelopeRecord> SyncOffline(const common::DeviceId& device_id,
                                                         std::size_t limit = 100) = 0;
  virtual common::VoidResult AcknowledgeEnvelope(const common::DeviceId& device_id,
                                                 const common::EnvelopeId& envelope_id) = 0;
};

class RelayService : public IRelayService {
public:
  RelayService(store::IEnvelopeRepository& envelopes,
               store::IConversationRepository& conversations,
               store::IDeviceRepository& devices,
               IDeliveryManager& delivery);

  common::Result<SendMessageResponse> SendEnvelope(const SendMessageRequest& request) override;
  std::vector<store::EnvelopeRecord> SyncOffline(const common::DeviceId& device_id, std::size_t limit = 100) override;
  common::VoidResult AcknowledgeEnvelope(const common::DeviceId& device_id,
                                         const common::EnvelopeId& envelope_id) override;

private:
  common::Timestamp Now();

  store::IEnvelopeRepository& envelopes_;
  store::IConversationRepository& conversations_;
  store::IDeviceRepository& devices_;
  IDeliveryManager& delivery_;
};

} // namespace vox::relay

#endif // VOX_RELAY_RELAY_SERVICE_HPP
