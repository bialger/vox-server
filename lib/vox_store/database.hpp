#ifndef VOX_STORE_DATABASE_HPP
#define VOX_STORE_DATABASE_HPP

#include <memory>
#include <mutex>
#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

namespace vox::store {

class Database {
public:
  explicit Database(const std::string& db_path);

  SQLite::Database& Connection();

  std::unique_lock<std::mutex> WriteLock();

private:
  void CreateSchema();

  std::unique_ptr<SQLite::Database> db_;
  std::mutex write_mutex_;
};

} // namespace vox::store

#endif // VOX_STORE_DATABASE_HPP
