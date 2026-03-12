#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "lib/vox_common/bounded_queue.hpp"
#include "test_suites/ThreadPoolTestSuite.hpp"

namespace {

constexpr int kSingleTaskResult = 42;
constexpr int kSubmitMultipleTasksCount = 100;
constexpr int kConcurrentThreadsCount = 8;
constexpr int kConcurrentSleepMs = 50;
constexpr int kGracefulShutdownTaskCount = 10;
constexpr int kGracefulShutdownSleepMs = 10;
constexpr int kWaitForIdleTaskCount = 20;
constexpr int kWaitForIdleSleepMs = 5;
constexpr int kBoundedQueueCapacity = 10;
constexpr int kBoundedQueueSmallCapacity = 2;
constexpr int kShutdownWakesSleepMs = 50;
constexpr int kFifoTaskCount = 50;
constexpr int kFifoQueueCapacity = 100;
constexpr int kSizeAndCapQueueCapacity = 5;

} // namespace

TEST_F(ThreadPoolTestSuite, SubmitSingleTask) {
  auto future = pool_->Submit([]() { return kSingleTaskResult; });
  ASSERT_EQ(future.get(), kSingleTaskResult);
}

TEST_F(ThreadPoolTestSuite, SubmitMultipleTasks) {
  std::vector<std::future<int>> futures;
  futures.reserve(kSubmitMultipleTasksCount);
  for (int i = 0; i < kSubmitMultipleTasksCount; ++i) {
    futures.push_back(pool_->Submit([i]() { return i * 2; }));
  }
  for (int i = 0; i < kSubmitMultipleTasksCount; ++i) {
    ASSERT_EQ(futures[i].get(), i * 2);
  }
}

TEST_F(ThreadPoolTestSuite, ConcurrentExecutionUsesMultipleThreads) {
  std::atomic<int> concurrent_count{0};
  std::atomic<int> max_concurrent{0};

  std::vector<std::future<void>> futures;
  futures.reserve(kConcurrentThreadsCount);
  for (int i = 0; i < kConcurrentThreadsCount; ++i) {
    futures.push_back(pool_->Submit([&]() {
      int current = concurrent_count.fetch_add(1) + 1;
      int expected = max_concurrent.load();
      while (current > expected && !max_concurrent.compare_exchange_weak(expected, current)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kConcurrentSleepMs));
      concurrent_count.fetch_sub(1);
    }));
  }
  for (auto& f : futures) {
    f.get();
  }
  ASSERT_GT(max_concurrent.load(), 1);
}

TEST_F(ThreadPoolTestSuite, GracefulShutdownCompletesWork) {
  std::atomic<int> completed{0};
  for (int i = 0; i < kGracefulShutdownTaskCount; ++i) {
    pool_->Submit([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(kGracefulShutdownSleepMs));
      completed.fetch_add(1);
    });
  }
  pool_->Shutdown();
  ASSERT_EQ(completed.load(), kGracefulShutdownTaskCount);
}

TEST_F(ThreadPoolTestSuite, WaitForIdleBlocksUntilDone) {
  std::atomic<int> completed{0};
  for (int i = 0; i < kWaitForIdleTaskCount; ++i) {
    pool_->Submit([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(kWaitForIdleSleepMs));
      completed.fetch_add(1);
    });
  }
  pool_->WaitForIdle();
  ASSERT_EQ(completed.load(), kWaitForIdleTaskCount);
}

TEST_F(ThreadPoolTestSuite, SubmitAfterShutdownThrows) {
  pool_->Shutdown();
  ASSERT_THROW(pool_->Submit([]() { return 1; }), std::runtime_error);
}

TEST_F(ThreadPoolTestSuite, ThreadCountReportsCorrectly) {
  ASSERT_EQ(pool_->ThreadCount(), kDefaultThreadCount);
}

TEST_F(ThreadPoolTestSuite, ManyConcurrentSubmissionsNoRaces) {
  std::atomic<int> counter{0};
  constexpr int kTasks = 1000;

  std::vector<std::future<void>> futures;
  futures.reserve(kTasks);
  for (int i = 0; i < kTasks; ++i) {
    futures.push_back(pool_->Submit([&counter]() { counter.fetch_add(1); }));
  }
  for (auto& f : futures) {
    f.get();
  }
  ASSERT_EQ(counter.load(), kTasks);
}

TEST(BoundedQueueTest, PushPopBasic) {
  vox::common::BoundedQueue<int> queue(kBoundedQueueCapacity);
  ASSERT_TRUE(queue.Push(kSingleTaskResult));
  auto val = queue.Pop();
  ASSERT_TRUE(val.has_value());
  if (val) {
    ASSERT_EQ(*val, kSingleTaskResult);
  }
}

TEST(BoundedQueueTest, TryPushWhenFull) {
  vox::common::BoundedQueue<int> queue(kBoundedQueueSmallCapacity);
  ASSERT_TRUE(queue.TryPush(1));
  ASSERT_TRUE(queue.TryPush(2));
  ASSERT_FALSE(queue.TryPush(3));
}

TEST(BoundedQueueTest, TryPopWhenEmpty) {
  vox::common::BoundedQueue<int> queue(kBoundedQueueCapacity);
  auto val = queue.TryPop();
  ASSERT_FALSE(val.has_value());
}

TEST(BoundedQueueTest, ShutdownWakesBlockedPop) {
  vox::common::BoundedQueue<int> queue(kBoundedQueueCapacity);
  std::atomic<bool> popped{false};
  std::jthread t([&](const std::stop_token&) {
    auto val = queue.Pop();
    popped.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(kShutdownWakesSleepMs));
  ASSERT_FALSE(popped.load());
  queue.Shutdown();
  t.join();
  ASSERT_TRUE(popped.load());
}

TEST(BoundedQueueTest, PushAfterShutdownFails) {
  vox::common::BoundedQueue<int> queue(kBoundedQueueCapacity);
  queue.Shutdown();
  ASSERT_FALSE(queue.Push(1));
  ASSERT_TRUE(queue.IsShutdown());
}

TEST(BoundedQueueTest, FifoOrdering) {
  vox::common::BoundedQueue<int> queue(kFifoQueueCapacity);
  for (int i = 0; i < kFifoTaskCount; ++i) {
    queue.TryPush(i);
  }
  for (int i = 0; i < kFifoTaskCount; ++i) {
    auto val = queue.TryPop();
    ASSERT_TRUE(val.has_value());
    if (val) {
      ASSERT_EQ(*val, i);
    }
  }
}

TEST(BoundedQueueTest, SizeAndCapacity) {
  vox::common::BoundedQueue<int> queue(kSizeAndCapQueueCapacity);
  ASSERT_EQ(queue.Capacity(), static_cast<std::size_t>(kSizeAndCapQueueCapacity));
  ASSERT_EQ(queue.Size(), 0u);
  ASSERT_TRUE(queue.Empty());
  queue.TryPush(1);
  ASSERT_EQ(queue.Size(), 1u);
  ASSERT_FALSE(queue.Empty());
}
