#include "NetApiTestSuite.hpp"

#include <filesystem>
#include <stdexcept>
#include <string_view>

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
constexpr std::size_t kStoragePoolThreads = 2;
constexpr std::size_t kStorageQueue = 128;
constexpr std::size_t kNetThreads = 2;
constexpr unsigned kHttp11 = 11;
constexpr unsigned kHttpOk = 200;
constexpr int kArgon2MemoryKib = 65536;

http::verb ParseHttpMethod(std::string_view m) {
  if (m == "GET") {
    return http::verb::get;
  }
  if (m == "POST") {
    return http::verb::post;
  }
  if (m == "PUT") {
    return http::verb::put;
  }
  if (m == "DELETE") {
    return http::verb::delete_;
  }
  if (m == "HEAD") {
    return http::verb::head;
  }
  if (m == "OPTIONS") {
    return http::verb::options;
  }
  if (m == "PATCH") {
    return http::verb::patch;
  }
  throw std::invalid_argument(std::string("Unsupported HTTP method: ") + std::string(m));
}

} // namespace

NetApiTestSuite::RegisteredUser NetApiTestSuite::RegisterUser(const std::string& username,
                                                              const std::string& device_id) {
  boost::json::object reg;
  reg["username"] = username;
  reg["password_derived_value"] = "pw";
  reg["device_id"] = device_id;
  reg["identity_key_public"] = "ik";
  reg["signed_prekey_public"] = "spk";
  reg["signed_prekey_signature"] = "sig";
  reg["wrapped_sync_key"] = "wsk";
  reg["sync_wrap_salt"] = "salt";
  boost::json::object wrap;
  wrap["algorithm"] = "argon2id";
  wrap["memory_kib"] = kArgon2MemoryKib;
  wrap["iterations"] = 3;
  wrap["parallelism"] = 1;
  reg["sync_wrap_params"] = wrap;
  auto [st, body] = HttpPost("/v1/register", boost::json::serialize(reg));
  if (st != kHttpOk) {
    throw std::runtime_error(std::string("RegisterUser failed: ") + body);
  }
  auto o = boost::json::parse(body).as_object();
  return RegisteredUser{.user_id = std::string(o["user_id"].as_string().c_str()),
                        .access_token = std::string(o["access_token"].as_string().c_str()),
                        .refresh_token = std::string(o["refresh_token"].as_string().c_str())};
}

void NetApiTestSuite::SetUp() {
  ProjectIntegrationTestSuite::SetUp();
  config_ = vox::common::ServerConfig::Default();
  config_.listen_address = "127.0.0.1";
  config_.listen_port = 0;
  config_.network_thread_count = kNetThreads;
  config_.session_token_pepper = "net-test-pepper";
  config_.auth_rate_limit_max = 0;
  config_.admin_token = "test-admin-secret";
  /// Small queue so a second message to the same device overflows to DB-backed offline delivery (sync/ack).
  config_.max_queue_depth_per_device = 1;

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
  sync_state_ = std::make_unique<vox::store::SyncStateRepository>(*db_);

  cpu_pool_ = std::make_unique<vox::common::ThreadPool>(kCpuPoolThreads, kCpuQueue);
  storage_pool_ = std::make_unique<vox::common::ThreadPool>(kStoragePoolThreads, kStorageQueue);
  hasher_ = std::make_unique<vox::auth::PasswordHasher>(
      config_.argon2_time_cost, config_.argon2_memory_cost, config_.argon2_parallelism);
  tokens_ = std::make_unique<vox::auth::TokenManager>(*sessions_,
                                                      config_.access_token_lifetime_seconds,
                                                      config_.refresh_token_lifetime_seconds,
                                                      config_.session_token_pepper);
  auth_ = std::make_unique<vox::auth::AuthService>(*users_, *devices_, *hasher_, *tokens_, *cpu_pool_);

  delivery_ = std::make_unique<vox::relay::DeliveryManager>(*envelopes_, config_.max_queue_depth_per_device);
  relay_ = std::make_unique<vox::relay::RelayService>(*envelopes_, *conversations_, *devices_, *delivery_, config_);
  conv_service_ =
      std::make_unique<vox::relay::ConversationService>(*conversations_, *envelopes_, *devices_, *delivery_, config_);
  attachment_service_ = std::make_unique<vox::attachments::AttachmentService>(*attachments_, *conversations_, config_);
  admin_service_ = std::make_unique<vox::admin::AdminService>(*db_, *users_, *sessions_);

  ws_registry_ = std::make_unique<vox::net::WsPushRegistry>();
  delivery_->SetEnqueueHook([this](const std::string& device_scope_key, const vox::relay::QueuedEnvelope& q) {
    boost::json::object o;
    o["type"] = "envelope";
    o["envelope_id"] = q.envelope_id;
    o["conversation_id"] = q.conversation_id;
    o["sender_user_id"] = q.sender_user_id;
    o["sender_device_id"] = q.sender_device_id;
    o["ciphertext"] = q.ciphertext;
    o["server_timestamp"] = q.server_timestamp;
    o["envelope_type"] = q.envelope_type;
    if (q.ordering_epoch) {
      o["ordering_epoch"] = *q.ordering_epoch;
    }
    ws_registry_->Notify(device_scope_key, boost::json::serialize(o));
  });

  server_ctx_ = std::make_unique<vox::net::ServerContext>(vox::net::ServerContext{
      .config = config_,
      .auth = *auth_,
      .tokens = *tokens_,
      .relay = *relay_,
      .conversations = *conv_service_,
      .delivery = *delivery_,
      .envelopes = *envelopes_,
      .conversations_store = *conversations_,
      .devices = *devices_,
      .users = *users_,
      .sync_state = *sync_state_,
      .attachments = *attachment_service_,
      .admin = *admin_service_,
      .ws_push = ws_registry_.get(),
  });

  ioc_ = std::make_unique<net::io_context>();
  server_ctx_->ioc_for_dispatch = ioc_.get();
  server_ctx_->storage_pool = storage_pool_.get();
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
  // ioc.stop + join before WaitForIdle: otherwise another io thread could Submit(DispatchHttp)
  // after storage looked idle, then ctx is destroyed while that task runs.
  if (ioc_) {
    ioc_->stop();
  }
  net_threads_.clear();
  if (storage_pool_) {
    storage_pool_->WaitForIdle();
  }
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
  storage_pool_.reset();
  cpu_pool_.reset();
  attachments_.reset();
  sync_state_.reset();
  envelopes_.reset();
  conversations_.reset();
  sessions_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
  ioc_.reset();
  ProjectIntegrationTestSuite::TearDown();
}

std::pair<unsigned, std::string> NetApiTestSuite::AsioHttpExchange(const std::string& method,
                                                                   const std::string& path,
                                                                   const std::string& body,
                                                                   const std::string& bearer,
                                                                   const std::string& admin_token,
                                                                   const std::string& content_type) {
  const http::verb verb = ParseHttpMethod(method);
  net::io_context cioc;
  tcp::resolver resolver(cioc);
  tcp::socket socket(cioc);
  auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port_));
  net::connect(socket, endpoints);

  http::request<http::string_body> req{verb, path, kHttp11};
  req.set(http::field::host, "127.0.0.1");
  if (!bearer.empty()) {
    req.set(http::field::authorization, "Bearer " + bearer);
  }
  if (!admin_token.empty()) {
    req.set("X-Admin-Token", admin_token);
  }
  std::string ct = content_type;
  if (ct.empty() && !body.empty() &&
      (verb == http::verb::post || verb == http::verb::put || verb == http::verb::patch)) {
    ct = "application/json";
  }
  if (!ct.empty()) {
    req.set(http::field::content_type, ct);
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

std::pair<unsigned, std::string> NetApiTestSuite::HttpPost(const std::string& path,
                                                           const std::string& body,
                                                           const std::string& bearer,
                                                           const std::string& admin_token) {
  return AsioHttpExchange("POST", path, body, bearer, admin_token, "application/json");
}

std::pair<unsigned, std::string> NetApiTestSuite::HttpGet(const std::string& path,
                                                          const std::string& bearer,
                                                          const std::string& admin_token) {
  return AsioHttpExchange("GET", path, "", bearer, admin_token, {});
}

std::pair<unsigned, std::string> NetApiTestSuite::HttpPut(const std::string& path,
                                                          const std::string& body,
                                                          const std::string& bearer,
                                                          const std::string& admin_token,
                                                          const std::string& content_type) {
  return AsioHttpExchange("PUT", path, body, bearer, admin_token, content_type);
}

std::pair<unsigned, std::string> NetApiTestSuite::HttpDelete(const std::string& path,
                                                             const std::string& body,
                                                             const std::string& bearer,
                                                             const std::string& admin_token) {
  return AsioHttpExchange(
      "DELETE", path, body, bearer, admin_token, body.empty() ? std::string{} : std::string{"application/json"});
}
