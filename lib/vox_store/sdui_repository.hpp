#ifndef VOX_STORE_SDUI_REPOSITORY_HPP
#define VOX_STORE_SDUI_REPOSITORY_HPP

#include <cstdint>
#include <optional>
#include <string>

#include "lib/vox_common/types.hpp"
#include "lib/vox_store/database.hpp"

namespace vox::store {

class ISduiRepository {
public:
  virtual ~ISduiRepository() = default;

  virtual bool HasAcceptedEula(const std::string& device_id, const std::string& eula_version) = 0;

  virtual common::Result<void> UpsertEulaAcceptance(const std::string& device_id,
                                                    const std::string& eula_version,
                                                    common::Timestamp accepted_at) = 0;

  virtual common::Result<void> InsertEvent(const std::string& device_id,
                                           const std::string& screen_id,
                                           const std::string& event,
                                           const std::optional<std::string>& meta_json,
                                           const std::optional<common::Timestamp>& client_time,
                                           common::Timestamp server_time) = 0;
};

class SduiRepository : public ISduiRepository {
public:
  explicit SduiRepository(IDatabase& db);

  bool HasAcceptedEula(const std::string& device_id, const std::string& eula_version) override;

  common::Result<void> UpsertEulaAcceptance(const std::string& device_id,
                                            const std::string& eula_version,
                                            common::Timestamp accepted_at) override;

  common::Result<void> InsertEvent(const std::string& device_id,
                                   const std::string& screen_id,
                                   const std::string& event,
                                   const std::optional<std::string>& meta_json,
                                   const std::optional<common::Timestamp>& client_time,
                                   common::Timestamp server_time) override;

private:
  IDatabase& db_;
};

} // namespace vox::store

#endif // VOX_STORE_SDUI_REPOSITORY_HPP
