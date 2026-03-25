#include "AuthTestSuite.hpp"

namespace {

constexpr std::uint32_t kArgon2MemoryCost = 1024;
constexpr std::uint32_t kArgon2Parallelism = 1;
constexpr vox::common::Timestamp kAccessTokenLifetimeSeconds = 900;
constexpr vox::common::Timestamp kRefreshTokenLifetimeSeconds = 2592000;
constexpr std::size_t kCpuPoolThreadCount = 2;
constexpr std::size_t kCpuPoolQueueCapacity = 32;

} // namespace

void AuthTestSuite::SetUp() {
  db_ = std::make_unique<vox::store::Database>(":memory:");
  users_ = std::make_unique<vox::store::UserRepository>(*db_);
  devices_ = std::make_unique<vox::store::DeviceRepository>(*db_);
  sessions_ = std::make_unique<vox::store::SessionRepository>(*db_);

  hasher_ = std::make_unique<vox::auth::PasswordHasher>(1, kArgon2MemoryCost, kArgon2Parallelism);
  tokens_ = std::make_unique<vox::auth::TokenManager>(
      *sessions_, kAccessTokenLifetimeSeconds, kRefreshTokenLifetimeSeconds, "auth-test-pepper");
  cpu_pool_ = std::make_unique<vox::common::ThreadPool>(kCpuPoolThreadCount, kCpuPoolQueueCapacity);
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
