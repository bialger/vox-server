#ifndef VOX_COMMON_SHARD_MAP_HPP
#define VOX_COMMON_SHARD_MAP_HPP

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace vox::common {

template <typename Key, typename Value, std::size_t ShardCount = 16>
class ShardMap {
  static_assert(ShardCount > 0, "ShardCount must be positive");

 public:
  ShardMap() = default;

  ShardMap(const ShardMap&) = delete;
  ShardMap& operator=(const ShardMap&) = delete;

  template <typename Fn>
  auto WithShard(const Key& key, Fn&& fn) -> decltype(fn(std::declval<std::unordered_map<Key, Value>&>())) {
    auto& shard = GetShard(key);
    std::unique_lock lock(shard.mutex);
    return fn(shard.map);
  }

  template <typename Fn>
  auto WithShardShared(const Key& key, Fn&& fn) const
      -> decltype(fn(std::declval<const std::unordered_map<Key, Value>&>())) {
    const auto& shard = GetShard(key);
    std::shared_lock lock(shard.mutex);
    return fn(shard.map);
  }

  void Insert(const Key& key, Value value) {
    auto& shard = GetShard(key);
    std::unique_lock lock(shard.mutex);
    shard.map.insert_or_assign(key, std::move(value));
  }

  bool Erase(const Key& key) {
    auto& shard = GetShard(key);
    std::unique_lock lock(shard.mutex);
    return shard.map.erase(key) > 0;
  }

  std::optional<Value> Find(const Key& key) const {
    const auto& shard = GetShard(key);
    std::shared_lock lock(shard.mutex);
    auto it = shard.map.find(key);
    if (it != shard.map.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Contains(const Key& key) const {
    const auto& shard = GetShard(key);
    std::shared_lock lock(shard.mutex);
    return shard.map.contains(key);
  }

  template <typename Fn>
  void ForEach(Fn&& fn) const {
    for (const auto& shard : shards_) {
      std::shared_lock lock(shard.mutex);
      for (const auto& [k, v] : shard.map) {
        fn(k, v);
      }
    }
  }

  std::size_t Size() const {
    std::size_t total = 0;
    for (const auto& shard : shards_) {
      std::shared_lock lock(shard.mutex);
      total += shard.map.size();
    }
    return total;
  }

 private:
  struct Shard {
    mutable std::shared_mutex mutex;
    std::unordered_map<Key, Value> map;
  };

  Shard& GetShard(const Key& key) {
    return shards_[std::hash<Key>{}(key) % ShardCount];
  }

  const Shard& GetShard(const Key& key) const {
    return shards_[std::hash<Key>{}(key) % ShardCount];
  }

  std::array<Shard, ShardCount> shards_;
};

}  // namespace vox::common

#endif  // VOX_COMMON_SHARD_MAP_HPP
