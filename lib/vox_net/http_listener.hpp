#ifndef VOX_NET_HTTP_LISTENER_HPP
#define VOX_NET_HTTP_LISTENER_HPP

#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include "lib/vox_net/http_session.hpp"
#include "lib/vox_net/server_context.hpp"
#include "lib/vox_net/ws_registry.hpp"

namespace vox::net {

namespace net = boost::asio;
using tcp = net::ip::tcp;

/// Accepts TCP connections and spawns HttpSession.
class HttpListener : public std::enable_shared_from_this<HttpListener> {
public:
  HttpListener(net::io_context& ioc, const tcp::endpoint& endpoint, ServerContext& ctx, WsPushRegistry& ws_registry);

  void run();

  /// Closes the acceptor so pending async_accept completes with operation_aborted; safe to call from io threads.
  void Shutdown();

  [[nodiscard]] std::uint16_t ListenPort() const;

private:
  void do_accept();

  tcp::acceptor acceptor_;
  ServerContext& ctx_;
  WsPushRegistry& ws_registry_;
};

} // namespace vox::net

#endif // VOX_NET_HTTP_LISTENER_HPP
