#ifndef VOX_NET_HTTP_DISPATCH_HPP
#define VOX_NET_HTTP_DISPATCH_HPP

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <boost/beast/http.hpp>

#include "lib/vox_net/server_context.hpp"
#include "lib/vox_store/session_repository.hpp"

namespace vox::net {

namespace beast = boost::beast;
namespace http = beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponseString = http::response<http::string_body>;
using HttpResponseFile = http::response<http::file_body>;
using HttpResponse = std::variant<HttpResponseString, HttpResponseFile>;

/// Handles one HTTP request (except WebSocket upgrade, which is handled in the session).
HttpResponse DispatchHttp(ServerContext& ctx,
                          const HttpRequest& req,
                          const std::optional<vox::store::SessionRecord>& session,
                          std::string_view client_ip = {});

std::string PathOnly(std::string_view target);
std::optional<std::string> QueryParam(std::string_view target, std::string_view key);

} // namespace vox::net

#endif // VOX_NET_HTTP_DISPATCH_HPP
