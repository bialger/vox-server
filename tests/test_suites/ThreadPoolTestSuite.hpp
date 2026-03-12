#ifndef THREADPOOLTESTSUITE_HPP
#define THREADPOOLTESTSUITE_HPP

#include <memory>

#include <gtest/gtest.h>

#include "lib/vox_common/thread_pool.hpp"

class ThreadPoolTestSuite : public testing::Test {
 protected:
  static constexpr std::size_t kDefaultThreadCount = 4;
  static constexpr std::size_t kDefaultQueueCapacity = 64;

  std::unique_ptr<vox::common::ThreadPool> pool_;

  void SetUp() override;
  void TearDown() override;
};

#endif  // THREADPOOLTESTSUITE_HPP
