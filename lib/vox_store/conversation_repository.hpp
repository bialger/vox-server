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

class IConversationRepository {
public:
  virtual ~IConversationRepository() = default;
  virtual common::VoidResult CreateConversation(const ConversationRecord& conv) = 0;
  virtual std::optional<ConversationRecord> FindById(const common::ConversationId& conv_id) = 0;

  virtual common::VoidResult AddMember(const common::ConversationId& conv_id,
                                       const common::UserId& user_id,
                                       common::MemberRole role,
                                       common::Timestamp now) = 0;
  virtual common::VoidResult RemoveMember(const common::ConversationId& conv_id,
                                          const common::UserId& user_id,
                                          common::Timestamp now) = 0;
  virtual std::vector<MemberRecord> GetMembers(const common::ConversationId& conv_id) = 0;
  virtual std::vector<ConversationRecord> GetConversationsForUser(const common::UserId& user_id) = 0;
  virtual bool IsUserInConversation(const common::ConversationId& conv_id, const common::UserId& user_id) = 0;
  virtual std::optional<MemberRecord> GetMember(const common::ConversationId& conv_id,
                                                const common::UserId& user_id) = 0;

  virtual common::VoidResult Subscribe(const common::ConversationId& conv_id,
                                       const common::UserId& user_id,
                                       common::Timestamp now) = 0;
  virtual common::VoidResult Unsubscribe(const common::ConversationId& conv_id,
                                         const common::UserId& user_id,
                                         common::Timestamp now) = 0;
  virtual std::vector<common::UserId> GetSubscribers(const common::ConversationId& conv_id) = 0;
  virtual std::size_t GetMemberCount(const common::ConversationId& conv_id) = 0;
};

class ConversationRepository : public IConversationRepository {
public:
  explicit ConversationRepository(IDatabase& db);

  common::VoidResult CreateConversation(const ConversationRecord& conv) override;
  std::optional<ConversationRecord> FindById(const common::ConversationId& conv_id) override;

  common::VoidResult AddMember(const common::ConversationId& conv_id,
                               const common::UserId& user_id,
                               common::MemberRole role,
                               common::Timestamp now) override;
  common::VoidResult RemoveMember(const common::ConversationId& conv_id,
                                  const common::UserId& user_id,
                                  common::Timestamp now) override;
  std::vector<MemberRecord> GetMembers(const common::ConversationId& conv_id) override;
  std::vector<ConversationRecord> GetConversationsForUser(const common::UserId& user_id) override;
  bool IsUserInConversation(const common::ConversationId& conv_id, const common::UserId& user_id) override;
  std::optional<MemberRecord> GetMember(const common::ConversationId& conv_id, const common::UserId& user_id) override;

  common::VoidResult Subscribe(const common::ConversationId& conv_id,
                               const common::UserId& user_id,
                               common::Timestamp now) override;
  common::VoidResult Unsubscribe(const common::ConversationId& conv_id,
                                 const common::UserId& user_id,
                                 common::Timestamp now) override;
  std::vector<common::UserId> GetSubscribers(const common::ConversationId& conv_id) override;
  std::size_t GetMemberCount(const common::ConversationId& conv_id) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_CONVERSATION_REPOSITORY_HPP
