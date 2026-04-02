#ifndef VOX_STORE_ENVELOPE_REPOSITORY_HPP
#define VOX_STORE_ENVELOPE_REPOSITORY_HPP

#include <cstdint>
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
  /// Optional MLS / control-plane epoch for ordering (opaque to server semantics).
  std::optional<std::int64_t> ordering_epoch;
};

struct DeliveryStateRecord {
  common::EnvelopeId envelope_id;
  common::DeviceId target_device_id;
  common::Timestamp queued_at;
  std::optional<common::Timestamp> delivered_at;
  std::optional<common::Timestamp> acked_at;
};

class IEnvelopeRepository {
public:
  virtual ~IEnvelopeRepository() = default;
  virtual common::VoidResult StoreEnvelope(const EnvelopeRecord& envelope) = 0;
  virtual common::VoidResult AddDeliveryState(const common::EnvelopeId& envelope_id,
                                              const common::DeviceId& target_device_id,
                                              common::Timestamp now) = 0;
  virtual std::vector<EnvelopeRecord> GetPendingForDevice(const common::DeviceId& device_id,
                                                          std::size_t limit = 100) = 0;

  struct EnvelopePage {
    std::vector<EnvelopeRecord> envelopes;
    std::string next_cursor;
    bool has_more = false;
  };

  /// Cursor format: `server_timestamp|envelope_id` of the last row from the previous page; empty for first page.
  virtual EnvelopePage GetPendingForDeviceCursored(const common::DeviceId& device_id,
                                                   const std::string& cursor,
                                                   std::size_t limit) = 0;

  /// Returns envelopes in `conversation_id` with server_timestamp strictly greater than `since_exclusive`.
  /// Pass `since_exclusive == 0` to start from the first stored message (all timestamps are expected positive).
  virtual std::vector<EnvelopeRecord> ListForConversation(const common::ConversationId& conversation_id,
                                                          common::Timestamp since_exclusive,
                                                          std::size_t limit) = 0;

  /// Cursor format: `server_timestamp|envelope_id`; empty for first page. Ordered by server_timestamp, envelope_id.
  virtual EnvelopePage ListForConversationCursored(const common::ConversationId& conversation_id,
                                                   const std::string& cursor,
                                                   std::size_t limit) = 0;
  virtual common::VoidResult MarkDelivered(const common::EnvelopeId& envelope_id,
                                           const common::DeviceId& device_id,
                                           common::Timestamp now) = 0;
  virtual common::VoidResult MarkAcked(const common::EnvelopeId& envelope_id,
                                       const common::DeviceId& device_id,
                                       common::Timestamp now) = 0;
  virtual int DeleteExpired(common::Timestamp now) = 0;
  /// Removes pending delivery rows for any device owned by `user_id` for envelopes in `conversation_id`.
  virtual common::VoidResult DeletePendingDeliveryForUserInConversation(const common::ConversationId& conversation_id,
                                                                        const common::UserId& user_id) = 0;
  virtual bool CheckDuplicate(const common::EnvelopeId& envelope_id) = 0;
  virtual std::optional<EnvelopeRecord> FindById(const common::EnvelopeId& envelope_id) = 0;
  virtual std::size_t CountPendingForDevice(const common::DeviceId& device_id) = 0;
};

class EnvelopeRepository : public IEnvelopeRepository {
public:
  explicit EnvelopeRepository(IDatabase& db);

  common::VoidResult StoreEnvelope(const EnvelopeRecord& envelope) override;
  common::VoidResult AddDeliveryState(const common::EnvelopeId& envelope_id,
                                      const common::DeviceId& target_device_id,
                                      common::Timestamp now) override;
  std::vector<EnvelopeRecord> GetPendingForDevice(const common::DeviceId& device_id, std::size_t limit = 100) override;

  EnvelopePage GetPendingForDeviceCursored(const common::DeviceId& device_id,
                                           const std::string& cursor,
                                           std::size_t limit) override;

  std::vector<EnvelopeRecord> ListForConversation(const common::ConversationId& conversation_id,
                                                  common::Timestamp since_exclusive,
                                                  std::size_t limit) override;

  EnvelopePage ListForConversationCursored(const common::ConversationId& conversation_id,
                                             const std::string& cursor,
                                             std::size_t limit) override;
  common::VoidResult MarkDelivered(const common::EnvelopeId& envelope_id,
                                   const common::DeviceId& device_id,
                                   common::Timestamp now) override;
  common::VoidResult MarkAcked(const common::EnvelopeId& envelope_id,
                               const common::DeviceId& device_id,
                               common::Timestamp now) override;
  int DeleteExpired(common::Timestamp now) override;
  common::VoidResult DeletePendingDeliveryForUserInConversation(const common::ConversationId& conversation_id,
                                                                const common::UserId& user_id) override;
  bool CheckDuplicate(const common::EnvelopeId& envelope_id) override;
  std::optional<EnvelopeRecord> FindById(const common::EnvelopeId& envelope_id) override;
  std::size_t CountPendingForDevice(const common::DeviceId& device_id) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_ENVELOPE_REPOSITORY_HPP
