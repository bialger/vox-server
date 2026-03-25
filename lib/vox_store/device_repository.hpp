#ifndef VOX_STORE_DEVICE_REPOSITORY_HPP
#define VOX_STORE_DEVICE_REPOSITORY_HPP

#include <optional>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct DeviceRecord {
  common::DeviceId device_id;
  common::UserId user_id;
  std::string identity_key_public;
  std::string signed_prekey_public;
  std::string signed_prekey_signature;
  std::optional<common::Timestamp> last_prekey_refresh_at;
  int client_protocol_version = 1;
};

struct PrekeyRecord {
  std::string prekey_id;
  common::DeviceId device_id;
  std::string prekey_public;
  std::optional<common::Timestamp> consumed_at;
};

struct PrekeyBundle {
  std::string identity_key_public;
  std::string signed_prekey_public;
  std::string signed_prekey_signature;
  std::optional<std::string> one_time_prekey_public;
  std::optional<std::string> one_time_prekey_id;
};

class IDeviceRepository {
public:
  virtual ~IDeviceRepository() = default;
  virtual common::VoidResult RegisterDevice(const DeviceRecord& device) = 0;
  virtual std::vector<DeviceRecord> GetDevicesForUser(const common::UserId& user_id) = 0;
  virtual std::optional<DeviceRecord> FindById(const common::DeviceId& device_id) = 0;
  virtual common::VoidResult StorePrekeys(const common::DeviceId& device_id,
                                            const std::vector<PrekeyRecord>& prekeys) = 0;
  virtual common::Result<PrekeyBundle> GetPrekeyBundle(const common::DeviceId& device_id) = 0;
  virtual common::Result<PrekeyRecord> ConsumeOneTimePrekey(const common::DeviceId& device_id) = 0;
};

class DeviceRepository : public IDeviceRepository {
public:
  explicit DeviceRepository(IDatabase& db);

  common::VoidResult RegisterDevice(const DeviceRecord& device) override;
  std::vector<DeviceRecord> GetDevicesForUser(const common::UserId& user_id) override;
  std::optional<DeviceRecord> FindById(const common::DeviceId& device_id) override;
  common::VoidResult StorePrekeys(const common::DeviceId& device_id, const std::vector<PrekeyRecord>& prekeys) override;
  common::Result<PrekeyBundle> GetPrekeyBundle(const common::DeviceId& device_id) override;
  common::Result<PrekeyRecord> ConsumeOneTimePrekey(const common::DeviceId& device_id) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_DEVICE_REPOSITORY_HPP
