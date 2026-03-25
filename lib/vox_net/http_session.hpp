#ifndef VOX_NET_HTTP_SESSION_HPP
#define VOX_NET_HTTP_SESSION_HPP

#include <memory>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>

#include "lib/vox_net/http_dispatch.hpp"
#include "lib/vox_net/server_context.hpp"
#include "lib/vox_net/ws_registry.hpp"

namespace vox::net {

namespace beast = boost::beast;
namespace net = boost::asio;
using tcp = net::ip::tcp;

/// One TCP connection: HTTP/1.1 or WebSocket upgrade for `/v1/ws`.
class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
  HttpSession(tcp::socket&& socket, ServerContext& ctx, WsPushRegistry& ws_registry);

  void Run();

private:
  void OnRead(beast::error_code ec, std::size_t bytes_transferred);
  void OnWrite(beast::error_code ec, std::size_t bytes_transferred, bool close, bool keep_alive);

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  HttpRequest req_;
  std::optional<HttpResponse> res_;
  ServerContext& ctx_;
  WsPushRegistry& ws_registry_;
};

} // namespace vox::net

#endif // VOX_NET_HTTP_SESSION_HPP
