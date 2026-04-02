#ifndef VOX_STORE_SYNC_STATE_REPOSITORY_HPP
#define VOX_STORE_SYNC_STATE_REPOSITORY_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

struct SyncRecordRow {
  std::int64_t row_id = 0;
  std::string record_id;
  std::string ciphertext;
  std::string content_hash;
  int version = 0;
  bool deleted = false;
  common::Timestamp server_updated_at = 0;
};

struct SyncPutResult {
  std::string record_id;
  int version = 0;
  common::Timestamp server_updated_at = 0;
  bool deleted = false;
};

struct SyncChangesPage {
  std::vector<SyncRecordRow> changes;
  std::string next_cursor;
  bool has_more = false;
};

class ISyncStateRepository {
public:
  virtual ~ISyncStateRepository() = default;

  virtual SyncChangesPage ListChangesAfterCursor(const common::UserId& user_id,
                                                 const std::string& collection,
                                                 const std::string& cursor,
                                                 std::size_t limit) = 0;

  virtual common::Result<SyncPutResult> UpsertRecord(const common::UserId& user_id,
                                                     const std::string& collection,
                                                     const std::string& record_id,
                                                     const std::string& ciphertext,
                                                     const std::string& content_hash,
                                                     int base_version,
                                                     common::Timestamp client_updated_at,
                                                     common::Timestamp server_now) = 0;

  virtual common::Result<SyncPutResult> TombstoneRecord(const common::UserId& user_id,
                                                        const std::string& collection,
                                                        const std::string& record_id,
                                                        int base_version,
                                                        common::Timestamp client_updated_at,
                                                        common::Timestamp server_now) = 0;
};

class SyncStateRepository : public ISyncStateRepository {
public:
  explicit SyncStateRepository(IDatabase& db);

  SyncChangesPage ListChangesAfterCursor(const common::UserId& user_id,
                                         const std::string& collection,
                                         const std::string& cursor,
                                         std::size_t limit) override;

  common::Result<SyncPutResult> UpsertRecord(const common::UserId& user_id,
                                             const std::string& collection,
                                             const std::string& record_id,
                                             const std::string& ciphertext,
                                             const std::string& content_hash,
                                             int base_version,
                                             common::Timestamp client_updated_at,
                                             common::Timestamp server_now) override;

  common::Result<SyncPutResult> TombstoneRecord(const common::UserId& user_id,
                                                const std::string& collection,
                                                const std::string& record_id,
                                                int base_version,
                                                common::Timestamp client_updated_at,
                                                common::Timestamp server_now) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_SYNC_STATE_REPOSITORY_HPP
