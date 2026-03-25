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

class AuthRateLimiter;

/// Non-owning references to services shared by HTTP/WebSocket handlers.
struct ServerContext {
  common::ServerConfig& config;
  vox::auth::IAuthService& auth;
  vox::auth::ITokenManager& tokens;
  vox::relay::IRelayService& relay;
  vox::relay::IConversationService& conversations;
  vox::relay::IDeliveryManager& delivery;
  vox::store::IEnvelopeRepository& envelopes;
  vox::store::IConversationRepository& conversations_store;
  vox::store::IDeviceRepository& devices;
  vox::attachments::IAttachmentService& attachments;
  vox::admin::IAdminService& admin;
  /// Optional; when non-null, limits auth endpoint frequency per client IP.
  AuthRateLimiter* auth_rate_limiter = nullptr;
};

} // namespace vox::net

#endif // VOX_NET_SERVER_CONTEXT_HPP
