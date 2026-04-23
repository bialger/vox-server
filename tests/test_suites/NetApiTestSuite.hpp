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
#include "lib/vox_store/sdui_repository.hpp"
#include "lib/vox_store/session_repository.hpp"
#include "lib/vox_store/sync_state_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

#include "ProjectIntegrationTestSuite.hpp"

/// Integration tests against a real HTTP listener (requires Boost; built only when TESTS_ONLY=OFF).
class NetApiTestSuite : public ProjectIntegrationTestSuite {
protected:
  void SetUp() override;
  void TearDown() override;

  struct RegisteredUser {
    std::string user_id;
    std::string access_token;
    std::string refresh_token;
  };

  /// Registers a user via `POST /v1/register` with fixed crypto fields; asserts HTTP 200.
  RegisteredUser RegisterUser(const std::string& username, const std::string& device_id);

  /// Opens a TCP connection to `127.0.0.1:Port()`, sends an HTTP/1.1 request, reads the response body,
  /// then closes the socket. Uses Boost.Asio (sync resolver/connect) and Boost.Beast.
  /// \param method  "GET", "POST", "PUT", "DELETE", etc.
  /// \param content_type  If non-empty, sets `Content-Type`. For POST/PUT with a non-empty \p body
  ///                      and empty \p content_type, defaults to `application/json`.
  std::pair<unsigned, std::string> AsioHttpExchange(const std::string& method,
                                                    const std::string& path,
                                                    const std::string& body = {},
                                                    const std::string& bearer = {},
                                                    const std::string& admin_token = {},
                                                    const std::string& content_type = {});

  /// HTTP status code and response body.
  std::pair<unsigned, std::string> HttpPost(const std::string& path,
                                            const std::string& body,
                                            const std::string& bearer = {},
                                            const std::string& admin_token = {});

  std::pair<unsigned, std::string> HttpGet(const std::string& path,
                                           const std::string& bearer = {},
                                           const std::string& admin_token = {});

  std::pair<unsigned, std::string> HttpPut(const std::string& path,
                                           const std::string& body,
                                           const std::string& bearer = {},
                                           const std::string& admin_token = {},
                                           const std::string& content_type = "application/octet-stream");

  std::pair<unsigned, std::string> HttpDelete(const std::string& path,
                                              const std::string& body = {},
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
  std::unique_ptr<vox::store::SyncStateRepository> sync_state_;
  std::unique_ptr<vox::store::SduiRepository> sdui_;
  std::unique_ptr<vox::common::ThreadPool> cpu_pool_;
  std::unique_ptr<vox::common::ThreadPool> storage_pool_;
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
