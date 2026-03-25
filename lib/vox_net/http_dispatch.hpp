#ifndef VOX_NET_HTTP_DISPATCH_HPP
#define VOX_NET_HTTP_DISPATCH_HPP

#include <optional>
#include <string>

#include <boost/beast/http.hpp>

#include "lib/vox_net/server_context.hpp"
#include "lib/vox_store/session_repository.hpp"

namespace vox::net {

using HttpRequest = boost::beast::http::request<boost::beast::http::string_body>;
using HttpResponse = boost::beast::http::response<boost::beast::http::string_body>;

/// Handles one HTTP request (except WebSocket upgrade, which is handled in the session).
HttpResponse DispatchHttp(ServerContext& ctx,
                          const HttpRequest& req,
                          const std::optional<vox::store::SessionRecord>& session);

std::string PathOnly(std::string_view target);
std::optional<std::string> QueryParam(std::string_view target, std::string_view key);

} // namespace vox::net

#endif // VOX_NET_HTTP_DISPATCH_HPP
