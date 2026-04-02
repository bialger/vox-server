#ifndef VOX_STORE_DEVICE_REPOSITORY_HPP
#define VOX_STORE_DEVICE_REPOSITORY_HPP

#include <optional>
#include <string>
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
  std::string device_label;
  common::Timestamp created_at = 0;
  common::Timestamp last_seen_at = 0;
  std::optional<common::Timestamp> revoked_at;
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
  /// Resolves `(user_id, device_id)`; the same `device_id` may exist for different users.
  virtual std::optional<DeviceRecord> FindByUserAndDevice(const common::UserId& user_id,
                                                          const common::DeviceId& device_id) = 0;
  /// All rows with this client `device_id` (for legacy routes when `user_id` is omitted).
  virtual std::vector<DeviceRecord> FindAllByDeviceId(const common::DeviceId& device_id) = 0;
  virtual common::VoidResult StorePrekeys(const common::UserId& user_id,
                                          const common::DeviceId& device_id,
                                          const std::vector<PrekeyRecord>& prekeys) = 0;
  virtual common::Result<PrekeyBundle> GetPrekeyBundle(const common::UserId& user_id,
                                                       const common::DeviceId& device_id) = 0;
  virtual common::Result<PrekeyRecord> ConsumeOneTimePrekey(const common::UserId& user_id,
                                                            const common::DeviceId& device_id) = 0;

  virtual common::VoidResult UpdateSignedPrekey(const common::UserId& user_id,
                                                const common::DeviceId& device_id,
                                                const std::string& signed_prekey_public,
                                                const std::string& signed_prekey_signature,
                                                common::Timestamp now) = 0;

  virtual common::VoidResult RevokeDevice(const common::UserId& user_id,
                                          const common::DeviceId& device_id,
                                          common::Timestamp now) = 0;

  virtual common::VoidResult UpdateLastSeen(const common::UserId& user_id,
                                            const common::DeviceId& device_id,
                                            common::Timestamp now) = 0;

  /// Count one-time prekeys with consumed_at IS NULL for this scoped device.
  virtual std::size_t CountAvailableOneTimePrekeys(const common::UserId& user_id,
                                                   const common::DeviceId& device_id) = 0;
};

class DeviceRepository : public IDeviceRepository {
public:
  explicit DeviceRepository(IDatabase& db);

  common::VoidResult RegisterDevice(const DeviceRecord& device) override;
  std::vector<DeviceRecord> GetDevicesForUser(const common::UserId& user_id) override;
  std::optional<DeviceRecord> FindByUserAndDevice(const common::UserId& user_id,
                                                  const common::DeviceId& device_id) override;
  std::vector<DeviceRecord> FindAllByDeviceId(const common::DeviceId& device_id) override;
  common::VoidResult StorePrekeys(const common::UserId& user_id,
                                  const common::DeviceId& device_id,
                                  const std::vector<PrekeyRecord>& prekeys) override;
  common::Result<PrekeyBundle> GetPrekeyBundle(const common::UserId& user_id,
                                               const common::DeviceId& device_id) override;
  common::Result<PrekeyRecord> ConsumeOneTimePrekey(const common::UserId& user_id,
                                                    const common::DeviceId& device_id) override;

  common::VoidResult UpdateSignedPrekey(const common::UserId& user_id,
                                        const common::DeviceId& device_id,
                                        const std::string& signed_prekey_public,
                                        const std::string& signed_prekey_signature,
                                        common::Timestamp now) override;

  common::VoidResult RevokeDevice(const common::UserId& user_id,
                                  const common::DeviceId& device_id,
                                  common::Timestamp now) override;

  common::VoidResult UpdateLastSeen(const common::UserId& user_id,
                                    const common::DeviceId& device_id,
                                    common::Timestamp now) override;

  std::size_t CountAvailableOneTimePrekeys(const common::UserId& user_id, const common::DeviceId& device_id) override;

private:
  /// Caller must hold `db_.WriteLock()` and an active transaction when applicable.
  std::optional<PrekeyRecord> ConsumeOneAvailableOtpLocked(const common::UserId& user_id,
                                                           const common::DeviceId& device_id);

  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_DEVICE_REPOSITORY_HPP
