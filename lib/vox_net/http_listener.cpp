#include "lib/vox_net/http_listener.hpp"

#include <stdexcept>

#include <boost/beast/core.hpp>

namespace beast = boost::beast;
namespace vox::net {

HttpListener::HttpListener(net::io_context& ioc,
                           const tcp::endpoint& endpoint,
                           ServerContext& ctx,
                           WsPushRegistry& ws_registry) : acceptor_(ioc), ctx_(ctx), ws_registry_(ws_registry) {
  beast::error_code ec;
  ec = acceptor_.open(endpoint.protocol(), ec);
  if (ec) {
    throw std::runtime_error("acceptor open: " + ec.message());
  }
  ec = acceptor_.set_option(net::socket_base::reuse_address(true), ec);
  if (ec) {
    throw std::runtime_error("acceptor reuse_address: " + ec.message());
  }
  ec = acceptor_.bind(endpoint, ec);
  if (ec) {
    throw std::runtime_error("acceptor bind: " + ec.message());
  }
  ec = acceptor_.listen(net::socket_base::max_listen_connections, ec);
  if (ec) {
    throw std::runtime_error("acceptor listen: " + ec.message());
  }
}

void HttpListener::run() {
  do_accept();
}

void HttpListener::Shutdown() {
  beast::error_code ec;
  ec = acceptor_.close(ec);
}

std::uint16_t HttpListener::ListenPort() const {
  return acceptor_.local_endpoint().port();
}

void HttpListener::do_accept() {
  acceptor_.async_accept([self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
    if (ec == net::error::operation_aborted) {
      return;
    }
    if (!ec) {
      std::make_shared<HttpSession>(std::move(socket), self->ctx_, self->ws_registry_)->Run();
    }
    self->do_accept();
  });
}

} // namespace vox::net
