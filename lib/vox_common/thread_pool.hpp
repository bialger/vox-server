#ifndef VOX_COMMON_THREAD_POOL_HPP
#define VOX_COMMON_THREAD_POOL_HPP

#include <cstddef>
#include <functional>
#include <future>
#include <thread>
#include <type_traits>
#include <vector>

#include "lib/vox_common/bounded_queue.hpp"

namespace vox::common {

class ThreadPool {
public:
  explicit ThreadPool(std::size_t thread_count, std::size_t queue_capacity = 1024);

  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template<typename F, typename... Args>
  auto Submit(F&& func, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(func), std::forward<Args>(args)...));
    auto future = task->get_future();
    bool pushed = queue_.Push([task]() { (*task)(); });
    if (!pushed) {
      throw std::runtime_error("ThreadPool: cannot submit task, pool is shut down");
    }
    return future;
  }

  void Shutdown();
  void WaitForIdle();

  [[nodiscard]] std::size_t ThreadCount() const;
  [[nodiscard]] std::size_t PendingTaskCount() const;
  [[nodiscard]] bool IsShutdown() const;

private:
  void WorkerLoop(std::stop_token stop_token);

  BoundedQueue<std::function<void()>> queue_;
  std::vector<std::jthread> workers_;
  std::atomic<std::size_t> active_tasks_{0};
  std::mutex idle_mutex_;
  std::condition_variable idle_cv_;
  bool shutdown_ = false;
};

} // namespace vox::common

#endif // VOX_COMMON_THREAD_POOL_HPP
