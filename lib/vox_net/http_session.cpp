#include "lib/vox_net/http_session.hpp"

#include <chrono>
#include <deque>
#include <variant>

#include <spdlog/spdlog.h>

#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include "lib/vox_net/http_dispatch.hpp"

namespace vox::net {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;

namespace {

constexpr int kHttpStreamTimeoutSeconds = 30;
constexpr int kWsDeferredAuthTimeoutSeconds = 5;

common::Timestamp NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::optional<std::string> ParseBearer(const HttpRequest& req) {
  auto it = req.find(http::field::authorization);
  if (it == req.end()) {
    return std::nullopt;
  }
  std::string v = std::string(it->value());
  const std::string prefix = "Bearer ";
  if (v.size() > prefix.size() && v.starts_with(prefix)) {
    return v.substr(prefix.size());
  }
  return std::nullopt;
}

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
  WebSocketSession(beast::tcp_stream&& stream,
                   ServerContext& ctx,
                   WsPushRegistry& registry,
                   HttpRequest&& req,
                   std::size_t max_ws_outbound_queue) :
      ws_(std::move(stream)), ctx_(ctx), registry_(registry), req_(std::move(req)),
      deferred_auth_timer_(ws_.get_executor()), max_ws_outbound_queue_(max_ws_outbound_queue) {
  }

  void Run() {
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) { res.set(http::field::server, "vox-server"); }));

    ws_.async_accept(req_, [self = shared_from_this()](beast::error_code ec) { self->OnAccept(ec); });
  }

private:
  void FailTcpConnection() {
    beast::error_code ignore;
    ignore = beast::get_lowest_layer(ws_).socket().shutdown(tcp::socket::shutdown_both, ignore);
  }

  void FinishAuth(const std::string& token) {
    auto session = ctx_.tokens.ValidateAccessToken(token, NowSeconds());
    if (!session) {
      FailTcpConnection();
      return;
    }
    device_id_ = session->device_id;

    registry_.Register(device_id_, [self = weak_from_this()](std::string msg) {
      if (auto s = self.lock()) {
        s->PostSend(std::move(msg));
      }
    });

    DoRead();
  }

  void OnAccept(beast::error_code ec) {
    if (ec) {
      return;
    }
    std::optional<std::string> token_opt = ParseBearer(req_);
    if (token_opt) {
      FinishAuth(*token_opt);
      return;
    }

    deferred_auth_active_ = true;
    deferred_auth_timer_.expires_after(std::chrono::seconds(kWsDeferredAuthTimeoutSeconds));
    deferred_auth_timer_.async_wait([self = shared_from_this()](beast::error_code timer_ec) {
      if (timer_ec == net::error::operation_aborted) {
        return;
      }
      if (self->deferred_auth_active_) {
        self->deferred_auth_active_ = false;
        beast::error_code ignore;
        ignore = beast::get_lowest_layer(self->ws_).socket().shutdown(tcp::socket::shutdown_both, ignore);
      }
    });

    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code read_ec, std::size_t) {
      if (!self->deferred_auth_active_) {
        return;
      }
      self->deferred_auth_active_ = false;
      self->deferred_auth_timer_.cancel();
      if (read_ec) {
        self->FailTcpConnection();
        return;
      }
      if (!self->ws_.got_text()) {
        self->FailTcpConnection();
        return;
      }
      std::string data = beast::buffers_to_string(self->buffer_.data());
      self->buffer_.consume(self->buffer_.size());
      boost::json::value jv;
      try {
        jv = boost::json::parse(data);
      } catch (...) {
        self->FailTcpConnection();
        return;
      }
      if (!jv.is_object()) {
        self->FailTcpConnection();
        return;
      }
      const auto& o = jv.as_object();
      if (!o.contains("type") || !o.at("type").is_string()) {
        self->FailTcpConnection();
        return;
      }
      if (std::string(o.at("type").as_string().c_str()) != "auth") {
        self->FailTcpConnection();
        return;
      }
      if (!o.contains("access_token") || !o.at("access_token").is_string()) {
        self->FailTcpConnection();
        return;
      }
      self->FinishAuth(std::string(o.at("access_token").as_string().c_str()));
    });
  }

  void PostSend(std::string msg) {
    net::post(ws_.get_executor(), [self = shared_from_this(), msg = std::move(msg)]() mutable {
      if (self->max_ws_outbound_queue_ > 0 && self->outbound_.size() >= self->max_ws_outbound_queue_) {
        spdlog::warn("WebSocket outbound queue full (device {}), closing session", self->device_id_);
        self->Shutdown();
        return;
      }
      self->outbound_.push_back(std::move(msg));
      self->PumpOutbound();
    });
  }

  void PumpOutbound() {
    if (write_in_progress_ || outbound_.empty()) {
      return;
    }
    write_in_progress_ = true;
    std::string chunk = std::move(outbound_.front());
    outbound_.pop_front();
    ws_.async_write(net::buffer(chunk), [self = shared_from_this()](beast::error_code ec, std::size_t) {
      self->write_in_progress_ = false;
      if (ec) {
        self->Shutdown();
        return;
      }
      self->PumpOutbound();
    });
  }

  void DoRead() {
    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
      if (ec) {
        self->Shutdown();
        return;
      }
      self->buffer_.consume(self->buffer_.size());
      self->DoRead();
    });
  }

  void Shutdown() {
    if (!device_id_.empty()) {
      registry_.Unregister(device_id_);
      device_id_.clear();
    }
    beast::error_code ec;
    ws_.close(websocket::close_code::normal, ec);
  }

  websocket::stream<beast::tcp_stream> ws_;
  ServerContext& ctx_;
  WsPushRegistry& registry_;
  HttpRequest req_;
  net::steady_timer deferred_auth_timer_;
  bool deferred_auth_active_ = false;
  beast::flat_buffer buffer_;
  std::string device_id_;
  std::deque<std::string> outbound_;
  bool write_in_progress_ = false;
  std::size_t max_ws_outbound_queue_;
};

bool IsWsPath(std::string_view path_only) {
  return path_only == "/v1/ws";
}

} // namespace

HttpSession::HttpSession(tcp::socket&& socket, ServerContext& ctx, WsPushRegistry& ws_registry) :
    stream_(std::move(socket)), ctx_(ctx), ws_registry_(ws_registry) {
}

void HttpSession::Run() {
  stream_.expires_after(std::chrono::seconds(kHttpStreamTimeoutSeconds));
  http::async_read(
      stream_, buffer_, req_, [self = shared_from_this()](beast::error_code ec, std::size_t) { self->OnRead(ec, 0); });
}

void HttpSession::OnRead(beast::error_code ec, std::size_t) {
  if (ec == http::error::end_of_stream) {
    beast::error_code sec;
    sec = stream_.socket().shutdown(tcp::socket::shutdown_send, sec);
    return;
  }
  if (ec) {
    return;
  }

  if (websocket::is_upgrade(req_)) {
    std::string p = PathOnly(req_.target());
    if (!IsWsPath(p)) {
      HttpResponseString res{http::status::not_found, req_.version()};
      res.set(http::field::content_type, "text/plain");
      res.body() = "Not Found";
      res.prepare_payload();
      res_ = HttpResponse{std::move(res)};
      std::visit(
          [this](auto& r) {
            http::async_write(stream_, r, [self = shared_from_this()](beast::error_code ec, std::size_t n) {
              self->OnWrite(ec, n, true, false);
            });
          },
          *res_);
      return;
    }
    auto ws = std::make_shared<WebSocketSession>(
        std::move(stream_), ctx_, ws_registry_, std::move(req_), ctx_.config.max_ws_outbound_queue);
    ws->Run();
    return;
  }

  std::optional<vox::store::SessionRecord> sess;
  if (auto bearer = ParseBearer(req_)) {
    auto vr = ctx_.tokens.ValidateAccessToken(*bearer, NowSeconds());
    if (vr) {
      sess = *vr;
    }
  }

  std::string client_ip;
  {
    boost::system::error_code ep_ec;
    const auto ep = stream_.socket().remote_endpoint(ep_ec);
    if (!ep_ec) {
      client_ip = ep.address().to_string();
    }
  }

  if (ctx_.storage_pool != nullptr && ctx_.ioc_for_dispatch != nullptr) {
    HttpRequest req_copy = std::move(req_);
    auto self = shared_from_this();
    auto* ioc = ctx_.ioc_for_dispatch;
    ctx_.storage_pool->Submit(
        [self, req_copy = std::move(req_copy), sess, client_ip = std::move(client_ip), ioc]() mutable {
          HttpResponse res = DispatchHttp(self->ctx_, req_copy, sess, client_ip);
          bool keep_alive = std::visit([](const auto& r) { return r.keep_alive(); }, res);
          std::visit([keep_alive](auto& r) { r.keep_alive(keep_alive); }, res);
          net::post(ioc->get_executor(), [self, res = std::move(res), keep_alive]() mutable {
            self->res_ = std::move(res);
            std::visit(
                [self, keep_alive](auto& r) {
                  http::async_write(self->stream_, r, [self, keep_alive](beast::error_code ec, std::size_t n) {
                    self->OnWrite(ec, n, false, keep_alive);
                  });
                },
                *self->res_);
          });
        });
    return;
  }

  HttpResponse res = DispatchHttp(ctx_, req_, sess, client_ip);
  bool keep_alive = std::visit([](const auto& r) { return r.keep_alive(); }, res);
  std::visit([keep_alive](auto& r) { r.keep_alive(keep_alive); }, res);
  res_ = std::move(res);

  std::visit(
      [this, keep_alive](auto& r) {
        http::async_write(stream_, r, [self = shared_from_this(), keep_alive](beast::error_code ec, std::size_t n) {
          self->OnWrite(ec, n, false, keep_alive);
        });
      },
      *res_);
}

void HttpSession::OnWrite(beast::error_code ec, std::size_t, bool close, bool keep_alive) {
  if (ec) {
    return;
  }
  if (close) {
    beast::error_code sec;
    sec = stream_.socket().shutdown(tcp::socket::shutdown_both, sec);
    return;
  }
  if (!keep_alive) {
    beast::error_code sec;
    sec = stream_.socket().shutdown(tcp::socket::shutdown_both, sec);
    return;
  }
  req_ = {};
  res_.reset();
  buffer_.consume(buffer_.size());
  stream_.expires_after(std::chrono::seconds(kHttpStreamTimeoutSeconds));
  http::async_read(stream_, buffer_, req_, [self = shared_from_this()](beast::error_code ec2, std::size_t) {
    self->OnRead(ec2, 0);
  });
}

} // namespace vox::net
