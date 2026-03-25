#ifndef VOX_RELAY_CONVERSATION_SERVICE_HPP
#define VOX_RELAY_CONVERSATION_SERVICE_HPP

#include <vector>

#include "lib/vox_common/config.hpp"
#include "lib/vox_common/types.hpp"
#include "lib/vox_store/conversation_repository.hpp"

namespace vox::relay {

/// Orchestrates conversation creation and membership with server policy (limits, roles).
class ConversationService {
public:
  ConversationService(store::ConversationRepository& conversations, common::ServerConfig config);

  common::Result<common::ConversationId> CreateDm(const common::UserId& user_a,
                                                  const common::UserId& user_b,
                                                  const common::UserId& created_by);

  /// `member_user_ids` must include `created_by`. Duplicates are ignored. Minimum two distinct users.
  common::Result<common::ConversationId> CreateGroup(const common::UserId& created_by,
                                                   std::vector<common::UserId> member_user_ids);

  /// Admins are recorded in `conversation_members` with admin/owner roles; all recipients must be subscribed.
  common::Result<common::ConversationId> CreateChannel(const common::UserId& created_by,
                                                       const std::vector<common::UserId>& admin_user_ids,
                                                       const std::vector<common::UserId>& subscriber_user_ids);

  common::VoidResult AddMember(const common::ConversationId& conv_id,
                               const common::UserId& actor_user_id,
                               const common::UserId& new_user_id,
                               common::MemberRole role);

  common::VoidResult RemoveMember(const common::ConversationId& conv_id,
                                  const common::UserId& actor_user_id,
                                  const common::UserId& target_user_id);

  common::VoidResult SubscribeChannel(const common::ConversationId& conv_id, const common::UserId& user_id);
  common::VoidResult UnsubscribeChannel(const common::ConversationId& conv_id, const common::UserId& user_id);

  std::vector<store::ConversationRecord> ListForUser(const common::UserId& user_id);

private:
  common::Timestamp Now();

  static void SortUnique(std::vector<common::UserId>& ids);

  bool ActorCanManageMembers(const store::ConversationRecord& conv, const common::UserId& actor) const;

  store::ConversationRepository& conversations_;
  common::ServerConfig config_;
};

} // namespace vox::relay

#endif // VOX_RELAY_CONVERSATION_SERVICE_HPP
