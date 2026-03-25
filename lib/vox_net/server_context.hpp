#ifndef VOX_NET_SERVER_CONTEXT_HPP
#define VOX_NET_SERVER_CONTEXT_HPP

#include "lib/vox_admin/admin_service.hpp"
#include "lib/vox_attachments/attachment_service.hpp"
#include "lib/vox_auth/auth_service.hpp"
#include "lib/vox_auth/token_manager.hpp"
#include "lib/vox_common/config.hpp"
#include "lib/vox_relay/conversation_service.hpp"
#include "lib/vox_relay/delivery_manager.hpp"
#include "lib/vox_relay/relay_service.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/envelope_repository.hpp"

namespace vox::net {

/// Non-owning references to services shared by HTTP/WebSocket handlers.
struct ServerContext {
  common::ServerConfig& config;
  vox::auth::AuthService& auth;
  vox::auth::TokenManager& tokens;
  vox::relay::RelayService& relay;
  vox::relay::ConversationService& conversations;
  vox::relay::DeliveryManager& delivery;
  vox::store::EnvelopeRepository& envelopes;
  vox::store::ConversationRepository& conversations_store;
  vox::store::DeviceRepository& devices;
  vox::attachments::AttachmentService& attachments;
  vox::admin::AdminService& admin;
};

} // namespace vox::net

#endif // VOX_NET_SERVER_CONTEXT_HPP
