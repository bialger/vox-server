#include "lib/vox_store/conversation_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <sqlite3.h>

namespace vox::store {

ConversationRepository::ConversationRepository(Database& db) : db_(db) {
}

common::VoidResult ConversationRepository::CreateConversation(const ConversationRecord& conv) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(db_.Connection(),
                           "INSERT INTO conversations (conversation_id, type, created_by, created_at, policy_blob) "
                           "VALUES (?, ?, ?, ?, ?)");
    stmt.bind(1, conv.conversation_id);
    stmt.bind(2, static_cast<int>(conv.type));
    stmt.bind(3, conv.created_by);
    stmt.bind(4, conv.created_at);
    stmt.bind(5, conv.policy_blob);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    if (e.getErrorCode() == SQLITE_CONSTRAINT) {
      return std::unexpected(common::Error{common::ErrorCode::kAlreadyExists, "Conversation already exists"});
    }
    return std::unexpected(common::Error{common::ErrorCode::kInternal, e.what()});
  }
}

std::optional<ConversationRecord> ConversationRepository::FindById(const common::ConversationId& conv_id) {
  SQLite::Statement stmt(db_.Connection(), "SELECT * FROM conversations WHERE conversation_id = ?");
  stmt.bind(1, conv_id);
  if (stmt.executeStep()) {
    ConversationRecord rec;
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.type = static_cast<common::ConversationType>(stmt.getColumn("type").getInt());
    rec.created_by = stmt.getColumn("created_by").getString();
    rec.created_at = stmt.getColumn("created_at").getInt64();
    rec.policy_blob = stmt.getColumn("policy_blob").getString();
    return rec;
  }
  return std::nullopt;
}

common::VoidResult ConversationRepository::AddMember(const common::ConversationId& conv_id,
                                                     const common::UserId& user_id,
                                                     common::MemberRole role,
                                                     common::Timestamp now) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(
        db_.Connection(),
        "INSERT INTO conversation_members (conversation_id, user_id, role, added_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(conversation_id, user_id) DO UPDATE SET removed_at = NULL, role = ?, added_at = ?");
    stmt.bind(1, conv_id);
    stmt.bind(2, user_id);
    stmt.bind(3, static_cast<int>(role));
    stmt.bind(4, now);
    stmt.bind(5, static_cast<int>(role));
    stmt.bind(6, now);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{common::ErrorCode::kInternal, e.what()});
  }
}

common::VoidResult ConversationRepository::RemoveMember(const common::ConversationId& conv_id,
                                                        const common::UserId& user_id,
                                                        common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE conversation_members SET removed_at = ? WHERE conversation_id = ? AND user_id = ? AND "
                         "removed_at IS NULL");
  stmt.bind(1, now);
  stmt.bind(2, conv_id);
  stmt.bind(3, user_id);
  stmt.exec();
  return {};
}

std::vector<MemberRecord> ConversationRepository::GetMembers(const common::ConversationId& conv_id) {
  std::vector<MemberRecord> result;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT * FROM conversation_members WHERE conversation_id = ? AND removed_at IS NULL");
  stmt.bind(1, conv_id);
  while (stmt.executeStep()) {
    MemberRecord rec;
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.user_id = stmt.getColumn("user_id").getString();
    rec.role = static_cast<common::MemberRole>(stmt.getColumn("role").getInt());
    rec.added_at = stmt.getColumn("added_at").getInt64();
    if (!stmt.getColumn("removed_at").isNull()) {
      rec.removed_at = stmt.getColumn("removed_at").getInt64();
    }
    result.push_back(std::move(rec));
  }
  return result;
}

std::vector<ConversationRecord> ConversationRepository::GetConversationsForUser(const common::UserId& user_id) {
  std::vector<ConversationRecord> result;
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT c.* FROM conversations c "
                         "JOIN conversation_members m ON c.conversation_id = m.conversation_id "
                         "WHERE m.user_id = ? AND m.removed_at IS NULL");
  stmt.bind(1, user_id);
  while (stmt.executeStep()) {
    ConversationRecord rec;
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.type = static_cast<common::ConversationType>(stmt.getColumn("type").getInt());
    rec.created_by = stmt.getColumn("created_by").getString();
    rec.created_at = stmt.getColumn("created_at").getInt64();
    rec.policy_blob = stmt.getColumn("policy_blob").getString();
    result.push_back(std::move(rec));
  }
  return result;
}

bool ConversationRepository::IsUserInConversation(const common::ConversationId& conv_id,
                                                  const common::UserId& user_id) {
  auto conv = FindById(conv_id);
  if (!conv) {
    return false;
  }

  if (conv->type == common::ConversationType::kChannel) {
    SQLite::Statement stmt(
        db_.Connection(),
        "SELECT 1 FROM channel_subscriptions WHERE conversation_id = ? AND user_id = ? AND unsubscribed_at IS NULL");
    stmt.bind(1, conv_id);
    stmt.bind(2, user_id);
    return stmt.executeStep();
  }

  SQLite::Statement stmt(
      db_.Connection(),
      "SELECT 1 FROM conversation_members WHERE conversation_id = ? AND user_id = ? AND removed_at IS NULL");
  stmt.bind(1, conv_id);
  stmt.bind(2, user_id);
  return stmt.executeStep();
}

std::optional<MemberRecord> ConversationRepository::GetMember(const common::ConversationId& conv_id,
                                                              const common::UserId& user_id) {
  SQLite::Statement stmt(
      db_.Connection(),
      "SELECT * FROM conversation_members WHERE conversation_id = ? AND user_id = ? AND removed_at IS NULL");
  stmt.bind(1, conv_id);
  stmt.bind(2, user_id);
  if (stmt.executeStep()) {
    MemberRecord rec;
    rec.conversation_id = stmt.getColumn("conversation_id").getString();
    rec.user_id = stmt.getColumn("user_id").getString();
    rec.role = static_cast<common::MemberRole>(stmt.getColumn("role").getInt());
    rec.added_at = stmt.getColumn("added_at").getInt64();
    return rec;
  }
  return std::nullopt;
}

common::VoidResult ConversationRepository::Subscribe(const common::ConversationId& conv_id,
                                                     const common::UserId& user_id,
                                                     common::Timestamp now) {
  try {
    auto lock = db_.WriteLock();
    SQLite::Statement stmt(
        db_.Connection(),
        "INSERT INTO channel_subscriptions (conversation_id, user_id, subscribed_at) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(conversation_id, user_id) DO UPDATE SET unsubscribed_at = NULL, subscribed_at = ?");
    stmt.bind(1, conv_id);
    stmt.bind(2, user_id);
    stmt.bind(3, now);
    stmt.bind(4, now);
    stmt.exec();
    return {};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{common::ErrorCode::kInternal, e.what()});
  }
}

common::VoidResult ConversationRepository::Unsubscribe(const common::ConversationId& conv_id,
                                                       const common::UserId& user_id,
                                                       common::Timestamp now) {
  auto lock = db_.WriteLock();
  SQLite::Statement stmt(db_.Connection(),
                         "UPDATE channel_subscriptions SET unsubscribed_at = ? "
                         "WHERE conversation_id = ? AND user_id = ? AND unsubscribed_at IS NULL");
  stmt.bind(1, now);
  stmt.bind(2, conv_id);
  stmt.bind(3, user_id);
  stmt.exec();
  return {};
}

std::vector<common::UserId> ConversationRepository::GetSubscribers(const common::ConversationId& conv_id) {
  std::vector<common::UserId> result;
  SQLite::Statement stmt(
      db_.Connection(),
      "SELECT user_id FROM channel_subscriptions WHERE conversation_id = ? AND unsubscribed_at IS NULL");
  stmt.bind(1, conv_id);
  while (stmt.executeStep()) {
    result.push_back(stmt.getColumn(0).getString());
  }
  return result;
}

std::size_t ConversationRepository::GetMemberCount(const common::ConversationId& conv_id) {
  SQLite::Statement stmt(db_.Connection(),
                         "SELECT COUNT(*) FROM conversation_members WHERE conversation_id = ? AND removed_at IS NULL");
  stmt.bind(1, conv_id);
  if (stmt.executeStep()) {
    return static_cast<std::size_t>(stmt.getColumn(0).getInt64());
  }
  return 0;
}

} // namespace vox::store
