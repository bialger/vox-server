#include "lib/vox_net/http_dispatch.hpp"

#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json.hpp>

#include "lib/vox_net/error_http.hpp"
#include "lib/vox_net/rate_limiter.hpp"
#include "lib/vox_store/device_repository.hpp"

namespace vox::net {

namespace beast = boost::beast;
namespace http = beast::http;

namespace detail {

constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpBadRequest = 400;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpForbidden = 403;
constexpr std::size_t kDefaultPaginationLimit = 100;

using OptRes = std::optional<HttpResponse>;

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

HttpResponse NotFoundRes(unsigned version) {
  HttpResponse res{http::status::not_found, version};
  res.prepare_payload();
  return res;
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

/// Parses `path` as `{prefix}{first_segment}/{rest...}`. `prefix` must end with `/`.
std::optional<std::pair<std::string, std::string_view>> SplitPrefixedPath(std::string_view path,
                                                                          std::string_view prefix) {
  if (!path.starts_with(prefix)) {
    return std::nullopt;
  }
  auto rest = path.substr(prefix.size());
  auto slash = rest.find('/');
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  return std::pair<std::string, std::string_view>{std::string(rest.substr(0, slash)), rest.substr(slash + 1)};
}

/// `/v1/conversations/{id}/...` → (conversation_id, tail after first slash).
std::optional<std::pair<std::string, std::string_view>> SplitConvSubPath(std::string_view path) {
  constexpr std::string_view kConvPrefix = "/v1/conversations/";
  return SplitPrefixedPath(path, kConvPrefix);
}

std::optional<std::string> SplitDeviceSubPath(std::string_view path, std::string_view suffix) {
  constexpr std::string_view kDevicePrefix = "/v1/devices/";
  auto p = SplitPrefixedPath(path, kDevicePrefix);
  if (!p) {
    return std::nullopt;
  }
  if (p->second != suffix) {
    return std::nullopt;
  }
  return std::move(p->first);
}

std::optional<std::string> SplitAttachmentSubPath(std::string_view path) {
  constexpr std::string_view kAttachPrefix = "/v1/attachments/";
  if (!path.starts_with(kAttachPrefix)) {
    return std::nullopt;
  }
  auto rest = path.substr(kAttachPrefix.size());
  if (rest.empty()) {
    return std::nullopt;
  }
  auto slash = rest.find('/');
  if (slash == std::string_view::npos) {
    return std::string(rest);
  }
  return std::string(rest.substr(0, slash));
}

template<typename ParseBody>
OptRes TryPublicAuth(ServerContext& ctx, http::verb m, std::string_view path, unsigned ver, ParseBody&& parse_body) {
  if (m != http::verb::post) {
    return std::nullopt;
  }

  if (path == "/v1/register") {
    boost::json::object o;
    if (auto br = std::forward<ParseBody>(parse_body)(o)) {
      return std::move(br);
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

  if (path == "/v1/login") {
    boost::json::object o;
    if (auto br = std::forward<ParseBody>(parse_body)(o)) {
      return std::move(br);
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

  if (path == "/v1/refresh") {
    boost::json::object o;
    if (auto br = std::forward<ParseBody>(parse_body)(o)) {
      return std::move(br);
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

  return std::nullopt;
}

OptRes TryAdmin(ServerContext& ctx, const HttpRequest& req, http::verb m, std::string_view path, unsigned ver) {
  if (req.find("X-Admin-Token") == req.end() || ctx.config.admin_token.empty()) {
    return std::nullopt;
  }
  auto token = std::string(req.at("X-Admin-Token"));
  if (token != ctx.config.admin_token) {
    return std::nullopt;
  }

  if (m == http::verb::get && path == "/v1/admin/stats") {
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

  if (m == http::verb::delete_ && path.starts_with("/v1/admin/users/")) {
    std::string uid = std::string(path.substr(std::strlen("/v1/admin/users/")));
    auto result = ctx.admin.DeleteUser(uid);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    return JsonRes(ver, kHttpOk, boost::json::object{});
  }

  return std::nullopt;
}

/// True if `path` + `m` matches a Bearer-protected route (same structure as `HandleAuthenticated`).
/// Used so unknown URLs return 404 instead of 401 when no session is present.
bool IsKnownProtectedRoute(http::verb m, std::string_view path) {
  switch (m) {
    case http::verb::post: {
      if (path == "/v1/logout" || path == "/v1/messages/send" || path == "/v1/messages/ack" ||
          path == "/v1/conversations" || path == "/v1/attachments/upload-init") {
        return true;
      }
      if (path.starts_with("/v1/attachments/") && path.ends_with("/finalize")) {
        return SplitAttachmentSubPath(path).has_value();
      }
      if (auto conv = SplitConvSubPath(path)) {
        const std::string_view tail = conv->second;
        if (tail == "members" || tail == "subscribe" || tail == "unsubscribe") {
          return true;
        }
      }
      return SplitDeviceSubPath(path, "prekeys").has_value();
    }
    case http::verb::get: {
      if (path == "/v1/sync/pending" || path == "/v1/conversations") {
        return true;
      }
      if (auto conv = SplitConvSubPath(path)) {
        if (conv->second == "envelopes") {
          return true;
        }
      }
      if (SplitDeviceSubPath(path, "prekey-bundle")) {
        return true;
      }
      if (path.starts_with("/v1/attachments/") && path.find("/chunk") == std::string::npos &&
          path.find("/finalize") == std::string::npos) {
        return SplitAttachmentSubPath(path).has_value();
      }
      return false;
    }
    case http::verb::put: {
      if (path.starts_with("/v1/attachments/") && path.find("/chunk") != std::string::npos) {
        return SplitAttachmentSubPath(path).has_value();
      }
      return false;
    }
    case http::verb::delete_: {
      if (auto conv = SplitConvSubPath(path)) {
        if (conv->second.starts_with("members/")) {
          return true;
        }
      }
      return false;
    }
    default:
      return false;
  }
}

template<typename ParseBody>
OptRes HandleAuthenticated(ServerContext& ctx,
                           const HttpRequest& req,
                           http::verb m,
                           std::string_view path,
                           unsigned ver,
                           const vox::store::SessionRecord& sess,
                           ParseBody&& parse_body) {
  switch (m) {
    case http::verb::post: {
      if (path == "/v1/logout") {
        auto result = ctx.auth.Logout(sess.session_id);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        return JsonRes(ver, kHttpOk, boost::json::object{});
      }

      if (path == "/v1/messages/send") {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
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

      if (path == "/v1/messages/ack") {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
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

      if (path == "/v1/conversations") {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
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

      if (auto conv = SplitConvSubPath(path)) {
        const std::string& conv_id = conv->first;
        std::string_view tail = conv->second;

        if (tail == "members") {
          boost::json::object o;
          if (auto br = std::forward<ParseBody>(parse_body)(o)) {
            return std::move(br);
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

        if (tail == "subscribe") {
          auto result = ctx.conversations.SubscribeChannel(conv_id, sess.user_id);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }

        if (tail == "unsubscribe") {
          auto result = ctx.conversations.UnsubscribeChannel(conv_id, sess.user_id);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
      }

      if (auto device_id = SplitDeviceSubPath(path, "prekeys")) {
        if (*device_id != sess.device_id) {
          common::Error e{.code = common::ErrorCode::kForbidden, .message = "Can only upload prekeys for own device"};
          return ErrRes(ver, kHttpForbidden, e);
        }
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
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
            pr.device_id = *device_id;
            prekeys.push_back(std::move(pr));
          }
        }
        auto result = ctx.devices.StorePrekeys(*device_id, prekeys);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        return JsonRes(ver, kHttpOk, boost::json::object{});
      }

      if (path == "/v1/attachments/upload-init") {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
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

      if (path.starts_with("/v1/attachments/") && path.ends_with("/finalize")) {
        if (auto attachment_id = SplitAttachmentSubPath(path)) {
          boost::json::object o;
          if (auto br = std::forward<ParseBody>(parse_body)(o)) {
            return std::move(br);
          }
          std::string hash = JsonString(o, "ciphertext_hash").value_or("");
          auto result = ctx.attachments.FinalizeUpload(*attachment_id, hash);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
      }

      break;
    }

    case http::verb::get: {
      if (path == "/v1/sync/pending") {
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

      if (auto conv = SplitConvSubPath(path)) {
        if (conv->second == "envelopes") {
          const std::string& conv_id = conv->first;
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
      }

      if (path == "/v1/conversations") {
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

      if (auto device_id = SplitDeviceSubPath(path, "prekey-bundle")) {
        auto result = ctx.devices.GetPrekeyBundle(*device_id);
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

      if (path.starts_with("/v1/attachments/") && path.find("/chunk") == std::string::npos &&
          path.find("/finalize") == std::string::npos) {
        if (auto attachment_id = SplitAttachmentSubPath(path)) {
          auto result = ctx.attachments.GetAttachment(*attachment_id, sess.user_id);
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
      }

      break;
    }

    case http::verb::put: {
      if (path.starts_with("/v1/attachments/") && path.find("/chunk") != std::string::npos) {
        if (auto attachment_id = SplitAttachmentSubPath(path)) {
          std::int64_t offset = 0;
          if (auto off = QueryParam(req.target(), "offset")) {
            offset = std::stoll(*off);
          }
          auto result = ctx.attachments.WriteChunk(*attachment_id, offset, req.body());
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
      }
      break;
    }

    case http::verb::delete_: {
      if (auto conv = SplitConvSubPath(path)) {
        const std::string& conv_id = conv->first;
        std::string_view tail = conv->second;
        if (tail.starts_with("members/")) {
          std::string target_user = std::string(tail.substr(std::strlen("members/")));
          auto result = ctx.conversations.RemoveMember(conv_id, sess.user_id, target_user);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
      }
      break;
    }

    default:
      break;
  }

  return std::nullopt;
}

} // namespace detail

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
                          const std::optional<vox::store::SessionRecord>& session,
                          std::string_view client_ip) {
  const unsigned ver = req.version();
  const http::verb m = req.method();
  const std::string path_owned = PathOnly(req.target());
  const std::string_view path(path_owned);

  if (req.body().size() > ctx.config.max_http_body_bytes) {
    common::Error e{.code = common::ErrorCode::kPayloadTooLarge, .message = "Request body too large"};
    return detail::ErrRes(ver, HttpStatusForError(e.code), e);
  }

  if (ctx.auth_rate_limiter && m == http::verb::post &&
      (path == "/v1/register" || path == "/v1/login" || path == "/v1/refresh")) {
    const std::string key = client_ip.empty() ? std::string("unknown") : std::string(client_ip);
    if (!ctx.auth_rate_limiter->Allow(key)) {
      common::Error e{.code = common::ErrorCode::kRateLimited, .message = "Too many requests"};
      return detail::ErrRes(ver, HttpStatusForError(e.code), e);
    }
  }

  auto parse_body = [&](boost::json::object& obj) -> std::optional<HttpResponse> {
    boost::json::value jv;
    common::Error pe;
    if (!detail::ParseJsonBody(req.body(), jv, pe)) {
      return detail::ErrRes(ver, HttpStatusForError(pe.code), pe);
    }
    if (!jv.is_object()) {
      common::Error e{.code = common::ErrorCode::kInvalidArgument, .message = "JSON object expected"};
      return detail::ErrRes(ver, detail::kHttpBadRequest, e);
    }
    obj = jv.as_object();
    return std::nullopt;
  };

  if (m == http::verb::get && path == "/v1/health") {
    boost::json::object out;
    out["status"] = "ok";
    return detail::JsonRes(ver, detail::kHttpOk, out);
  }

  if (auto r = detail::TryPublicAuth(ctx, m, path, ver, parse_body)) {
    return std::move(*r);
  }

  if (auto r = detail::TryAdmin(ctx, req, m, path, ver)) {
    return std::move(*r);
  }

  if (!session.has_value()) {
    if (!detail::IsKnownProtectedRoute(m, path)) {
      return detail::NotFoundRes(ver);
    }
    common::Error e{.code = common::ErrorCode::kUnauthorized, .message = "Authentication required"};
    return detail::ErrRes(ver, detail::kHttpUnauthorized, e);
  }

  if (auto r = detail::HandleAuthenticated(ctx, req, m, path, ver, session.value(), parse_body)) {
    return std::move(*r);
  }

  return detail::NotFoundRes(ver);
}

} // namespace vox::net
