#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "test_suites/ThreadPoolTestSuite.hpp"
#include "lib/vox_common/bounded_queue.hpp"

TEST_F(ThreadPoolTestSuite, SubmitSingleTask) {
  auto future = pool_->Submit([]() { return 42; });
  ASSERT_EQ(future.get(), 42);
}

TEST_F(ThreadPoolTestSuite, SubmitMultipleTasks) {
  std::vector<std::future<int>> futures;
  for (int i = 0; i < 100; ++i) {
    futures.push_back(pool_->Submit([i]() { return i * 2; }));
  }
  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(futures[i].get(), i * 2);
  }
}

TEST_F(ThreadPoolTestSuite, ConcurrentExecutionUsesMultipleThreads) {
  std::atomic<int> concurrent_count{0};
  std::atomic<int> max_concurrent{0};

  std::vector<std::future<void>> futures;
  for (int i = 0; i < 8; ++i) {
    futures.push_back(pool_->Submit([&]() {
      int current = concurrent_count.fetch_add(1) + 1;
      int expected = max_concurrent.load();
      while (current > expected && !max_concurrent.compare_exchange_weak(expected, current)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
  for (int i = 0; i < 10; ++i) {
    pool_->Submit([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      completed.fetch_add(1);
    });
  }
  pool_->Shutdown();
  ASSERT_EQ(completed.load(), 10);
}

TEST_F(ThreadPoolTestSuite, WaitForIdleBlocksUntilDone) {
  std::atomic<int> completed{0};
  for (int i = 0; i < 20; ++i) {
    pool_->Submit([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      completed.fetch_add(1);
    });
  }
  pool_->WaitForIdle();
  ASSERT_EQ(completed.load(), 20);
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
  vox::common::BoundedQueue<int> queue(10);
  ASSERT_TRUE(queue.Push(42));
  auto val = queue.Pop();
  ASSERT_TRUE(val.has_value());
  ASSERT_EQ(*val, 42);
}

TEST(BoundedQueueTest, TryPushWhenFull) {
  vox::common::BoundedQueue<int> queue(2);
  ASSERT_TRUE(queue.TryPush(1));
  ASSERT_TRUE(queue.TryPush(2));
  ASSERT_FALSE(queue.TryPush(3));
}

TEST(BoundedQueueTest, TryPopWhenEmpty) {
  vox::common::BoundedQueue<int> queue(10);
  auto val = queue.TryPop();
  ASSERT_FALSE(val.has_value());
}

TEST(BoundedQueueTest, ShutdownWakesBlockedPop) {
  vox::common::BoundedQueue<int> queue(10);
  std::atomic<bool> popped{false};
  std::jthread t([&](std::stop_token) {
    auto val = queue.Pop();
    popped.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ASSERT_FALSE(popped.load());
  queue.Shutdown();
  t.join();
  ASSERT_TRUE(popped.load());
}

TEST(BoundedQueueTest, PushAfterShutdownFails) {
  vox::common::BoundedQueue<int> queue(10);
  queue.Shutdown();
  ASSERT_FALSE(queue.Push(1));
  ASSERT_TRUE(queue.IsShutdown());
}

TEST(BoundedQueueTest, FifoOrdering) {
  vox::common::BoundedQueue<int> queue(100);
  for (int i = 0; i < 50; ++i) {
    queue.TryPush(i);
  }
  for (int i = 0; i < 50; ++i) {
    auto val = queue.TryPop();
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(*val, i);
  }
}

TEST(BoundedQueueTest, SizeAndCapacity) {
  vox::common::BoundedQueue<int> queue(5);
  ASSERT_EQ(queue.Capacity(), 5u);
  ASSERT_EQ(queue.Size(), 0u);
  ASSERT_TRUE(queue.Empty());
  queue.TryPush(1);
  ASSERT_EQ(queue.Size(), 1u);
  ASSERT_FALSE(queue.Empty());
}
