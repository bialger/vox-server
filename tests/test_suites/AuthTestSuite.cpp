#include "AuthTestSuite.hpp"

void AuthTestSuite::SetUp() {
  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  sessions_ = std::make_unique<vox::store::SessionRepository>(*db_);

  hasher_ = std::make_unique<vox::auth::PasswordHasher>(1, 1024, 1);
  tokens_ = std::make_unique<vox::auth::TokenManager>(*sessions_, 900, 2592000);
  cpu_pool_ = std::make_unique<vox::common::ThreadPool>(2, 32);
  auth_ = std::make_unique<vox::auth::AuthService>(*users_, *devices_, *hasher_, *tokens_, *cpu_pool_);
}

void AuthTestSuite::TearDown() {
  auth_.reset();
  cpu_pool_.reset();
  tokens_.reset();
  hasher_.reset();
  sessions_.reset();
  devices_.reset();
  users_.reset();
  db_.reset();
}
