#ifndef VOX_RELAY_CONVERSATION_SERVICE_HPP
#define VOX_RELAY_CONVERSATION_SERVICE_HPP

#include <vector>

#include "lib/vox_common/config.hpp"
#include "lib/vox_common/types.hpp"
#include "lib/vox_relay/delivery_manager.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/envelope_repository.hpp"

namespace vox::relay {

/// Orchestrates conversation creation and membership with server policy (limits, roles).
class IConversationService {
public:
  virtual ~IConversationService() = default;
  virtual common::Result<common::ConversationId> CreateDm(const common::UserId& user_a,
                                                          const common::UserId& user_b,
                                                          const common::UserId& created_by) = 0;

  /// `member_user_ids` must include `created_by`. Duplicates are ignored. Minimum two distinct users.
  virtual common::Result<common::ConversationId> CreateGroup(const common::UserId& created_by,
                                                             std::vector<common::UserId> member_user_ids) = 0;

  /// Admins are recorded in `conversation_members` with admin/owner roles; all recipients must be subscribed.
  virtual common::Result<common::ConversationId> CreateChannel(
      const common::UserId& created_by,
      const std::vector<common::UserId>& admin_user_ids,
      const std::vector<common::UserId>& subscriber_user_ids) = 0;

  virtual common::VoidResult AddMember(const common::ConversationId& conv_id,
                                       const common::UserId& actor_user_id,
                                       const common::UserId& new_user_id,
                                       common::MemberRole role) = 0;

  virtual common::VoidResult RemoveMember(const common::ConversationId& conv_id,
                                          const common::UserId& actor_user_id,
                                          const common::UserId& target_user_id) = 0;

  virtual common::VoidResult SubscribeChannel(const common::ConversationId& conv_id, const common::UserId& user_id) = 0;
  virtual common::VoidResult UnsubscribeChannel(const common::ConversationId& conv_id,
                                                const common::UserId& user_id) = 0;

  virtual std::vector<store::ConversationRecord> ListForUser(const common::UserId& user_id) = 0;
};

class ConversationService : public IConversationService {
public:
  ConversationService(store::IConversationRepository& conversations,
                      store::IEnvelopeRepository& envelopes,
                      store::IDeviceRepository& devices,
                      IDeliveryManager& delivery,
                      common::ServerConfig config);

  common::Result<common::ConversationId> CreateDm(const common::UserId& user_a,
                                                  const common::UserId& user_b,
                                                  const common::UserId& created_by) override;

  common::Result<common::ConversationId> CreateGroup(const common::UserId& created_by,
                                                     std::vector<common::UserId> member_user_ids) override;

  common::Result<common::ConversationId> CreateChannel(const common::UserId& created_by,
                                                       const std::vector<common::UserId>& admin_user_ids,
                                                       const std::vector<common::UserId>& subscriber_user_ids) override;

  common::VoidResult AddMember(const common::ConversationId& conv_id,
                               const common::UserId& actor_user_id,
                               const common::UserId& new_user_id,
                               common::MemberRole role) override;

  common::VoidResult RemoveMember(const common::ConversationId& conv_id,
                                  const common::UserId& actor_user_id,
                                  const common::UserId& target_user_id) override;

  common::VoidResult SubscribeChannel(const common::ConversationId& conv_id, const common::UserId& user_id) override;
  common::VoidResult UnsubscribeChannel(const common::ConversationId& conv_id, const common::UserId& user_id) override;

  std::vector<store::ConversationRecord> ListForUser(const common::UserId& user_id) override;

private:
  common::Timestamp Now();

  static void SortUnique(std::vector<common::UserId>& ids);

  bool ActorCanManageMembers(const store::ConversationRecord& conv, const common::UserId& actor) const;

  void PurgeMemberDelivery(const common::ConversationId& conv_id, const common::UserId& user_id);

  store::IConversationRepository& conversations_;
  store::IEnvelopeRepository& envelopes_;
  store::IDeviceRepository& devices_;
  IDeliveryManager& delivery_;
  common::ServerConfig config_;
};

} // namespace vox::relay

#endif // VOX_RELAY_CONVERSATION_SERVICE_HPP
