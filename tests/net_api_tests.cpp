#include <string>

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include "test_suites/NetApiTestSuite.hpp"

namespace {

constexpr std::int64_t kSmallAttachmentBytes = 12;

boost::json::object ParseObj(const std::string& body) {
  return boost::json::parse(body).as_object();
}

std::string JsonString(const std::string& json, std::string_view key) {
  return {ParseObj(json).at(key).as_string().c_str()};
}

} // namespace

TEST_F(NetApiTestSuite, RegisterAndLoginSuccess) {
  boost::json::object reg;
  reg["username"] = "net_alice";
  reg["password_derived_value"] = "derived";
  reg["device_id"] = "dev_alice";
  reg["identity_key_public"] = "ik";
  reg["signed_prekey_public"] = "spk";
  reg["signed_prekey_signature"] = "sig";
  auto [st, body] = HttpPost("/v1/register", boost::json::serialize(reg));
  ASSERT_EQ(st, 200u);
  ASSERT_FALSE(JsonString(body, "access_token").empty());

  boost::json::object login;
  login["username"] = "net_alice";
  login["password_derived_value"] = "derived";
  login["device_id"] = "dev_alice";
  auto [st2, body2] = HttpPost("/v1/login", boost::json::serialize(login));
  ASSERT_EQ(st2, 200u);
  ASSERT_FALSE(JsonString(body2, "access_token").empty());
}

TEST_F(NetApiTestSuite, ProtectedRouteWithoutTokenReturns401) {
  auto [st, body] = HttpGet("/v1/conversations");
  ASSERT_EQ(st, 401u);
  (void) body;
}

TEST_F(NetApiTestSuite, UnknownPathWithoutTokenReturns404) {
  auto [st, body] = HttpGet("/v1/no/such/route/ever");
  ASSERT_EQ(st, 404u);
  (void) body;
}

TEST_F(NetApiTestSuite, HealthGetReturnsOkWithoutAuth) {
  auto [st, body] = HttpGet("/v1/health");
  ASSERT_EQ(st, 200u);
  ASSERT_EQ(ParseObj(body).at("status").as_string(), "ok");
}

TEST_F(NetApiTestSuite, SendMessageFlow) {
  boost::json::object reg_a;
  reg_a["username"] = "sa";
  reg_a["password_derived_value"] = "p";
  reg_a["device_id"] = "da";
  reg_a["identity_key_public"] = "ik";
  reg_a["signed_prekey_public"] = "spk";
  reg_a["signed_prekey_signature"] = "sig";
  auto [r1, b1] = HttpPost("/v1/register", boost::json::serialize(reg_a));
  ASSERT_EQ(r1, 200u);
  std::string uid_a = JsonString(b1, "user_id");

  boost::json::object reg_b;
  reg_b["username"] = "sb";
  reg_b["password_derived_value"] = "p";
  reg_b["device_id"] = "db";
  reg_b["identity_key_public"] = "ik";
  reg_b["signed_prekey_public"] = "spk";
  reg_b["signed_prekey_signature"] = "sig";
  auto [r2, b2] = HttpPost("/v1/register", boost::json::serialize(reg_b));
  ASSERT_EQ(r2, 200u);
  std::string tok_b = JsonString(b2, "access_token");

  boost::json::object conv;
  conv["type"] = "dm";
  conv["peer_user_id"] = uid_a;
  auto [r3, b3] = HttpPost("/v1/conversations", boost::json::serialize(conv), tok_b);
  ASSERT_EQ(r3, 200u);
  std::string conv_id = JsonString(b3, "conversation_id");

  boost::json::object send;
  send["device_id"] = "db";
  send["conversation_id"] = conv_id;
  send["ciphertext"] = "cipher_blob";
  auto [r4, b4] = HttpPost("/v1/messages/send", boost::json::serialize(send), tok_b);
  ASSERT_EQ(r4, 200u);
  (void) b4;
}

TEST_F(NetApiTestSuite, AdminStatsWithToken) {
  auto [st, body] = HttpGet("/v1/admin/stats", "", "test-admin-secret");
  ASSERT_EQ(st, 200u);
  auto v = boost::json::parse(body);
  ASSERT_TRUE(v.as_object().contains("user_count"));
}

TEST_F(NetApiTestSuite, AdminStatsWrongToken) {
  auto [st, body] = HttpGet("/v1/admin/stats", "", "wrong");
  ASSERT_TRUE(st == 401u || st == 403u || st == 404u);
  (void) body;
}

TEST_F(NetApiTestSuite, RefreshReturnsNewTokens) {
  auto u = RegisterUser("refresh_u", "dev_r");
  boost::json::object rf;
  rf["refresh_token"] = u.refresh_token;
  rf["device_id"] = "dev_r";
  auto [st, body] = HttpPost("/v1/refresh", boost::json::serialize(rf));
  ASSERT_EQ(st, 200u);
  auto o = ParseObj(body);
  ASSERT_FALSE(o["access_token"].as_string().empty());
  ASSERT_FALSE(o["refresh_token"].as_string().empty());
}

TEST_F(NetApiTestSuite, LogoutRevokesBearer) {
  auto u = RegisterUser("logout_u", "dev_l");
  auto [st1, body1] = HttpPost("/v1/logout", "{}", u.access_token);
  ASSERT_EQ(st1, 200u);
  (void) body1;
  auto [st2, body2] = HttpGet("/v1/conversations", u.access_token);
  ASSERT_EQ(st2, 401u);
  (void) body2;
}

TEST_F(NetApiTestSuite, ListConversationsIncludesDm) {
  auto a = RegisterUser("list_a", "dla");
  auto b = RegisterUser("list_b", "dlb");
  boost::json::object conv;
  conv["type"] = "dm";
  conv["peer_user_id"] = a.user_id;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(conv), b.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  auto [gst, gbody] = HttpGet("/v1/conversations", b.access_token);
  ASSERT_EQ(gst, 200u);
  auto arr = ParseObj(gbody)["conversations"].as_array();
  ASSERT_FALSE(arr.empty());
  bool found = false;
  for (const auto& item : arr) {
    if (item.as_object().at("conversation_id").as_string() == conv_id) {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);
}

TEST_F(NetApiTestSuite, GetConversationEnvelopes) {
  auto a = RegisterUser("env_a", "dea");
  auto b = RegisterUser("env_b", "deb");
  boost::json::object conv;
  conv["type"] = "dm";
  conv["peer_user_id"] = a.user_id;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(conv), b.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  boost::json::object send;
  send["device_id"] = "deb";
  send["conversation_id"] = conv_id;
  send["ciphertext"] = "hello";
  auto [sst, sbody] = HttpPost("/v1/messages/send", boost::json::serialize(send), b.access_token);
  ASSERT_EQ(sst, 200u);
  (void) sbody;

  std::string path = std::string("/v1/conversations/") + conv_id + "/envelopes?since=0&limit=10";
  auto [gst, gbody] = HttpGet(path, b.access_token);
  ASSERT_EQ(gst, 200u);
  auto envs = ParseObj(gbody)["envelopes"].as_array();
  ASSERT_FALSE(envs.empty());
}

TEST_F(NetApiTestSuite, EnvelopesForbiddenForNonMember) {
  auto a = RegisterUser("nm_a", "dna");
  auto b = RegisterUser("nm_b", "dnb");
  auto c = RegisterUser("nm_c", "dnc");
  boost::json::object conv;
  conv["type"] = "dm";
  conv["peer_user_id"] = a.user_id;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(conv), b.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  std::string path = std::string("/v1/conversations/") + conv_id + "/envelopes";
  auto [gst, gbody] = HttpGet(path, c.access_token);
  ASSERT_EQ(gst, 403u);
  (void) gbody;
}

TEST_F(NetApiTestSuite, SyncPendingAndAck) {
  auto a = RegisterUser("sync_a", "dsa");
  auto b = RegisterUser("sync_b", "dsb");
  boost::json::object conv;
  conv["type"] = "dm";
  conv["peer_user_id"] = a.user_id;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(conv), b.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  boost::json::object send1;
  send1["device_id"] = "dsb";
  send1["conversation_id"] = conv_id;
  send1["ciphertext"] = "first";
  send1["envelope_id"] = "env-net-1";
  auto [s1st, s1body] = HttpPost("/v1/messages/send", boost::json::serialize(send1), b.access_token);
  ASSERT_EQ(s1st, 200u);
  (void) s1body;

  boost::json::object send2;
  send2["device_id"] = "dsb";
  send2["conversation_id"] = conv_id;
  send2["ciphertext"] = "second";
  send2["envelope_id"] = "env-net-2";
  auto [s2st, s2body] = HttpPost("/v1/messages/send", boost::json::serialize(send2), b.access_token);
  ASSERT_EQ(s2st, 200u);
  std::string env_id = JsonString(s2body, "envelope_id");

  auto [pst, pbody] = HttpGet("/v1/sync/pending?limit=50", a.access_token);
  ASSERT_EQ(pst, 200u);
  auto arr = ParseObj(pbody)["envelopes"].as_array();
  ASSERT_FALSE(arr.empty());
  bool found = false;
  for (const auto& e : arr) {
    if (e.as_object().at("envelope_id").as_string() == env_id) {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  boost::json::object ack;
  ack["device_id"] = "dsa";
  ack["envelope_id"] = env_id;
  auto [ast, abody] = HttpPost("/v1/messages/ack", boost::json::serialize(ack), a.access_token);
  ASSERT_EQ(ast, 200u);
  (void) abody;
}

TEST_F(NetApiTestSuite, SendWrongDeviceIdForbidden) {
  auto a = RegisterUser("wd_a", "wda");
  auto b = RegisterUser("wd_b", "wdb");
  boost::json::object conv;
  conv["type"] = "dm";
  conv["peer_user_id"] = a.user_id;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(conv), b.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  boost::json::object send;
  send["device_id"] = "wrong-device";
  send["conversation_id"] = conv_id;
  send["ciphertext"] = "x";
  auto [sst, sbody] = HttpPost("/v1/messages/send", boost::json::serialize(send), b.access_token);
  ASSERT_EQ(sst, 403u);
  (void) sbody;
}

TEST_F(NetApiTestSuite, GroupCreateAddRemoveMember) {
  auto u1 = RegisterUser("grp1", "g1d");
  auto u2 = RegisterUser("grp2", "g2d");
  auto u3 = RegisterUser("grp3", "g3d");

  boost::json::object cr;
  cr["type"] = "group";
  boost::json::array members;
  members.push_back(boost::json::value(u1.user_id));
  members.push_back(boost::json::value(u2.user_id));
  cr["members"] = members;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(cr), u1.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  boost::json::object add;
  add["user_id"] = u3.user_id;
  add["role"] = "member";
  std::string add_path = std::string("/v1/conversations/") + conv_id + "/members";
  auto [ast, abody] = HttpPost(add_path, boost::json::serialize(add), u1.access_token);
  ASSERT_EQ(ast, 200u);
  (void) abody;

  std::string del_path = std::string("/v1/conversations/") + conv_id + "/members/" + u3.user_id;
  auto [dst, dbody] = HttpDelete(del_path, u1.access_token);
  ASSERT_EQ(dst, 200u);
  (void) dbody;
}

TEST_F(NetApiTestSuite, ChannelSubscribeUnsubscribe) {
  auto u1 = RegisterUser("ch1", "c1d");
  auto u2 = RegisterUser("ch2", "c2d");

  boost::json::object cr;
  cr["type"] = "channel";
  boost::json::array admins;
  admins.push_back(boost::json::value(u1.user_id));
  cr["admins"] = admins;
  boost::json::array subs;
  subs.push_back(boost::json::value(u2.user_id));
  cr["subscribers"] = subs;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(cr), u1.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  std::string sub_path = std::string("/v1/conversations/") + conv_id + "/subscribe";
  auto [sst, sbody] = HttpPost(sub_path, "{}", u2.access_token);
  ASSERT_EQ(sst, 200u);
  (void) sbody;

  std::string uns_path = std::string("/v1/conversations/") + conv_id + "/unsubscribe";
  auto [ust, ubody] = HttpPost(uns_path, "{}", u2.access_token);
  ASSERT_EQ(ust, 200u);
  (void) ubody;
}

TEST_F(NetApiTestSuite, UploadPrekeysAndGetPrekeyBundle) {
  auto a = RegisterUser("pk_a", "pkdev_a");
  auto b = RegisterUser("pk_b", "pkdev_b");

  boost::json::object body;
  boost::json::array pks;
  boost::json::object pk1;
  pk1["prekey_id"] = "pk1";
  pk1["prekey_public"] = "pub1";
  pks.push_back(pk1);
  boost::json::object pk2;
  pk2["prekey_id"] = "pk2";
  pk2["prekey_public"] = "pub2";
  pks.push_back(pk2);
  body["prekeys"] = pks;

  std::string post_path = std::string("/v1/devices/") + "pkdev_a" + "/prekeys";
  auto [pst, pbody] = HttpPost(post_path, boost::json::serialize(body), a.access_token);
  ASSERT_EQ(pst, 200u);
  (void) pbody;

  std::string get_path = std::string("/v1/devices/") + "pkdev_a" + "/prekey-bundle";
  auto [gst, gbody] = HttpGet(get_path, b.access_token);
  ASSERT_EQ(gst, 200u);
  auto o = ParseObj(gbody);
  ASSERT_FALSE(o["identity_key_public"].as_string().empty());
  ASSERT_TRUE(o.contains("one_time_prekey_id"));
  ASSERT_EQ(std::string(o["one_time_prekey_id"].as_string().c_str()), "pk1");

  auto [gst2, gbody2] = HttpGet(get_path, b.access_token);
  ASSERT_EQ(gst2, 200u);
  auto o2 = ParseObj(gbody2);
  ASSERT_EQ(std::string(o2["one_time_prekey_id"].as_string().c_str()), "pk2");

  auto [gst3, gbody3] = HttpGet(get_path, b.access_token);
  ASSERT_EQ(gst3, 200u);
  auto o3 = ParseObj(gbody3);
  ASSERT_FALSE(o3.contains("one_time_prekey_id"));
}

TEST_F(NetApiTestSuite, PrekeysWrongDeviceForbidden) {
  auto a = RegisterUser("pkw_a", "pkwd_a");
  boost::json::object body;
  body["prekeys"] = boost::json::array{};
  std::string post_path = std::string("/v1/devices/other_device/prekeys");
  auto [pst, pbody] = HttpPost(post_path, boost::json::serialize(body), a.access_token);
  ASSERT_EQ(pst, 403u);
  (void) pbody;
}

TEST_F(NetApiTestSuite, AttachmentUploadDownloadFlow) {
  auto a = RegisterUser("att_a", "adev");
  auto b = RegisterUser("att_b", "bdev");
  boost::json::object dm;
  dm["type"] = "dm";
  dm["peer_user_id"] = a.user_id;
  auto [cst, cbody] = HttpPost("/v1/conversations", boost::json::serialize(dm), b.access_token);
  ASSERT_EQ(cst, 200u);
  std::string conv_id = JsonString(cbody, "conversation_id");

  boost::json::object init;
  init["conversation_id"] = conv_id;
  init["file_size"] = kSmallAttachmentBytes;
  init["mime_hint"] = "application/octet-stream";
  auto [ist, ibody] = HttpPost("/v1/attachments/upload-init", boost::json::serialize(init), b.access_token);
  ASSERT_EQ(ist, 200u);
  std::string att_id = JsonString(ibody, "attachment_id");

  std::string chunk_path = std::string("/v1/attachments/") + att_id + "/chunk?offset=0";
  std::string bytes(static_cast<std::size_t>(kSmallAttachmentBytes), 'Z');
  auto [pst, pbody] = HttpPut(chunk_path, bytes, b.access_token);
  ASSERT_EQ(pst, 200u);
  (void) pbody;

  boost::json::object fin;
  fin["ciphertext_hash"] = "hash_net_test";
  std::string fin_path = std::string("/v1/attachments/") + att_id + "/finalize";
  auto [fst, fbody] = HttpPost(fin_path, boost::json::serialize(fin), b.access_token);
  ASSERT_EQ(fst, 200u);
  (void) fbody;

  std::string get_path = std::string("/v1/attachments/") + att_id;
  auto [gst, gbody] = HttpGet(get_path, b.access_token);
  ASSERT_EQ(gst, 200u);
  ASSERT_EQ(gbody.size(), static_cast<std::size_t>(kSmallAttachmentBytes));
  for (char c : gbody) {
    ASSERT_EQ(c, 'Z');
  }
}

TEST_F(NetApiTestSuite, RegisterDuplicateReturns409) {
  boost::json::object reg;
  reg["username"] = "dup_user";
  reg["password_derived_value"] = "p";
  reg["device_id"] = "dd1";
  reg["identity_key_public"] = "ik";
  reg["signed_prekey_public"] = "spk";
  reg["signed_prekey_signature"] = "sig";
  auto [st1, b1] = HttpPost("/v1/register", boost::json::serialize(reg));
  ASSERT_EQ(st1, 200u);
  reg["device_id"] = "dd2";
  auto [st2, b2] = HttpPost("/v1/register", boost::json::serialize(reg));
  ASSERT_EQ(st2, 409u);
  (void) b2;
}

TEST_F(NetApiTestSuite, CreateConversationInvalidType400) {
  auto u = RegisterUser("bad_t", "bd");
  boost::json::object bad;
  bad["type"] = "not_a_valid_type";
  auto [st, body] = HttpPost("/v1/conversations", boost::json::serialize(bad), u.access_token);
  ASSERT_EQ(st, 400u);
  (void) body;
}

TEST_F(NetApiTestSuite, InvalidJsonBodyOnRegister) {
  auto [st, body] = HttpPost("/v1/register", "{not json");
  ASSERT_EQ(st, 400u);
  (void) body;
}

TEST_F(NetApiTestSuite, UnknownConversationEnvelopesReturns404) {
  auto u = RegisterUser("nf_u", "nfd");
  auto [st, body] = HttpGet("/v1/conversations/00000000-0000-0000-0000-000000000000/envelopes", u.access_token);
  ASSERT_TRUE(st == 404u || st == 403u);
  (void) body;
}

TEST_F(NetApiTestSuite, AdminDeleteUser) {
  boost::json::object reg;
  reg["username"] = "to_delete";
  reg["password_derived_value"] = "p";
  reg["device_id"] = "deldev";
  reg["identity_key_public"] = "ik";
  reg["signed_prekey_public"] = "spk";
  reg["signed_prekey_signature"] = "sig";
  auto [rst, rbody] = HttpPost("/v1/register", boost::json::serialize(reg));
  ASSERT_EQ(rst, 200u);
  std::string uid = JsonString(rbody, "user_id");

  auto [dst, dbody] = HttpDelete(std::string("/v1/admin/users/") + uid, "", "test-admin-secret");
  ASSERT_EQ(dst, 200u);
  (void) dbody;

  boost::json::object login;
  login["username"] = "to_delete";
  login["password_derived_value"] = "p";
  login["device_id"] = "deldev";
  auto [lst, lbody] = HttpPost("/v1/login", boost::json::serialize(login));
  ASSERT_EQ(lst, 401u);
  (void) lbody;
}

// Explicit Boost.Asio + Beast client path (TCP connect per request via AsioHttpExchange).
TEST_F(NetApiTestSuite, AsioTcpHealthGetViaExchange) {
  auto [st, body] = AsioHttpExchange("GET", "/v1/health");
  ASSERT_EQ(st, 200u);
  ASSERT_EQ(ParseObj(body).at("status").as_string(), "ok");
}

TEST_F(NetApiTestSuite, AsioTcpRegisterAndLoginViaExchange) {
  boost::json::object reg;
  reg["username"] = "asio_tcp_user";
  reg["password_derived_value"] = "derived_asio";
  reg["device_id"] = "asio_dev";
  reg["identity_key_public"] = "ik";
  reg["signed_prekey_public"] = "spk";
  reg["signed_prekey_signature"] = "sig";
  auto [st, body] = AsioHttpExchange("POST", "/v1/register", boost::json::serialize(reg));
  ASSERT_EQ(st, 200u);
  ASSERT_FALSE(JsonString(body, "access_token").empty());

  boost::json::object login;
  login["username"] = "asio_tcp_user";
  login["password_derived_value"] = "derived_asio";
  login["device_id"] = "asio_dev";
  auto [st2, body2] = AsioHttpExchange("POST", "/v1/login", boost::json::serialize(login));
  ASSERT_EQ(st2, 200u);
  ASSERT_FALSE(JsonString(body2, "access_token").empty());
}

TEST_F(NetApiTestSuite, AsioTcpSequentialIndependentConnections) {
  auto [a1, b1] = AsioHttpExchange("GET", "/v1/health");
  ASSERT_EQ(a1, 200u);
  auto [a2, b2] = AsioHttpExchange("GET", "/v1/health");
  ASSERT_EQ(a2, 200u);
  ASSERT_EQ(ParseObj(b1).at("status").as_string(), "ok");
  ASSERT_EQ(ParseObj(b2).at("status").as_string(), "ok");
}

TEST_F(NetApiTestSuite, AsioTcpProtectedRouteWithoutAuthViaExchange) {
  auto [st, body] = AsioHttpExchange("GET", "/v1/conversations");
  ASSERT_EQ(st, 401u);
  (void) body;
}
