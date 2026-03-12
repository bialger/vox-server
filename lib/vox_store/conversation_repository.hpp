#ifndef VOX_STORE_CONVERSATION_REPOSITORY_HPP
#define VOX_STORE_CONVERSATION_REPOSITORY_HPP

#include <optional>
#include <string>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct ConversationRecord {
  common::ConversationId conversation_id;
  common::ConversationType type;
  common::UserId created_by;
  common::Timestamp created_at;
  std::string policy_blob;
};

struct MemberRecord {
  common::ConversationId conversation_id;
  common::UserId user_id;
  common::MemberRole role;
  common::Timestamp added_at;
  std::optional<common::Timestamp> removed_at;
};

class ConversationRepository {
public:
  explicit ConversationRepository(Database& db);

  common::VoidResult CreateConversation(const ConversationRecord& conv);
  std::optional<ConversationRecord> FindById(const common::ConversationId& conv_id);

  common::VoidResult AddMember(const common::ConversationId& conv_id,
                               const common::UserId& user_id,
                               common::MemberRole role,
                               common::Timestamp now);
  common::VoidResult RemoveMember(const common::ConversationId& conv_id,
                                  const common::UserId& user_id,
                                  common::Timestamp now);
  std::vector<MemberRecord> GetMembers(const common::ConversationId& conv_id);
  std::vector<ConversationRecord> GetConversationsForUser(const common::UserId& user_id);
  bool IsUserInConversation(const common::ConversationId& conv_id, const common::UserId& user_id);
  std::optional<MemberRecord> GetMember(const common::ConversationId& conv_id, const common::UserId& user_id);

  common::VoidResult Subscribe(const common::ConversationId& conv_id,
                               const common::UserId& user_id,
                               common::Timestamp now);
  common::VoidResult Unsubscribe(const common::ConversationId& conv_id,
                                 const common::UserId& user_id,
                                 common::Timestamp now);
  std::vector<common::UserId> GetSubscribers(const common::ConversationId& conv_id);
  std::size_t GetMemberCount(const common::ConversationId& conv_id);

private:
  Database& db_;
};

} // namespace vox::store

#endif // VOX_STORE_CONVERSATION_REPOSITORY_HPP
