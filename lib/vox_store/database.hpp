#ifndef VOX_STORE_DATABASE_HPP
#define VOX_STORE_DATABASE_HPP

#include <memory>
#include <mutex>
#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

namespace vox::store {

class IDatabase {
public:
  virtual ~IDatabase() = default;
  virtual SQLite::Database& Connection() = 0;
  /// Serialize all use of the single SQLite connection (recursive: nested calls from repositories OK).
  virtual std::unique_lock<std::recursive_mutex> WriteLock() = 0;
  /// Same mutex as WriteLock; use for read-only SELECT paths.
  virtual std::unique_lock<std::recursive_mutex> ReadLock() = 0;
};

class Database : public IDatabase {
public:
  explicit Database(const std::string& db_path);

  SQLite::Database& Connection() override;

  std::unique_lock<std::recursive_mutex> WriteLock() override;
  std::unique_lock<std::recursive_mutex> ReadLock() override;

private:
  void CreateSchema();

  std::unique_ptr<SQLite::Database> db_;
  std::recursive_mutex connection_mutex_;
};

} // namespace vox::store

#endif // VOX_STORE_DATABASE_HPP
