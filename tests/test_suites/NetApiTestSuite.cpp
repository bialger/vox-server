#include "NetApiTestSuite.hpp"

#include <filesystem>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/json.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

constexpr std::size_t kCpuPoolThreads = 2;
constexpr std::size_t kCpuQueue = 64;
constexpr std::size_t kNetThreads = 2;
constexpr unsigned kHttp11 = 11;

} // namespace

void NetApiTestSuite::SetUp() {
  ProjectIntegrationTestSuite::SetUp();
  config_ = vox::common::ServerConfig::Default();
  config_.listen_address = "127.0.0.1";
  config_.listen_port = 0;
  config_.network_thread_count = kNetThreads;
  config_.admin_token = "test-admin-secret";

  db_path_ = kTemporaryDirectoryName / "net_test.db";
  blob_path_ = kTemporaryDirectoryName / "blobs";
  std::filesystem::create_directories(blob_path_);
  config_.db_path = db_path_;
  config_.blob_storage_path = blob_path_;

  db_ = std::make_unique<vox::store::Database>(db_path_.string());
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  sessions_ = std::make_unique<vox::store::SessionRepository>(*db_);
  conversations_ = std::make_unique<vox::store::ConversationRepository>(*db_);
  envelopes_ = std::make_unique<vox::store::EnvelopeRepository>(*db_);
  attachments_ = std::make_unique<vox::store::AttachmentRepository>(*db_);

  cpu_pool_ = std::make_unique<vox::common::ThreadPool>(kCpuPoolThreads, kCpuQueue);
  hasher_ = std::make_unique<vox::auth::PasswordHasher>(
      config_.argon2_time_cost, config_.argon2_memory_cost, config_.argon2_parallelism);
  tokens_ = std::make_unique<vox::auth::TokenManager>(*sessions_, config_.access_token_lifetime_seconds,
                                                      config_.refresh_token_lifetime_seconds);
  auth_ = std::make_unique<vox::auth::AuthService>(*users_, *devices_, *hasher_, *tokens_, *cpu_pool_);

  delivery_ = std::make_unique<vox::relay::DeliveryManager>(*envelopes_, config_.max_queue_depth_per_device);
  relay_ = std::make_unique<vox::relay::RelayService>(*envelopes_, *conversations_, *devices_, *delivery_);
  conv_service_ = std::make_unique<vox::relay::ConversationService>(*conversations_, config_);
  attachment_service_ = std::make_unique<vox::attachments::AttachmentService>(*attachments_, *conversations_, config_);
  admin_service_ = std::make_unique<vox::admin::AdminService>(*db_, *users_, *sessions_);

  ws_registry_ = std::make_unique<vox::net::WsPushRegistry>();
  delivery_->SetEnqueueHook([this](const vox::common::DeviceId& device_id, const vox::relay::QueuedEnvelope& q) {
    boost::json::object o;
    o["type"] = "envelope";
    o["envelope_id"] = q.envelope_id;
    o["conversation_id"] = q.conversation_id;
    o["sender_device_id"] = q.sender_device_id;
    o["ciphertext"] = q.ciphertext;
    o["server_timestamp"] = q.server_timestamp;
    ws_registry_->Notify(device_id, boost::json::serialize(o));
  });

  server_ctx_ = std::make_unique<vox::net::ServerContext>(vox::net::ServerContext{
      config_,
      *auth_,
      *tokens_,
      *relay_,
      *conv_service_,
      *delivery_,
      *envelopes_,
      *conversations_,
      *devices_,
      *attachment_service_,
      *admin_service_,
  });

  ioc_ = std::make_unique<net::io_context>();
  tcp::endpoint ep(net::ip::make_address(config_.listen_address), config_.listen_port);
  listener_ = std::make_shared<vox::net::HttpListener>(*ioc_, ep, *server_ctx_, *ws_registry_);
  listener_->run();
  port_ = listener_->ListenPort();

  for (std::size_t i = 0; i < config_.network_thread_count; ++i) {
    net_threads_.emplace_back([this]() { ioc_->run(); });
  }
}

void NetApiTestSuite::TearDown() {
  if (listener_) {
    listener_->Shutdown();
  }
  if (ioc_) {
    ioc_->stop();
  }
  net_threads_.clear();
  listener_.reset();
  server_ctx_.reset();
  ws_registry_.reset();
  delivery_.reset();
  relay_.reset();
  conv_service_.reset();
  attachment_service_.reset();
  admin_service_.reset();
  auth_.reset();
  tokens_.reset();
  hasher_.reset();
  cpu_pool_.reset();
  attachments_.reset();
  envelopes_.reset();
  conversations_.reset();
  sessions_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
  ioc_.reset();
  ProjectIntegrationTestSuite::TearDown();
}

std::pair<unsigned, std::string> NetApiTestSuite::HttpPost(const std::string& path,
                                                           const std::string& body,
                                                           const std::string& bearer,
                                                           const std::string& admin_token) {
  net::io_context cioc;
  tcp::resolver resolver(cioc);
  tcp::socket socket(cioc);
  auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port_));
  net::connect(socket, endpoints);

  http::request<http::string_body> req{http::verb::post, path, kHttp11};
  req.set(http::field::host, "127.0.0.1");
  req.set(http::field::content_type, "application/json");
  if (!bearer.empty()) {
    req.set(http::field::authorization, "Bearer " + bearer);
  }
  if (!admin_token.empty()) {
    req.set("X-Admin-Token", admin_token);
  }
  req.body() = body;
  req.prepare_payload();
  http::write(socket, req);

  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(socket, buffer, res);
  beast::error_code ec;
  ec = socket.shutdown(tcp::socket::shutdown_both, ec);
  return {static_cast<unsigned>(res.result()), std::string(res.body())};
}

std::pair<unsigned, std::string> NetApiTestSuite::HttpGet(const std::string& path,
                                                          const std::string& bearer,
                                                          const std::string& admin_token) {
  net::io_context cioc;
  tcp::resolver resolver(cioc);
  tcp::socket socket(cioc);
  auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port_));
  net::connect(socket, endpoints);

  http::request<http::string_body> req{http::verb::get, path, kHttp11};
  req.set(http::field::host, "127.0.0.1");
  if (!bearer.empty()) {
    req.set(http::field::authorization, "Bearer " + bearer);
  }
  if (!admin_token.empty()) {
    req.set("X-Admin-Token", admin_token);
  }
  req.prepare_payload();
  http::write(socket, req);

  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(socket, buffer, res);
  beast::error_code ec;
  ec = socket.shutdown(tcp::socket::shutdown_both, ec);
  return {static_cast<unsigned>(res.result()), std::string(res.body())};
}
