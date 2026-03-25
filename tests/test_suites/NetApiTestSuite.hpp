#ifndef NETAPITESTSUITE_HPP
#define NETAPITESTSUITE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio/io_context.hpp>

#include "lib/vox_admin/admin_service.hpp"
#include "lib/vox_attachments/attachment_service.hpp"
#include "lib/vox_auth/auth_service.hpp"
#include "lib/vox_auth/password_hasher.hpp"
#include "lib/vox_auth/token_manager.hpp"
#include "lib/vox_common/config.hpp"
#include "lib/vox_common/thread_pool.hpp"
#include "lib/vox_net/http_listener.hpp"
#include "lib/vox_net/server_context.hpp"
#include "lib/vox_net/ws_registry.hpp"
#include "lib/vox_relay/conversation_service.hpp"
#include "lib/vox_relay/delivery_manager.hpp"
#include "lib/vox_relay/relay_service.hpp"
#include "lib/vox_store/attachment_repository.hpp"
#include "lib/vox_store/conversation_repository.hpp"
#include "lib/vox_store/database.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/envelope_repository.hpp"
#include "lib/vox_store/session_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

#include "ProjectIntegrationTestSuite.hpp"

/// Integration tests against a real HTTP listener (requires Boost; built only when TESTS_ONLY=OFF).
class NetApiTestSuite : public ProjectIntegrationTestSuite {
protected:
  void SetUp() override;
  void TearDown() override;

  /// HTTP status code and response body.
  std::pair<unsigned, std::string> HttpPost(const std::string& path,
                                            const std::string& body,
                                            const std::string& bearer = {},
                                            const std::string& admin_token = {});

  std::pair<unsigned, std::string> HttpGet(const std::string& path,
                                           const std::string& bearer = {},
                                           const std::string& admin_token = {});

  std::uint16_t Port() const {
    return port_;
  }

  std::filesystem::path DbPath() const {
    return db_path_;
  }
  std::filesystem::path BlobPath() const {
    return blob_path_;
  }

  vox::common::ServerConfig config_;
  std::unique_ptr<vox::store::Database> db_;
  std::unique_ptr<vox::store::UserRepository> users_;
  std::unique_ptr<vox::store::DeviceRepository> devices_;
  std::unique_ptr<vox::store::SessionRepository> sessions_;
  std::unique_ptr<vox::store::ConversationRepository> conversations_;
  std::unique_ptr<vox::store::EnvelopeRepository> envelopes_;
  std::unique_ptr<vox::store::AttachmentRepository> attachments_;
  std::unique_ptr<vox::common::ThreadPool> cpu_pool_;
  std::unique_ptr<vox::auth::PasswordHasher> hasher_;
  std::unique_ptr<vox::auth::TokenManager> tokens_;
  std::unique_ptr<vox::auth::AuthService> auth_;
  std::unique_ptr<vox::relay::DeliveryManager> delivery_;
  std::unique_ptr<vox::relay::RelayService> relay_;
  std::unique_ptr<vox::relay::ConversationService> conv_service_;
  std::unique_ptr<vox::attachments::AttachmentService> attachment_service_;
  std::unique_ptr<vox::admin::AdminService> admin_service_;
  std::unique_ptr<vox::net::WsPushRegistry> ws_registry_;
  std::unique_ptr<vox::net::ServerContext> server_ctx_;
  std::shared_ptr<vox::net::HttpListener> listener_;
  std::unique_ptr<boost::asio::io_context> ioc_;
  std::vector<std::jthread> net_threads_;
  std::uint16_t port_ = 0;
  std::filesystem::path db_path_;
  std::filesystem::path blob_path_;
};

#endif // NETAPITESTSUITE_HPP
