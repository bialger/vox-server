#include <string>

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include "test_suites/NetApiTestSuite.hpp"

namespace {

std::string JsonString(const std::string& json, std::string_view key) {
  auto v = boost::json::parse(json);
  return {v.as_object().at(key).as_string().c_str()};
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
  (void)body;
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
  (void)b4;
}

TEST_F(NetApiTestSuite, AdminStatsWithToken) {
  auto [st, body] = HttpGet("/v1/admin/stats", "", "test-admin-secret");
  ASSERT_EQ(st, 200u);
  auto v = boost::json::parse(body);
  ASSERT_TRUE(v.as_object().contains("user_count"));
}

TEST_F(NetApiTestSuite, AdminStatsWrongToken) {
  auto [st, body] = HttpGet("/v1/admin/stats", "", "wrong");
  // Wrong admin token does not satisfy admin branch; request then fails user auth.
  ASSERT_TRUE(st == 401u || st == 403u || st == 404u);
  (void)body;
}
