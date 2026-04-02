#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Boost.Asio implements io_context.ipp ↔ io_context.hpp include cycle; third-party.
// NOLINTNEXTLINE(misc-header-include-cycle)
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json.hpp>
#include <spdlog/spdlog.h>

#include "lib/vox_admin/admin_service.hpp"
#include "lib/vox_attachments/attachment_service.hpp"
#include "lib/vox_auth/auth_service.hpp"
#include "lib/vox_auth/password_hasher.hpp"
#include "lib/vox_auth/token_manager.hpp"
#include "lib/vox_common/config.hpp"
#include "lib/vox_common/logging.hpp"
#include "lib/vox_common/thread_pool.hpp"
#include "lib/vox_net/http_listener.hpp"
#include "lib/vox_net/rate_limiter.hpp"
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
#include "lib/vox_store/sync_state_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

void PrintUsage() {
  std::cout << "vox-server — Vox messenger server\n"
            << "Usage: vox-server [options]\n"
            << "  --help, -h              Show this help\n"
            << "  --config <path>         Load key=value settings (default: vox.conf if present; or VOX_CONFIG_FILE)\n"
            << "  --listen <addr>         Bind address (default: 127.0.0.1)\n"
            << "  --port <n>              TCP port (default: 8080)\n"
            << "  --db <path>             SQLite database file\n"
            << "  --blobs <path>          Encrypted attachment storage directory\n"
            << "  --threads <n>           io_context worker threads (default: from config)\n"
            << "  --session-pepper <sec>  Secret for HMAC of session tokens (or env VOX_SESSION_PEPPER)\n"
            << "  --admin-token <secret>  Enable /v1/admin/* with X-Admin-Token header\n";
}

} // namespace

int main(int argc, char** argv) {
  vox::common::InitLogging();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    }
  }

  vox::common::ServerConfig config = vox::common::ServerConfig::Default();

  std::optional<std::filesystem::path> explicit_config_path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      explicit_config_path = argv[++i];
    }
  }

  std::optional<std::filesystem::path> config_to_load;
  if (explicit_config_path.has_value()) {
    config_to_load = *explicit_config_path;
  } else if (const char* env_path = std::getenv("VOX_CONFIG_FILE")) {
    if (env_path[0] != '\0') {
      config_to_load = env_path;
    }
  }
  if (!config_to_load.has_value()) {
    const std::filesystem::path implicit = std::filesystem::current_path() / "vox.conf";
    if (std::filesystem::exists(implicit)) {
      config_to_load = implicit;
    }
  }

  if (config_to_load.has_value()) {
    std::string err;
    if (!vox::common::LoadServerConfigFile(*config_to_load, config, &err)) {
      std::cerr << "Fatal: config file " << config_to_load->string() << ": " << err << "\n";
      return 1;
    }
  }

  if (const char* p = std::getenv("VOX_SESSION_PEPPER")) {
    config.session_token_pepper = p;
  }
  if (const char* p = std::getenv("VOX_ADMIN_TOKEN")) {
    config.admin_token = p;
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      ++i;
      continue;
    }
    if (arg == "--listen" && i + 1 < argc) {
      config.listen_address = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      config.listen_port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--db" && i + 1 < argc) {
      config.db_path = argv[++i];
    } else if (arg == "--blobs" && i + 1 < argc) {
      config.blob_storage_path = argv[++i];
    } else if (arg == "--threads" && i + 1 < argc) {
      config.network_thread_count = static_cast<std::size_t>(std::stoull(argv[++i]));
    } else if (arg == "--session-pepper" && i + 1 < argc) {
      config.session_token_pepper = argv[++i];
    } else if (arg == "--admin-token" && i + 1 < argc) {
      config.admin_token = argv[++i];
    }
  }

  if (config.session_token_pepper.empty()) {
    std::cerr << "Fatal: set VOX_SESSION_PEPPER or --session-pepper to a non-empty secret.\n"
              << "Existing sessions are invalidated when the pepper changes.\n";
    return 1;
  }

  try {
    vox::store::Database db(config.db_path.string());
    vox::store::UserRepository users(db);
    vox::store::DeviceRepository devices(db);
    vox::store::SessionRepository sessions(db);
    vox::store::ConversationRepository conversations(db);
    vox::store::EnvelopeRepository envelopes(db);
    vox::store::AttachmentRepository attachments(db);
    vox::store::SyncStateRepository sync_state(db);

    vox::common::ThreadPool cpu_pool(config.cpu_pool_size, config.task_queue_capacity);
    vox::common::ThreadPool storage_pool(config.storage_pool_size, config.task_queue_capacity);
    vox::auth::PasswordHasher hasher(config.argon2_time_cost, config.argon2_memory_cost, config.argon2_parallelism);
    vox::auth::TokenManager tokens(sessions,
                                   config.access_token_lifetime_seconds,
                                   config.refresh_token_lifetime_seconds,
                                   config.session_token_pepper);
    vox::auth::AuthService auth(users, devices, hasher, tokens, cpu_pool);

    vox::relay::DeliveryManager delivery(envelopes, config.max_queue_depth_per_device);
    vox::relay::RelayService relay(envelopes, conversations, devices, delivery, config);
    vox::relay::ConversationService conversation_service(conversations, envelopes, devices, delivery, config);
    vox::attachments::AttachmentService attachment_service(attachments, conversations, config);
    vox::admin::AdminService admin_service(db, users, sessions);

    std::unique_ptr<vox::net::AuthRateLimiter> auth_rate_limiter;
    if (config.auth_rate_limit_max > 0) {
      auth_rate_limiter = std::make_unique<vox::net::AuthRateLimiter>(
          config.auth_rate_limit_max, std::chrono::seconds(config.auth_rate_limit_window_seconds));
    }
    std::unique_ptr<vox::net::AccountRateLimiter> account_rate_limiter;
    if (config.account_rate_limit_max > 0) {
      account_rate_limiter = std::make_unique<vox::net::AccountRateLimiter>(
          config.account_rate_limit_max, std::chrono::seconds(config.account_rate_limit_window_seconds));
    }

    vox::net::WsPushRegistry ws_registry;

    vox::net::ServerContext ctx{.config = config,
                                .auth = auth,
                                .tokens = tokens,
                                .relay = relay,
                                .conversations = conversation_service,
                                .delivery = delivery,
                                .envelopes = envelopes,
                                .conversations_store = conversations,
                                .devices = devices,
                                .users = users,
                                .sync_state = sync_state,
                                .attachments = attachment_service,
                                .admin = admin_service,
                                .auth_rate_limiter = auth_rate_limiter.get(),
                                .account_rate_limiter = account_rate_limiter.get(),
                                .ws_push = &ws_registry,
                                .ioc_for_dispatch = nullptr,
                                .storage_pool = nullptr};

    delivery.SetEnqueueHook([&ws_registry](const std::string& device_scope_key, const vox::relay::QueuedEnvelope& q) {
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
      ws_registry.Notify(device_scope_key, boost::json::serialize(o));
    });

    net::io_context ioc;
    ctx.ioc_for_dispatch = &ioc;
    ctx.storage_pool = &storage_pool;
    tcp::endpoint endpoint(net::ip::make_address(config.listen_address), config.listen_port);
    auto listener = std::make_shared<vox::net::HttpListener>(ioc, endpoint, ctx, ws_registry);
    listener->run();

    std::optional<std::jthread> maintenance_thread;
    if (config.maintenance_purge_interval_seconds > 0) {
      maintenance_thread.emplace([&envelopes,
                                  &attachment_service,
                                  &storage_pool,
                                  interval = config.maintenance_purge_interval_seconds](const std::stop_token& st) {
        while (!st.stop_requested()) {
          for (std::int64_t i = 0; i < interval && !st.stop_requested(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
          }
          if (st.stop_requested()) {
            break;
          }
          storage_pool.Submit([&envelopes, &attachment_service]() {
            const auto now =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            const int env_n = envelopes.DeleteExpired(now);
            const int att_n = attachment_service.DeleteExpired();
            spdlog::info("Maintenance purge: envelope rows deleted {}, attachment rows {}", env_n, att_n);
          });
        }
      });
    }

    net::signal_set signals(ioc);
    signals.add(SIGINT);
    signals.add(SIGTERM);
#ifdef _WIN32
    signals.add(SIGBREAK);
#endif
    signals.async_wait([&ioc, listener, &maintenance_thread](const boost::system::error_code& ec, int signo) {
      if (ec) {
        return;
      }
      std::cout << "Shutting down (signal " << signo << ")\n";
      if (maintenance_thread) {
        maintenance_thread->request_stop();
      }
      listener->Shutdown();
      ioc.stop();
    });

    std::vector<std::jthread> workers;
    workers.reserve(config.network_thread_count);
    for (std::size_t t = 0; t < config.network_thread_count; ++t) {
      workers.emplace_back([&ioc]() { ioc.run(); });
    }

    std::cout << "Listening on " << config.listen_address << ":" << config.listen_port << "\n";
    for (auto& w : workers) {
      w.join();
    }
    // `ctx` is destroyed before `storage_pool` (reverse declaration order). Ensure no
    // `DispatchHttp` task still runs on `storage_pool` after `ioc` stopped.
    storage_pool.WaitForIdle();
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
