#include "lib/vox_store/sdui_repository.hpp"

#include <SQLiteCpp/SQLiteCpp.h>
#include <cstdint>

namespace vox::store {

SduiRepository::SduiRepository(IDatabase& db) : db_(db) {
}

bool SduiRepository::HasAcceptedEula(const std::string& device_id, const std::string& eula_version) {
  auto lk = db_.ReadLock();
  SQLite::Statement st(db_.Connection(),
                       "SELECT 1 FROM sdui_eula_acceptances WHERE device_id = ? AND eula_version = ? LIMIT 1");
  st.bind(1, device_id);
  st.bind(2, eula_version);
  return st.executeStep();
}

common::Result<void> SduiRepository::UpsertEulaAcceptance(const std::string& device_id,
                                                          const std::string& eula_version,
                                                          common::Timestamp accepted_at) {
  try {
    auto lk = db_.WriteLock();
    SQLite::Statement st(db_.Connection(),
                         "INSERT INTO sdui_eula_acceptances(device_id, eula_version, accepted_at) VALUES(?, ?, ?) "
                         "ON CONFLICT(device_id, eula_version) DO UPDATE SET accepted_at = excluded.accepted_at");
    st.bind(1, device_id);
    st.bind(2, eula_version);
    st.bind(3, static_cast<std::int64_t>(accepted_at));
    st.exec();
    return common::Result<void>{};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

common::Result<void> SduiRepository::InsertEvent(const std::string& device_id,
                                                 const std::string& screen_id,
                                                 const std::string& event,
                                                 const std::optional<std::string>& meta_json,
                                                 const std::optional<common::Timestamp>& client_time,
                                                 common::Timestamp server_time) {
  try {
    constexpr int kBindDeviceId = 1;
    constexpr int kBindScreenId = 2;
    constexpr int kBindEvent = 3;
    constexpr int kBindMetaJson = 4;
    constexpr int kBindClientTime = 5;
    constexpr int kBindServerTime = 6;

    auto lk = db_.WriteLock();
    SQLite::Statement st(db_.Connection(),
                         "INSERT INTO sdui_events(device_id, screen_id, event, meta_json, client_time, server_time) "
                         "VALUES(?, ?, ?, ?, ?, ?)");
    st.bind(kBindDeviceId, device_id);
    st.bind(kBindScreenId, screen_id);
    st.bind(kBindEvent, event);
    if (meta_json.has_value()) {
      st.bind(kBindMetaJson, *meta_json);
    } else {
      st.bind(kBindMetaJson);
    }
    if (client_time.has_value()) {
      st.bind(kBindClientTime, static_cast<std::int64_t>(*client_time));
    } else {
      st.bind(kBindClientTime);
    }
    st.bind(kBindServerTime, static_cast<std::int64_t>(server_time));
    st.exec();
    return common::Result<void>{};
  } catch (const SQLite::Exception& e) {
    return std::unexpected(common::Error{.code = common::ErrorCode::kInternal, .message = e.what()});
  }
}

} // namespace vox::store
