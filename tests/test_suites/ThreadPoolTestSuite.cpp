#include "ThreadPoolTestSuite.hpp"

void ThreadPoolTestSuite::SetUp() {
  pool_ = std::make_unique<vox::common::ThreadPool>(kDefaultThreadCount, kDefaultQueueCapacity);
}

void ThreadPoolTestSuite::TearDown() {
  pool_.reset();
}
