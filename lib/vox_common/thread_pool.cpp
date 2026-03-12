#include "lib/vox_common/thread_pool.hpp"

#include <spdlog/spdlog.h>

namespace vox::common {

ThreadPool::ThreadPool(std::size_t thread_count, std::size_t queue_capacity) : queue_(queue_capacity) {
  workers_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    workers_.emplace_back([this](const std::stop_token& st) { WorkerLoop(st); });
  }
}

ThreadPool::~ThreadPool() {
  Shutdown();
}

void ThreadPool::Shutdown() {
  {
    std::lock_guard lock(idle_mutex_);
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
  }
  queue_.Shutdown();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  idle_cv_.notify_all();
}

void ThreadPool::WaitForIdle() {
  std::unique_lock lock(idle_mutex_);
  idle_cv_.wait(lock, [this] { return (active_tasks_.load() == 0 && queue_.Size() == 0) || shutdown_; });
}

std::size_t ThreadPool::ThreadCount() const {
  return workers_.size();
}

std::size_t ThreadPool::PendingTaskCount() const {
  return queue_.Size();
}

bool ThreadPool::IsShutdown() const {
  return shutdown_;
}

void ThreadPool::WorkerLoop(const std::stop_token& /*stop_token*/) {
  while (true) {
    auto task = queue_.Pop();
    if (!task.has_value()) {
      break;
    }
    active_tasks_.fetch_add(1);
    try {
      (*task)();
    } catch (const std::exception& e) {
      spdlog::error("ThreadPool: unhandled exception in task: {}", e.what());
    }
    active_tasks_.fetch_sub(1);
    idle_cv_.notify_all();
  }
}

} // namespace vox::common
