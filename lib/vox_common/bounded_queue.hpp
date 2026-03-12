#ifndef VOX_COMMON_BOUNDED_QUEUE_HPP
#define VOX_COMMON_BOUNDED_QUEUE_HPP

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace vox::common {

template<typename T>
class BoundedQueue {
public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
  }

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  bool Push(T item) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < capacity_ || shutdown_; });
    if (shutdown_) {
      return false;
    }
    queue_.push(std::move(item));
    not_empty_.notify_one();
    return true;
  }

  bool TryPush(T item) {
    std::lock_guard lock(mutex_);
    if (shutdown_ || queue_.size() >= capacity_) {
      return false;
    }
    queue_.push(std::move(item));
    not_empty_.notify_one();
    return true;
  }

  std::optional<T> Pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
    if (queue_.empty()) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return item;
  }

  std::optional<T> TryPop() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return item;
  }

  void Shutdown() {
    std::lock_guard lock(mutex_);
    shutdown_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  [[nodiscard]] bool IsShutdown() const {
    std::lock_guard lock(mutex_);
    return shutdown_;
  }

  [[nodiscard]] std::size_t Size() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
  }

  [[nodiscard]] std::size_t Capacity() const {
    return capacity_;
  }

  [[nodiscard]] bool Empty() const {
    std::lock_guard lock(mutex_);
    return queue_.empty();
  }

private:
  const std::size_t capacity_;
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool shutdown_ = false;
};

} // namespace vox::common

#endif // VOX_COMMON_BOUNDED_QUEUE_HPP
