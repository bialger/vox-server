#include "lib/vox_net/http_dispatch.hpp"

#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <boost/beast/http/file_body.hpp>
#include <boost/json.hpp>

#include "lib/vox_common/types.hpp"
#include "lib/vox_net/error_http.hpp"
#include "lib/vox_net/rate_limiter.hpp"
#include "lib/vox_net/ws_registry.hpp"
#include "lib/vox_store/device_repository.hpp"
#include "lib/vox_store/user_repository.hpp"

#include <spdlog/spdlog.h>

namespace vox::net {

namespace beast = boost::beast;
namespace http = beast::http;

namespace detail {

constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpBadRequest = 400;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpForbidden = 403;
constexpr unsigned kHttpNotFound = 404;
constexpr std::size_t kDefaultPaginationLimit = 100;
constexpr std::size_t kDefaultUserSearchLimit = 20;
constexpr std::size_t kMaxUserSearchLimit = 50;

common::Timestamp NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

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
  HttpResponseString res{static_cast<http::status>(status), version};
  res.set(http::field::content_type, "application/json");
  res.body() = boost::json::serialize(v);
  res.prepare_payload();
  return HttpResponse{std::move(res)};
}

HttpResponse ErrRes(unsigned version, unsigned st, const common::Error& e) {
  return JsonRes(version, st, ErrObj(e));
}

HttpResponse NotFoundRes(unsigned version) {
  HttpResponseString res{http::status::not_found, version};
  res.prepare_payload();
  return HttpResponse{std::move(res)};
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

/// `/v1/users/{user_id}/devices/{device_id}/{suffix}` → (user_id, device_id).
std::optional<std::pair<std::string, std::string>> SplitUserDeviceSubPath(std::string_view path,
                                                                          std::string_view suffix) {
  constexpr std::string_view kPrefix = "/v1/users/";
  if (!path.starts_with(kPrefix)) {
    return std::nullopt;
  }
  auto rest = path.substr(kPrefix.size());
  constexpr std::string_view kMid = "/devices/";
  const auto mid = rest.find(kMid);
  if (mid == std::string_view::npos) {
    return std::nullopt;
  }
  std::string uid = std::string(rest.substr(0, mid));
  const auto after_mid = rest.substr(mid + kMid.size());
  const auto slash = after_mid.find('/');
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  std::string did = std::string(after_mid.substr(0, slash));
  if (after_mid.substr(slash + 1) != suffix) {
    return std::nullopt;
  }
  return std::pair{std::move(uid), std::move(did)};
}

/// `/v1/conversations/{id}` with no further path segments.
std::optional<std::string> ConversationIdOnly(std::string_view path) {
  constexpr std::string_view kPrefix = "/v1/conversations/";
  if (!path.starts_with(kPrefix)) {
    return std::nullopt;
  }
  auto rest = path.substr(kPrefix.size());
  if (rest.empty() || rest.find('/') != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(rest);
}

std::string SerializeSyncWrapParams(const boost::json::object& o) {
  if (!o.contains("sync_wrap_params")) {
    return {};
  }
  const auto& v = o.at("sync_wrap_params");
  if (v.is_object()) {
    return boost::json::serialize(v.as_object());
  }
  if (v.is_string()) {
    return {v.as_string().c_str()};
  }
  return {};
}

void NotifyUserDevicesExcept(ServerContext& ctx,
                             const common::UserId& user_id,
                             const common::UserId& except_user_id,
                             const common::DeviceId& except_device,
                             std::string_view json_line) {
  if (ctx.ws_push == nullptr) {
    return;
  }
  for (const auto& d : ctx.devices.GetDevicesForUser(user_id)) {
    if (d.revoked_at.has_value()) {
      continue;
    }
    if (d.user_id == except_user_id && d.device_id == except_device) {
      continue;
    }
    ctx.ws_push->Notify(common::DeviceScopeKey(d.user_id, d.device_id), std::string(json_line));
  }
}

void NotifyConversationMembershipChanged(ServerContext& ctx, const common::ConversationId& conv_id) {
  if (ctx.ws_push == nullptr) {
    return;
  }
  auto conv = ctx.conversations_store.FindById(conv_id);
  if (!conv) {
    return;
  }
  boost::json::object evt;
  evt["type"] = "conversation_membership_changed";
  evt["conversation_id"] = conv_id;
  evt["membership_version"] = conv->membership_version;
  const std::string json_line = boost::json::serialize(evt);
  std::unordered_set<std::string> seen;
  for (const auto& m : ctx.conversations_store.GetMembers(conv_id)) {
    if (!seen.insert(m.user_id).second) {
      continue;
    }
    for (const auto& d : ctx.devices.GetDevicesForUser(m.user_id)) {
      if (d.revoked_at.has_value()) {
        continue;
      }
      ctx.ws_push->Notify(common::DeviceScopeKey(d.user_id, d.device_id), json_line);
    }
  }
  if (conv->type == common::ConversationType::kChannel) {
    for (const auto& uid : ctx.conversations_store.GetSubscribers(conv_id)) {
      if (!seen.insert(uid).second) {
        continue;
      }
      for (const auto& d : ctx.devices.GetDevicesForUser(uid)) {
        if (d.revoked_at.has_value()) {
          continue;
        }
        ctx.ws_push->Notify(common::DeviceScopeKey(d.user_id, d.device_id), json_line);
      }
    }
  }
}

/// `/v1/sync/records/{collection}/{record_id}`
std::optional<std::pair<std::string, std::string>> SplitSyncRecordPath(std::string_view path) {
  constexpr std::string_view kPrefix = "/v1/sync/records/";
  if (!path.starts_with(kPrefix)) {
    return std::nullopt;
  }
  auto rest = path.substr(kPrefix.size());
  auto slash = rest.find('/');
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  return std::pair<std::string, std::string>{std::string(rest.substr(0, slash)), std::string(rest.substr(slash + 1))};
}

boost::json::object ParsePolicyBlob(const std::string& policy_blob) {
  if (policy_blob.empty()) {
    return {};
  }
  try {
    auto v = boost::json::parse(policy_blob);
    if (v.is_object()) {
      return v.as_object();
    }
  } catch (...) {
    spdlog::trace("Policy blob JSON parse failed");
  }
  return {};
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
    rr.device_label = JsonString(o, "device_label").value_or("");
    rr.identity_key_public = JsonString(o, "identity_key_public").value_or("");
    rr.signed_prekey_public = JsonString(o, "signed_prekey_public").value_or("");
    rr.signed_prekey_signature = JsonString(o, "signed_prekey_signature").value_or("");
    rr.wrapped_sync_key = JsonString(o, "wrapped_sync_key").value_or("");
    rr.sync_wrap_salt = JsonString(o, "sync_wrap_salt").value_or("");
    rr.sync_wrap_params = SerializeSyncWrapParams(o);
    auto result = ctx.auth.Register(rr);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["user_id"] = result->user_id;
    out["access_token"] = result->tokens.access_token;
    out["refresh_token"] = result->tokens.refresh_token;
    out["device_status"] = result->device_status;
    out["sync_key_version"] = result->sync_key_version;
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
    lr.device_label = JsonString(o, "device_label").value_or("");
    lr.identity_key_public = JsonString(o, "identity_key_public").value_or("");
    lr.signed_prekey_public = JsonString(o, "signed_prekey_public").value_or("");
    lr.signed_prekey_signature = JsonString(o, "signed_prekey_signature").value_or("");
    auto result = ctx.auth.Login(lr);
    if (!result) {
      return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
    }
    boost::json::object out;
    out["user_id"] = result->user_id;
    out["access_token"] = result->tokens.access_token;
    out["refresh_token"] = result->tokens.refresh_token;
    out["device_status"] = result->device_status;
    out["sync_key_version"] = result->sync_key_version;
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
          path == "/v1/conversations" || path == "/v1/attachments/upload-init" ||
          path == "/v1/account/change-password") {
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
      if (SplitDeviceSubPath(path, "prekeys") || SplitUserDeviceSubPath(path, "prekeys")) {
        return true;
      }
      return SplitDeviceSubPath(path, "signed-prekey").has_value() ||
             SplitUserDeviceSubPath(path, "signed-prekey").has_value();
    }
    case http::verb::get: {
      if (path == "/v1/me" || path == "/v1/me/devices" || path == "/v1/sync/pending" || path == "/v1/conversations" ||
          path == "/v1/sync/key-bundle") {
        return true;
      }
      if (path.starts_with("/v1/sync/changes")) {
        return true;
      }
      if (path.starts_with("/v1/users/")) {
        return true;
      }
      if (ConversationIdOnly(path)) {
        return true;
      }
      if (auto conv = SplitConvSubPath(path)) {
        if (conv->second == "envelopes" || conv->second == "members") {
          return true;
        }
      }
      if (SplitDeviceSubPath(path, "prekey-bundle") || SplitUserDeviceSubPath(path, "prekey-bundle")) {
        return true;
      }
      if (path.starts_with("/v1/users/") && path.find("/prekey-bundles") != std::string_view::npos) {
        return true;
      }
      if (path.starts_with("/v1/attachments/") && path.find("/chunk") == std::string::npos &&
          path.find("/finalize") == std::string::npos) {
        return SplitAttachmentSubPath(path).has_value();
      }
      return false;
    }
    case http::verb::put: {
      if (path == "/v1/sync/key-bundle") {
        return true;
      }
      if (path.starts_with("/v1/sync/records/")) {
        return true;
      }
      if (SplitDeviceSubPath(path, "signed-prekey") || SplitUserDeviceSubPath(path, "signed-prekey")) {
        return true;
      }
      if (path.starts_with("/v1/attachments/") && path.find("/chunk") != std::string::npos) {
        return SplitAttachmentSubPath(path).has_value();
      }
      return false;
    }
    case http::verb::delete_: {
      if (path.starts_with("/v1/me/devices/")) {
        return true;
      }
      if (path.starts_with("/v1/sync/records/")) {
        return true;
      }
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

      if (path == "/v1/account/change-password") {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
        }
        vox::auth::ChangePasswordRequest cr;
        cr.current_password_derived_value = JsonString(o, "current_password_derived_value").value_or("");
        cr.new_password_derived_value = JsonString(o, "new_password_derived_value").value_or("");
        cr.wrapped_sync_key = JsonString(o, "wrapped_sync_key").value_or("");
        cr.sync_wrap_salt = JsonString(o, "sync_wrap_salt").value_or("");
        cr.sync_wrap_params = SerializeSyncWrapParams(o);
        auto result = ctx.auth.ChangePassword(sess.user_id, cr);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        boost::json::object out;
        out["sync_key_version"] = result->sync_key_version;
        return JsonRes(ver, kHttpOk, out);
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
        sr.sender_user_id = sess.user_id;
        sr.sender_device_id = device_id;
        sr.conversation_id = JsonString(o, "conversation_id").value_or("");
        sr.ciphertext = JsonString(o, "ciphertext").value_or("");
        sr.envelope_id = JsonString(o, "envelope_id").value_or("");
        if (auto et = JsonInt(o, "envelope_type")) {
          sr.envelope_type = static_cast<int>(*et);
        }
        sr.ordering_epoch = JsonInt(o, "ordering_epoch");
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
        auto result = ctx.relay.AcknowledgeEnvelope(sess.user_id, device_id, envelope_id);
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
          NotifyConversationMembershipChanged(ctx, *r);
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
          NotifyConversationMembershipChanged(ctx, *r);
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
          NotifyConversationMembershipChanged(ctx, *r);
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
          NotifyConversationMembershipChanged(ctx, conv_id);
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }

        if (tail == "subscribe") {
          auto result = ctx.conversations.SubscribeChannel(conv_id, sess.user_id);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          NotifyConversationMembershipChanged(ctx, conv_id);
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }

        if (tail == "unsubscribe") {
          auto result = ctx.conversations.UnsubscribeChannel(conv_id, sess.user_id);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          NotifyConversationMembershipChanged(ctx, conv_id);
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
      }

      {
        std::optional<std::pair<std::string, std::string>> prekey_scope;
        if (auto ud = SplitUserDeviceSubPath(path, "prekeys")) {
          prekey_scope = std::move(ud);
        } else if (auto legacy_id = SplitDeviceSubPath(path, "prekeys")) {
          prekey_scope = std::pair<std::string, std::string>{sess.user_id, std::move(*legacy_id)};
        }
        if (prekey_scope.has_value()) {
          const auto& owner_uid = prekey_scope->first;
          const auto& dev_id = prekey_scope->second;
          if (owner_uid != sess.user_id || dev_id != sess.device_id) {
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
              pr.device_id = dev_id;
              prekeys.push_back(std::move(pr));
            }
          }
          auto result = ctx.devices.StorePrekeys(owner_uid, dev_id, prekeys);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
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
      if (path == "/v1/me") {
        auto u = ctx.users.FindById(sess.user_id);
        if (!u) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        boost::json::object out;
        out["user_id"] = sess.user_id;
        out["username"] = u->username;
        out["current_device_id"] = sess.device_id;
        out["sync_key_version"] = u->sync_key_version;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path == "/v1/me/devices") {
        auto devs = ctx.devices.GetDevicesForUser(sess.user_id);
        boost::json::array arr;
        for (const auto& d : devs) {
          boost::json::object o;
          o["device_id"] = d.device_id;
          o["device_label"] = d.device_label;
          o["created_at"] = d.created_at;
          o["last_seen_at"] = d.last_seen_at;
          o["is_current"] = (d.device_id == sess.device_id);
          o["is_revoked"] = d.revoked_at.has_value();
          arr.push_back(o);
        }
        boost::json::object out;
        out["devices"] = arr;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path == "/v1/sync/key-bundle") {
        auto b = ctx.users.GetSyncKeyBundle(sess.user_id);
        if (!b) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        boost::json::object out;
        out["sync_key_version"] = b->sync_key_version;
        out["wrapped_sync_key"] = b->wrapped_sync_key;
        out["sync_wrap_salt"] = b->sync_wrap_salt;
        try {
          out["sync_wrap_params"] = boost::json::parse(b->sync_wrap_params);
        } catch (...) {
          out["sync_wrap_params"] = boost::json::object{};
        }
        return JsonRes(ver, kHttpOk, out);
      }

      if (path.starts_with("/v1/sync/changes")) {
        std::string coll = QueryParam(req.target(), "collection").value_or("");
        if (coll.empty()) {
          common::Error e{.code = common::ErrorCode::kInvalidArgument, .message = "collection required"};
          return ErrRes(ver, kHttpBadRequest, e);
        }
        std::size_t limit = kDefaultPaginationLimit;
        if (auto l = QueryParam(req.target(), "limit")) {
          limit = static_cast<std::size_t>(std::stoull(*l));
        }
        std::string cursor = QueryParam(req.target(), "cursor").value_or("");
        auto page = ctx.sync_state.ListChangesAfterCursor(sess.user_id, coll, cursor, limit);
        boost::json::array arr;
        for (const auto& ch : page.changes) {
          boost::json::object row;
          row["record_id"] = ch.record_id;
          row["ciphertext"] = ch.ciphertext;
          row["content_hash"] = ch.content_hash;
          row["version"] = ch.version;
          row["server_updated_at"] = ch.server_updated_at;
          row["deleted"] = ch.deleted;
          arr.push_back(row);
        }
        boost::json::object out;
        out["collection"] = coll;
        out["changes"] = arr;
        out["next_cursor"] = page.next_cursor;
        out["has_more"] = page.has_more;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path.starts_with("/v1/users/by-username/")) {
        std::string uname = std::string(path.substr(std::strlen("/v1/users/by-username/")));
        auto u = ctx.users.FindByUsername(uname);
        if (!u || u->disabled_at.has_value()) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        boost::json::object out;
        out["user_id"] = u->user_id;
        out["username"] = u->username;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path == "/v1/users/search") {
        std::string q = QueryParam(req.target(), "q").value_or("");
        std::size_t lim = kDefaultUserSearchLimit;
        if (auto l = QueryParam(req.target(), "limit")) {
          lim = static_cast<std::size_t>(std::stoull(*l));
          if (lim > kMaxUserSearchLimit) {
            lim = kMaxUserSearchLimit;
          }
        }
        auto found = ctx.users.SearchByUsernamePrefix(q, lim);
        boost::json::array arr;
        for (const auto& u : found) {
          boost::json::object o;
          o["user_id"] = u.user_id;
          o["username"] = u.username;
          arr.push_back(o);
        }
        boost::json::object out;
        out["users"] = arr;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path.ends_with("/prekey-bundles")) {
        constexpr std::string_view kSuf = "/prekey-bundles";
        if (!path.ends_with(kSuf) || path.size() <= kSuf.size()) {
          break;
        }
        std::string_view base = path.substr(0, path.size() - kSuf.size());
        constexpr std::string_view kPfx = "/v1/users/";
        if (!base.starts_with(kPfx)) {
          break;
        }
        std::string uid = std::string(base.substr(kPfx.size()));
        auto u = ctx.users.FindById(uid);
        if (!u || u->disabled_at.has_value()) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        boost::json::object out;
        out["user_id"] = u->user_id;
        out["username"] = u->username;
        boost::json::array bundles;
        for (const auto& d : ctx.devices.GetDevicesForUser(uid)) {
          if (d.revoked_at.has_value()) {
            continue;
          }
          auto pb = ctx.devices.GetPrekeyBundle(uid, d.device_id);
          if (!pb) {
            continue;
          }
          boost::json::object bo;
          bo["device_id"] = d.device_id;
          bo["device_label"] = d.device_label;
          bo["identity_key_public"] = pb->identity_key_public;
          bo["signed_prekey_public"] = pb->signed_prekey_public;
          bo["signed_prekey_signature"] = pb->signed_prekey_signature;
          const auto& otp_pub = pb->one_time_prekey_public;
          if (otp_pub.has_value()) {
            bo["one_time_prekey_public"] = *otp_pub;
          }
          const auto& otp_id = pb->one_time_prekey_id;
          if (otp_id.has_value()) {
            bo["one_time_prekey_id"] = *otp_id;
          }
          bundles.push_back(bo);
        }
        out["bundles"] = bundles;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path.ends_with("/devices")) {
        constexpr std::string_view kSuf = "/devices";
        if (!path.ends_with(kSuf) || path.size() <= kSuf.size()) {
          break;
        }
        std::string_view base = path.substr(0, path.size() - kSuf.size());
        constexpr std::string_view kPfx = "/v1/users/";
        if (!base.starts_with(kPfx)) {
          break;
        }
        std::string uid = std::string(base.substr(kPfx.size()));
        auto u = ctx.users.FindById(uid);
        if (!u || u->disabled_at.has_value()) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        boost::json::array arr;
        for (const auto& d : ctx.devices.GetDevicesForUser(uid)) {
          if (d.revoked_at.has_value()) {
            continue;
          }
          boost::json::object o;
          o["device_id"] = d.device_id;
          o["device_label"] = d.device_label;
          o["is_revoked"] = false;
          o["has_prekeys"] = (ctx.devices.CountAvailableOneTimePrekeys(uid, d.device_id) > 0);
          arr.push_back(o);
        }
        boost::json::object out;
        out["devices"] = arr;
        return JsonRes(ver, kHttpOk, out);
      }

      if (path.starts_with("/v1/users/")) {
        std::string_view rest = path.substr(std::strlen("/v1/users/"));
        if (rest.find('/') != std::string_view::npos) {
          break;
        }
        if (rest == "search" || rest.starts_with("by-username")) {
          break;
        }
        std::string uid = std::string(rest);
        auto u = ctx.users.FindById(uid);
        if (!u || u->disabled_at.has_value()) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        boost::json::object out;
        out["user_id"] = u->user_id;
        out["username"] = u->username;
        return JsonRes(ver, kHttpOk, out);
      }

      if (auto conv_only = ConversationIdOnly(path)) {
        auto conv = ctx.conversations_store.FindById(*conv_only);
        if (!conv) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "Conversation not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        if (!ctx.conversations_store.IsUserInConversation(*conv_only, sess.user_id)) {
          common::Error e{.code = common::ErrorCode::kForbidden, .message = "Not a member of this conversation"};
          return ErrRes(ver, kHttpForbidden, e);
        }
        auto pol = ParsePolicyBlob(conv->policy_blob);
        boost::json::object out;
        out["conversation_id"] = conv->conversation_id;
        out["type"] = static_cast<int>(conv->type);
        out["created_by"] = conv->created_by;
        out["created_at"] = conv->created_at;
        out["membership_version"] = conv->membership_version;
        if (conv->type == common::ConversationType::kChannel) {
          if (pol.contains("title")) {
            out["title"] = pol.at("title");
          } else {
            out["title"] = "";
          }
          std::string cpp = "admins_only";
          if (pol.contains("channel_post_policy") && pol["channel_post_policy"].is_string()) {
            cpp = std::string(pol["channel_post_policy"].as_string().c_str());
          }
          out["channel_post_policy"] = cpp;
        }
        auto mem = ctx.conversations_store.GetMember(*conv_only, sess.user_id);
        if (mem) {
          const char* rs = "member";
          if (mem->role == common::MemberRole::kOwner) {
            rs = "owner";
          } else if (mem->role == common::MemberRole::kAdmin) {
            rs = "admin";
          }
          out["my_role"] = rs;
        } else if (conv->type == common::ConversationType::kChannel) {
          out["my_role"] = "member";
        }
        return JsonRes(ver, kHttpOk, out);
      }

      if (auto conv = SplitConvSubPath(path)) {
        if (conv->second == "members") {
          const std::string& conv_id = conv->first;
          if (!ctx.conversations_store.IsUserInConversation(conv_id, sess.user_id)) {
            common::Error e{.code = common::ErrorCode::kForbidden, .message = "Not a member of this conversation"};
            return ErrRes(ver, kHttpForbidden, e);
          }
          auto crec = ctx.conversations_store.FindById(conv_id);
          if (!crec) {
            common::Error e{.code = common::ErrorCode::kNotFound, .message = "Conversation not found"};
            return ErrRes(ver, kHttpNotFound, e);
          }
          boost::json::object out;
          out["conversation_id"] = conv_id;
          out["membership_version"] = crec->membership_version;
          if (crec->type == common::ConversationType::kDm || crec->type == common::ConversationType::kGroup) {
            boost::json::array members;
            for (const auto& m : ctx.conversations_store.GetMembers(conv_id)) {
              boost::json::object mo;
              mo["user_id"] = m.user_id;
              const char* rs = "member";
              if (m.role == common::MemberRole::kOwner) {
                rs = "owner";
              } else if (m.role == common::MemberRole::kAdmin) {
                rs = "admin";
              }
              mo["role"] = rs;
              members.push_back(mo);
            }
            out["members"] = members;
            return JsonRes(ver, kHttpOk, out);
          }
          if (crec->type == common::ConversationType::kChannel) {
            auto actor = ctx.conversations_store.GetMember(conv_id, sess.user_id);
            const bool is_admin =
                actor && (actor->role == common::MemberRole::kOwner || actor->role == common::MemberRole::kAdmin);
            boost::json::array admins;
            for (const auto& m : ctx.conversations_store.GetMembers(conv_id)) {
              boost::json::object mo;
              mo["user_id"] = m.user_id;
              const char* rs = "member";
              if (m.role == common::MemberRole::kOwner) {
                rs = "owner";
              } else if (m.role == common::MemberRole::kAdmin) {
                rs = "admin";
              }
              mo["role"] = rs;
              admins.push_back(mo);
            }
            out["admins"] = admins;
            if (is_admin) {
              boost::json::array subs;
              auto admin_ids = ctx.conversations_store.GetMembers(conv_id);
              std::unordered_set<std::string> admin_set;
              for (const auto& m : admin_ids) {
                admin_set.insert(m.user_id);
              }
              for (const auto& su : ctx.conversations_store.GetSubscribers(conv_id)) {
                if (admin_set.count(su)) {
                  continue;
                }
                boost::json::object so;
                so["user_id"] = su;
                so["role"] = "member";
                subs.push_back(so);
              }
              out["subscribers"] = subs;
            } else {
              out["subscription_state"] = "subscribed";
              out["member_count"] = static_cast<std::int64_t>(ctx.conversations_store.GetSubscribers(conv_id).size());
            }
            return JsonRes(ver, kHttpOk, out);
          }
        }
      }

      if (path == "/v1/sync/pending") {
        std::size_t limit = kDefaultPaginationLimit;
        if (auto l = QueryParam(req.target(), "limit")) {
          limit = static_cast<std::size_t>(std::stoull(*l));
        }
        std::string cursor = QueryParam(req.target(), "cursor").value_or("");
        vox::store::IEnvelopeRepository::EnvelopePage page =
            ctx.envelopes.GetPendingForDeviceCursored(sess.user_id, sess.device_id, cursor, limit);
        boost::json::array arr;
        for (const auto& e : page.envelopes) {
          boost::json::object eo;
          eo["envelope_id"] = e.envelope_id;
          eo["conversation_id"] = e.conversation_id;
          eo["sender_user_id"] = e.sender_user_id;
          eo["sender_device_id"] = e.sender_device_id;
          eo["ciphertext"] = e.ciphertext;
          eo["server_timestamp"] = e.server_timestamp;
          eo["envelope_type"] = e.envelope_type;
          if (e.ordering_epoch) {
            eo["ordering_epoch"] = *e.ordering_epoch;
          }
          arr.push_back(eo);
        }
        boost::json::object out;
        out["envelopes"] = arr;
        out["next_cursor"] = page.next_cursor;
        out["has_more"] = page.has_more;
        return JsonRes(ver, kHttpOk, out);
      }

      if (auto conv = SplitConvSubPath(path)) {
        if (conv->second == "envelopes") {
          const std::string& conv_id = conv->first;
          if (!ctx.conversations_store.IsUserInConversation(conv_id, sess.user_id)) {
            common::Error e{.code = common::ErrorCode::kForbidden, .message = "Not a member of this conversation"};
            return ErrRes(ver, kHttpForbidden, e);
          }
          std::size_t limit = kDefaultPaginationLimit;
          if (auto l = QueryParam(req.target(), "limit")) {
            limit = static_cast<std::size_t>(std::stoull(*l));
          }
          std::string cursor = QueryParam(req.target(), "cursor").value_or("");
          boost::json::array arr;
          std::string next_cursor;
          bool has_more = false;
          if (cursor.empty() && QueryParam(req.target(), "since").has_value()) {
            common::Timestamp since = 0;
            if (auto s = QueryParam(req.target(), "since")) {
              since = static_cast<common::Timestamp>(std::stoll(*s));
            }
            auto list = ctx.envelopes.ListForConversation(conv_id, since, limit);
            for (const auto& e : list) {
              boost::json::object eo;
              eo["envelope_id"] = e.envelope_id;
              eo["conversation_id"] = e.conversation_id;
              eo["sender_user_id"] = e.sender_user_id;
              eo["sender_device_id"] = e.sender_device_id;
              eo["ciphertext"] = e.ciphertext;
              eo["server_timestamp"] = e.server_timestamp;
              eo["envelope_type"] = e.envelope_type;
              if (e.ordering_epoch) {
                eo["ordering_epoch"] = *e.ordering_epoch;
              }
              arr.push_back(eo);
            }
          } else {
            auto page = ctx.envelopes.ListForConversationCursored(conv_id, cursor, limit);
            for (const auto& e : page.envelopes) {
              boost::json::object eo;
              eo["envelope_id"] = e.envelope_id;
              eo["conversation_id"] = e.conversation_id;
              eo["sender_user_id"] = e.sender_user_id;
              eo["sender_device_id"] = e.sender_device_id;
              eo["ciphertext"] = e.ciphertext;
              eo["server_timestamp"] = e.server_timestamp;
              eo["envelope_type"] = e.envelope_type;
              if (e.ordering_epoch) {
                eo["ordering_epoch"] = *e.ordering_epoch;
              }
              arr.push_back(eo);
            }
            next_cursor = page.next_cursor;
            has_more = page.has_more;
          }
          boost::json::object out;
          out["envelopes"] = arr;
          out["next_cursor"] = next_cursor;
          out["has_more"] = has_more;
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
          co["membership_version"] = c.membership_version;
          arr.push_back(co);
        }
        boost::json::object out;
        out["conversations"] = arr;
        return JsonRes(ver, kHttpOk, out);
      }

      if (auto ud = SplitUserDeviceSubPath(path, "prekey-bundle")) {
        auto result = ctx.devices.GetPrekeyBundle(ud->first, ud->second);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        boost::json::object out;
        out["user_id"] = ud->first;
        out["device_id"] = ud->second;
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
      if (auto device_id = SplitDeviceSubPath(path, "prekey-bundle")) {
        auto matches = ctx.devices.FindAllByDeviceId(*device_id);
        if (matches.empty()) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "Device not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        if (matches.size() > 1) {
          common::Error e{
              .code = common::ErrorCode::kInvalidArgument,
              .message = "device_id is ambiguous; use /v1/users/{user_id}/devices/{device_id}/prekey-bundle"};
          return ErrRes(ver, kHttpBadRequest, e);
        }
        auto result = ctx.devices.GetPrekeyBundle(matches.front().user_id, *device_id);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        boost::json::object out;
        out["user_id"] = matches.front().user_id;
        out["device_id"] = *device_id;
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
          HttpResponseFile res{http::status::ok, ver};
          res.set(http::field::content_type, "application/octet-stream");
          beast::error_code fec;
          res.body().open(result->string().c_str(), beast::file_mode::read, fec);
          if (fec) {
            common::Error e{.code = common::ErrorCode::kInternal, .message = "Cannot open attachment blob"};
            return ErrRes(ver, HttpStatusForError(e.code), e);
          }
          res.prepare_payload();
          return HttpResponse{std::move(res)};
        }
      }

      break;
    }

    case http::verb::put: {
      if (path == "/v1/sync/key-bundle") {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
        }
        auto u = ctx.users.FindById(sess.user_id);
        if (!u) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "User not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        vox::store::SyncKeyBundleRecord b;
        b.sync_key_version = u->sync_key_version + 1;
        b.wrapped_sync_key = JsonString(o, "wrapped_sync_key").value_or("");
        b.sync_wrap_salt = JsonString(o, "sync_wrap_salt").value_or("");
        b.sync_wrap_params = SerializeSyncWrapParams(o);
        auto r = ctx.users.SetSyncKeyBundle(sess.user_id, b);
        if (!r) {
          return ErrRes(ver, HttpStatusForError(r.error().code), r.error());
        }
        boost::json::object out;
        out["sync_key_version"] = b.sync_key_version;
        {
          boost::json::object evt;
          evt["type"] = "user_devices_changed";
          evt["user_id"] = sess.user_id;
          NotifyUserDevicesExcept(ctx, sess.user_id, sess.user_id, sess.device_id, boost::json::serialize(evt));
        }
        return JsonRes(ver, kHttpOk, out);
      }

      if (auto sr = SplitSyncRecordPath(path)) {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
        }
        std::string device_id = JsonString(o, "device_id").value_or("");
        if (device_id != sess.device_id) {
          common::Error e{.code = common::ErrorCode::kForbidden, .message = "device_id must match session device"};
          return ErrRes(ver, kHttpForbidden, e);
        }
        const auto now = NowSeconds();
        auto result = ctx.sync_state.UpsertRecord(sess.user_id,
                                                  sr->first,
                                                  sr->second,
                                                  JsonString(o, "ciphertext").value_or(""),
                                                  JsonString(o, "content_hash").value_or(""),
                                                  static_cast<int>(JsonInt(o, "base_version").value_or(0)),
                                                  JsonInt(o, "client_updated_at").value_or(now),
                                                  now);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        boost::json::object out;
        out["record_id"] = result->record_id;
        out["version"] = result->version;
        out["server_updated_at"] = result->server_updated_at;
        {
          boost::json::object evt;
          evt["type"] = "sync_record_changed";
          evt["collection"] = sr->first;
          evt["record_id"] = sr->second;
          evt["version"] = result->version;
          NotifyUserDevicesExcept(ctx, sess.user_id, sess.user_id, sess.device_id, boost::json::serialize(evt));
        }
        return JsonRes(ver, kHttpOk, out);
      }

      {
        std::optional<std::pair<std::string, std::string>> spk_scope;
        if (auto ud = SplitUserDeviceSubPath(path, "signed-prekey")) {
          spk_scope = std::move(ud);
        } else if (auto legacy_id = SplitDeviceSubPath(path, "signed-prekey")) {
          spk_scope = std::pair<std::string, std::string>{sess.user_id, std::move(*legacy_id)};
        }
        if (spk_scope.has_value()) {
          const auto& owner_uid = spk_scope->first;
          const auto& dev_id = spk_scope->second;
          if (owner_uid != sess.user_id || dev_id != sess.device_id) {
            common::Error e{.code = common::ErrorCode::kForbidden,
                            .message = "Can only rotate signed prekey for own device"};
            return ErrRes(ver, kHttpForbidden, e);
          }
          boost::json::object o;
          if (auto br = std::forward<ParseBody>(parse_body)(o)) {
            return std::move(br);
          }
          auto r = ctx.devices.UpdateSignedPrekey(owner_uid,
                                                  dev_id,
                                                  JsonString(o, "signed_prekey_public").value_or(""),
                                                  JsonString(o, "signed_prekey_signature").value_or(""),
                                                  NowSeconds());
          if (!r) {
            return ErrRes(ver, HttpStatusForError(r.error().code), r.error());
          }
          boost::json::object evt;
          evt["type"] = "user_devices_changed";
          evt["user_id"] = sess.user_id;
          NotifyUserDevicesExcept(ctx, sess.user_id, sess.user_id, sess.device_id, boost::json::serialize(evt));
          return JsonRes(ver, kHttpOk, boost::json::object{});
        }
      }

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
      if (path.starts_with("/v1/me/devices/")) {
        std::string target = std::string(path.substr(std::strlen("/v1/me/devices/")));
        if (target.empty()) {
          break;
        }
        if (target == sess.device_id) {
          common::Error e{.code = common::ErrorCode::kInvalidArgument, .message = "Cannot revoke current device"};
          return ErrRes(ver, kHttpBadRequest, e);
        }
        auto dev = ctx.devices.FindByUserAndDevice(sess.user_id, target);
        if (!dev) {
          common::Error e{.code = common::ErrorCode::kNotFound, .message = "Device not found"};
          return ErrRes(ver, kHttpNotFound, e);
        }
        const auto now = NowSeconds();
        auto rv = ctx.devices.RevokeDevice(sess.user_id, target, now);
        if (!rv) {
          return ErrRes(ver, HttpStatusForError(rv.error().code), rv.error());
        }
        if (auto tr = ctx.tokens.RevokeAllForDevice(sess.user_id, target, now); !tr) {
          return ErrRes(ver, HttpStatusForError(tr.error().code), tr.error());
        }
        boost::json::object evt;
        evt["type"] = "user_devices_changed";
        evt["user_id"] = sess.user_id;
        NotifyUserDevicesExcept(ctx, sess.user_id, sess.user_id, sess.device_id, boost::json::serialize(evt));
        return JsonRes(ver, kHttpOk, boost::json::object{});
      }

      if (auto sr = SplitSyncRecordPath(path)) {
        boost::json::object o;
        if (auto br = std::forward<ParseBody>(parse_body)(o)) {
          return std::move(br);
        }
        std::string device_id = JsonString(o, "device_id").value_or("");
        if (device_id != sess.device_id) {
          common::Error e{.code = common::ErrorCode::kForbidden, .message = "device_id must match session device"};
          return ErrRes(ver, kHttpForbidden, e);
        }
        const auto now = NowSeconds();
        auto result = ctx.sync_state.TombstoneRecord(sess.user_id,
                                                     sr->first,
                                                     sr->second,
                                                     static_cast<int>(JsonInt(o, "base_version").value_or(0)),
                                                     JsonInt(o, "client_updated_at").value_or(now),
                                                     now);
        if (!result) {
          return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
        }
        boost::json::object out;
        out["record_id"] = result->record_id;
        out["version"] = result->version;
        out["server_updated_at"] = result->server_updated_at;
        out["deleted"] = true;
        {
          boost::json::object evt;
          evt["type"] = "sync_record_changed";
          evt["collection"] = sr->first;
          evt["record_id"] = sr->second;
          evt["version"] = result->version;
          NotifyUserDevicesExcept(ctx, sess.user_id, sess.user_id, sess.device_id, boost::json::serialize(evt));
        }
        return JsonRes(ver, kHttpOk, out);
      }

      if (auto conv = SplitConvSubPath(path)) {
        const std::string& conv_id = conv->first;
        std::string_view tail = conv->second;
        if (tail.starts_with("members/")) {
          std::string target_user = std::string(tail.substr(std::strlen("members/")));
          auto result = ctx.conversations.RemoveMember(conv_id, sess.user_id, target_user);
          if (!result) {
            return ErrRes(ver, HttpStatusForError(result.error().code), result.error());
          }
          NotifyConversationMembershipChanged(ctx, conv_id);
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

  if (ctx.account_rate_limiter != nullptr) {
    const bool heavy_route =
        (m == http::verb::post && path == "/v1/messages/send") ||
        (m == http::verb::put && path.starts_with("/v1/attachments/") && path.find("/chunk") != std::string_view::npos);
    if (heavy_route && !ctx.account_rate_limiter->Allow(session->user_id)) {
      common::Error e{.code = common::ErrorCode::kRateLimited, .message = "Too many requests"};
      return detail::ErrRes(ver, HttpStatusForError(e.code), e);
    }
  }

  if (auto r = detail::HandleAuthenticated(ctx, req, m, path, ver, session.value(), parse_body)) {
    return std::move(*r);
  }

  return detail::NotFoundRes(ver);
}

} // namespace vox::net
