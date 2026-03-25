#include "lib/vox_relay/conversation_service.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>

#include "lib/vox_common/uuid.hpp"

namespace vox::relay {

namespace {

common::Error Err(common::ErrorCode c, std::string m) {
  return common::Error{.code = c, .message = std::move(m)};
}

} // namespace

ConversationService::ConversationService(store::ConversationRepository& conversations, common::ServerConfig config) :
    conversations_(conversations), config_(std::move(config)) {
}

void ConversationService::SortUnique(std::vector<common::UserId>& ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

common::Timestamp ConversationService::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

common::Result<common::ConversationId> ConversationService::CreateDm(const common::UserId& user_a,
                                                                    const common::UserId& user_b,
                                                                    const common::UserId& created_by) {
  if (user_a == user_b) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "DM requires two distinct users"));
  }
  if (created_by != user_a && created_by != user_b) {
    return std::unexpected(Err(common::ErrorCode::kForbidden, "Creator must be one of the participants"));
  }

  auto now = Now();
  common::ConversationId conv_id = common::GenerateUuid();
  store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = common::ConversationType::kDm;
  conv.created_by = created_by;
  conv.created_at = now;

  auto cr = conversations_.CreateConversation(conv);
  if (!cr) {
    return std::unexpected(cr.error());
  }

  auto owner = (created_by == user_a) ? user_a : user_b;
  auto other = (owner == user_a) ? user_b : user_a;

  if (auto r1 = conversations_.AddMember(conv_id, owner, common::MemberRole::kOwner, now); !r1) {
    return std::unexpected(r1.error());
  }
  if (auto r2 = conversations_.AddMember(conv_id, other, common::MemberRole::kMember, now); !r2) {
    return std::unexpected(r2.error());
  }

  return conv_id;
}

common::Result<common::ConversationId> ConversationService::CreateGroup(const common::UserId& created_by,
                                                                        std::vector<common::UserId> member_user_ids) {
  SortUnique(member_user_ids);
  if (member_user_ids.size() < 2) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Group requires at least two members"));
  }
  if (std::find(member_user_ids.begin(), member_user_ids.end(), created_by) == member_user_ids.end()) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Creator must be included in members"));
  }
  if (member_user_ids.size() > config_.max_group_size) {
    return std::unexpected(
        Err(common::ErrorCode::kQuotaExceeded, "Group exceeds configured maximum member count"));
  }

  auto now = Now();
  common::ConversationId conv_id = common::GenerateUuid();
  store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = common::ConversationType::kGroup;
  conv.created_by = created_by;
  conv.created_at = now;

  auto cr = conversations_.CreateConversation(conv);
  if (!cr) {
    return std::unexpected(cr.error());
  }

  for (const auto& uid : member_user_ids) {
    auto role = (uid == created_by) ? common::MemberRole::kOwner : common::MemberRole::kMember;
    if (auto r = conversations_.AddMember(conv_id, uid, role, now); !r) {
      return std::unexpected(r.error());
    }
  }

  return conv_id;
}

common::Result<common::ConversationId> ConversationService::CreateChannel(const common::UserId& created_by,
                                                                          const std::vector<common::UserId>& admin_user_ids,
                                                                          const std::vector<common::UserId>& subscriber_user_ids) {
  if (admin_user_ids.empty()) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Channel requires at least one admin"));
  }
  if (std::find(admin_user_ids.begin(), admin_user_ids.end(), created_by) == admin_user_ids.end()) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Creator must be listed as admin"));
  }

  std::unordered_set<std::string> seen;
  std::size_t total_subscribers = 0;
  for (const auto& a : admin_user_ids) {
    if (!seen.insert(a).second) {
      return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Duplicate admin user id"));
    }
    ++total_subscribers;
  }
  for (const auto& s : subscriber_user_ids) {
    if (seen.count(s)) {
      continue;
    }
    seen.insert(s);
    ++total_subscribers;
  }

  if (total_subscribers > config_.max_channel_size) {
    return std::unexpected(
        Err(common::ErrorCode::kQuotaExceeded, "Channel exceeds configured maximum subscriber count"));
  }

  auto now = Now();
  common::ConversationId conv_id = common::GenerateUuid();
  store::ConversationRecord conv;
  conv.conversation_id = conv_id;
  conv.type = common::ConversationType::kChannel;
  conv.created_by = created_by;
  conv.created_at = now;

  auto cr = conversations_.CreateConversation(conv);
  if (!cr) {
    return std::unexpected(cr.error());
  }

  for (const auto& uid : admin_user_ids) {
    auto role = (uid == created_by) ? common::MemberRole::kOwner : common::MemberRole::kAdmin;
    if (auto r = conversations_.AddMember(conv_id, uid, role, now); !r) {
      return std::unexpected(r.error());
    }
    if (auto r = conversations_.Subscribe(conv_id, uid, now); !r) {
      return std::unexpected(r.error());
    }
  }

  for (const auto& uid : subscriber_user_ids) {
    if (std::find(admin_user_ids.begin(), admin_user_ids.end(), uid) != admin_user_ids.end()) {
      continue;
    }
    if (auto r = conversations_.Subscribe(conv_id, uid, now); !r) {
      return std::unexpected(r.error());
    }
  }

  return conv_id;
}

bool ConversationService::ActorCanManageMembers(const store::ConversationRecord& conv,
                                                const common::UserId& actor) const {
  auto m = conversations_.GetMember(conv.conversation_id, actor);
  if (!m) {
    return false;
  }
  return m->role == common::MemberRole::kOwner || m->role == common::MemberRole::kAdmin;
}

common::VoidResult ConversationService::AddMember(const common::ConversationId& conv_id,
                                                  const common::UserId& actor_user_id,
                                                  const common::UserId& new_user_id,
                                                  common::MemberRole role) {
  auto conv = conversations_.FindById(conv_id);
  if (!conv) {
    return std::unexpected(Err(common::ErrorCode::kNotFound, "Conversation not found"));
  }
  if (conv->type == common::ConversationType::kDm) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Cannot add members to a DM"));
  }
  if (conv->type == common::ConversationType::kChannel) {
    return std::unexpected(
        Err(common::ErrorCode::kInvalidArgument, "Use channel subscribe for channel membership"));
  }

  if (!ActorCanManageMembers(*conv, actor_user_id)) {
    return std::unexpected(Err(common::ErrorCode::kForbidden, "Insufficient permissions"));
  }

  if (conversations_.GetMemberCount(conv_id) >= config_.max_group_size) {
    return std::unexpected(Err(common::ErrorCode::kQuotaExceeded, "Group is full"));
  }

  return conversations_.AddMember(conv_id, new_user_id, role, Now());
}

common::VoidResult ConversationService::RemoveMember(const common::ConversationId& conv_id,
                                                     const common::UserId& actor_user_id,
                                                     const common::UserId& target_user_id) {
  auto conv = conversations_.FindById(conv_id);
  if (!conv) {
    return std::unexpected(Err(common::ErrorCode::kNotFound, "Conversation not found"));
  }
  if (conv->type == common::ConversationType::kDm) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Cannot remove members from a DM"));
  }
  if (conv->type == common::ConversationType::kChannel) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Use channel unsubscribe"));
  }

  if (!ActorCanManageMembers(*conv, actor_user_id)) {
    return std::unexpected(Err(common::ErrorCode::kForbidden, "Insufficient permissions"));
  }

  return conversations_.RemoveMember(conv_id, target_user_id, Now());
}

common::VoidResult ConversationService::SubscribeChannel(const common::ConversationId& conv_id,
                                                         const common::UserId& user_id) {
  auto conv = conversations_.FindById(conv_id);
  if (!conv) {
    return std::unexpected(Err(common::ErrorCode::kNotFound, "Conversation not found"));
  }
  if (conv->type != common::ConversationType::kChannel) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Not a channel conversation"));
  }

  auto subs = conversations_.GetSubscribers(conv_id);
  for (const auto& u : subs) {
    if (u == user_id) {
      return conversations_.Subscribe(conv_id, user_id, Now());
    }
  }
  if (subs.size() >= config_.max_channel_size) {
    return std::unexpected(Err(common::ErrorCode::kQuotaExceeded, "Channel subscriber limit reached"));
  }

  return conversations_.Subscribe(conv_id, user_id, Now());
}

common::VoidResult ConversationService::UnsubscribeChannel(const common::ConversationId& conv_id,
                                                           const common::UserId& user_id) {
  auto conv = conversations_.FindById(conv_id);
  if (!conv) {
    return std::unexpected(Err(common::ErrorCode::kNotFound, "Conversation not found"));
  }
  if (conv->type != common::ConversationType::kChannel) {
    return std::unexpected(Err(common::ErrorCode::kInvalidArgument, "Not a channel conversation"));
  }

  auto m = conversations_.GetMember(conv_id, user_id);
  if (m && (m->role == common::MemberRole::kOwner || m->role == common::MemberRole::kAdmin)) {
    return std::unexpected(Err(common::ErrorCode::kForbidden, "Admins cannot unsubscribe; remove admin role first"));
  }

  return conversations_.Unsubscribe(conv_id, user_id, Now());
}

std::vector<store::ConversationRecord> ConversationService::ListForUser(const common::UserId& user_id) {
  return conversations_.GetConversationsForUser(user_id);
}

} // namespace vox::relay
