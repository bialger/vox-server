#include "lib/vox_relay/relay_service.hpp"

#include <chrono>

#include <spdlog/spdlog.h>

#include "lib/vox_common/uuid.hpp"

namespace vox::relay {

RelayService::RelayService(store::EnvelopeRepository& envelopes,
                           store::ConversationRepository& conversations,
                           store::DeviceRepository& devices,
                           DeliveryManager& delivery) :
    envelopes_(envelopes), conversations_(conversations), devices_(devices), delivery_(delivery) {
}

common::Result<SendMessageResponse> RelayService::SendMessage(const SendMessageRequest& request) {
  if (request.ciphertext.empty()) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kInvalidArgument, .message = "Ciphertext is required"});
  }

  auto sender_device = devices_.FindById(request.sender_device_id);
  if (!sender_device) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kUnauthorized, .message = "Sender device not found"});
  }

  auto conv = conversations_.FindById(request.conversation_id);
  if (!conv) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kNotFound, .message = "Conversation not found"});
  }

  bool is_member = conversations_.IsUserInConversation(request.conversation_id, sender_device->user_id);
  if (!is_member) {
    return std::unexpected(
        common::Error{.code = common::ErrorCode::kForbidden, .message = "Sender is not a member of this conversation"});
  }

  if (conv->type == common::ConversationType::kChannel) {
    auto member = conversations_.GetMember(request.conversation_id, sender_device->user_id);
    if (!member || (member->role != common::MemberRole::kOwner && member->role != common::MemberRole::kAdmin)) {
      return std::unexpected(common::Error{.code = common::ErrorCode::kForbidden,
                                           .message = "Only admins/owners can publish to channels"});
    }
  }

  auto envelope_id = request.envelope_id.empty() ? common::GenerateUuid() : request.envelope_id;

  if (envelopes_.CheckDuplicate(envelope_id)) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kDuplicate, .message = "Duplicate envelope"});
  }

  auto now = Now();

  store::EnvelopeRecord envelope;
  envelope.envelope_id = envelope_id;
  envelope.conversation_id = request.conversation_id;
  envelope.sender_device_id = request.sender_device_id;
  envelope.ciphertext = request.ciphertext;
  envelope.server_timestamp = now;
  envelope.envelope_type = request.envelope_type;

  auto store_result = envelopes_.StoreEnvelope(envelope);
  if (!store_result) {
    return std::unexpected(store_result.error());
  }

  std::vector<common::UserId> target_users;
  if (conv->type == common::ConversationType::kChannel) {
    target_users = conversations_.GetSubscribers(request.conversation_id);
  } else {
    auto members = conversations_.GetMembers(request.conversation_id);
    for (const auto& m : members) {
      target_users.push_back(m.user_id);
    }
  }

  QueuedEnvelope queued;
  queued.envelope_id = envelope_id;
  queued.conversation_id = request.conversation_id;
  queued.sender_device_id = request.sender_device_id;
  queued.ciphertext = request.ciphertext;
  queued.server_timestamp = now;

  std::size_t delivered_count = 0;
  for (const auto& uid : target_users) {
    auto user_devices = devices_.GetDevicesForUser(uid);
    for (const auto& dev : user_devices) {
      if (dev.device_id == request.sender_device_id) {
        continue;
      }
      auto enqueue_result = delivery_.Enqueue(dev.device_id, queued);
      if (enqueue_result) {
        ++delivered_count;
      } else {
        if (auto add_result = envelopes_.AddDeliveryState(envelope_id, dev.device_id, now); !add_result) {
          spdlog::warn("Failed to add delivery state for device {}: {}", dev.device_id, add_result.error().message);
        }
      }
    }
  }

  return SendMessageResponse{
      .envelope_id = envelope_id, .server_timestamp = now, .delivered_to_count = delivered_count};
}

std::vector<store::EnvelopeRecord> RelayService::SyncOffline(const common::DeviceId& device_id, std::size_t limit) {
  return envelopes_.GetPendingForDevice(device_id, limit);
}

common::VoidResult RelayService::AcknowledgeEnvelope(const common::DeviceId& device_id,
                                                     const common::EnvelopeId& envelope_id) {
  return delivery_.Acknowledge(device_id, envelope_id);
}

common::Timestamp RelayService::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace vox::relay
