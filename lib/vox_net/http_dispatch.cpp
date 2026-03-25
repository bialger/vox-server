#include "lib/vox_net/http_dispatch.hpp"

#include <cstring>
#include <fstream>

#include <boost/json.hpp>

#include "lib/vox_net/error_http.hpp"
#include "lib/vox_store/device_repository.hpp"

namespace vox::net {

namespace beast = boost::beast;
namespace http = beast::http;

namespace {

constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpBadRequest = 400;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpForbidden = 403;
constexpr std::size_t kDefaultPaginationLimit = 100;

boost::json::object ErrObj(const common::Error& e) {
  boost::json::object o;
  o["code"] = static_cast<std::int64_t>(e.code);
  o["message"] = e.message;
  boost::json::object wrap;
  wrap["error"] = o;
  return wrap;
}

HttpResponse JsonRes(unsigned version, unsigned status, const boost::json::value& v) {
  HttpResponse res{static_cast<http::status>(status), version};
  res.set(http::field::content_type, "application/json");
  res.body() = boost::json::serialize(v);
  res.prepare_payload();
  return res;
}

HttpResponse ErrRes(unsigned version, unsigned st, const common::Error& e) {
  return JsonRes(version, st, ErrObj(e));
}

std::optional<std::string> JsonString(const boost::json::object& o, std::string_view key) {
  if (!o.contains(key)) {
    return std::nullopt;
  }
  auto& v = o.at(key);
  if (!v.is_string()) {
    return std::nullopt;
  }
  return std::string(v.as_string().c_str());
}

std::optional<std::int64_t> JsonInt(const boost::json::object& o, std::string_view key) {
  if (!o.contains(key)) {
    return std::nullopt;
  }
  auto& v = o.at(key);
  if (v.is_int64()) {
    return v.as_int64();
  }
  if (v.is_uint64()) {
    return static_cast<std::int64_t>(v.as_uint64());
  }
  return std::nullopt;
}

bool ParseJsonBody(const std::string& body, boost::json::value& out, common::Error& err) {
  try {
    out = boost::json::parse(body);
    return true;
  } catch (const std::exception& e) {
    err = common::Error{.code = common::ErrorCode::kInvalidArgument, .message = e.what()};
    return false;
  }
}

common::MemberRole ParseMemberRole(std::string_view s) {
  if (s == "owner") {
    return common::MemberRole::kOwner;
  }
  if (s == "admin") {
    return common::MemberRole::kAdmin;
  }
  return common::MemberRole::kMember;
}

} // namespace

std::string PathOnly(std::string_view target) {
  auto q = target.find('?');
  if (q == std::string_view::npos) {
    return std::string(target);
  }
  return std::string(target.substr(0, q));
}

std::optional<std::string> QueryParam(std::string_view target, std::string_view key) {
  auto q = target.find('?');
  if (q == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view query = target.substr(q + 1);
  while (!query.empty()) {
    auto amp = query.find('&');
    std::string_view pair = query.substr(0, amp);
    auto eq = pair.find('=');
    if (eq != std::string_view::npos) {
      std::string_view k = pair.substr(0, eq);
      std::string_view v = pair.substr(eq + 1);
      if (k == key) {
        return std::string(v);
      }
    }
    if (amp == std::string_view::npos) {
      break;
    }
    query.remove_prefix(amp + 1);
  }
  return std::nullopt;
}

HttpResponse DispatchHttp(ServerContext& ctx,
                          const HttpRequest& req,
                          const std::optional<vox::store::SessionRecord>& session) {
  const unsigned ver = req.version();
  const std::string method = std::string(req.method_string());
  const std::string path = PathOnly(req.target());

  auto parse_body = [&](boost::json::object& obj) -> std::optional<HttpResponse> {
    boost::json::value jv;
    common::Error pe;
    if (!ParseJsonBody(req.body(), jv, pe)) {
      return ErrRes(ver, HttpStatusForError(pe.code), pe);
    }
    if (!jv.is_object()) {
      common::Error e{.code = common::ErrorCode::kInvalidArgument, .message = "JSON object expected"};
      return ErrRes(ver, kHttpBadRequest, e);
    }
    obj = jv.as_object();
    return std::nullopt;
  };

  // --- Public auth routes ---
  if (method == "POST" && path == "/v1/register") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    vox::auth::RegisterRequest rr;
    rr.username = JsonString(o, "username").value_or("");
    rr.password_derived_value = JsonString(o, "password_derived_value").value_or("");
    rr.device_id = JsonString(o, "device_id").value_or("");
    rr.identity_key_public = JsonString(o, "identity_key_public").value_or("");
    rr.signed_prekey_public = JsonString(o, "signed_prekey_public").value_or("");
    rr.signed_prekey_signature = JsonString(o, "signed_prekey_signature").value_or("");
    auto result = ctx.auth.Register(rr);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["user_id"] = result->user_id;
    out["access_token"] = result->tokens.access_token;
    out["refresh_token"] = result->tokens.refresh_token;
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "POST" && path == "/v1/login") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    vox::auth::LoginRequest lr;
    lr.username = JsonString(o, "username").value_or("");
    lr.password_derived_value = JsonString(o, "password_derived_value").value_or("");
    lr.device_id = JsonString(o, "device_id").value_or("");
    auto result = ctx.auth.Login(lr);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["user_id"] = result->user_id;
    out["access_token"] = result->tokens.access_token;
    out["refresh_token"] = result->tokens.refresh_token;
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "POST" && path == "/v1/refresh") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    vox::auth::RefreshRequest rr;
    rr.refresh_token = JsonString(o, "refresh_token").value_or("");
    rr.device_id = JsonString(o, "device_id").value_or("");
    auto result = ctx.auth.Refresh(rr);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["access_token"] = result->access_token;
    out["refresh_token"] = result->refresh_token;
    return JsonRes(ver, kHttpOk, out);
  }

  // --- Admin (X-Admin-Token only, no user session) ---
  if (req.find("X-Admin-Token") != req.end() && !ctx.config.admin_token.empty()) {
    std::string token = std::string(req.at("X-Admin-Token"));
    if (token == ctx.config.admin_token) {
      if (method == "GET" && path == "/v1/admin/stats") {
        auto stats = ctx.admin.GetServerStats();
        boost::json::object out;
        out["user_count"] = static_cast<std::int64_t>(stats.user_count);
        out["device_count"] = static_cast<std::int64_t>(stats.device_count);
        out["active_session_count"] = static_cast<std::int64_t>(stats.active_session_count);
        out["conversation_count"] = static_cast<std::int64_t>(stats.conversation_count);
        out["pending_envelope_count"] = static_cast<std::int64_t>(stats.pending_envelope_count);
        out["total_storage_bytes"] = stats.total_storage_bytes;
        return JsonRes(ver, kHttpOk, out);
      }
      if (method == "DELETE" && path.starts_with("/v1/admin/users/")) {
        std::string uid = std::string(path.substr(std::strlen("/v1/admin/users/")));
        auto result = ctx.admin.DeleteUser(uid);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        return JsonRes(ver, kHttpOk, boost::json::object{});
      }
    }
  }

  // --- Authenticated routes ---
  if (!session.has_value()) {
    common::Error e{.code = common::ErrorCode::kUnauthorized, .message = "Authentication required"};
    return ErrRes(ver, kHttpUnauthorized, e);
  }
  const auto& sess = session.value();

  if (method == "POST" && path == "/v1/logout") {
    auto result = ctx.auth.Logout(sess.session_id);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "POST" && path == "/v1/messages/send") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::string device_id = JsonString(o, "device_id").value_or("");
    if (device_id != sess.device_id) {
      common::Error e{.code = common::ErrorCode::kForbidden, .message = "device_id must match session device"};
      return ErrRes(ver, kHttpForbidden, e);
    }
    vox::relay::SendMessageRequest sr;
    sr.sender_device_id = device_id;
    sr.conversation_id = JsonString(o, "conversation_id").value_or("");
    sr.ciphertext = JsonString(o, "ciphertext").value_or("");
    sr.envelope_id = JsonString(o, "envelope_id").value_or("");
    if (auto et = JsonInt(o, "envelope_type")) {
      sr.envelope_type = static_cast<int>(*et);
    }
    auto result = ctx.relay.SendEnvelope(sr);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["envelope_id"] = result->envelope_id;
    out["server_timestamp"] = result->server_timestamp;
    out["delivered_to_count"] = static_cast<std::int64_t>(result->delivered_to_count);
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "POST" && path == "/v1/messages/ack") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::string device_id = JsonString(o, "device_id").value_or("");
    if (device_id != sess.device_id) {
      common::Error e{.code = common::ErrorCode::kForbidden, .message = "device_id must match session device"};
      return ErrRes(ver, kHttpForbidden, e);
    }
    std::string envelope_id = JsonString(o, "envelope_id").value_or("");
    auto result = ctx.relay.AcknowledgeEnvelope(device_id, envelope_id);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "GET" && path == "/v1/sync/pending") {
    std::size_t limit = kDefaultPaginationLimit;
    if (auto l = QueryParam(req.target(), "limit")) {
      limit = static_cast<std::size_t>(std::stoull(*l));
    }
    auto list = ctx.relay.SyncOffline(sess.device_id, limit);
    boost::json::array arr;
    for (const auto& e : list) {
      boost::json::object eo;
      eo["envelope_id"] = e.envelope_id;
      eo["conversation_id"] = e.conversation_id;
      eo["sender_device_id"] = e.sender_device_id;
      eo["ciphertext"] = e.ciphertext;
      eo["server_timestamp"] = e.server_timestamp;
      eo["envelope_type"] = e.envelope_type;
      arr.push_back(eo);
    }
    boost::json::object out;
    out["envelopes"] = arr;
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "GET" && path.starts_with("/v1/conversations/") && path.ends_with("/envelopes")) {
    std::string conv_id;
    {
      constexpr std::string_view kPrefix = "/v1/conversations/";
      auto rest = std::string_view(path).substr(kPrefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      conv_id = std::string(rest.substr(0, slash));
    }
    if (!ctx.conversations_store.IsUserInConversation(conv_id, sess.user_id)) {
      common::Error e{.code = common::ErrorCode::kForbidden, .message = "Not a member of this conversation"};
      return ErrRes(ver, kHttpForbidden, e);
    }
    common::Timestamp since = 0;
    if (auto s = QueryParam(req.target(), "since")) {
      since = static_cast<common::Timestamp>(std::stoll(*s));
    }
    std::size_t limit = kDefaultPaginationLimit;
    if (auto l = QueryParam(req.target(), "limit")) {
      limit = static_cast<std::size_t>(std::stoull(*l));
    }
    auto list = ctx.envelopes.ListForConversation(conv_id, since, limit);
    boost::json::array arr;
    for (const auto& e : list) {
      boost::json::object eo;
      eo["envelope_id"] = e.envelope_id;
      eo["conversation_id"] = e.conversation_id;
      eo["sender_device_id"] = e.sender_device_id;
      eo["ciphertext"] = e.ciphertext;
      eo["server_timestamp"] = e.server_timestamp;
      eo["envelope_type"] = e.envelope_type;
      arr.push_back(eo);
    }
    boost::json::object out;
    out["envelopes"] = arr;
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "GET" && path == "/v1/conversations") {
    auto list = ctx.conversations.ListForUser(sess.user_id);
    boost::json::array arr;
    for (const auto& c : list) {
      boost::json::object co;
      co["conversation_id"] = c.conversation_id;
      co["type"] = static_cast<int>(c.type);
      co["created_by"] = c.created_by;
      co["created_at"] = c.created_at;
      arr.push_back(co);
    }
    boost::json::object out;
    out["conversations"] = arr;
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "POST" && path == "/v1/conversations") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::string type = JsonString(o, "type").value_or("");
    if (type == "dm") {
      std::string peer = JsonString(o, "peer_user_id").value_or("");
      auto r = ctx.conversations.CreateDm(sess.user_id, peer, sess.user_id);
      if (!r) {
        return ErrRes(ver, HttpStatusForError(r.error().code), r.error());
      }
      boost::json::object out;
      out["conversation_id"] = *r;
      return JsonRes(ver, kHttpOk, out);
    }
    if (type == "group") {
      std::vector<std::string> members;
      if (o.contains("members") && o["members"].is_array()) {
        for (const auto& v : o["members"].as_array()) {
          if (v.is_string()) {
            members.emplace_back(v.as_string().c_str());
          }
        }
      }
      auto r = ctx.conversations.CreateGroup(sess.user_id, std::move(members));
      if (!r) {
        return ErrRes(ver, HttpStatusForError(r.error().code), r.error());
      }
      boost::json::object out;
      out["conversation_id"] = *r;
      return JsonRes(ver, kHttpOk, out);
    }
    if (type == "channel") {
      std::vector<std::string> admins;
      std::vector<std::string> subscribers;
      if (o.contains("admins") && o["admins"].is_array()) {
        for (const auto& v : o["admins"].as_array()) {
          if (v.is_string()) {
            admins.emplace_back(v.as_string().c_str());
          }
        }
      }
      if (o.contains("subscribers") && o["subscribers"].is_array()) {
        for (const auto& v : o["subscribers"].as_array()) {
          if (v.is_string()) {
            subscribers.emplace_back(v.as_string().c_str());
          }
        }
      }
      auto r = ctx.conversations.CreateChannel(sess.user_id, admins, subscribers);
      if (!r) {
        return ErrRes(ver, HttpStatusForError(r.error().code), r.error());
      }
      boost::json::object out;
      out["conversation_id"] = *r;
      return JsonRes(ver, kHttpOk, out);
    }
    common::Error e{.code = common::ErrorCode::kInvalidArgument, .message = "type must be dm, group, or channel"};
    return ErrRes(ver, kHttpBadRequest, e);
  }

  if (method == "POST" && path.starts_with("/v1/conversations/") && path.find("/members") != std::string::npos &&
      path.rfind("/members") == path.size() - std::strlen("/members")) {
    std::string conv_id;
    {
      constexpr std::string_view kPfx = "/v1/conversations/";
      auto rest = std::string_view(path).substr(kPfx.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      conv_id = std::string(rest.substr(0, slash));
    }
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::string new_user = JsonString(o, "user_id").value_or("");
    std::string role_s = JsonString(o, "role").value_or("member");
    auto role = ParseMemberRole(role_s);
    auto result = ctx.conversations.AddMember(conv_id, sess.user_id, new_user, role);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "DELETE" && path.starts_with("/v1/conversations/") && path.find("/members/") != std::string::npos) {
    std::string conv_id;
    std::string target_user;
    {
      constexpr std::string_view kPfx = "/v1/conversations/";
      auto rest = std::string_view(path).substr(kPfx.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      conv_id = std::string(rest.substr(0, slash));
      rest = rest.substr(slash + 1);
      if (!rest.starts_with("members/")) {
        return HttpResponse{http::status::not_found, ver};
      }
      target_user = std::string(rest.substr(std::strlen("members/")));
    }
    auto result = ctx.conversations.RemoveMember(conv_id, sess.user_id, target_user);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "POST" && path.starts_with("/v1/conversations/") && path.ends_with("/subscribe")) {
    std::string conv_id;
    constexpr std::string_view kPfx = "/v1/conversations/";
    auto rest = std::string_view(path).substr(kPfx.size());
    auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
      return HttpResponse{http::status::not_found, ver};
    }
    conv_id = std::string(rest.substr(0, slash));
    auto result = ctx.conversations.SubscribeChannel(conv_id, sess.user_id);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "POST" && path.starts_with("/v1/conversations/") && path.ends_with("/unsubscribe")) {
    std::string conv_id;
    constexpr std::string_view kPfx = "/v1/conversations/";
    auto rest = std::string_view(path).substr(kPfx.size());
    auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
      return HttpResponse{http::status::not_found, ver};
    }
    conv_id = std::string(rest.substr(0, slash));
    auto result = ctx.conversations.UnsubscribeChannel(conv_id, sess.user_id);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "POST" && path.starts_with("/v1/devices/") && path.ends_with("/prekeys")) {
    std::string device_id;
    {
      constexpr std::string_view kPfx = "/v1/devices/";
      auto rest = std::string_view(path).substr(kPfx.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      device_id = std::string(rest.substr(0, slash));
    }
    if (device_id != sess.device_id) {
      common::Error e{.code = common::ErrorCode::kForbidden, .message = "Can only upload prekeys for own device"};
      return ErrRes(ver, kHttpForbidden, e);
    }
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::vector<vox::store::PrekeyRecord> prekeys;
    if (o.contains("prekeys") && o["prekeys"].is_array()) {
      for (const auto& item : o["prekeys"].as_array()) {
        if (!item.is_object()) {
          continue;
        }
        const auto& po = item.as_object();
        vox::store::PrekeyRecord pr;
        pr.prekey_id = JsonString(po, "prekey_id").value_or("");
        pr.prekey_public = JsonString(po, "prekey_public").value_or("");
        pr.device_id = device_id;
        prekeys.push_back(std::move(pr));
      }
    }
    auto result = ctx.devices.StorePrekeys(device_id, prekeys);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "GET" && path.starts_with("/v1/devices/") && path.ends_with("/prekey-bundle")) {
    std::string device_id;
    {
      constexpr std::string_view kPfx = "/v1/devices/";
      auto rest = std::string_view(path).substr(kPfx.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      device_id = std::string(rest.substr(0, slash));
    }
    auto result = ctx.devices.GetPrekeyBundle(device_id);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["identity_key_public"] = result->identity_key_public;
    out["signed_prekey_public"] = result->signed_prekey_public;
    out["signed_prekey_signature"] = result->signed_prekey_signature;
    auto one_time_pub = result->one_time_prekey_public;
    if (one_time_pub) {
      out["one_time_prekey_public"] = *one_time_pub;
    }
    auto one_time_id = result->one_time_prekey_id;
    if (one_time_id) {
      out["one_time_prekey_id"] = *one_time_id;
    }
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "POST" && path == "/v1/attachments/upload-init") {
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::string conv_id = JsonString(o, "conversation_id").value_or("");
    std::int64_t file_size = JsonInt(o, "file_size").value_or(0);
    std::string mime = JsonString(o, "mime_hint").value_or("");
    auto result = ctx.attachments.InitUpload(sess.user_id, conv_id, file_size, mime);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["attachment_id"] = result->attachment_id;
    out["blob_path"] = result->blob_path;
    return JsonRes(ver, kHttpOk, out);
  }

  if (method == "PUT" && path.starts_with("/v1/attachments/") && path.find("/chunk") != std::string::npos) {
    std::string attachment_id;
    {
      constexpr std::string_view kPfx = "/v1/attachments/";
      auto rest = std::string_view(path).substr(kPfx.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      attachment_id = std::string(rest.substr(0, slash));
    }
    std::int64_t offset = 0;
    if (auto off = QueryParam(req.target(), "offset")) {
      offset = std::stoll(*off);
    }
    auto result = ctx.attachments.WriteChunk(attachment_id, offset, req.body());
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "POST" && path.starts_with("/v1/attachments/") && path.ends_with("/finalize")) {
    std::string attachment_id;
    {
      constexpr std::string_view kPfx = "/v1/attachments/";
      auto rest = std::string_view(path).substr(kPfx.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return HttpResponse{http::status::not_found, ver};
      }
      attachment_id = std::string(rest.substr(0, slash));
    }
    boost::json::object o;
    if (auto br = parse_body(o)) {
      return *br;
    }
    std::string hash = JsonString(o, "ciphertext_hash").value_or("");
    auto result = ctx.attachments.FinalizeUpload(attachment_id, hash);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  if (method == "GET" && path.starts_with("/v1/attachments/") && path.find("/chunk") == std::string::npos &&
      path.find("/finalize") == std::string::npos && path.starts_with("/v1/attachments/")) {
    std::string attachment_id;
    {
      constexpr std::string_view kPfx = "/v1/attachments/";
      auto rest = std::string_view(path).substr(kPfx.size());
      if (rest.empty()) {
        return HttpResponse{http::status::not_found, ver};
      }
      attachment_id = std::string(rest);
    }
    auto result = ctx.attachments.GetAttachment(attachment_id, sess.user_id);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    HttpResponse res{http::status::ok, ver};
    res.set(http::field::content_type, "application/octet-stream");
    std::ifstream file(*result, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    res.body() = std::move(ss).str();
    res.prepare_payload();
    return res;
  }

  return HttpResponse{http::status::not_found, ver};
}

} // namespace vox::net
