#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Boost.Asio implements io_context.ipp ↔ io_context.hpp include cycle; third-party.
// NOLINTNEXTLINE(misc-header-include-cycle)
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json.hpp>

#include "lib/vox_admin/admin_service.hpp"
#include "lib/vox_attachments/attachment_service.hpp"
#include "lib/vox_auth/auth_service.hpp"
#include "lib/vox_auth/password_hasher.hpp"
#include "lib/vox_auth/token_manager.hpp"
#include "lib/vox_common/config.hpp"
#include "lib/vox_common/logging.hpp"
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

namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

void PrintUsage() {
  std::cout << "vox-server — Vox messenger server\n"
            << "Usage: vox-server [options]\n"
            << "  --help, -h              Show this help\n"
            << "  --listen <addr>         Bind address (default: 127.0.0.1)\n"
            << "  --port <n>              TCP port (default: 8080)\n"
            << "  --db <path>             SQLite database file\n"
            << "  --blobs <path>          Encrypted attachment storage directory\n"
            << "  --threads <n>           io_context worker threads (default: from config)\n"
            << "  --admin-token <secret>  Enable /v1/admin/* with X-Admin-Token header\n";
}

} // namespace

int main(int argc, char** argv) {
  vox::common::InitLogging();
  vox::common::ServerConfig config = vox::common::ServerConfig::Default();

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
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
    } else if (arg == "--admin-token" && i + 1 < argc) {
      config.admin_token = argv[++i];
    }
  }

  try {
    vox::store::Database db(config.db_path.string());
    vox::store::UserRepository users(db);
    vox::store::DeviceRepository devices(db);
    vox::store::SessionRepository sessions(db);
    vox::store::ConversationRepository conversations(db);
    vox::store::EnvelopeRepository envelopes(db);
    vox::store::AttachmentRepository attachments(db);

    vox::common::ThreadPool cpu_pool(config.cpu_pool_size, config.task_queue_capacity);
    vox::auth::PasswordHasher hasher(config.argon2_time_cost, config.argon2_memory_cost, config.argon2_parallelism);
    vox::auth::TokenManager tokens(sessions, config.access_token_lifetime_seconds, config.refresh_token_lifetime_seconds);
    vox::auth::AuthService auth(users, devices, hasher, tokens, cpu_pool);

    vox::relay::DeliveryManager delivery(envelopes, config.max_queue_depth_per_device);
    vox::relay::RelayService relay(envelopes, conversations, devices, delivery);
    vox::relay::ConversationService conversation_service(conversations, config);
    vox::attachments::AttachmentService attachment_service(attachments, conversations, config);
    vox::admin::AdminService admin_service(db, users, sessions);

    vox::net::WsPushRegistry ws_registry;
    delivery.SetEnqueueHook([&ws_registry](const vox::common::DeviceId& device_id, const vox::relay::QueuedEnvelope& q) {
      boost::json::object o;
      o["type"] = "envelope";
      o["envelope_id"] = q.envelope_id;
      o["conversation_id"] = q.conversation_id;
      o["sender_device_id"] = q.sender_device_id;
      o["ciphertext"] = q.ciphertext;
      o["server_timestamp"] = q.server_timestamp;
      ws_registry.Notify(device_id, boost::json::serialize(o));
    });

    vox::net::ServerContext ctx{config,
                                auth,
                                tokens,
                                relay,
                                conversation_service,
                                delivery,
                                envelopes,
                                conversations,
                                devices,
                                attachment_service,
                                admin_service};

    net::io_context ioc;
    tcp::endpoint endpoint(net::ip::make_address(config.listen_address), config.listen_port);
    auto listener = std::make_shared<vox::net::HttpListener>(ioc, endpoint, ctx, ws_registry);
    listener->run();

    net::signal_set signals(ioc);
    signals.add(SIGINT);
    signals.add(SIGTERM);
#ifdef _WIN32
    signals.add(SIGBREAK);
#endif
    signals.async_wait([&ioc, listener](const boost::system::error_code& ec, int signo) {
      if (ec) {
        return;
      }
      std::cout << "Shutting down (signal " << signo << ")\n";
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
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
